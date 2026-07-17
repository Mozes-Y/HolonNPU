#include "tb_coverage.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <verilated.h>
#include <verilated_cov.h>

namespace holon_npu_tb {

namespace {

constexpr std::string_view kCoverageRootArg = "--tb-coverage-root";

std::string sanitize_name(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (const char ch : name) {
        const bool keep = ((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z')) ||
                          ((ch >= '0') && (ch <= '9')) || (ch == '_') || (ch == '-');
        out.push_back(keep ? ch : '_');
    }
    return out;
}

void write_points(const std::filesystem::path& path, const std::vector<coverage_point>& points) {
    std::vector<coverage_point> sorted = points;
    std::ranges::sort(sorted);
    const auto duplicates = std::ranges::unique(sorted);
    sorted.erase(duplicates.begin(), duplicates.end());

    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    for (const coverage_point point : sorted) {
        out << coverage_point_name(point) << '\n';
    }
}

std::vector<coverage_point> all_coverage_points() {
    std::vector<coverage_point> points;
    points.reserve(coverage_registry.size());
    for (const coverage_point_info& info : coverage_registry) {
        points.push_back(info.point);
    }
    return points;
}

bool is_coverage_root_arg(std::string_view arg) {
    return arg == kCoverageRootArg || arg.starts_with(std::string{kCoverageRootArg} + "=");
}

std::string_view coverage_root_value(std::string_view arg) {
    const std::string prefix = std::string{kCoverageRootArg} + "=";
    if (!arg.starts_with(prefix)) {
        return {};
    }
    return arg.substr(prefix.size());
}

}  // namespace

std::string_view coverage_point_name(coverage_point point) {
    for (const coverage_point_info& info : coverage_registry) {
        if (info.point == point) {
            return info.name;
        }
    }
    throw std::logic_error{"unknown coverage point"};
}

test_run::test_run(std::string_view test_name, int argc, char** argv)
    : test_name_{test_name},
      sanitized_name_{sanitize_name(test_name)} {
    verilator_args_.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        const std::string_view arg{argv[index]};
        if (arg == kCoverageRootArg) {
            if ((index + 1) >= argc) {
                throw std::invalid_argument{"--tb-coverage-root requires a path"};
            }
            coverage_root_ = argv[++index];
            continue;
        }
        if (is_coverage_root_arg(arg)) {
            coverage_root_ = coverage_root_value(arg);
            continue;
        }
        verilator_args_.emplace_back(arg);
    }

    verilator_argv_.reserve(verilator_args_.size());
    for (std::string& arg : verilator_args_) {
        verilator_argv_.push_back(arg.data());
    }
    Verilated::commandArgs(static_cast<int>(verilator_argv_.size()), verilator_argv_.data());
}

void test_run::observe(coverage_point point, bool verified) {
    if (verified) {
        hit_points_.push_back(point);
    }
}

void test_run::observe(std::initializer_list<coverage_point> points, bool verified) {
    if (!verified) {
        return;
    }
    for (const coverage_point point : points) {
        hit_points_.push_back(point);
    }
}

int test_run::finish(bool passed) const {
    if (!passed) {
        return 1;
    }

    if (!coverage_root_.empty()) {
        const std::filesystem::path functional_dir = coverage_root_ / "functional";
        const std::filesystem::path stem = sanitized_name_ + ".txt";

        write_points(functional_dir / "required" / stem, all_coverage_points());
        write_points(functional_dir / "hit" / stem, hit_points_);

        const std::filesystem::path raw_path = coverage_root_ / "raw" / (sanitized_name_ + ".dat");
        std::filesystem::create_directories(raw_path.parent_path());
        VerilatedCov::write(raw_path.string().c_str());
    }

    return 0;
}

}  // namespace holon_npu_tb
