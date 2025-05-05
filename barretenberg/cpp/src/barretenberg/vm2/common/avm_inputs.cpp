#include "barretenberg/vm2/common/avm_inputs.hpp"

#include <vector>

#include "barretenberg/serialize/msgpack.hpp"

namespace bb::avm2 {

PublicInputs PublicInputs::from(const std::vector<uint8_t>& data)
{
    PublicInputs inputs;
    msgpack::unpack(reinterpret_cast<const char*>(data.data()), data.size()).get().convert(inputs);
    return inputs;
}

AvmProvingInputs AvmProvingInputs::from(const std::vector<uint8_t>& data)
{
    AvmProvingInputs inputs;
    msgpack::unpack(reinterpret_cast<const char*>(data.data()), data.size()).get().convert(inputs);
    return inputs;
}

// WARNING: If updating this columns conversion, you must also update columns serialization
// in the Noir `AvmCircuitPublicInputs` struct in avm_circuit_public_inputs.nr
std::vector<std::vector<FF>> PublicInputs::to_columns() const
{
    std::vector<std::vector<FF>> cols(AVM_NUM_PUBLIC_INPUT_COLUMNS,
                                      std::vector<FF>(AVM_PUBLIC_INPUTS_COLUMNS_MAX_LENGTH));

    // Global variables
    cols[0][AVM_PUBLIC_INPUTS_GLOBAL_VARIABLES_CHAIN_ID_ROW_IDX] = globalVariables.chainId;
    cols[0][AVM_PUBLIC_INPUTS_GLOBAL_VARIABLES_VERSION_ROW_IDX] = globalVariables.version;
    cols[0][AVM_PUBLIC_INPUTS_GLOBAL_VARIABLES_BLOCK_NUMBER_ROW_IDX] = globalVariables.blockNumber;
    cols[0][AVM_PUBLIC_INPUTS_GLOBAL_VARIABLES_SLOT_NUMBER_ROW_IDX] = globalVariables.slotNumber;
    cols[0][AVM_PUBLIC_INPUTS_GLOBAL_VARIABLES_TIMESTAMP_ROW_IDX] = globalVariables.timestamp;
    cols[0][AVM_PUBLIC_INPUTS_GLOBAL_VARIABLES_COINBASE_ROW_IDX] = globalVariables.coinbase;
    cols[0][AVM_PUBLIC_INPUTS_GLOBAL_VARIABLES_FEE_RECIPIENT_ROW_IDX] = globalVariables.feeRecipient;
    cols[0][AVM_PUBLIC_INPUTS_GLOBAL_VARIABLES_GAS_FEES_ROW_IDX] = globalVariables.gasFees.feePerDaGas;
    cols[1][AVM_PUBLIC_INPUTS_GLOBAL_VARIABLES_GAS_FEES_ROW_IDX] = globalVariables.gasFees.feePerL2Gas;

    // Start tree snapshots
    cols[0][AVM_PUBLIC_INPUTS_START_TREE_SNAPSHOTS_L1_TO_L2_MESSAGE_TREE_ROW_IDX] =
        startTreeSnapshots.l1ToL2MessageTree.root;
    cols[1][AVM_PUBLIC_INPUTS_START_TREE_SNAPSHOTS_L1_TO_L2_MESSAGE_TREE_ROW_IDX] =
        startTreeSnapshots.l1ToL2MessageTree.nextAvailableLeafIndex;
    cols[0][AVM_PUBLIC_INPUTS_START_TREE_SNAPSHOTS_NOTE_HASH_TREE_ROW_IDX] = startTreeSnapshots.noteHashTree.root;
    cols[1][AVM_PUBLIC_INPUTS_START_TREE_SNAPSHOTS_NOTE_HASH_TREE_ROW_IDX] =
        startTreeSnapshots.noteHashTree.nextAvailableLeafIndex;
    cols[0][AVM_PUBLIC_INPUTS_START_TREE_SNAPSHOTS_NULLIFIER_TREE_ROW_IDX] = startTreeSnapshots.nullifierTree.root;
    cols[1][AVM_PUBLIC_INPUTS_START_TREE_SNAPSHOTS_NULLIFIER_TREE_ROW_IDX] =
        startTreeSnapshots.nullifierTree.nextAvailableLeafIndex;
    cols[0][AVM_PUBLIC_INPUTS_START_TREE_SNAPSHOTS_PUBLIC_DATA_TREE_ROW_IDX] = startTreeSnapshots.publicDataTree.root;
    cols[1][AVM_PUBLIC_INPUTS_START_TREE_SNAPSHOTS_PUBLIC_DATA_TREE_ROW_IDX] =
        startTreeSnapshots.publicDataTree.nextAvailableLeafIndex;

    // Start gas used
    cols[0][AVM_PUBLIC_INPUTS_START_GAS_USED_ROW_IDX] = startGasUsed.daGas;
    cols[1][AVM_PUBLIC_INPUTS_START_GAS_USED_ROW_IDX] = startGasUsed.l2Gas;

    // Gas settings
    cols[0][AVM_PUBLIC_INPUTS_GAS_SETTINGS_GAS_LIMITS_ROW_IDX] = gasSettings.gasLimits.daGas;
    cols[1][AVM_PUBLIC_INPUTS_GAS_SETTINGS_GAS_LIMITS_ROW_IDX] = gasSettings.gasLimits.l2Gas;
    cols[0][AVM_PUBLIC_INPUTS_GAS_SETTINGS_TEARDOWN_GAS_LIMITS_ROW_IDX] = gasSettings.teardownGasLimits.daGas;
    cols[1][AVM_PUBLIC_INPUTS_GAS_SETTINGS_TEARDOWN_GAS_LIMITS_ROW_IDX] = gasSettings.teardownGasLimits.l2Gas;
    cols[0][AVM_PUBLIC_INPUTS_GAS_SETTINGS_MAX_FEES_PER_GAS_ROW_IDX] = gasSettings.maxFeesPerGas.feePerDaGas;
    cols[1][AVM_PUBLIC_INPUTS_GAS_SETTINGS_MAX_FEES_PER_GAS_ROW_IDX] = gasSettings.maxFeesPerGas.feePerL2Gas;
    cols[0][AVM_PUBLIC_INPUTS_GAS_SETTINGS_MAX_PRIORITY_FEES_PER_GAS_ROW_IDX] =
        gasSettings.maxPriorityFeesPerGas.feePerDaGas;
    cols[1][AVM_PUBLIC_INPUTS_GAS_SETTINGS_MAX_PRIORITY_FEES_PER_GAS_ROW_IDX] =
        gasSettings.maxPriorityFeesPerGas.feePerL2Gas;

    // Fee payer
    cols[0][AVM_PUBLIC_INPUTS_FEE_PAYER_ROW_IDX] = feePayer;

    // Public setup call requests
    for (size_t i = 0; i < publicSetupCallRequests.size(); ++i) {
        size_t row = AVM_PUBLIC_INPUTS_PUBLIC_SETUP_CALL_REQUESTS_ROW_IDX + i;
        cols[0][row] = publicSetupCallRequests[i].msgSender;
        cols[1][row] = publicSetupCallRequests[i].contractAddress;
        cols[2][row] = publicSetupCallRequests[i].isStaticCall;
        cols[3][row] = publicSetupCallRequests[i].calldataHash;
    }

    // Public app logic call requests
    for (size_t i = 0; i < publicAppLogicCallRequests.size(); ++i) {
        size_t row = AVM_PUBLIC_INPUTS_PUBLIC_APP_LOGIC_CALL_REQUESTS_ROW_IDX + i;
        cols[0][row] = publicAppLogicCallRequests[i].msgSender;
        cols[1][row] = publicAppLogicCallRequests[i].contractAddress;
        cols[2][row] = publicAppLogicCallRequests[i].isStaticCall;
        cols[3][row] = publicAppLogicCallRequests[i].calldataHash;
    }

    // Public teardown call request
    size_t row = AVM_PUBLIC_INPUTS_PUBLIC_TEARDOWN_CALL_REQUEST_ROW_IDX;
    cols[0][row] = publicTeardownCallRequest.msgSender;
    cols[1][row] = publicTeardownCallRequest.contractAddress;
    cols[2][row] = publicTeardownCallRequest.isStaticCall;
    cols[3][row] = publicTeardownCallRequest.calldataHash;

    // Previous non-revertible accumulated data lengths
    cols[0][AVM_PUBLIC_INPUTS_PREVIOUS_NON_REVERTIBLE_ACCUMULATED_DATA_ARRAY_LENGTHS_ROW_IDX] =
        previousNonRevertibleAccumulatedDataArrayLengths.noteHashes;
    cols[1][AVM_PUBLIC_INPUTS_PREVIOUS_NON_REVERTIBLE_ACCUMULATED_DATA_ARRAY_LENGTHS_ROW_IDX] =
        previousNonRevertibleAccumulatedDataArrayLengths.nullifiers;
    cols[2][AVM_PUBLIC_INPUTS_PREVIOUS_NON_REVERTIBLE_ACCUMULATED_DATA_ARRAY_LENGTHS_ROW_IDX] =
        previousNonRevertibleAccumulatedDataArrayLengths.l2ToL1Msgs;

    // Previous revertible accumulated data array lengths
    cols[0][AVM_PUBLIC_INPUTS_PREVIOUS_REVERTIBLE_ACCUMULATED_DATA_ARRAY_LENGTHS_ROW_IDX] =
        previousRevertibleAccumulatedDataArrayLengths.noteHashes;
    cols[1][AVM_PUBLIC_INPUTS_PREVIOUS_REVERTIBLE_ACCUMULATED_DATA_ARRAY_LENGTHS_ROW_IDX] =
        previousRevertibleAccumulatedDataArrayLengths.nullifiers;
    cols[2][AVM_PUBLIC_INPUTS_PREVIOUS_REVERTIBLE_ACCUMULATED_DATA_ARRAY_LENGTHS_ROW_IDX] =
        previousRevertibleAccumulatedDataArrayLengths.l2ToL1Msgs;

    // Previous non-revertible accumulated data
    // note hashes
    for (size_t i = 0; i < previousNonRevertibleAccumulatedData.noteHashes.size(); ++i) {
        size_t row = AVM_PUBLIC_INPUTS_PREVIOUS_NON_REVERTIBLE_ACCUMULATED_DATA_NOTE_HASHES_ROW_IDX + i;
        cols[0][row] = previousNonRevertibleAccumulatedData.noteHashes[i];
    }
    // nullifiers
    for (size_t i = 0; i < previousNonRevertibleAccumulatedData.nullifiers.size(); ++i) {
        size_t row = AVM_PUBLIC_INPUTS_PREVIOUS_NON_REVERTIBLE_ACCUMULATED_DATA_NULLIFIERS_ROW_IDX + i;
        cols[0][row] = previousNonRevertibleAccumulatedData.nullifiers[i];
    }
    // l2_to_l1_msgs
    for (size_t i = 0; i < previousNonRevertibleAccumulatedData.l2ToL1Msgs.size(); ++i) {
        size_t row = AVM_PUBLIC_INPUTS_PREVIOUS_NON_REVERTIBLE_ACCUMULATED_DATA_L2_TO_L1_MSGS_ROW_IDX + i;
        cols[0][row] = previousNonRevertibleAccumulatedData.l2ToL1Msgs[i].message.recipient;
        cols[1][row] = previousNonRevertibleAccumulatedData.l2ToL1Msgs[i].message.content;
        cols[2][row] = previousNonRevertibleAccumulatedData.l2ToL1Msgs[i].message.counter;
        cols[3][row] = previousNonRevertibleAccumulatedData.l2ToL1Msgs[i].contractAddress;
    }

    // Previous revertible accumulated data
    // note hashes
    for (size_t i = 0; i < previousRevertibleAccumulatedData.noteHashes.size(); ++i) {
        size_t row = AVM_PUBLIC_INPUTS_PREVIOUS_REVERTIBLE_ACCUMULATED_DATA_NOTE_HASHES_ROW_IDX + i;
        cols[0][row] = previousRevertibleAccumulatedData.noteHashes[i];
    }
    // nullifiers
    for (size_t i = 0; i < previousRevertibleAccumulatedData.nullifiers.size(); ++i) {
        size_t row = AVM_PUBLIC_INPUTS_PREVIOUS_REVERTIBLE_ACCUMULATED_DATA_NULLIFIERS_ROW_IDX + i;
        cols[0][row] = previousRevertibleAccumulatedData.nullifiers[i];
    }
    // l2_to_l1_msgs
    for (size_t i = 0; i < previousRevertibleAccumulatedData.l2ToL1Msgs.size(); ++i) {
        size_t row = AVM_PUBLIC_INPUTS_PREVIOUS_REVERTIBLE_ACCUMULATED_DATA_L2_TO_L1_MSGS_ROW_IDX + i;
        cols[0][row] = previousRevertibleAccumulatedData.l2ToL1Msgs[i].message.recipient;
        cols[1][row] = previousRevertibleAccumulatedData.l2ToL1Msgs[i].message.content;
        cols[2][row] = previousRevertibleAccumulatedData.l2ToL1Msgs[i].message.counter;
        cols[3][row] = previousRevertibleAccumulatedData.l2ToL1Msgs[i].contractAddress;
    }

    // End tree snapshots
    cols[0][AVM_PUBLIC_INPUTS_END_TREE_SNAPSHOTS_L1_TO_L2_MESSAGE_TREE_ROW_IDX] =
        endTreeSnapshots.l1ToL2MessageTree.root;
    cols[1][AVM_PUBLIC_INPUTS_END_TREE_SNAPSHOTS_L1_TO_L2_MESSAGE_TREE_ROW_IDX] =
        endTreeSnapshots.l1ToL2MessageTree.nextAvailableLeafIndex;
    cols[0][AVM_PUBLIC_INPUTS_END_TREE_SNAPSHOTS_NOTE_HASH_TREE_ROW_IDX] = endTreeSnapshots.noteHashTree.root;
    cols[1][AVM_PUBLIC_INPUTS_END_TREE_SNAPSHOTS_NOTE_HASH_TREE_ROW_IDX] =
        endTreeSnapshots.noteHashTree.nextAvailableLeafIndex;
    cols[0][AVM_PUBLIC_INPUTS_END_TREE_SNAPSHOTS_NULLIFIER_TREE_ROW_IDX] = endTreeSnapshots.nullifierTree.root;
    cols[1][AVM_PUBLIC_INPUTS_END_TREE_SNAPSHOTS_NULLIFIER_TREE_ROW_IDX] =
        endTreeSnapshots.nullifierTree.nextAvailableLeafIndex;
    cols[0][AVM_PUBLIC_INPUTS_END_TREE_SNAPSHOTS_PUBLIC_DATA_TREE_ROW_IDX] = endTreeSnapshots.publicDataTree.root;
    cols[1][AVM_PUBLIC_INPUTS_END_TREE_SNAPSHOTS_PUBLIC_DATA_TREE_ROW_IDX] =
        endTreeSnapshots.publicDataTree.nextAvailableLeafIndex;

    // End gas used
    cols[0][AVM_PUBLIC_INPUTS_END_GAS_USED_ROW_IDX] = endGasUsed.daGas;
    cols[1][AVM_PUBLIC_INPUTS_END_GAS_USED_ROW_IDX] = endGasUsed.l2Gas;

    // End accumulated data
    // note hashes
    for (size_t i = 0; i < accumulatedData.noteHashes.size(); ++i) {
        size_t row = AVM_PUBLIC_INPUTS_END_ACCUMULATED_DATA_NOTE_HASHES_ROW_IDX + i;
        cols[0][row] = accumulatedData.noteHashes[i];
    }
    // nullifiers
    for (size_t i = 0; i < accumulatedData.nullifiers.size(); ++i) {
        size_t row = AVM_PUBLIC_INPUTS_END_ACCUMULATED_DATA_NULLIFIERS_ROW_IDX + i;
        cols[0][row] = accumulatedData.nullifiers[i];
    }
    // l2_to_l1_msgs
    for (size_t i = 0; i < accumulatedData.l2ToL1Msgs.size(); ++i) {
        size_t row = AVM_PUBLIC_INPUTS_END_ACCUMULATED_DATA_L2_TO_L1_MSGS_ROW_IDX + i;
        cols[0][row] = accumulatedData.l2ToL1Msgs[i].message.recipient;
        cols[1][row] = accumulatedData.l2ToL1Msgs[i].message.content;
        cols[2][row] = accumulatedData.l2ToL1Msgs[i].message.counter;
        cols[3][row] = accumulatedData.l2ToL1Msgs[i].contractAddress;
    }
    // public logs
    for (size_t i = 0; i < accumulatedData.publicLogs.size(); ++i) {
        size_t first_row_for_log =
            AVM_PUBLIC_INPUTS_END_ACCUMULATED_DATA_PUBLIC_LOGS_ROW_IDX + (i * PUBLIC_LOG_DATA_SIZE_IN_FIELDS);
        for (size_t j = 0; j < PUBLIC_LOG_DATA_SIZE_IN_FIELDS; ++j) {
            // always set contract address in col 0 so that some entry in the row is always non-zero
            cols[0][first_row_for_log + j] = accumulatedData.publicLogs[i].contractAddress;
            // and set the actual log data entry
            cols[1][first_row_for_log + j] = accumulatedData.publicLogs[i].log[j];
        }
    }
    // public data writes
    for (size_t i = 0; i < accumulatedData.publicDataWrites.size(); ++i) {
        size_t row = AVM_PUBLIC_INPUTS_END_ACCUMULATED_DATA_PUBLIC_DATA_WRITES_ROW_IDX + i;
        cols[0][row] = accumulatedData.publicDataWrites[i].leafSlot;
        cols[1][row] = accumulatedData.publicDataWrites[i].value;
    }

    // transaction fee
    cols[0][AVM_PUBLIC_INPUTS_TRANSACTION_FEE_ROW_IDX] = transactionFee;

    // reverted
    cols[0][AVM_PUBLIC_INPUTS_REVERTED_ROW_IDX] = static_cast<uint8_t>(reverted);

    return cols;
}

} // namespace bb::avm2
