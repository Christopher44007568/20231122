#include "mi/scenarios.hpp"

namespace mi {
namespace {

MaterialParams baseline_params() {
  MaterialParams params;
  params.base_color = {0.25F, 0.30F, 0.36F, 1.0F};
  params.roughness = 0.55F;
  params.metallic = 0.10F;
  params.shader_variant = 1;
  return params;
}

MaterialParams update_params() {
  MaterialParams params;
  params.base_color = {0.82F, 0.22F, 0.08F, 1.0F};
  params.roughness = 0.33F;
  params.metallic = 0.78F;
  params.shader_variant = 7;
  return params;
}

MaterialParams conflicting_params() {
  MaterialParams params = update_params();
  params.roughness = 0.91F;
  return params;
}

void add(Scenario* scenario, Op op, std::string expected,
         std::string material = "mi_steel", std::string key = {},
         std::uint64_t tx = 0, std::uint64_t version = 0,
         MaterialParams params = {}) {
  Command command;
  command.scenario = scenario->name;
  command.scenario_seq = scenario->commands.size();
  command.op = op;
  command.material = std::move(material);
  command.key = std::move(key);
  command.tx = tx;
  command.version = version;
  command.params = params;
  command.expected = std::move(expected);
  scenario->commands.push_back(command);
}

void reset(Scenario* scenario) {
  add(scenario, Op::Reset, "accepted", "mi_steel", {}, 0, 1,
      baseline_params());
}

Scenario make_cancel_primary() {
  Scenario scenario;
  scenario.name = "cancel_primary";
  scenario.purpose =
      "在 GPU 上传已开始、提交尚未发生时取消；检查活动材质不变且暂存资源释放。";
  reset(&scenario);
  add(&scenario, Op::Submit, "accepted", "mi_steel", "hot-v2", 0, 2,
      update_params());
  add(&scenario, Op::Validate, "accepted", "mi_steel", {}, 1);
  add(&scenario, Op::UploadBegin, "accepted", "mi_steel", {}, 1);
  add(&scenario, Op::Cancel, "canceled", "mi_steel", {}, 1);
  add(&scenario, Op::Commit, "invalid_transition", "mi_steel", {}, 1);
  return scenario;
}

Scenario make_cancel_control() {
  Scenario scenario;
  scenario.name = "cancel_control";
  scenario.control_of = "cancel_primary";
  scenario.purpose =
      "重复中途取消对照：额外执行一次相同取消，证明取消操作幂等且不改变回滚结果。";
  reset(&scenario);
  add(&scenario, Op::Submit, "accepted", "mi_steel", "hot-v2-control", 0, 2,
      update_params());
  add(&scenario, Op::Submit, "duplicate_accepted", "mi_steel",
      "hot-v2-control", 0, 2, update_params());
  add(&scenario, Op::Validate, "accepted", "mi_steel", {}, 1);
  add(&scenario, Op::UploadBegin, "accepted", "mi_steel", {}, 1);
  add(&scenario, Op::Cancel, "canceled", "mi_steel", {}, 1);
  add(&scenario, Op::Cancel, "already_canceled", "mi_steel", {}, 1);
  add(&scenario, Op::Commit, "invalid_transition", "mi_steel", {}, 1);
  return scenario;
}

Scenario make_no_cancel() {
  Scenario scenario;
  scenario.name = "no_cancel_baseline";
  scenario.purpose =
      "无取消基线：相同载荷完成上传与提交，证明失败并非由载荷或正常路径导致。";
  reset(&scenario);
  add(&scenario, Op::Submit, "accepted", "mi_steel", "hot-v2-baseline", 0, 2,
      update_params());
  add(&scenario, Op::Validate, "accepted", "mi_steel", {}, 1);
  add(&scenario, Op::UploadBegin, "accepted", "mi_steel", {}, 1);
  add(&scenario, Op::UploadComplete, "accepted", "mi_steel", {}, 1);
  add(&scenario, Op::Commit, "committed", "mi_steel", {}, 1);
  add(&scenario, Op::Cancel, "too_late", "mi_steel", {}, 1);
  return scenario;
}

Scenario make_reordered() {
  Scenario scenario;
  scenario.name = "reordered_events";
  scenario.purpose =
      "先发送 upload_begin 与 commit 的越序事件，均须拒绝；再按合法顺序完成更新。";
  reset(&scenario);
  add(&scenario, Op::Submit, "accepted", "mi_steel", "hot-v2-reordered", 0, 2,
      update_params());
  add(&scenario, Op::UploadBegin, "invalid_transition", "mi_steel", {}, 1);
  add(&scenario, Op::Validate, "accepted", "mi_steel", {}, 1);
  add(&scenario, Op::Commit, "invalid_transition", "mi_steel", {}, 1);
  add(&scenario, Op::UploadBegin, "accepted", "mi_steel", {}, 1);
  add(&scenario, Op::UploadComplete, "accepted", "mi_steel", {}, 1);
  add(&scenario, Op::Commit, "committed", "mi_steel", {}, 1);
  return scenario;
}

Scenario make_duplicate() {
  Scenario scenario;
  scenario.name = "duplicate_submit";
  scenario.purpose =
      "重复提交相同 key/载荷必须去重；相同 key 不同载荷必须冲突；提交只增加一个版本。";
  reset(&scenario);
  add(&scenario, Op::Submit, "accepted", "mi_steel", "hot-v2-dup", 0, 2,
      update_params());
  add(&scenario, Op::Submit, "duplicate_accepted", "mi_steel", "hot-v2-dup", 0,
      2, update_params());
  add(&scenario, Op::Submit, "idempotency_conflict", "mi_steel", "hot-v2-dup",
      0, 2, conflicting_params());
  add(&scenario, Op::Validate, "accepted", "mi_steel", {}, 1);
  add(&scenario, Op::UploadBegin, "accepted", "mi_steel", {}, 1);
  add(&scenario, Op::UploadComplete, "accepted", "mi_steel", {}, 1);
  add(&scenario, Op::Commit, "committed", "mi_steel", {}, 1);
  add(&scenario, Op::Submit, "duplicate_accepted", "mi_steel", "hot-v2-dup", 0,
      2, update_params());
  return scenario;
}

}  // namespace

std::vector<Scenario> build_scenarios() {
  return {make_cancel_primary(), make_cancel_control(), make_no_cancel(),
          make_reordered(), make_duplicate()};
}

}  // namespace mi
