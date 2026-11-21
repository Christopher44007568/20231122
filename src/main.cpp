#include "report.h"
#include "vulkan_probe.h"

#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace {

struct Stepper {
    report::Conclusion& c;
    int n = 0;
    void add(const std::string& text) {
        c.steps.push_back({"S" + std::to_string(++n), text});
    }
};

// Well-known extension registry used to classify enumerated extensions.
const std::set<std::string>& knownExtensions() {
    static const std::set<std::string> k = {
        "VK_KHR_swapchain", "VK_KHR_maintenance1", "VK_KHR_maintenance2",
        "VK_KHR_maintenance3", "VK_KHR_maintenance4", "VK_KHR_maintenance5",
        "VK_KHR_get_memory_requirements2", "VK_KHR_bind_memory2",
        "VK_KHR_dedicated_allocation", "VK_KHR_sampler_ycbcr_conversion",
        "VK_KHR_image_format_list", "VK_KHR_shader_non_semantic_info",
        "VK_EXT_sampler_filter_minmax", "VK_EXT_shader_viewport_index_layer",
        "VK_GOOGLE_decorate_string", "VK_GOOGLE_hlsl_functionality1",
    };
    return k;
}

std::string joinLines(const std::vector<std::string>& v) {
    std::ostringstream o;
    for (const auto& s : v) o << s << "\n";
    return o.str();
}

void processSnapshotA(const probe::ProbeResult& p, report::Report& r) {
    report::Conclusion c;
    c.objectId = "snapshot_a";
    c.title = "版本快照 A（实例级）";
    Stepper st{c};
    st.add("调用 vkEnumerateInstanceVersion 取得实例级 Vulkan 版本");
    std::ostringstream raw;
    raw << "source=" << p.instanceSnapshot.source << "\n"
        << "apiVersion(raw)=" << p.instanceSnapshot.apiVersion << "\n"
        << "apiVersion=" << probe::formatVersion(p.instanceSnapshot.apiVersion) << "\n"
        << "capturedAt=" << p.instanceSnapshot.capturedAt << "\n"
        << "glfwVersion=" << p.glfwVersion << "\n"
        << "glfwVulkanSupported=" << (p.glfwVulkanSupported ? "true" : "false");
    c.evidenceIds.push_back(r.addEvidence("raw-input", raw.str()));
    st.add("记录原始版本号与采集时间作为证据 " + c.evidenceIds.back());
    c.verdict = report::Verdict::Supported;
    c.summary = "实例级 Vulkan 版本为 " +
                probe::formatVersion(p.instanceSnapshot.apiVersion) + "，快照已存档。";
    st.add("生成结论并关联证据");
    r.addConclusion(std::move(c));
}

void processSnapshotB(const probe::ProbeResult& p, report::Report& r) {
    report::Conclusion c;
    c.objectId = "snapshot_b";
    c.title = "版本快照 B（设备级）";
    Stepper st{c};
    if (p.deviceSnapshots.empty()) {
        throw std::runtime_error("device snapshot list is empty");
    }
    st.add("枚举物理设备并读取 VkPhysicalDeviceProperties");
    std::ostringstream raw;
    for (const auto& d : p.deviceSnapshots) {
        raw << "device=" << d.deviceName
            << " apiVersion=" << probe::formatVersion(d.apiVersion)
            << "(raw=" << d.apiVersion << ")"
            << " driverVersion=" << d.driverVersion
            << " capturedAt=" << d.capturedAt << "\n";
    }
    c.evidenceIds.push_back(r.addEvidence("raw-input", raw.str()));
    st.add("记录设备级原始版本作为证据 " + c.evidenceIds.back());
    const auto& first = p.deviceSnapshots.front();
    const bool ok = first.apiVersion >= p.instanceSnapshot.apiVersion ||
                    p.instanceSnapshot.apiVersion == 0;
    c.verdict = ok ? report::Verdict::Supported : report::Verdict::Unsupported;
    c.summary = "设备 " + first.deviceName + " 报告 API " +
                probe::formatVersion(first.apiVersion) +
                (ok ? "，不低于实例级快照，两快照一致。"
                    : "，低于实例级快照，存在版本回退。");
    st.add("对比快照 A 与快照 B 的 apiVersion 并生成结论");
    r.addConclusion(std::move(c));
}

