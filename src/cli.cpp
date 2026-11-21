#include "cli.h"

#include "app/vulkan_app.h"
#include "core/acceptance.h"
#include "core/time_util.h"
#include "core/workspace.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace clipfix {
namespace {

using Json = nlohmann::json;

struct CommandLine {
    std::string command = "gui";
    std::map<std::string, std::string> values;
    std::set<std::string> flags;
};

const std::set<std::string> kValueOptions = {
    "workspace", "material", "object", "offsets", "out", "reason", "frames",
    "screenshot", "shot-frame", "view"};
const std::set<std::string> kFlagOptions = {
    "json", "previous", "no-fill", "help", "version", "no-surface", "wireframe"};
const std::set<std::string> kCommands = {
    "init", "run", "run-all", "compare", "export", "status", "verify",
    "checkpoint", "accept", "gui", "help", "version"};

void printUsage() {
    std::cout
        << "Curved Trim Plane / 曲面裁剪平面局部修正工具\n\n"
        << "用法:\n"
        << "  curved_trim [gui] [--workspace DIR] [--frames N]\n"
        << "               [--screenshot FILE --shot-frame N]\n"
        << "               [--view corrected|original|diff] [--no-surface] [--wireframe]\n"
        << "  curved_trim init [--workspace DIR] [--material DIR]\n"
        << "  curved_trim run --object ID|all [--offsets A,B,C]\n"
        << "  curved_trim run-all [--offsets A,B,C]\n"
        << "  curved_trim compare --object ID [--previous]\n"
        << "  curved_trim export --object ID|all [--out DIR]\n"
        << "  curved_trim status [--json]\n"
        << "  curved_trim verify\n"
        << "  curved_trim checkpoint --reason TEXT\n"
        << "  curved_trim accept [--out DIR] [--no-fill]\n\n"
        << "每次进程退出后重新运行命令会自动载入 workspace/session.json。\n"
        << "GUI 按 Esc 保存并退出；原始材料只读副本位于 workspace/original_material。\n";
}

CommandLine parseCommandLine(int argc, char** argv) {
    CommandLine parsed;
    bool commandSeen = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--") {
            continue;
        }
        if (argument.rfind("--", 0) == 0) {
            const std::string option = argument.substr(2);
            const auto equals = option.find('=');
            const std::string name = option.substr(0, equals);
            const bool inlineValue = equals != std::string::npos;
            if (kFlagOptions.count(name) != 0) {
                if (inlineValue) {
                    throw std::runtime_error("--" + name + " does not take a value");
                }
                parsed.flags.insert(name);
            } else if (kValueOptions.count(name) != 0) {
                if (inlineValue) {
                    parsed.values[name] = option.substr(equals + 1);
                } else if (i + 1 >= argc) {
                    throw std::runtime_error("missing value for --" + name);
                } else {
                    parsed.values[name] = argv[++i];
                }
            } else {
                throw std::runtime_error("unknown option: " + argument);
            }
            continue;
        }
        if (!commandSeen) {
            if (kCommands.count(argument) == 0) {
                throw std::runtime_error("unknown command: " + argument);
            }
            parsed.command = argument;
            commandSeen = true;
        } else {
            throw std::runtime_error("unexpected argument: " + argument);
        }
    }
    return parsed;
}

std::filesystem::path workspacePath(const CommandLine& commandLine) {
    if (commandLine.values.count("workspace") != 0) {
        return commandLine.values.at("workspace");
    }
    if (const char* environment = std::getenv("CLIPFIX_WORKSPACE"); environment != nullptr) {
        return environment;
    }
    return "workspace";
}

std::filesystem::path materialPath(const CommandLine& commandLine) {
    if (commandLine.values.count("material") != 0) {
        return commandLine.values.at("material");
    }
    if (const char* environment = std::getenv("CLIPFIX_RAW_DIR"); environment != nullptr) {
        return environment;
    }
    return std::filesystem::path(CLIPFIX_SOURCE_DIR) / "materials" / "raw";
}

std::filesystem::path acceptancePath(const CommandLine& commandLine) {
    if (commandLine.values.count("out") != 0) {
        return commandLine.values.at("out");
    }
    if (const char* environment = std::getenv("CLIPFIX_ARTIFACTS"); environment != nullptr) {
        return std::filesystem::path(environment) / "acceptance";
    }
    return std::filesystem::path(CLIPFIX_SOURCE_DIR) / "artifacts" / "acceptance";
}

