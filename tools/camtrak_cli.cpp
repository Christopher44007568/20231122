#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cli_commands.hpp"
#include "camtrak/engine.hpp"
#include "camtrak/store.hpp"

namespace {

using camtrak::Store;
struct Args {
  std::string command;
  std::vector<std::string> positional;
  std::vector<std::pair<std::string, std::string>> options;
};

Args parseArgs(int argc, char** argv) {
  Args args;
  if (argc >= 2) args.command = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string token = argv[i];
    if (token.rfind("--", 0) == 0) {
      const std::string key = token.substr(2);
      if (i + 1 < argc) {
        args.options.emplace_back(key, argv[++i]);
      } else {
        args.options.emplace_back(key, "true");
      }
    } else {
      args.positional.push_back(token);
    }
  }
  return args;
}

void printUsage() {
  std::cout
      << "camtrak - camera trajectory interpolation correction tool\n\n"
      << "Usage:\n"
      << "  camtrak init-demo --project DIR\n"
      << "  camtrak status --project DIR\n"
      << "  camtrak run --project DIR --object ID --t0 T --t1 T "
         "[--period S] [--method linear|cubic]\n"
      << "  camtrak resume --project DIR --object ID\n"
      << "  camtrak compare --project DIR --run-a ID --run-b ID\n"
      << "  camtrak export --project DIR --batch ID --runs ID1,ID2 "
         "[--note TEXT]\n"
      << "  camtrak verify --project DIR [--run ID]\n"
      << "  camtrak log --project DIR\n";
}

}  // namespace

int main(int argc, char** argv) {
  const Args args = parseArgs(argc, argv);
  auto option = [&](const std::string& name, const std::string& fallback = "") {
    for (const auto& [key, value] : args.options) {
      if (key == name) return value;
    }
    return fallback;
  };
  if (args.command.empty() || args.command == "help") {
    printUsage();
    return 0;
  }
  try {
    const std::string project_dir = option("project", ".");
    if (args.command == "init-demo") {
      return camtrak::cliInitDemo(project_dir);
    }
    Store store(project_dir);
    camtrak::Engine engine(store);
    if (args.command == "status") {
      return camtrak::cliStatus(store);
    }
    if (args.command == "run") {
      return camtrak::cliRun(store, engine, args.options);
    }
    if (args.command == "resume") {
      return camtrak::cliResume(store, engine, args.options);
    }
    if (args.command == "compare") {
      return camtrak::cliCompare(store, engine, args.options);
    }
    if (args.command == "export") {
      return camtrak::cliExport(store, args.options);
    }
    if (args.command == "verify") {
      return camtrak::cliVerify(store, engine, args.options);
    }
    if (args.command == "log") {
      return camtrak::cliLog(store);
    }
    std::cerr << "unknown command: " << args.command << "\n";
    printUsage();
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
