#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "config.hpp"
#include "evidence.hpp"
#include "json.hpp"
#include "renderer.hpp"
#include "sha256.hpp"

namespace fs = std::filesystem;

namespace {

std::string makeRunId() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream id;
    id << "run-" << std::put_time(&tm, "%Y%m%d-%H%M%S");
    // 同秒内多次运行时用进程地址熵保证目录唯一，避免证据互相覆盖
    id << "-" << std::hex
       << (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&id)) & 0xffff);
    return id.str();
}

std::string readText(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void writeText(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

// 自动结论报告：只陈述程序可验证的事实，不做主观判断
std::string buildAutoReport(const grid::Config& config,
                            const std::vector<grid::FrameEvidence>& frames,
                            const std::vector<grid::DrawCallRecord>& drawCalls,
                            const std::vector<grid::BarrierRecord>& barriers,
                            const std::string& runId,
                            const std::string& deviceName,
                            bool cancelled,
                            uint32_t cancelFrame) {
    std::ostringstream r;
    r << "# 自动结论报告 (" << runId << ")\n\n";
    r << "## 运行参数（归一化后）\n\n";
    r << "- " << grid::describeConfig(config) << "\n";
    r << "- 设备: " << deviceName << "\n";
    r << "- 计划帧数: " << config.totalFrames
      << "，实际完成帧: " << frames.size() << "\n";
    r << "- 中途取消: " << (cancelled ? "是" : "否");
    if (cancelled) r << "（第 " << cancelFrame << " 帧完成后停止提交新帧）";
    r << "\n\n";

    r << "## Draw Call 记录（最后一帧，全帧序列相同）\n\n";
    r << "| 序号 | 语义 | 拓扑 | vertexCount | instanceCount |\n";
    r << "|---|---|---|---|---|\n";
    for (const auto& d : drawCalls) {
        r << "| " << d.index << " | " << d.name << " | "
          << (d.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ? "TRIANGLE_LIST"
                                                                 : "LINE_LIST")
          << " | " << d.vertexCount << " | " << d.instanceCount << " |\n";
    }
    r << "\n## 资源状态转换记录（最后一帧）\n\n";
    r << "| 屏障序号 | 资源 | from | to |\n|---|---|---|---|\n";
    for (const auto& b : barriers) {
        r << "| " << b.sequence << " | " << b.image << " | "
          << b.fromLayout << " | " << b.toLayout << " |\n";
    }

    r << "\n## 帧缓冲与画面证据\n\n";
    r << "| 帧 | 截图 | 截图 SHA-256 | 像素 SHA-256 | 边界像素 | 与上帧差异像素 | draw calls | 最终 layout |\n";
    r << "|---|---|---|---|---|---|---|---|\n";
    for (const auto& f : frames) {
        r << "| " << f.frameIndex << " | `" << f.imagePath << "` | `"
          << f.imageSha256.substr(0, 16) << "…` | `"
          << f.pixelSha256.substr(0, 16) << "…` | "
          << f.boundaryPixels << " | " << f.diffPixelsFromPrevious << " | "
          << f.drawCallCount << " | " << f.finalImageLayout << " |\n";
    }

    // 程序可直接验证的结论
    r << "\n## 自动核验结论\n\n";
    bool allTwoDraws = true;
    bool stablePixels = true;
    bool boundaryNonZero = true;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].drawCallCount != 2) allTwoDraws = false;
        if (frames[i].boundaryPixels == 0) boundaryNonZero = false;
        if (i > 0 && frames[i].diffPixelsFromPrevious != 0) stablePixels = false;
    }
    r << "- [x] 每帧 draw call 数=2（背景 1 + 边界 1）: "
      << (allTwoDraws ? "通过" : "失败") << "\n";
    r << "- [x] 每帧均检测到边界像素: "
      << (boundaryNonZero ? "通过" : "失败") << "\n";
    r << "- [x] 静态网格各帧像素一致（第 2 帧起差异=0）: "
      << (stablePixels ? "通过" : "失败") << "\n";
    r << "- [x] 取消语义生效: "
      << (config.cancelAfterFrame == 0
              ? "未配置取消，完整跑完"
              : (cancelled && frames.size() == config.cancelAfterFrame ? "通过" : "失败"))
      << "\n";
    r << "- 完整指纹与原始事件见 `manifest.json`、`events.log`、`config.raw.json`。\n";
    return r.str();
}

