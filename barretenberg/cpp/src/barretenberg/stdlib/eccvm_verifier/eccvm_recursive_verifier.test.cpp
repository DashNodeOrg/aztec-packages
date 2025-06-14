#include "barretenberg/stdlib/eccvm_verifier/eccvm_recursive_verifier.hpp"
#include "barretenberg/circuit_checker/circuit_checker.hpp"
#include "barretenberg/eccvm/eccvm_prover.hpp"
#include "barretenberg/eccvm/eccvm_verifier.hpp"
#include "barretenberg/stdlib/honk_verifier/ultra_verification_keys_comparator.hpp"
#include "barretenberg/stdlib/plonk_recursion/pairing_points.hpp"
#include "barretenberg/stdlib/test_utils/tamper_proof.hpp"
#include "barretenberg/ultra_honk/ultra_prover.hpp"
#include "barretenberg/ultra_honk/ultra_verifier.hpp"

#include <gtest/gtest.h>

namespace {
auto& engine = bb::numeric::get_debug_randomness();
}
namespace bb {
template <typename RecursiveFlavor> class ECCVMRecursiveTests : public ::testing::Test {
  public:
    using InnerFlavor = typename RecursiveFlavor::NativeFlavor;
    using InnerBuilder = typename InnerFlavor::CircuitBuilder;
    using InnerProver = ECCVMProver;
    using InnerVerifier = ECCVMVerifier;
    using InnerG1 = InnerFlavor::Commitment;
    using InnerFF = InnerFlavor::FF;
    using InnerBF = InnerFlavor::BF;
    using InnerPK = InnerFlavor::ProvingKey;
    using InnerVK = InnerFlavor::VerificationKey;

    using Transcript = InnerFlavor::Transcript;

    using RecursiveVerifier = ECCVMRecursiveVerifier_<RecursiveFlavor>;

    using OuterBuilder = typename RecursiveFlavor::CircuitBuilder;
    using OuterFlavor = std::conditional_t<IsMegaBuilder<OuterBuilder>, MegaFlavor, UltraFlavor>;
    using OuterProver = UltraProver_<OuterFlavor>;
    using OuterVerifier = UltraVerifier_<OuterFlavor>;
    using OuterDeciderProvingKey = DeciderProvingKey_<OuterFlavor>;
    static void SetUpTestSuite()
    {
        srs::init_grumpkin_crs_factory(bb::srs::get_grumpkin_crs_path());
        bb::srs::init_crs_factory(bb::srs::get_ignition_crs_path());
    }

    /**
     * @brief Adds operations in BN254 to the op_queue and then constructs and ECCVM circuit from the op_queue.
     *
     * @param engine
     * @return ECCVMCircuitBuilder
     */
    static InnerBuilder generate_circuit(numeric::RNG* engine = nullptr, const size_t num_iterations = 1)
    {
        using Curve = curve::BN254;
        using G1 = Curve::Element;
        using Fr = Curve::ScalarField;

        std::shared_ptr<ECCOpQueue> op_queue = std::make_shared<ECCOpQueue>();
        G1 a = G1::random_element(engine);
        G1 b = G1::random_element(engine);
        G1 c = G1::random_element(engine);
        Fr x = Fr::random_element(engine);
        Fr y = Fr::random_element(engine);
        for (size_t idx = 0; idx < num_iterations; idx++) {
            op_queue->add_accumulate(a);
            op_queue->mul_accumulate(a, x);
            op_queue->mul_accumulate(b, x);
            op_queue->mul_accumulate(b, y);
            op_queue->add_accumulate(a);
            op_queue->mul_accumulate(b, x);
            op_queue->eq_and_reset();
            op_queue->add_accumulate(c);
            op_queue->mul_accumulate(a, x);
            op_queue->mul_accumulate(b, x);
            op_queue->eq_and_reset();
            op_queue->mul_accumulate(a, x);
            op_queue->mul_accumulate(b, x);
            op_queue->mul_accumulate(c, x);
        }
        InnerBuilder builder{ op_queue };
        return builder;
    }

    static void test_recursive_verification()
    {
        InnerBuilder builder = generate_circuit(&engine);
        InnerProver prover(builder);
        ECCVMProof proof = prover.construct_proof();
        auto verification_key = std::make_shared<typename InnerFlavor::VerificationKey>(prover.key);

        info("ECCVM Recursive Verifier");
        OuterBuilder outer_circuit;
        RecursiveVerifier verifier{ &outer_circuit, verification_key };
        auto [opening_claim, ipa_transcript] = verifier.verify_proof(proof);
        stdlib::recursion::PairingPoints<OuterBuilder>::add_default_to_public_inputs(outer_circuit);

        info("Recursive Verifier: num gates = ", outer_circuit.get_estimated_num_finalized_gates());

        // Check for a failure flag in the recursive verifier circuit
        EXPECT_EQ(outer_circuit.failed(), false) << outer_circuit.err();

        bool result = CircuitChecker::check(outer_circuit);
        EXPECT_TRUE(result);

        InnerVerifier native_verifier(prover.key);
        bool native_result = native_verifier.verify_proof(proof);
        EXPECT_TRUE(native_result);
        auto recursive_manifest = verifier.transcript->get_manifest();
        auto native_manifest = native_verifier.transcript->get_manifest();

        ASSERT(recursive_manifest.size() > 0);
        for (size_t i = 0; i < recursive_manifest.size(); ++i) {
            EXPECT_EQ(recursive_manifest[i], native_manifest[i])
                << "Recursive Verifier/Verifier manifest discrepency in round " << i;
        }

        // Ensure verification key is the same
        EXPECT_EQ(static_cast<uint64_t>(verifier.key->circuit_size.get_value()), verification_key->circuit_size);
        EXPECT_EQ(static_cast<uint64_t>(verifier.key->log_circuit_size.get_value()),
                  verification_key->log_circuit_size);
        EXPECT_EQ(static_cast<uint64_t>(verifier.key->num_public_inputs.get_value()),
                  verification_key->num_public_inputs);
        for (auto [vk_poly, native_vk_poly] : zip_view(verifier.key->get_all(), verification_key->get_all())) {
            EXPECT_EQ(vk_poly.get_value(), native_vk_poly);
        }

        // Construct a full proof from the recursive verifier circuit
        {
            auto proving_key = std::make_shared<OuterDeciderProvingKey>(outer_circuit);
            OuterProver prover(proving_key);
            auto verification_key = std::make_shared<typename OuterFlavor::VerificationKey>(proving_key->proving_key);
            OuterVerifier verifier(verification_key);
            auto proof = prover.construct_proof();
            bool verified = verifier.verify_proof(proof);

            ASSERT(verified);
        }
    }

    static void test_recursive_verification_failure()
    {
        InnerBuilder builder = generate_circuit(&engine);
        builder.op_queue->add_erroneous_equality_op_for_testing();
        InnerProver prover(builder);
        ECCVMProof proof = prover.construct_proof();
        auto verification_key = std::make_shared<typename InnerFlavor::VerificationKey>(prover.key);

        OuterBuilder outer_circuit;
        RecursiveVerifier verifier{ &outer_circuit, verification_key };
        [[maybe_unused]] auto output = verifier.verify_proof(proof);
        stdlib::recursion::PairingPoints<OuterBuilder>::add_default_to_public_inputs(outer_circuit);
        info("Recursive Verifier: estimated num finalized gates = ", outer_circuit.get_estimated_num_finalized_gates());

        // Check for a failure flag in the recursive verifier circuit
        EXPECT_FALSE(CircuitChecker::check(outer_circuit));
    }

    static void test_recursive_verification_failure_tampered_proof()
    {
        for (size_t idx = 0; idx < static_cast<size_t>(TamperType::END); idx++) {
            InnerBuilder builder = generate_circuit(&engine);
            InnerProver prover(builder);
            ECCVMProof proof = prover.construct_proof();

            // Tamper with the proof to be verified
            TamperType tamper_type = static_cast<TamperType>(idx);
            tamper_with_proof<InnerProver, InnerFlavor>(prover, proof, tamper_type);

            auto verification_key = std::make_shared<typename InnerFlavor::VerificationKey>(prover.key);

            OuterBuilder outer_circuit;
            RecursiveVerifier verifier{ &outer_circuit, verification_key };
            [[maybe_unused]] auto output = verifier.verify_proof(proof);
            stdlib::recursion::PairingPoints<OuterBuilder>::add_default_to_public_inputs(outer_circuit);

            // Check for a failure flag in the recursive verifier circuit
            EXPECT_FALSE(CircuitChecker::check(outer_circuit));
        }
    }

    static void test_independent_vk_hash()
    {

        // Retrieves the trace blocks (each consisting of a specific gate) from the recursive verifier circuit
        auto get_blocks = [](size_t inner_size) -> std::tuple<typename OuterBuilder::ExecutionTrace,
                                                              std::shared_ptr<typename OuterFlavor::VerificationKey>> {
            auto inner_circuit = generate_circuit(&engine, inner_size);
            InnerProver inner_prover(inner_circuit);

            ECCVMProof inner_proof = inner_prover.construct_proof();
            auto verification_key = std::make_shared<typename InnerFlavor::VerificationKey>(inner_prover.key);

            // Create a recursive verification circuit for the proof of the inner circuit
            OuterBuilder outer_circuit;

            RecursiveVerifier verifier{ &outer_circuit, verification_key };

            auto [opening_claim, ipa_transcript] = verifier.verify_proof(inner_proof);
            stdlib::recursion::PairingPoints<OuterBuilder>::add_default_to_public_inputs(outer_circuit);

            auto outer_proving_key = std::make_shared<OuterDeciderProvingKey>(outer_circuit);
            auto outer_verification_key =
                std::make_shared<typename OuterFlavor::VerificationKey>(outer_proving_key->proving_key);

            return { outer_circuit.blocks, outer_verification_key };
        };

        auto [blocks_20, verification_key_20] = get_blocks(20);
        auto [blocks_40, verification_key_40] = get_blocks(40);

        compare_ultra_blocks_and_verification_keys<OuterFlavor>({ blocks_20, blocks_40 },
                                                                { verification_key_20, verification_key_40 });
    };
};
using FlavorTypes = testing::Types<ECCVMRecursiveFlavor_<UltraCircuitBuilder>>;

TYPED_TEST_SUITE(ECCVMRecursiveTests, FlavorTypes);

TYPED_TEST(ECCVMRecursiveTests, SingleRecursiveVerification)
{
    TestFixture::test_recursive_verification();
};

TYPED_TEST(ECCVMRecursiveTests, SingleRecursiveVerificationFailure)
{
    TestFixture::test_recursive_verification_failure();
};

TYPED_TEST(ECCVMRecursiveTests, SingleRecursiveVerificationFailureTamperedProof)
{
    TestFixture::test_recursive_verification_failure_tampered_proof();
};

TYPED_TEST(ECCVMRecursiveTests, IndependentVKHash)
{
    TestFixture::test_independent_vk_hash();
};
} // namespace bb