Workspace openWorkspace(const CommandLine& commandLine) {
    return Workspace::open({workspacePath(commandLine), materialPath(commandLine)});
}

std::string selectedObject(const Workspace& workspace) {
    if (workspace.state().objects.empty()) {
        throw std::runtime_error("workspace has no render objects");
    }
    return workspace.state().objects[workspace.state().selectedObject].id;
}

std::array<double, 3> parseOffsets(const std::string& value) {
    std::array<double, 3> offsets{};
    std::istringstream input(value);
    std::string part;
    int count = 0;
    while (std::getline(input, part, ',')) {
        if (count >= 3) {
            throw std::runtime_error("--offsets requires exactly A,B,C");
        }
        std::size_t consumed = 0;
        offsets[count] = std::stod(part, &consumed);
        if (consumed != part.size()) {
            throw std::runtime_error("invalid offset value: " + part);
        }
        ++count;
    }
    if (count != 3) {
        throw std::runtime_error("--offsets requires exactly A,B,C");
    }
    return offsets;
}

void printAnchors(const Workspace& workspace) {
    const auto raw = workspace.rawOffsets();
    const auto current = workspace.state().currentOffsetsMm;
    const auto positions = workspace.anchorPositions();
    for (std::size_t i = 0; i < 3; ++i) {
        std::cout << "  " << workspace.material().scene.anchors[i].name
                  << " pos=(" << formatFixed(positions[i].x) << ","
                  << formatFixed(positions[i].y) << ","
                  << formatFixed(positions[i].z) << ")"
                  << " raw=" << formatMm(raw[i])
                  << " applied=" << formatMm(current[i]) << "\n";
    }
}

void printRun(const RunRecord& run) {
    std::cout << "  run " << run.runId << "\n"
              << "    started  " << run.startedUtc << "\n"
              << "    finished " << run.finishedUtc << "\n"
              << "    raw offsets     " << formatFixed(run.rawOffsetsMm[0]) << ", "
              << formatFixed(run.rawOffsetsMm[1]) << ", "
              << formatFixed(run.rawOffsetsMm[2]) << " mm\n"
              << "    applied offsets " << formatFixed(run.appliedOffsetsMm[0]) << ", "
              << formatFixed(run.appliedOffsetsMm[1]) << ", "
              << formatFixed(run.appliedOffsetsMm[2]) << " mm\n"
              << "    affected " << run.stats.affectedCount << "/"
              << run.stats.vertexCount << " max before "
              << formatMm(run.stats.maxAbsBeforeMm) << " max after "
              << formatMm(run.stats.maxAbsAfterMm) << "\n"
              << "    output " << run.outputObjPath << "\n"
              << "    sha256 " << run.outputObjSha256 << "\n";
}

void printVerification(const VerificationResult& verification) {
    std::cout << "raw material : " << (verification.rawMaterialOk ? "OK" : "FAIL") << "\n"
              << "evidence     : " << (verification.evidenceChainOk ? "OK" : "FAIL")
              << " (" << verification.evidenceEventCount << " events)\n"
              << "session      : " << (verification.sessionLoaded ? "OK" : "FAIL") << "\n"
              << "aggregate    : " << verification.aggregateSha256 << "\n";
    if (!verification.rawError.empty()) {
        std::cout << "raw error    : " << verification.rawError << "\n";
    }
    if (!verification.evidenceError.empty()) {
        std::cout << "evidence err : " << verification.evidenceError << "\n";
    }
}

int commandRun(const CommandLine& commandLine) {
    Workspace workspace = openWorkspace(commandLine);
    if (commandLine.values.count("offsets") != 0) {
        workspace.setCurrentOffsets(parseOffsets(commandLine.values.at("offsets")));
    }
    const std::string requested =
        commandLine.values.count("object") != 0
        ? commandLine.values.at("object")
        : selectedObject(workspace);
    std::cout << "workspace: " << workspace.root().string() << "\n";
    if (requested == "all") {
        for (const auto& run : workspace.runAllObjects()) {
            printRun(run);
        }
    } else {
        printRun(workspace.runObject(requested));
    }
    return 0;
}