std::string buildManualTemplate(const grid::Config& config,
                                const std::vector<grid::FrameEvidence>& frames,
                                const std::string& runId) {
    std::ostringstream r;
    r << "# 人工复核确认单 (" << runId << ")\n\n";
    r << "> 本文件只提供检查项与证据索引，结论由复核人手工填写。\n\n";
    r << "复核人: ____________  日期: ____________\n\n";
    r << "## 检查项（请勾选并记录观察）\n\n";
    r << "- [ ] 打开 `frames/frame_0000.ppm`，画面为 " << config.gridCols << "x"
      << config.gridRows << " 网格且仅外轮廓为高亮边界（观察: ________）\n";
    r << "- [ ] 对照 `events.log` 中两条 drawcall 事件，确认 draw#0 为背景三角形、"
         "draw#1 为网格边界（观察: ________）\n";
    r << "- [ ] 对照 barrier 事件，确认 colorImage 经历 "
         "UNDEFINED→COLOR_ATTACHMENT_OPTIMAL→TRANSFER_SRC_OPTIMAL（观察: ________）\n";
    for (const auto& f : frames) {
        r << "- [ ] 复核 `" << f.imagePath << "` 的 SHA-256 与 manifest 记录一致"
          << "（实测: ________）\n";
    }
    if (config.cancelAfterFrame > 0) {
        r << "- [ ] 确认仅存在 " << frames.size()
          << " 张截图，且第 " << config.cancelAfterFrame
          << " 帧之后无新帧/新事件（观察: ________）\n";
    }
    r << "\n## 结论\n\n- [ ] 通过  - [ ] 有条件通过  - [ ] 不通过\n\n";
    r << "备注: ____________________________________________\n";
    return r.str();
}

std::string buildFailureGuide(const std::string& message) {
    std::ostringstream r;
    r << "# 失败处置指南\n\n";
    r << "## 本次失败原因（程序记录）\n\n- " << message << "\n\n";
    r << "## 可执行的后续操作（按顺序排查）\n\n";
    r << "1. 查看 `events.log` 最后一条事件，定位失败阶段（init/vulkan/drawcall/transfer）。\n";
    r << "2. 若提示无 Vulkan 设备：`ls /usr/share/vulkan/icd.d/` 确认 ICD，"
         "软件回退可安装/启用 lavapipe（lvp）或 SwiftShader。\n";
    r << "3. 若 validation 报错：保留完整 stderr，核对 `config.raw.json` 中尺寸/网格参数。\n";
    r << "4. 若 SPIR-V 缺失：在构建目录执行 `cmake --build .` 重新生成 `shaders/*.spv`。\n";
    r << "5. 若截图读回失败：检查 `artifacts/runs/<run-id>/frames/` 写权限与磁盘空间。\n";
    r << "6. 复现命令：`./grid_marker --config <你的配置文件> --headless`。\n";
    r << "7. 仍失败时，连同本目录（events.log、config.raw.json、stderr 日志）提交缺陷单。\n";
    return r.str();
}

struct CliArgs {
    std::string configPath = "config/demo_v2.json";
    bool headlessOverride = false;
    bool headlessValue = true;
};

CliArgs parseArgs(int argc, char** argv) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) args.configPath = argv[++i];
        else if (arg == "--headless") { args.headlessOverride = true; args.headlessValue = true; }
        else if (arg == "--windowed") { args.headlessOverride = true; args.headlessValue = false; }
    }
    return args;
}

}  // namespace

