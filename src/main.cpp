#include "gbi/commands.hpp"
#include "gbi/util.hpp"

#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct ParsedArgs {
  std::string command;
  std::map<std::string, std::string> options;
  std::set<std::string> flags;

  bool has(const std::string& key) const {
    return flags.find(key) != flags.end() || options.find(key) != options.end();
  }

  std::string value(const std::string& key,
                    const std::string& fallback = {}) const {
    const auto iterator = options.find(key);
    return iterator == options.end() ? fallback : iterator->second;
  }
};

ParsedArgs parseArgs(int argc, char** argv) {
  static const std::set<std::string> boolean_options = {
      "force", "clear", "help", "version"};
  ParsedArgs parsed;
  std::vector<std::string> positional;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (gbi::startsWith(argument, "--")) {
      std::string key = argument.substr(2);
      const std::size_t equals = key.find('=');
      if (equals != std::string::npos) {
        parsed.options[key.substr(0, equals)] = key.substr(equals + 1);
        continue;
      }
      if (boolean_options.find(key) != boolean_options.end()) {
        parsed.flags.insert(key);
        continue;
      }
      if (index + 1 >= argc) {
        throw std::runtime_error("missing value for --" + key);
      }
      parsed.options[key] = argv[++index];
    } else {
      positional.push_back(argument);
    }
  }
  if (!positional.empty()) {
    parsed.command = positional.front();
  }
  return parsed;
}

void printHelp() {
  std::cout
      << "GBuffer Inspector 1.0 - deferred rendering GBuffer local correction\n"
      << "Stack: C++17 / Vulkan 1.1 / GLFW 3\n\n"
      << "Usage:\n"
      << "  gbuffer-inspector init [--workspace DIR] [--force]\n"
      << "  gbuffer-inspector status [--workspace DIR]\n"
      << "  gbuffer-inspector anchors [--workspace DIR]\n"
      << "  gbuffer-inspector correct --object ID [--albedo #RRGGBB]\n"
      << "      [--roughness 0..1] [--metallic 0..1] [--clear]\n"
      << "  gbuffer-inspector capture --anchor A0|A1|A2 [--object ID]\n"
      << "      [--revision LABEL] [--note TEXT] [--width 640] [--height 360]\n"
      << "  gbuffer-inspector compare [--object ID] [--from A0] [--to A1|A2]\n"
      << "      [--left-capture ID] [--right-capture ID]\n"
      << "  gbuffer-inspector export [--object ID] [--from A0] [--to A1|A2]\n"
      << "      [--out DIR] [--left-capture ID] [--right-capture ID]\n"
      << "  gbuffer-inspector verify [--workspace DIR]\n"
      << "  gbuffer-inspector resume [--workspace DIR]\n"
      << "  gbuffer-inspector demo [--workspace DIR] [--force]\n"
      << "  gbuffer-inspector run [--workspace DIR]\n\n"
      << "Anchors:\n"
      << "  A0  immutable original full-scene GBuffer\n"
      << "  A1  one object isolated rerun with its local overlay\n"
      << "  A2  full-scene acceptance recomposition with all overlays\n\n"
      << "The original material is imported read-only under\n"
      << "  <workspace>/originals/scene.json\n"
      << "and is never modified by corrections.\n";
}

std::uint32_t optionUint(const ParsedArgs& args, const std::string& key,
                         std::uint32_t fallback) {
  if (!args.has(key)) {
    return fallback;
  }
  std::uint32_t value = 0;
  if (!gbi::parseUint(args.value(key), value) || value == 0) {
    throw std::runtime_error("--" + key + " requires a positive integer");
  }
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  gbi::InterruptGuard interrupt_guard;
  try {
    const ParsedArgs args = parseArgs(argc, argv);
    if (args.has("version")) {
      std::cout << "gbuffer-inspector 1.0 (C++17, Vulkan, GLFW)\n";
      return 0;
    }
    if (args.command.empty() || args.has("help")) {
      printHelp();
      return 0;
    }
    gbi::CommonOptions common;
    if (!args.value("workspace").empty()) {
      common.workspace = args.value("workspace");
    }
    const std::string command = gbi::toLower(args.command);
    if (command == "init") {
      return gbi::commandInit(common, args.has("force"));
    }
    if (command == "status") {
      return gbi::commandStatus(common);
    }
    if (command == "anchors") {
      return gbi::commandAnchors(common);
    }
    if (command == "capture") {
      gbi::CaptureOptions options;
      options.workspace = common.workspace;
      options.anchor = args.value("anchor");
      options.object_id = args.value("object");
      options.revision = args.value("revision", "manual");
      options.note = args.value("note");
      options.width = optionUint(args, "width", 640);
      options.height = optionUint(args, "height", 360);
      if (options.anchor.empty()) {
        throw std::runtime_error("capture requires --anchor A0|A1|A2");
      }
      return gbi::commandCapture(options);
    }
    if (command == "correct") {
      const std::string object_id = args.value("object");
      if (object_id.empty()) {
        throw std::runtime_error("correct requires --object ID");
      }
      return gbi::commandCorrect(common, object_id, args.value("albedo"),
                                 args.value("roughness"),
                                 args.value("metallic"), args.has("clear"));
    }
    if (command == "compare" || command == "export") {
      gbi::ExportOptions options;
      options.workspace = common.workspace;
      options.object_id = args.value("object");
      options.left_anchor = args.value("from", "A0");
      options.right_anchor = args.value("to", "A1");
      options.left_capture = args.value("left-capture");
      options.right_capture = args.value("right-capture");
      options.output = args.value("out");
      return command == "compare" ? gbi::commandCompare(options)
                                  : gbi::commandExport(options);
    }
    if (command == "verify") {
      return gbi::commandVerify(common);
    }
    if (command == "resume") {
      return gbi::commandResume(common);
    }
    if (command == "demo") {
      return gbi::commandDemo(common, args.has("force"));
    }
    if (command == "run") {
      return gbi::commandRun(common);
    }
    throw std::runtime_error("unknown command: " + args.command);
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    if (gbi::InterruptGuard::requested()) {
      std::cerr << "interrupted; run the matching command again or use "
                   "`resume`\n";
      return 130;
    }
    return 2;
  }
}