int commandExport(const CommandLine& commandLine) {
    Workspace workspace = openWorkspace(commandLine);
    const std::string requested =
        commandLine.values.count("object") != 0
        ? commandLine.values.at("object")
        : selectedObject(workspace);
    std::vector<ExportRecord> records;
    if (requested == "all") {
        for (const auto& object : workspace.material().scene.objects) {
            records.push_back(workspace.exportObject(object.id));
        }
    } else {
        records.push_back(workspace.exportObject(requested));
    }
    std::filesystem::path extraOut;
    if (commandLine.values.count("out") != 0) {
        extraOut = commandLine.values.at("out");
        std::filesystem::create_directories(extraOut);
    }
    for (const auto& record : records) {
        std::cout << record.objectId << " exported\n"
                  << "  obj  " << workspace.path(record.objPath).string() << "\n"
                  << "  meta " << workspace.path(record.reportPath).string() << "\n"
                  << "  sha  " << record.objSha256 << "\n";
        if (!extraOut.empty()) {
            std::filesystem::copy_file(
                workspace.path(record.objPath),
                extraOut / std::filesystem::path(record.objPath).filename(),
                std::filesystem::copy_options::overwrite_existing);
            std::filesystem::copy_file(
                workspace.path(record.reportPath),
                extraOut / std::filesystem::path(record.reportPath).filename(),
                std::filesystem::copy_options::overwrite_existing);
        }
    }
    return 0;
}

int commandAccept(const CommandLine& commandLine) {
    Workspace workspace = openWorkspace(commandLine);
    AcceptanceOptions options;
    options.reportDirectory = acceptancePath(commandLine);
    options.fillMissingEvidence = commandLine.flags.count("no-fill") == 0;
    const AcceptanceReport report = runAcceptance(workspace, options);
    std::cout << "acceptance: " << (report.pass ? "PASS" : "FAIL") << "\n"
              << "  json     " << report.jsonPath.string() << "\n"
              << "  markdown " << report.markdownPath.string() << "\n"
              << "  timeline " << report.timelineCsvPath.string() << "\n";
    for (const auto& failure : report.failures) {
        std::cout << "  FAILED: " << failure << "\n";
    }
    return report.pass ? 0 : 1;
}

}  // namespace

