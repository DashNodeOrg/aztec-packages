#include "get_bn254_crs.hpp"
#include "barretenberg/api/file_io.hpp"

namespace {
std::vector<uint8_t> download_bn254_g1_data(size_t num_points)
{
    size_t g1_end = num_points * 64 - 1;

    std::string url = "https://aztec-ignition.s3.amazonaws.com/MAIN%20IGNITION/flat/g1.dat";

    // IMPORTANT: this currently uses a shell, DO NOT let user-controlled strings here.
    std::string command = "curl -H \"Range: bytes=0-" + std::to_string(g1_end) + "\" '" + url + "'";

    auto data = bb::exec_pipe(command);
    // Header + num_points * sizeof point.
    if (data.size() < g1_end) {
        THROW std::runtime_error("Failed to download g1 data.");
    }

    return data;
}

std::vector<uint8_t> download_bn254_g2_data()
{
    std::string url = "https://aztec-ignition.s3.amazonaws.com/MAIN%20IGNITION/flat/g2.dat";
    // IMPORTANT: this currently uses a shell, DO NOT let user-controlled strings here.
    std::string command = "curl '" + url + "'";
    return bb::exec_pipe(command);
}
} // namespace

namespace bb {
std::vector<g1::affine_element> get_bn254_g1_data(const std::filesystem::path& path, size_t num_points)
{
    // TODO(AD): per Charlie this should just download and replace the flat file portion atomically so we have no race
    // condition
    std::filesystem::create_directories(path);

    auto g1_path = path / "bn254_g1.dat";
    size_t g1_file_size = get_file_size(g1_path);

    if (g1_file_size >= num_points * 64 && g1_file_size % 64 == 0) {
        vinfo("using cached bn254 crs of size ", std::to_string(g1_file_size / 64), " at ", g1_path);
        auto data = read_file(g1_path, num_points * 64);
        auto points = std::vector<g1::affine_element>(num_points);
        for (size_t i = 0; i < num_points; ++i) {
            points[i] = from_buffer<g1::affine_element>(data, i * 64);
        }
        return points;
    }

    vinfo("downloading bn254 crs...");
    auto data = download_bn254_g1_data(num_points);
    write_file(g1_path, data);

    auto points = std::vector<g1::affine_element>(num_points);
    for (size_t i = 0; i < num_points; ++i) {
        points[i] = from_buffer<g1::affine_element>(data, i * 64);
    }
    return points;
}

g2::affine_element get_bn254_g2_data(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path);

    auto g2_path = path / "bn254_g2.dat";
    size_t g2_file_size = get_file_size(g2_path);

    if (g2_file_size == 128) {
        auto data = read_file(g2_path);
        return from_buffer<g2::affine_element>(data.data());
    }

    auto data = download_bn254_g2_data();
    write_file(g2_path, data);
    return from_buffer<g2::affine_element>(data.data());
}
} // namespace bb
