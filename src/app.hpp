#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace inspect {

enum class Command {
    Help,
    Run,
    Resume,
    Compare,
    Acceptance,
    Gui,
};

struct AppOptions {
    Command command = Command::Help;
    std::filesystem::path run_dir;
    std::filesystem::path material = GS_DEFAULT_MATERIAL;
    std::string requested_mode = "auto";
    std::uint64_t memory_budget_bytes = 0;
    std::uint32_t width = 512;
    std::uint32_t height = 512;
    bool force_low_memory = false;
    bool headless = false;
    bool verbose = false;
};

int runApplication(const AppOptions& options);
std::string commandHelp();

}  // namespace inspect