int runCli(int argc, char** argv) {
    try {
        const CommandLine commandLine = parseCommandLine(argc, argv);
        if (commandLine.flags.count("help") != 0 || commandLine.command == "help") {
            printUsage();
            return 0;
        }
        if (commandLine.flags.count("version") != 0 || commandLine.command == "version") {
            std::cout << "curved_trim 1.0 / C++17 / Vulkan / GLFW\n";
            return 0;
        }

        if (commandLine.command == "init") {
            Workspace workspace = openWorkspace(commandLine);
            std::cout << "initialized: " << workspace.root().string() << "\n"
                      << "raw copy:    " << workspace.rawCopyRoot().string() << "\n"
                      << "aggregate:   " << workspace.aggregateSha256() << "\n"
                      << "anchors:\n";
            printAnchors(workspace);
            return 0;
        }
        if (commandLine.command == "run") {
            return commandRun(commandLine);
        }
        if (commandLine.command == "run-all") {
            Workspace workspace = openWorkspace(commandLine);
            if (commandLine.values.count("offsets") != 0) {
                workspace.setCurrentOffsets(parseOffsets(commandLine.values.at("offsets")));
            }
            for (const auto& run : workspace.runAllObjects()) {
                printRun(run);
            }
            return 0;
        }
        if (commandLine.command == "compare") {
            Workspace workspace = openWorkspace(commandLine);
            const std::string object =
                commandLine.values.count("object") != 0
                ? commandLine.values.at("object")
                : selectedObject(workspace);
            const auto compare = workspace.compareObject(
                object,
                commandLine.flags.count("previous") != 0);
            std::cout << "compared " << compare.aKind << " -> " << compare.bKind << "\n"
                      << "  time       " << compare.createdUtc << "\n"
                      << "  changed    " << compare.changedVertices << "/"
                      << compare.comparedVertices << "\n"
                      << "  max delta  " << formatMm(compare.maxDeltaMm) << "\n"
                      << "  mean delta " << formatMm(compare.meanDeltaMm) << "\n"
                      << "  report     " << workspace.path(compare.reportPath).string() << "\n";
            return 0;
        }
        if (commandLine.command == "export") {
            return commandExport(commandLine);
        }
        if (commandLine.command == "status") {
            Workspace workspace = openWorkspace(commandLine);
            const auto verification = workspace.verify();
            if (commandLine.flags.count("json") != 0) {
                Json output = workspace.buildSessionJson();
                output["workspace"] = workspace.root().string();
                output["raw_copy"] = workspace.rawCopyRoot().string();
                output["raw_aggregate_sha256"] = workspace.aggregateSha256();
                output["raw_offsets_mm"] = workspace.rawOffsets();
                output["verification"] = {
                    {"raw_material_ok", verification.rawMaterialOk},
                    {"evidence_chain_ok", verification.evidenceChainOk},
                    {"session_loaded", verification.sessionLoaded},
                    {"evidence_event_count", verification.evidenceEventCount},
                    {"raw_error", verification.rawError},
                    {"evidence_error", verification.evidenceError},
                };
                std::cout << output.dump(2) << "\n";
            } else {
                std::cout << "workspace:  " << workspace.root().string() << "\n"
                          << "session:    revision=" << workspace.state().revision
                          << " selected=" << selectedObject(workspace)
                          << " suspended=" << (workspace.state().suspended ? "yes" : "no")
                          << "\n"
                          << "last suspend: " << workspace.state().lastSuspendAt << "\n"
                          << "last resume:  " << workspace.state().lastResumeAt
                          << " (count=" << workspace.state().resumeCount << ")\n"
                          << "raw copy:   " << workspace.rawCopyRoot().string() << "\n";
                printAnchors(workspace);
                for (const auto& object : workspace.state().objects) {
                    std::cout << "  " << object.id << " dirty="
                              << (object.dirty ? "yes" : "no")
                              << " runs=" << object.runs.size();
                    if (object.hasCompare) {
                        std::cout << " compare=" << object.lastCompare.createdUtc;
                    }
                    if (object.hasExport) {
                        std::cout << " export=" << object.lastExport.exportedUtc;
                    }
                    if (!object.runs.empty()) {
                        std::cout << " last_run=" << object.runs.back().finishedUtc;
                    }
                    std::cout << "\n";
                }
                printVerification(verification);
            }
            return 0;
        }
        if (commandLine.command == "verify") {
            Workspace workspace = openWorkspace(commandLine);
            const auto verification = workspace.verify();
            printVerification(verification);
            return verification.rawMaterialOk && verification.evidenceChainOk &&
                           verification.sessionLoaded
                ? 0
                : 1;
        }
        if (commandLine.command == "checkpoint") {
            Workspace workspace = openWorkspace(commandLine);
            const std::string reason =
                commandLine.values.count("reason") != 0
                ? commandLine.values.at("reason")
                : "manual_checkpoint";
            workspace.suspend(reason);
            std::cout << "checkpointed at " << workspace.state().lastSuspendAt << "\n"
                      << "reason: " << reason << "\n"
                      << "reopen any command to emit session_resumed evidence\n";
            return 0;
        }
        if (commandLine.command == "accept") {
            return commandAccept(commandLine);
        }
        if (commandLine.command == "gui") {
            GuiOptions options;
            options.workspace = workspacePath(commandLine);
            options.material = materialPath(commandLine);
            if (commandLine.values.count("frames") != 0) {
                options.autoExitFrames = std::stoi(commandLine.values.at("frames"));
            }
            if (commandLine.values.count("screenshot") != 0) {
                options.screenshot = commandLine.values.at("screenshot");
            }
            if (commandLine.values.count("shot-frame") != 0) {
                options.screenshotAfterFrames =
                    std::stoi(commandLine.values.at("shot-frame"));
            }
            if (commandLine.values.count("view") != 0) {
                options.view = commandLine.values.at("view");
            }
            options.showClipSurface = commandLine.flags.count("no-surface") == 0;
            options.showWireframe = commandLine.flags.count("wireframe") != 0;
            return runGui(options);
        }
        printUsage();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}

}  // namespace clipfix
