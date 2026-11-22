#include "audit_types.h"
#include "report.h"
#include "vulkan_runner.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using audit::AuditBundle;
using audit::AutoVerdict;
using audit::ReportPaths;
using audit::VulkanRunner;

namespace {

std::string verdictConsole(AutoVerdict verdict) {
  switch (verdict) {
    case AutoVerdict::Occluded:
      return "OCCLUDED";
    case AutoVerdict::NotOccluded:
      return "NOT_OCCLUDED";
    case AutoVerdict::Cancelled:
      return "CANCELLED";
    case AutoVerdict::Undecidable:
      return "UNDECIDABLE";
  }
  return "UNKNOWN";
}

}  // namespace

int main(int argc, char** argv) {
  std::string outputDir = "out";
  for (int i = 1; i + 1 < argc; ++i) {
    std::string flag = argv[i];
    if ((flag == "--out" || flag == "-o") && i + 1 < argc) {
      outputDir = argv[++i];
    }
  }
  std::error_code ec;
  std::filesystem::create_directories(outputDir, ec);
  if (ec) {
    std::cerr << "无法创建输出目录: " << outputDir << " ("
              << ec.message() << ")\n";
    return 2;
  }

  VulkanRunner runner;
  std::string error;
  if (!runner.initialize(error)) {
    std::cerr << "初始化失败: " << error << "\n";
    std::cerr << "可执行后续操作:\n"
              << "  1. 确认 Vulkan 驱动已安装并设置 "
                 "VK_ICD_FILENAMES 指向可用 ICD（如 lvp_icd.json）\n"
              << "  2. 无显示环境时本程序使用 GLFW 隐藏窗口；若仍失败，"
                 "检查 Wayland/X11 socket 或使用 Xvfb\n"
              << "  3. 重新运行: " << argv[0] << " --out " << outputDir
              << "\n";
    return 1;
  }

  AuditBundle bundle = runner.runAllScenarios();
  runner.shutdown();

  std::string rawLedger = audit::buildRawInputLedger(bundle);
  bundle.inputSha256 = audit::sha256Hex(rawLedger);
  std::ofstream ledgerFile(outputDir + "/raw_input_ledger.txt",
                           std::ios::binary);
  ledgerFile.write(rawLedger.data(),
                   static_cast<std::streamsize>(rawLedger.size()));
  ledgerFile.close();

  bundle.ledgerSha256 = audit::sha256Hex(
      audit::buildJsonReport(bundle));
  ReportPaths paths = audit::writeReports(bundle, outputDir);

  std::cout << "遮挡查询来源检查完成\n";
  std::cout << "运行编号: " << bundle.runId << "\n";
  std::cout << "设备: " << bundle.environment.deviceName << "\n";
  std::cout << "输入账本 SHA-256: " << bundle.inputSha256 << "\n";
  std::cout << "JSON 账本 SHA-256: " << bundle.ledgerSha256 << "\n\n";
  for (const auto& rec : bundle.scenarios) {
    std::cout << rec.id << " | " << verdictConsole(rec.verdict)
              << " | vkResult=" << rec.result.vkResult
              << " availability=" << rec.result.availability
              << " samples=" << rec.result.sampleCount << "\n";
  }
  std::cout << "\n报告文件:\n"
            << "  - " << paths.htmlPath << " (来源检查页)\n"
            << "  - " << paths.jsonPath << " (机器可读账本)\n"
            << "  - " << paths.rawInputPath << " (原始输入账本)\n";
  return 0;
}
