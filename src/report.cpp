#include "mi/report.hpp"

#include <sstream>

#include "mi/json.hpp"
#include "mi/scenarios.hpp"

namespace mi {
namespace {

std::string status_word(bool ok) { return ok ? "PASS" : "FAIL"; }

std::string short_hash(const std::string& hash) {
  return hash.size() <= 16 ? hash : hash.substr(0, 16);
}

std::string failure_actions(const ReportContext& context) {
  std::ostringstream out;
  out << "```bash\n"
      << "sudo apt install libvulkan-dev libglfw3-dev vulkan-tools\n"
      << "cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo\n"
      << "cmake --build build -j\n"
      << "ctest --test-dir build --output-on-failure\n"
      << "VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json "
      << "./build/material_hotupdate --backend vulkan --output "
      << context.output_dir << "-retry\n"
      << "./build/material_hotupdate --backend fake --output "
      << context.output_dir << "-fake\n"
      << "```\n\n";
  out << "若真实 Vulkan 初始化失败，保留 `"
      << context.output_dir << "/environment.json` 和 stderr；"
      << "先用 `--backend fake` 判断状态机是否通过，再按 Vulkan 驱动错误单独修复。\n";
  return out.str();
}

void render_scenario_table(std::ostringstream& out,
                           const ExperimentResult& result) {
  out << "| 场景 | 自动结果 | 检查项 | 目的 |\n"
      << "|---|---:|---:|---|\n";
  for (const auto& scenario : result.scenarios) {
    std::size_t passed = 0;
    for (const auto& check : scenario.checks) {
      passed += check.ok ? 1 : 0;
    }
    out << "| `" << scenario.name << "` | "
        << status_word(scenario.passed) << " | " << passed << "/"
        << scenario.checks.size() << " | " << scenario.purpose << " |\n";
  }
}

void render_checks(std::ostringstream& out, const ExperimentResult& result) {
  for (const auto& scenario : result.scenarios) {
    out << "### `" << scenario.name << "`\n\n";
    for (const auto& check : scenario.checks) {
      out << "- " << (check.ok ? "[PASS] " : "[FAIL] ") << "`" << check.id
          << "`: " << check.detail << "\n";
    }
    out << "\n";
  }
}

}  // namespace

std::string render_report(const ExperimentResult& result,
                          const ReportContext& context) {
  std::ostringstream out;
  out << "# 材质实例热更新可回放实验报告\n\n"
      << "## 自动结论\n\n"
      << "- 总体结果：**" << status_word(result.passed) << "**\n"
      << "- 回放：**" << status_word(result.replay.ok) << "**，"
      << result.replay.replayed_steps << "/" << result.replay.original_steps
      << " 步一致，最终事件哈希 `"
      << short_hash(result.replay.replayed_final_hash) << "`。\n"
      << "- GPU 后端：`" << context.backend.name << "` / `"
      << context.backend.device << "`；"
      << (context.backend.asynchronous_submit ? "异步提交" : "同步/模拟提交")
      << "。\n"
      << "- 记录：每步均含输入、变更前状态、结果、变更后状态和证据字段；"
      << "事件使用 SHA-256 链式哈希。\n\n";

  render_scenario_table(out, result);
  out << "\n自动判定只依据断言和回放哈希，不把人工观察计为通过。\n\n";

  out << "## 关键证据\n\n"
      << "- 原始步骤：`" << context.output_dir << "/events.jsonl`（"
      << result.all_steps.size() << " 条）\n"
      << "- 回放输入：`" << context.output_dir << "/commands.jsonl`\n"
      << "- 回放结果：`" << context.output_dir << "/replay.json`\n"
      << "- 最终状态：`" << context.output_dir << "/final_state.json`\n"
      << "- 检查明细：`" << context.output_dir << "/checks.json`\n"
      << "- 环境快照：`" << context.output_dir << "/environment.json`\n\n";

  for (const auto& scenario : result.scenarios) {
    if (scenario.name == "cancel_primary") {
      out << "中途取消证据：原始事件 `events.jsonl` 中 `op=cancel` 的 "
          << "`state_before.pending_stage` 应为 `uploading`，"
          << "`evidence.gpu_released=true`；"
          << "随后 `op=commit` 返回 `invalid_transition`。\n\n";
    }
    if (scenario.name == "duplicate_submit") {
      out << "重复提交证据：`op=submit` 的第一次与重复提交返回同一 `tx`；"
          << "冲突载荷返回 `idempotency_conflict`；最终只增加一个版本。\n\n";
    }
  }

  out << "## 断言明细\n\n";
  render_checks(out, result);

  out << "## 人工确认\n\n"
      << "以下项目**未由自动断言判定**，需人工签字确认：\n\n"
      << "- [ ] 真实 Vulkan 设备信息符合目标部署硬件；软件设备 `llvmpipe` "
      << "只能证明流程可执行。\n"
      << "- [ ] 审查 `cancel_primary` 和 `cancel_control`，确认“取消后保留旧材质”"
      << "符合产品语义。\n"
      << "- [ ] 确认幂等键作用域（材质 + key）与客户端重试策略一致。\n"
      << "- [ ] 确认越序事件拒绝策略不会吞掉可恢复的合法重试。\n"
      << "- [ ] 本次取消点在 `upload_begin` 之后、`upload_complete` 之前；"
      << "确认它符合产品对“中途”的时序定义（不是硬件队列仍在执行的证明）。\n"
      << "- [ ] 如需视觉验收，执行 `--visible` 并观察窗口和日志序号稳定。\n\n"
      << "可填写模板：`" << context.output_dir
      << "/manual_confirmation.md`。\n\n";

  if (!result.passed) {
    out << "## 失败后的可执行操作\n\n"
        << failure_actions(context);
  } else {
    out << "## 失败后的可执行操作\n\n"
        << "当前自动检查通过。若后续环境变化导致失败，执行：\n\n"
        << failure_actions(context);
  }

  return out.str();
}

std::string render_manual_confirmation(const ExperimentResult& result,
                                       const ReportContext& context) {
  std::ostringstream out;
  out << "# 人工确认单\n\n"
      << "自动结论：**" << status_word(result.passed) << "**。"
      << "本文件不由程序自动判为通过。\n\n"
      << "## 环境\n\n"
      << "- Vulkan 后端：`" << context.backend.name << "`\n"
      << "- 设备：`" << context.backend.device << "`\n"
      << "- 驱动：`" << context.backend.driver << "`\n"
      << "- 输出目录：`" << context.output_dir << "`\n\n"
      << "## 待确认\n\n"
      << "| 项目 | 结果（通过/不通过/不适用） | 确认人 | 备注 |\n"
      << "|---|---|---|---|\n"
      << "| 取消后旧材质语义符合需求 |  |  |  |\n"
      << "| 重复取消为无害 no-op |  |  |  |\n"
      << "| 幂等键作用域符合协议 |  |  |  |\n"
      << "| 越序事件拒绝策略可接受 |  |  |  |\n"
      << "| 取消点满足产品定义的“中途”时序 |  |  |  |\n"
      << "| 目标硬件/驱动与实验环境一致 |  |  |  |\n"
      << "| 视觉或产品验收（若需要） |  |  |  |\n\n"
      << "## 签字\n\n"
      << "- 姓名：\n- 日期：\n- 结论备注：\n";
  return out.str();
}

std::string render_failure_report(const ReportContext& context) {
  std::ostringstream out;
  out << "# 材质实例热更新实验：运行失败\n\n"
      << "## 自动结论\n\n"
      << "**FAIL**：实验在生成完整断言前终止。\n\n"
      << "```text\n" << context.failure << "\n```\n\n"
      << "## 人工确认\n\n"
      << "尚无可确认的实验结论；请勿把失败运行标记为通过。\n\n"
      << "## 失败后的可执行操作\n\n"
      << failure_actions(context);
  return out.str();
}

}  // namespace mi
