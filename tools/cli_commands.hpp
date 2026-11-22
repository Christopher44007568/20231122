#pragma once

#include <string>

#include "camtrak/engine.hpp"
#include "camtrak/store.hpp"

namespace camtrak {

int cliInitDemo(const std::string& project_dir);
int cliStatus(Store& store);
int cliRun(Store& store, Engine& engine,
           const std::vector<std::pair<std::string, std::string>>& options);
int cliResume(Store& store, Engine& engine,
              const std::vector<std::pair<std::string, std::string>>& options);
int cliCompare(Store& store, Engine& engine,
               const std::vector<std::pair<std::string, std::string>>&
                   options);
int cliExport(Store& store,
              const std::vector<std::pair<std::string, std::string>>& options);
int cliVerify(Store& store, Engine& engine,
              const std::vector<std::pair<std::string, std::string>>&
                  options);
int cliLog(Store& store);

}  // namespace camtrak