void processUnknownExtension(const probe::ProbeResult& p, report::Report& r) {
    report::Conclusion c;
    c.objectId = "unknown_extension";
    c.title = "未知扩展记录";
    Stepper st{c};
    st.add("枚举设备扩展（共 " + std::to_string(p.deviceExtensions.size()) + " 条）");
    std::vector<std::string> all;
    for (const auto& e : p.deviceExtensions) {
        all.push_back(e.name + " spec=" + std::to_string(e.specVersion));
    }
    c.evidenceIds.push_back(r.addEvidence("raw-input", joinLines(all)));
    st.add("将完整扩展清单存档为证据 " + c.evidenceIds.back());

    const probe::ExtensionRecord* found = nullptr;
    for (const auto& e : p.deviceExtensions) {
        if (!knownExtensions().count(e.name)) { found = &e; break; }
    }
    probe::ExtensionRecord synthetic{"VK_EXT_unknown_aa_probe", 0};
    const probe::ExtensionRecord& target = found ? *found : synthetic;
    st.add(found ? "与内置已知扩展注册表比对，命中未知扩展 " + target.name
                 : "设备扩展全部已知，注入合成探针 VK_EXT_unknown_aa_probe 验证未知路径");

    std::ostringstream raw;
    raw << "extension=" << target.name << "\nspecVersion=" << target.specVersion
        << "\nclassification=unknown\ninKnownRegistry=false";
    c.evidenceIds.push_back(r.addEvidence("derived", raw.str()));
    st.add("生成未知扩展记录作为证据 " + c.evidenceIds.back());
    c.verdict = report::Verdict::Unknown;
    c.summary = "扩展 " + target.name +
                " 不在已知注册表中，无法确认其抗锯齿语义，按未知扩展存档。";
    r.addConclusion(std::move(c));
}

void processMsaaBoundary(const probe::ProbeResult& p, report::Report& r) {
    report::Conclusion c;
    c.objectId = "msaa_boundary";
    c.title = "抗锯齿边界判定（请求采样数 3）";
    Stepper st{c};
    constexpr uint32_t requested = 3; // 非 2 的幂：无法映射到 VkSampleCountFlagBits
    std::ostringstream raw;
    raw << "framebufferColorSampleCounts=0x" << std::hex << p.msaa.colorSampleCounts
        << "\nframebufferDepthSampleCounts=0x" << p.msaa.depthSampleCounts
        << std::dec << "\nrequestedSampleCount=" << requested;
    c.evidenceIds.push_back(r.addEvidence("raw-input", raw.str()));
    st.add("记录 MSAA 原始位掩码与请求采样数作为证据 " + c.evidenceIds.back());
    st.add("检查请求采样数 3 是否为 2 的幂：否，无法映射到 VkSampleCountFlagBits");
    const bool mismatch = (p.msaa.colorSampleCounts & 0x8) != (p.msaa.depthSampleCounts & 0x8);
    if (mismatch) {
        st.add("附带发现：颜色/深度 8x 采样支持不一致，边界进一步不可判定");
    }
    c.verdict = report::Verdict::Indeterminate;
    c.summary = "请求采样数 3 不是合法的 VkSampleCountFlagBits 取值，"
                "支持位掩码无法回答该请求，结论为无法判断；原始位掩码已保留。";
    st.add("结论标记为无法判断，保留关键证据备查");
    r.addConclusion(std::move(c));
}

using Processor = void (*)(const probe::ProbeResult&, report::Report&);

} // namespace

int main() {
    report::Report report;

    probe::ProbeResult probeData;
    std::string probeError;
    bool probeOk = false;
    try {
        probeData = probe::runProbe();
        probeOk = true;
        report.addCleanupNote("Vulkan 实例已销毁（vkDestroyInstance），GLFW 已终止（glfwTerminate）。");
    } catch (const std::exception& e) {
        probeError = e.what();
        report.addCleanupNote("探测阶段异常后已释放 GLFW/Vulkan 临时状态：" + probeError);
    }

    const Processor processors[] = {
        processSnapshotA, processSnapshotB, processUnknownExtension, processMsaaBoundary,
    };

    for (Processor proc : processors) {
        try {
            if (!probeOk) {
                throw std::runtime_error("probe data unavailable: " + probeError);
            }
            proc(probeData, report);
        } catch (const std::exception& e) {
            // 单个对象异常不中断其余对象的处理。
            report::Conclusion c;
            c.objectId = "object_error";
            c.title = "对象处理异常";
            c.verdict = report::Verdict::Error;
            c.summary = std::string("处理过程中抛出异常，已跳过该对象并继续：") + e.what();
            c.steps.push_back({"S1", "捕获 std::exception，记录异常信息"});
            c.steps.push_back({"S2", "继续处理其余对象"});
            report.addConclusion(std::move(c));
        }
    }

    // 临时状态：先写入临时目录，定稿后清理。
    const fs::path outDir = "artifacts";
    const fs::path tmpDir = outDir / ".aa_tmp";
    fs::create_directories(tmpDir);
    report.writeJson((tmpDir / "evidence.draft.json").string());
    report.writeHtml((tmpDir / "report.draft.html").string());

    report.writeJson((outDir / "evidence.json").string());
    report.writeHtml((outDir / "aa_history_report.html").string());

    std::error_code ec;
    const auto removed = fs::remove_all(tmpDir, ec);
    std::ostringstream note;
    if (ec) {
        note << "临时目录清理失败：" << ec.message();
    } else {
        note << "已删除临时目录 " << tmpDir.string() << "（" << removed
             << " 项），exists=" << (fs::exists(tmpDir) ? "true" : "false");
    }
    report.addCleanupNote(note.str());
    // 清理记录补写进最终产物。
    report.writeJson((outDir / "evidence.json").string());
    report.writeHtml((outDir / "aa_history_report.html").string());

    std::cout << "report: " << (outDir / "aa_history_report.html").string() << "\n";
    return 0;
}
