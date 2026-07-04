#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace holon_npu_tb {

struct GeneratedGemmCase {
    int m;
    int n;
    int k;
    std::uint32_t seed;
    std::string name;
};

inline std::vector<GeneratedGemmCase> fixed_shape_anchors(std::string prefix) {
    return {
        GeneratedGemmCase{1, 1, 1, 1, prefix + "1x1x1"},
        GeneratedGemmCase{16, 16, 16, 2, prefix + "16x16x16"},
        GeneratedGemmCase{17, 19, 23, 3, prefix + "17x19x23"},
        GeneratedGemmCase{64, 64, 64, 4, prefix + "64x64x64"},
    };
}

inline std::vector<GeneratedGemmCase> constrained_random_gemm_cases(
    std::uint32_t base_seed,
    int count,
    std::string prefix
) {
    std::vector<GeneratedGemmCase> cases;
    cases.reserve(static_cast<std::size_t>(count));

    const std::vector<GeneratedGemmCase> anchors = {
        {1, 1, 1, base_seed + 1, prefix + "shape-1"},
        {7, 5, 3, base_seed + 2, prefix + "shape-lt16"},
        {16, 16, 16, base_seed + 3, prefix + "shape-16"},
        {17, 16, 16, base_seed + 4, prefix + "m-tail"},
        {16, 19, 16, base_seed + 5, prefix + "n-tail"},
        {16, 16, 23, base_seed + 6, prefix + "k-tail"},
        {31, 33, 29, base_seed + 7, prefix + "mixed-tail"},
        {48, 32, 16, base_seed + 8, prefix + "multi-tile"},
        {64, 64, 64, base_seed + 9, prefix + "shape-64"},
    };

    for (const auto& test_case : anchors) {
        if (static_cast<int>(cases.size()) >= count) {
            return cases;
        }
        cases.push_back(test_case);
    }

    std::mt19937 rng(base_seed);
    const std::vector<int> shape_values = {
        1, 2, 3, 5, 7, 11, 15, 16, 17, 18, 19, 23, 31, 32, 33, 47, 48, 63, 64
    };

    while (static_cast<int>(cases.size()) < count) {
        const auto pick = [&](std::uint32_t salt) {
            const auto index = (rng() + salt) % shape_values.size();
            return shape_values.at(index);
        };

        const auto index = cases.size();
        const int m = pick(0);
        const int n = pick(17);
        const int k = pick(31);
        const auto seed = static_cast<std::uint32_t>(base_seed + 1000U + index);
        cases.push_back(GeneratedGemmCase{
            m,
            n,
            k,
            seed,
            prefix + "cr-" + std::to_string(index) + "-" + std::to_string(m) + "x" +
                std::to_string(n) + "x" + std::to_string(k),
        });
    }

    return cases;
}

}  // namespace holon_npu_tb