int main(int argc, char** argv) {
    CliArgs cli = parseArgs(argc, argv);
    std::string runId = makeRunId();

    grid::Config config;
    std::string error;
    config = grid::loadConfig(cli.configPath, &error);
    if (!error.empty()) {
        std::cerr << "配置加载失败: " << error << "\n";
        fs::create_directories("artifacts/runs/startup-error");
        writeText("artifacts/runs/startup-error/FAILURE.md", buildFailureGuide(error));
        return 2;
    }
    if (cli.headlessOverride) config.headless = cli.headlessValue;

    grid::EvidenceStore evidence;
    if (!evidence.open(config.outputDir, runId, cli.configPath, &error)) {
        std::cerr << error << "\n";
        fs::path fallback = fs::path(config.outputDir) / runId;
        fs::create_directories(fallback);
        writeText((fallback / "FAILURE.md").string(), buildFailureGuide(error));
        return 3;
    }
    const std::string runDir = evidence.runDir();
    evidence.event("config", grid::describeConfig(config));

    grid::Renderer renderer;
    if (!renderer.initialize(config, &evidence, &error)) {
        evidence.event("fatal", error);
        writeText(runDir + "/FAILURE.md", buildFailureGuide(error));
        std::cerr << "初始化失败: " << error << "\n详见 " << runDir << "/FAILURE.md\n";
        return 4;
    }

    std::vector<grid::FrameEvidence> allFrames;
    bool cancelled = false;
    uint32_t cancelFrame = 0;

    for (uint32_t frame = 0; frame < config.totalFrames; ++frame) {
        if (config.cancelAfterFrame > 0 && frame >= config.cancelAfterFrame) {
            cancelled = true;
            cancelFrame = frame;
            evidence.event("cancel",
                           "达到取消点 after_frame=" +
                               std::to_string(config.cancelAfterFrame) +
                               "，停止提交新帧，已在途帧均已完成并保留证据");
            break;
        }

        std::vector<uint8_t> pixels;
        grid::FrameEvidence frameEvidence;
        if (!renderer.renderFrame(frame, &pixels, &frameEvidence, &error)) {
            evidence.event("fatal", error);
            writeText(runDir + "/FAILURE.md", buildFailureGuide(error));
            renderer.shutdown();
            std::cerr << "渲染失败: " << error << "\n详见 " << runDir << "/FAILURE.md\n";
            return 5;
        }

        if (!evidence.savePpm(frame, pixels.data(), config.width, config.height,
                              config.clearColor, &frameEvidence, &error)) {
            evidence.event("fatal", error);
            writeText(runDir + "/FAILURE.md", buildFailureGuide(error));
            renderer.shutdown();
            return 6;
        }
        evidence.event("evidence",
                       "frame=" + std::to_string(frame) + " 截图已保存 " +
                           frameEvidence.imagePath + " sha256=" +
                           frameEvidence.imageSha256);
        allFrames.push_back(frameEvidence);
    }

    // 收尾：留存 SPIR-V 指纹、归一化配置，写 manifest 与双报告
    for (const char* shader : {"shaders/background.vert.spv", "shaders/grid.vert.spv",
                               "shaders/grid.frag.spv"}) {
        std::string hash = crypto::Sha256::ofFile(shader);
        evidence.event("shader", std::string(shader) + " sha256=" + hash);
    }

    std::ostringstream extra;
    extra << "  \"run_id\":\"" << runId << "\",\n"
          << "  \"cancelled\":" << (cancelled ? "true" : "false") << ",\n"
          << "  \"cancel_frame\":" << cancelFrame << ",\n"
          << "  \"device\":\"" << minijson::escape(renderer.deviceName()) << "\",\n"
          << "  \"config_normalized\":\"" << minijson::escape(describeConfig(config)) << "\"";
    evidence.writeManifest(extra.str());

    writeText(runDir + "/REPORT_auto.md",
              buildAutoReport(config, allFrames, renderer.lastDrawCalls(),
                              renderer.lastBarriers(), runId, renderer.deviceName(),
                              cancelled, cancelFrame));
    writeText(runDir + "/REPORT_manual.md",
              buildManualTemplate(config, allFrames, runId));

    renderer.shutdown();
    evidence.event("run", "正常结束");
    std::cout << "运行完成: " << runDir << "\n"
              << "  自动报告: " << runDir << "/REPORT_auto.md\n"
              << "  人工确认: " << runDir << "/REPORT_manual.md\n"
              << "  帧截图:   " << runDir << "/frames/\n";
    return 0;
}
