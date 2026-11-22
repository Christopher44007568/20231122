#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

constexpr float kSidebarWidth = 228.0f;
constexpr float kTopBarHeight = 64.0f;
constexpr float kContentLeft = 252.0f;
constexpr float kPageRightMargin = 24.0f;
constexpr float kColumnGap = 18.0f;

constexpr ImU32 C(unsigned int rgb, int alpha = 255) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}

const ImU32 kInk = C(0x181B1E);
const ImU32 kMuted = C(0x555A58);
const ImU32 kFaint = C(0x7F8481);
const ImU32 kLine = C(0xDDD9CF);
const ImU32 kLineStrong = C(0xCFCABE);
const ImU32 kPage = C(0xF2F0EA);
const ImU32 kPanel = C(0xFCFBF7);
const ImU32 kPanelAlt = C(0xF7F5EF);
const ImU32 kSidebar = C(0x111519);
const ImU32 kSidebarRaised = C(0x1A2025);
const ImU32 kSidebarText = C(0xEDEDE8);
const ImU32 kSidebarMuted = C(0x7F898D);
const ImU32 kLime = C(0xC8F36B);
const ImU32 kLimeDim = C(0xE7F7C5);
const ImU32 kOlive = C(0x557220);
const ImU32 kOliveSoft = C(0xEDF5DD);
const ImU32 kAmber = C(0xE0A34C);
const ImU32 kAmberInk = C(0x8A5A16);
const ImU32 kAmberSoft = C(0xFAF0DE);
const ImU32 kRed = C(0xC8584B);
const ImU32 kRedSoft = C(0xFAEAE6);
const ImU32 kDarkPanel = C(0x161B1F);
const ImU32 kDarkLine = C(0x2B3338);
const ImU32 kCyan = C(0x8CD0D9);
bool g_BlockHits = false;

const char* kChineseGlyphText =
    "细线抗锯齿来源检查记录一次恢复后的重放和一个未知扩展让每个结论都能回到原始输入与处理步骤"
    "无法判断边界保留关键证据失败时给出可执行的后续操作报告自动人工确认概览来源账本证据链"
    "恢复重放未知扩展决策分层请求复核导出证据包只读快照冻结于输入样本哈希一致已对齐待人工"
    "可归因高置信原始读取去块重建覆盖率比较阈值归因到而非未引用本机管线一致_edge_mask_strength"
    "声明覆盖模式但缺少正规化参数采样在边界内不可区分现有探针不足原始块解码失败日志打开运行"
    "复现确认未知退回补证关键证据步骤全部保留补证请求已记录当前快照聚焦于来源检查复核目标"
    "接受为永久待确认不覆盖判定仅使用冻结证据输入未被改写输出写入独立目录若失败保留后切换重跑"
    "并导出探针与驱动信息不会覆盖原始导出一份证据包清单复制引用关联来源链操作完成退出码"
    "时间处理基线构建本地只读快照冻结探针结果通过未通过项实验补证已切换中心采样重新执行"
    "请先冻结输入防止哈希漂移然后使用中心采样重跑并将探针与驱动信息导出到独立证据目录"
    "复核请求提交后人工确认仍保持独立记录拒绝自动覆盖已复制可执行后续命令实验边界两路采样"
    "扩展字段偏移字节版本失败后续重放失败时输入哈希保留切换中心采样导出探针复制命令";

void TextRaw(ImDrawList* drawList, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text) {
    drawList->AddText(font, size, pos, color, text);
}

void Textf(ImDrawList* drawList, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    drawList->AddText(font, size, pos, color, buffer);
}

float TextWidth(ImFont* font, float size, const char* text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
}

void TextRight(ImDrawList* drawList, ImFont* font, float size, float right, float y, ImU32 color, const char* text) {
    const float width = TextWidth(font, size, text);
    drawList->AddText(font, size, ImVec2(right - width, y), color, text);
}

void Panel(ImDrawList* drawList, ImVec2 min, ImVec2 max, bool shadow = true) {
    if (shadow) {
        drawList->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 3.0f), ImVec2(max.x + 1.0f, max.y + 3.0f),
                                C(0x1A1B18, 12), 11.0f);
    }
    drawList->AddRectFilled(min, max, kPanel, 11.0f);
    drawList->AddRect(min, max, kLine, 11.0f, 0, 1.0f);
}

void Rule(ImDrawList* drawList, float x0, float x1, float y, ImU32 color = kLine) {
    drawList->AddLine(ImVec2(x0, y), ImVec2(x1, y), color, 1.0f);
}

void Badge(ImDrawList* drawList, ImFont* font, ImVec2 pos, const char* text, ImU32 fill, ImU32 ink) {
    const float width = TextWidth(font, 10.5f, text) + 17.0f;
    drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 22.0f), fill, 11.0f);
    drawList->AddText(font, 10.5f, ImVec2(pos.x + 8.5f, pos.y + 5.0f), ink, text);
}

void CheckIcon(ImDrawList* drawList, ImVec2 center, float radius, ImU32 fill, ImU32 mark) {
    drawList->AddCircleFilled(center, radius, fill);
    drawList->AddLine(ImVec2(center.x - radius * 0.44f, center.y + radius * 0.02f),
                      ImVec2(center.x - radius * 0.10f, center.y + radius * 0.35f), mark, 1.55f);
    drawList->AddLine(ImVec2(center.x - radius * 0.10f, center.y + radius * 0.35f),
                      ImVec2(center.x + radius * 0.48f, center.y - radius * 0.30f), mark, 1.55f);
}

void Dot(ImDrawList* drawList, ImVec2 center, float radius, ImU32 color) {
    drawList->AddCircleFilled(center, radius, color);
}

bool Hit(ImVec2 min, ImVec2 max, const char* id) {
    if (g_BlockHits) {
        return false;
    }
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton(id, ImVec2(max.x - min.x, max.y - min.y));
    return ImGui::IsItemClicked(ImGuiMouseButton_Left);
}

bool Hovered(ImVec2 min, ImVec2 max) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    return mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
}

void DrawEvidenceChip(ImDrawList* drawList, ImFont* mono, ImVec2 pos, const char* id, bool accent = false) {
    const float width = TextWidth(mono, 9.8f, id) + 13.0f;
    const ImU32 fill = accent ? C(0xE9F3D4) : C(0xEFEEE8);
    const ImU32 ink = accent ? kOlive : C(0x5F6462);
    drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 18.0f), fill, 5.0f);
    drawList->AddText(mono, 9.8f, ImVec2(pos.x + 6.5f, pos.y + 4.0f), ink, id);
}

void DrawChipButton(ImDrawList* drawList, ImFont* mono, ImVec2 pos, const char* id, const char* label,
                    bool hovered, ImU32 fill, ImU32 ink) {
    const float width = TextWidth(mono, 9.8f, label) + 15.0f;
    drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 19.0f), hovered ? C(0xE3EFC9) : fill, 5.0f);
    drawList->AddText(mono, 9.8f, ImVec2(pos.x + 7.0f, pos.y + 4.3f), ink, label);
    (void)id;
}

} // namespace

namespace aa {

void UiApp::LoadFonts(ImGuiIO& io) {
    static ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    builder.AddText(kChineseGlyphText);
    builder.AddText("Δ→←↑↓±×·—…✓");
    builder.BuildRanges(&ranges);

    const std::string cjkPath = std::string(AA_ASSET_DIR) + "/fonts/NotoSansSC-Medium.ttf";
    const std::string monoPath = std::string(AA_ASSET_DIR) + "/fonts/DejaVuSansMono.ttf";

    ImFontConfig bodyConfig;
    bodyConfig.OversampleH = 2;
    bodyConfig.OversampleV = 2;
    bodyConfig.PixelSnapH = false;
    bodyConfig.RasterizerDensity = 1.0f;
    bodyConfig.GlyphRanges = ranges.Data;

    fonts_.body = io.Fonts->AddFontFromFileTTF(cjkPath.c_str(), 15.6f, &bodyConfig);
    fonts_.medium = io.Fonts->AddFontFromFileTTF(cjkPath.c_str(), 17.0f, &bodyConfig);
    fonts_.small = io.Fonts->AddFontFromFileTTF(cjkPath.c_str(), 12.8f, &bodyConfig);
    fonts_.label = io.Fonts->AddFontFromFileTTF(cjkPath.c_str(), 10.8f, &bodyConfig);
    fonts_.title = io.Fonts->AddFontFromFileTTF(cjkPath.c_str(), 21.5f, &bodyConfig);
    fonts_.hero = io.Fonts->AddFontFromFileTTF(cjkPath.c_str(), 29.0f, &bodyConfig);

    ImFontConfig monoConfig;
    monoConfig.OversampleH = 1;
    monoConfig.OversampleV = 2;
    monoConfig.PixelSnapH = true;
    fonts_.mono = io.Fonts->AddFontFromFileTTF(monoPath.c_str(), 11.7f, &monoConfig);
    fonts_.monoSmall = io.Fonts->AddFontFromFileTTF(monoPath.c_str(), 10.3f, &monoConfig);

    if (fonts_.body == nullptr || fonts_.mono == nullptr) {
        std::fprintf(stderr, "Failed to load bundled fonts. Expected files in %s/fonts\n", AA_ASSET_DIR);
        std::abort();
    }
    io.FontDefault = fonts_.body;
}

void UiApp::Initialize(ImGuiIO& io) {
    LoadFonts(io);
    if (const char* evidence = std::getenv("AA_OPEN_EVIDENCE")) {
        evidenceModal_ = std::atoi(evidence);
    }
    if (const char* failure = std::getenv("AA_OPEN_FAILURE")) {
        failureModal_ = std::atoi(failure) != 0;
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.FramePadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 10.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(kInk);
    style.Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(kPage);
    style.Colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(kPanel);
    style.Colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(kLine);
    style.Colors[ImGuiCol_Button] = ImGui::ColorConvertU32ToFloat4(kInk);
    style.Colors[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(C(0x2C3235));
    style.Colors[ImGuiCol_ButtonActive] = ImGui::ColorConvertU32ToFloat4(C(0x3B4245));
    style.Colors[ImGuiCol_TextSelectedBg] = ImGui::ColorConvertU32ToFloat4(C(0xC8F36B, 110));
}

void UiApp::Draw() {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##provenance-root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(ImVec2(0.0f, 0.0f), size, kPage);
    g_BlockHits = evidenceModal_ >= 0 || failureModal_;
    DrawSidebar(size);
    DrawTopBar(size);
    DrawHeader(size);
    DrawStatStrip(size);

    const float rightWidth = std::clamp(size.x * 0.272f, 360.0f, 410.0f);
    const float contentRight = size.x - kPageRightMargin;
    const float rightLeft = contentRight - rightWidth;
    const float leftRight = rightLeft - kColumnGap;
    const float bodyTop = 238.0f;
    const float bodyBottom = size.y - 20.0f;
    const float bodyHeight = bodyBottom - bodyTop;
    const float unknownHeight = std::clamp(bodyHeight * 0.42f, 246.0f, 280.0f);
    const float replayHeight = bodyHeight - unknownHeight - 16.0f;
    const float decisionHeight = bodyHeight * 0.508f;
    const float evidenceHeight = 178.0f;
    const float runbookHeight = bodyHeight - decisionHeight - evidenceHeight - 28.0f;

    DrawReplayCard(ImVec2(kContentLeft, bodyTop), ImVec2(leftRight, bodyTop + replayHeight));
    DrawUnknownCard(ImVec2(kContentLeft, bodyTop + replayHeight + 16.0f),
                    ImVec2(leftRight, bodyBottom));
    DrawDecisionCard(ImVec2(rightLeft, bodyTop), ImVec2(contentRight, bodyTop + decisionHeight));
    DrawEvidenceCard(ImVec2(rightLeft, bodyTop + decisionHeight + 14.0f),
                     ImVec2(contentRight, bodyTop + decisionHeight + 14.0f + evidenceHeight));
    DrawRunbookCard(ImVec2(rightLeft, bodyTop + decisionHeight + evidenceHeight + 28.0f),
                    ImVec2(contentRight, bodyBottom));

    if (evidenceModal_ >= 0) {
        DrawEvidenceModal(size);
    }
    if (failureModal_) {
        DrawFailureModal(size);
    }
    DrawToast(size);
    ImGui::End();
}

void UiApp::DrawSidebar(const ImVec2& size) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(kSidebarWidth, size.y), kSidebar);
    drawList->AddLine(ImVec2(kSidebarWidth, 0.0f), ImVec2(kSidebarWidth, size.y), C(0x2A3034), 1.0f);

    drawList->AddRectFilled(ImVec2(22.0f, 23.0f), ImVec2(54.0f, 55.0f), kLime, 8.0f);
    drawList->AddLine(ImVec2(31.0f, 43.0f), ImVec2(38.0f, 32.0f), C(0x18200F), 2.2f);
    drawList->AddLine(ImVec2(38.0f, 32.0f), ImVec2(46.0f, 43.0f), C(0x18200F), 2.2f);
    drawList->AddLine(ImVec2(34.0f, 39.0f), ImVec2(43.0f, 39.0f), C(0x18200F), 2.2f);
    TextRaw(drawList, fonts_.mono, 10.0f, ImVec2(66.0f, 27.0f), kSidebarText, "PROVENANCE");
    TextRaw(drawList, fonts_.monoSmall, 8.8f, ImVec2(66.0f, 40.0f), kSidebarMuted, "THIN-LINE LAB");

    Rule(drawList, 22.0f, kSidebarWidth - 22.0f, 77.0f, C(0x2A3034));
    TextRaw(drawList, fonts_.monoSmall, 8.8f, ImVec2(22.0f, 96.0f), kSidebarMuted, "CASE FILE");
    TextRaw(drawList, fonts_.mono, 16.0f, ImVec2(22.0f, 109.0f), kSidebarText, "AA-1122-07");
    TextRaw(drawList, fonts_.medium, 13.5f, ImVec2(22.0f, 139.0f), C(0xD3D7D2), "细线抗锯齿");
    TextRaw(drawList, fonts_.small, 10.6f, ImVec2(22.0f, 160.0f), kSidebarMuted, "恢复 / 重放 / 扩展");

    const ImU32 activeFill = C(0x1C2328);
    const ImU32 hoverFill = C(0x171D21);
    const float navX0 = 14.0f;
    const float navX1 = kSidebarWidth - 14.0f;
    const float navTop = 198.0f;
    const float itemHeight = 38.0f;
    const char* indexes[] = {"01", "02", "03", "04", "05"};
    const char* labels[] = {"检查概览", "来源账本", "恢复重放", "未知扩展", "证据链"};
    const bool active[] = {true, false, false, false, false};
    const int itemCount = 5;

    TextRaw(drawList, fonts_.monoSmall, 8.7f, ImVec2(22.0f, navTop - 20.0f), kSidebarMuted, "INSPECTION");
    for (int i = 0; i < itemCount; ++i) {
        const float y = navTop + static_cast<float>(i) * (itemHeight + 3.0f);
        const ImVec2 min(navX0, y);
        const ImVec2 max(navX1, y + itemHeight);
        char id[32];
        std::snprintf(id, sizeof(id), "##nav-%d", i);
        const bool hover = Hovered(min, max);
        if (Hit(min, max, id)) {
            if (i == 1) {
                std::snprintf(toast_, sizeof(toast_), "来源账本显示 04 个输入，哈希与快照一致");
            } else if (i == 2) {
                std::snprintf(toast_, sizeof(toast_), "已定位 R-17 的 03 个处理步骤");
            } else if (i == 3) {
                std::snprintf(toast_, sizeof(toast_), "B-02 已保留原始 chunk 与探针输出");
            } else if (i == 4) {
                std::snprintf(toast_, sizeof(toast_), "证据链已冻结，共 07 项证据");
            } else {
                std::snprintf(toast_, sizeof(toast_), "当前位于来源检查概览");
            }
            toastUntil_ = ImGui::GetTime() + 3.0;
        }
        if (active[i] || hover) {
            drawList->AddRectFilled(min, max, active[i] ? activeFill : hoverFill, 7.0f);
        }
        if (active[i]) {
            drawList->AddRectFilled(ImVec2(min.x, min.y + 9.0f), ImVec2(min.x + 2.0f, max.y - 9.0f), kLime, 1.0f);
        }
        TextRaw(drawList, fonts_.monoSmall, 9.2f, ImVec2(min.x + 15.0f, min.y + 14.0f),
                active[i] ? kLime : kSidebarMuted, indexes[i]);
        TextRaw(drawList, fonts_.body, 12.5f, ImVec2(min.x + 43.0f, min.y + 9.0f),
                active[i] ? kSidebarText : C(0xA9B0B1), labels[i]);
        if (i == 2) {
            drawList->AddRectFilled(ImVec2(max.x - 30.0f, min.y + 12.0f), ImVec2(max.x - 12.0f, max.y - 12.0f),
                                    C(0x2A3630), 7.0f);
            TextRaw(drawList, fonts_.monoSmall, 8.8f, ImVec2(max.x - 24.0f, min.y + 15.5f), kLime, "1");
        }
        if (i == 3) {
            Dot(drawList, ImVec2(max.x - 20.0f, min.y + 19.0f), 3.0f, kAmber);
        }
    }

    const float footerTop = size.y - 126.0f;
    Rule(drawList, 22.0f, kSidebarWidth - 22.0f, footerTop, C(0x2A3034));
    TextRaw(drawList, fonts_.monoSmall, 8.7f, ImVec2(22.0f, footerTop + 17.0f), kSidebarMuted, "PROCESS BASE");
    TextRaw(drawList, fonts_.mono, 9.5f, ImVec2(22.0f, footerTop + 31.0f), C(0xBEC5C3), "C++17  ·  VULKAN  ·  GLFW");
    Dot(drawList, ImVec2(25.0f, footerTop + 58.0f), 3.0f, kLime);
    TextRaw(drawList, fonts_.small, 10.3f, ImVec2(34.0f, footerTop + 50.0f), C(0x9FA7A7), "本地只读快照");
    TextRaw(drawList, fonts_.monoSmall, 8.6f, ImVec2(22.0f, footerTop + 74.0f), kSidebarMuted,
            "BUILD 0.4.2   /   2026.11.22");
}

void UiApp::DrawTopBar(const ImVec2& size) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(size.x, kTopBarHeight), kPage);
    drawList->AddLine(ImVec2(kSidebarWidth, kTopBarHeight - 1.0f), ImVec2(size.x, kTopBarHeight - 1.0f), kLine, 1.0f);

    const float x = kContentLeft;
    TextRaw(drawList, fonts_.monoSmall, 9.4f, ImVec2(x, 27.0f), kFaint, "CASES");
    drawList->AddLine(ImVec2(x + 38.0f, 28.0f), ImVec2(x + 47.0f, 37.0f), kLineStrong, 1.0f);
    drawList->AddLine(ImVec2(x + 47.0f, 28.0f), ImVec2(x + 38.0f, 37.0f), kLineStrong, 1.0f);
    TextRaw(drawList, fonts_.mono, 10.5f, ImVec2(x + 56.0f, 26.0f), kInk, "AA-1122-07");
    TextRaw(drawList, fonts_.monoSmall, 9.4f, ImVec2(x + 138.0f, 27.0f), kFaint, "/");
    TextRaw(drawList, fonts_.mono, 10.0f, ImVec2(x + 150.0f, 26.0f), kOlive, "SOURCE CHECK");

    const char* exportText = "导出证据包";
    const char* reviewText = "请求复核";
    const float exportWidth = TextWidth(fonts_.body, 12.0f, exportText) + 28.0f;
    const float reviewWidth = TextWidth(fonts_.body, 12.0f, reviewText) + 30.0f;
    const float reviewX = size.x - kPageRightMargin - reviewWidth;
    const float exportX = reviewX - exportWidth - 10.0f;
    const ImVec2 exportMin(exportX, 16.0f);
    const ImVec2 exportMax(exportX + exportWidth, 48.0f);
    const ImVec2 reviewMin(reviewX, 16.0f);
    const ImVec2 reviewMax(reviewX + reviewWidth, 48.0f);

    const bool exportHover = Hovered(exportMin, exportMax);
    if (Hit(exportMin, exportMax, "##export-bundle")) {
        const char* manifest =
            "AA-1122-07 evidence bundle\n"
            "source: thinline_aa_1080p.exr sha256:9f3a...5c81d\n"
            "replay: R-17, samples=8, order=center, delta=0.019\n"
            "extension: thinline.aa.ext#7 @0x1E, 96 bytes\n"
            "boundary: B-02, 0.500 +/- 0.012, unresolved\n"
            "evidence: E-01 E-03 E-06 E-07\n";
        ImGui::SetClipboardText(manifest);
        std::snprintf(toast_, sizeof(toast_), "证据包清单已复制到剪贴板");
        toastUntil_ = ImGui::GetTime() + 3.0;
    }
    drawList->AddRectFilled(exportMin, exportMax, exportHover ? C(0xEAE7DE) : kPanel, 7.0f);
    drawList->AddRect(exportMin, exportMax, kLineStrong, 7.0f, 0, 1.0f);
    TextRaw(drawList, fonts_.body, 12.0f,
            ImVec2(exportMin.x + 14.0f, exportMin.y + 8.0f), kInk, exportText);

    const bool reviewHover = Hovered(reviewMin, reviewMax);
    if (Hit(reviewMin, reviewMax, "##request-review")) {
        pendingReview_ = true;
        std::snprintf(toast_, sizeof(toast_), "复核请求已登记，目标 H-01");
        toastUntil_ = ImGui::GetTime() + 3.0;
    }
    drawList->AddRectFilled(reviewMin, reviewMax, reviewHover ? C(0x30383B) : C(0x1B2023), 7.0f);
    TextRaw(drawList, fonts_.body, 12.0f,
            ImVec2(reviewMin.x + 15.0f, reviewMin.y + 8.0f), C(0xF2F3ED), reviewText);
}

void UiApp::DrawHeader(const ImVec2& size) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float x = kContentLeft;
    TextRaw(drawList, fonts_.monoSmall, 9.2f, ImVec2(x, 80.0f), kOlive, "SOURCE PROVENANCE / REVIEW 07");
    const char* title = "细线抗锯齿 · 来源检查";
    TextRaw(drawList, fonts_.hero, 28.0f, ImVec2(x, 94.0f), kInk, title);
    TextRaw(drawList, fonts_.body, 12.6f, ImVec2(x, 136.0f), kMuted,
            "记录一次恢复重放与一个未知扩展。每条结论都回到原始输入、处理步骤与冻结证据。");

    const char* snapshot = "SNAPSHOT 2026-11-22  23:48 UTC";
    const float snapWidth = TextWidth(fonts_.monoSmall, 8.9f, snapshot) + 20.0f;
    const float snapX = size.x - kPageRightMargin - snapWidth;
    drawList->AddRectFilled(ImVec2(snapX, 100.0f), ImVec2(snapX + snapWidth, 122.0f), C(0xE9E7E0), 11.0f);
    Dot(drawList, ImVec2(snapX + 10.0f, 111.0f), 2.6f, kOlive);
    TextRaw(drawList, fonts_.monoSmall, 8.7f, ImVec2(snapX + 17.0f, 105.0f), C(0x646966), snapshot);

    const char* readOnly = "只读";
    const float roWidth = TextWidth(fonts_.small, 10.5f, readOnly) + 20.0f;
    const float roX = snapX - roWidth - 8.0f;
    drawList->AddRectFilled(ImVec2(roX, 100.0f), ImVec2(roX + roWidth, 122.0f), kPanel, 11.0f);
    drawList->AddRect(ImVec2(roX, 100.0f), ImVec2(roX + roWidth, 122.0f), kLine, 11.0f, 0, 1.0f);
    TextRaw(drawList, fonts_.small, 10.5f, ImVec2(roX + 10.0f, 103.5f), kMuted, readOnly);
}

void UiApp::DrawStatStrip(const ImVec2& size) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min(kContentLeft, 164.0f);
    const ImVec2 max(size.x - kPageRightMargin, 222.0f);
    Panel(drawList, min, max, false);

    struct Stat {
        const char* value;
        const char* label;
        const char* state;
        ImU32 valueColor;
        ImU32 dotColor;
    };
    const Stat stats[] = {
        {"04", "输入样本", "2 项哈希一致", kInk, kOlive},
        {"R-17", "恢复重放", "已对齐 · Δ 0.019", kOlive, kLime},
        {"01", "未知扩展", "0.9.3 · 96 B", kAmberInk, kAmber},
        {"01", "未决边界", humanConfirmed_ ? "B-02 · 人工保留" : "B-02 · 待人工",
         kInk, humanConfirmed_ ? kOlive : kAmber},
    };
    const float cellWidth = (max.x - min.x) / 4.0f;
    for (int i = 0; i < 4; ++i) {
        const float x = min.x + cellWidth * static_cast<float>(i);
        if (i > 0) {
            drawList->AddLine(ImVec2(x, min.y + 13.0f), ImVec2(x, max.y - 13.0f), kLine, 1.0f);
        }
        Dot(drawList, ImVec2(x + 19.0f, min.y + 20.0f), 3.0f, stats[i].dotColor);
        TextRaw(drawList, fonts_.medium, 16.5f, ImVec2(x + 30.0f, min.y + 10.0f), stats[i].valueColor, stats[i].value);
        TextRaw(drawList, fonts_.small, 10.2f, ImVec2(x + 30.0f, min.y + 34.0f), kMuted, stats[i].label);
        TextRaw(drawList, fonts_.small, 9.9f, ImVec2(x + 91.0f, min.y + 34.0f), kFaint, stats[i].state);
    }
}

void UiApp::DrawReplayCard(const ImVec2& min, const ImVec2& max) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    Panel(drawList, min, max);
    const float pad = 20.0f;
    const float x0 = min.x + pad;
    const float x1 = max.x - pad;

    TextRaw(drawList, fonts_.monoSmall, 8.8f, ImVec2(x0, min.y + 17.0f), kOlive,
            "AUTO CONCLUSION  /  REPLAY RECORD");
    TextRaw(drawList, fonts_.title, 20.0f, ImVec2(x0, min.y + 32.0f), kInk, "恢复后的重放");
    TextRaw(drawList, fonts_.monoSmall, 9.1f, ImVec2(x0 + 121.0f, min.y + 40.0f), kFaint, "R-17");

    const char* badge = "可归因 · 高置信";
    const float badgeWidth = TextWidth(fonts_.small, 10.5f, badge) + 23.0f;
    Badge(drawList, fonts_.small, ImVec2(x1 - badgeWidth, min.y + 24.0f), badge, kOliveSoft, kOlive);

    Rule(drawList, x0, x1, min.y + 69.0f);
    TextRaw(drawList, fonts_.monoSmall, 8.6f, ImVec2(x0, min.y + 78.0f), kFaint, "SOURCE");
    TextRaw(drawList, fonts_.small, 11.8f, ImVec2(x0 + 48.0f, min.y + 75.0f), kInk,
            "thinline_aa_1080p.exr");
    TextRaw(drawList, fonts_.monoSmall, 8.8f, ImVec2(x0 + 190.0f, min.y + 77.0f), kFaint,
            "RGBA16F  ·  1920×1080  ·  FRAME 0142");
    TextRight(drawList, fonts_.monoSmall, 8.8f, x1, min.y + 77.0f, kMuted, "sha256 9f3a…5c81d");

    const float previewWidth = 238.0f;
    const float previewLeft = x1 - previewWidth;
    const float previewTop = min.y + 102.0f;
    const float previewBottom = previewTop + 172.0f;
    const ImVec2 pMin(previewLeft, previewTop);
    const ImVec2 pMax(previewLeft + previewWidth, previewBottom);

    drawList->AddRectFilled(pMin, pMax, kDarkPanel, 8.0f);
    drawList->AddRect(pMin, pMax, C(0x30383C), 8.0f, 0, 1.0f);
    TextRaw(drawList, fonts_.monoSmall, 8.4f, ImVec2(pMin.x + 12.0f, pMin.y + 10.0f), C(0x7F8A8D),
            "EDGE COVERAGE  /  ×8");
    const char* delta = "Δmax 0.019";
    TextRight(drawList, fonts_.monoSmall, 8.5f, pMax.x - 11.0f, pMin.y + 10.0f, kLime, delta);

    const ImVec2 chartMin(pMin.x + 14.0f, pMin.y + 31.0f);
    const ImVec2 chartMax(pMax.x - 12.0f, pMax.y - 31.0f);
    for (int i = 0; i <= 4; ++i) {
        const float y = chartMin.y + (chartMax.y - chartMin.y) * static_cast<float>(i) / 4.0f;
        drawList->AddLine(ImVec2(chartMin.x, y), ImVec2(chartMax.x, y), C(0x2A3338, 170), 1.0f);
    }
    for (int i = 0; i <= 6; ++i) {
        const float x = chartMin.x + (chartMax.x - chartMin.x) * static_cast<float>(i) / 6.0f;
        drawList->AddLine(ImVec2(x, chartMin.y), ImVec2(x, chartMax.y), C(0x252D31, 140), 1.0f);
    }

    const float uncertaintyX = chartMin.x + (chartMax.x - chartMin.x) * 0.62f;
    const float bandWidth = 10.0f;
    drawList->AddRectFilled(ImVec2(uncertaintyX - bandWidth * 0.5f, chartMin.y),
                            ImVec2(uncertaintyX + bandWidth * 0.5f, chartMax.y), C(0xE0A34C, 34), 2.0f);

    const float sourceValues[] = {0.02f, 0.03f, 0.08f, 0.31f, 0.72f, 0.95f, 0.55f, 0.18f,
                                  0.06f, 0.04f, 0.07f, 0.22f, 0.63f, 0.97f, 0.62f, 0.20f,
                                  0.06f, 0.03f, 0.02f};
    const float replayValues[] = {0.02f, 0.04f, 0.10f, 0.34f, 0.75f, 0.91f, 0.52f, 0.17f,
                                  0.05f, 0.03f, 0.08f, 0.25f, 0.68f, 0.94f, 0.57f, 0.19f,
                                  0.07f, 0.04f, 0.02f};
    ImVec2 sourcePoints[IM_ARRAYSIZE(sourceValues)];
    ImVec2 replayPoints[IM_ARRAYSIZE(replayValues)];
    for (int i = 0; i < IM_ARRAYSIZE(sourceValues); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(IM_ARRAYSIZE(sourceValues) - 1);
        const float x = chartMin.x + (chartMax.x - chartMin.x) * t;
        sourcePoints[i] = ImVec2(x, chartMax.y - sourceValues[i] * (chartMax.y - chartMin.y));
        replayPoints[i] = ImVec2(x, chartMax.y - replayValues[i] * (chartMax.y - chartMin.y));
    }
    drawList->AddPolyline(sourcePoints, IM_ARRAYSIZE(sourcePoints), C(0xD9DCD6), 0, 1.45f);
    drawList->AddPolyline(replayPoints, IM_ARRAYSIZE(replayPoints), kLime, 0, 1.65f);
    drawList->AddCircleFilled(ImVec2(uncertaintyX, chartMin.y + 2.0f), 2.4f, kAmber);
    TextRaw(drawList, fonts_.monoSmall, 7.8f, ImVec2(uncertaintyX + 5.0f, chartMin.y - 1.0f), kAmber, "B-02");

    drawList->AddLine(ImVec2(pMin.x + 13.0f, pMax.y - 21.0f), ImVec2(pMin.x + 25.0f, pMax.y - 21.0f),
                      C(0xD9DCD6), 1.4f);
    TextRaw(drawList, fonts_.monoSmall, 7.9f, ImVec2(pMin.x + 29.0f, pMax.y - 25.0f), C(0x9EA7A7), "SRC");
    drawList->AddLine(ImVec2(pMin.x + 63.0f, pMax.y - 21.0f), ImVec2(pMin.x + 75.0f, pMax.y - 21.0f),
                      kLime, 1.6f);
    TextRaw(drawList, fonts_.monoSmall, 7.9f, ImVec2(pMin.x + 79.0f, pMax.y - 25.0f), C(0x9EA7A7), "R-17");
    TextRight(drawList, fonts_.monoSmall, 7.6f, pMax.x - 12.0f, pMax.y - 25.0f, C(0x657075), "STEP 01–04");

    struct PipelineStep {
        const char* index;
        const char* title;
        const char* detail;
        const char* evidence;
        bool verified;
    };
    const PipelineStep steps[] = {
        {"01", "原始输入", "read 1920×1080 · sha256 9f3a…5c81d", "E-01", true},
        {"02", "恢复去块", "mask=edge · strength=0.82 · output 1920×1080", "E-03", true},
        {"03", "重放 R-17", "samples=8 · order=center · 28.4 ms", "E-03", true},
        {"04", "覆盖比较", "Δmax=0.019 · threshold=0.020 · PASS", "E-06", true},
    };
    const float timelineTop = min.y + 112.0f;
    const float rowHeight = 40.0f;
    const float timelineX = x0 + 8.0f;
    for (int i = 0; i < 4; ++i) {
        const float rowY = timelineTop + rowHeight * static_cast<float>(i);
        if (i < 3) {
            drawList->AddLine(ImVec2(timelineX, rowY + 13.0f), ImVec2(timelineX, rowY + rowHeight + 10.0f),
                              C(0xD5D3CA), 1.0f);
        }
        drawList->AddCircleFilled(ImVec2(timelineX, rowY + 9.0f), 6.5f, kOliveSoft);
        if (steps[i].verified) {
            CheckIcon(drawList, ImVec2(timelineX, rowY + 9.0f), 5.0f, kOliveSoft, kOlive);
        }
        TextRaw(drawList, fonts_.monoSmall, 8.0f, ImVec2(timelineX + 16.0f, rowY - 1.0f), kFaint, steps[i].index);
        TextRaw(drawList, fonts_.medium, 12.4f, ImVec2(timelineX + 37.0f, rowY - 2.0f), kInk, steps[i].title);
        TextRaw(drawList, fonts_.monoSmall, 8.6f, ImVec2(timelineX + 37.0f, rowY + 16.0f), kMuted, steps[i].detail);

        const char* ev = steps[i].evidence;
        const float evWidth = TextWidth(fonts_.monoSmall, 9.8f, ev) + 13.0f;
        const ImVec2 evMin(previewLeft - 14.0f - evWidth, rowY + 3.0f);
        const ImVec2 evMax(evMin.x + evWidth, evMin.y + 18.0f);
        const bool evHover = Hovered(evMin, evMax);
        char evId[32];
        std::snprintf(evId, sizeof(evId), "##replay-ev-%d", i);
        if (Hit(evMin, evMax, evId)) {
            evidenceModal_ = (i == 1 || i == 2) ? 1 : (i == 3 ? 2 : 0);
        }
        drawList->AddRectFilled(evMin, evMax, evHover ? C(0xE3EDCB) : C(0xEFEEE8), 5.0f);
        TextRaw(drawList, fonts_.monoSmall, 9.8f, ImVec2(evMin.x + 6.5f, evMin.y + 4.0f),
                evHover ? kOlive : C(0x5F6462), ev);
    }

    Rule(drawList, x0, x1, min.y + 302.0f);
    Badge(drawList, fonts_.small, ImVec2(x0, min.y + 314.0f), "自动结论", kOliveSoft, kOlive);
    TextRaw(drawList, fonts_.body, 12.1f, ImVec2(x0 + 65.0f, min.y + 314.0f), kInk,
            "R-17 与冻结管线一致，归因到原始输入，未引用未知扩展。");
    const float linkWidth = TextWidth(fonts_.small, 10.4f, "定位步骤 01–04  →");
    const ImVec2 linkMin(x1 - linkWidth - 2.0f, min.y + 314.0f);
    const ImVec2 linkMax(x1, min.y + 333.0f);
    const bool linkHover = Hovered(linkMin, linkMax);
    if (Hit(linkMin, linkMax, "##locate-pipeline")) {
        std::snprintf(toast_, sizeof(toast_), "已定位原始输入 → 恢复 → 重放 → 比较的完整处理步骤");
        toastUntil_ = ImGui::GetTime() + 3.0;
    }
    TextRaw(drawList, fonts_.small, 10.4f, linkMin, linkHover ? kOlive : kMuted, "定位步骤 01–04  →");
}

void UiApp::DrawUnknownCard(const ImVec2& min, const ImVec2& max) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    Panel(drawList, min, max);
    const float pad = 20.0f;
    const float x0 = min.x + pad;
    const float x1 = max.x - pad;

    TextRaw(drawList, fonts_.monoSmall, 8.8f, ImVec2(x0, min.y + 17.0f), kAmberInk,
            "UNRESOLVED  /  EXTENSION");
    TextRaw(drawList, fonts_.title, 19.0f, ImVec2(x0, min.y + 32.0f), kInk, "未知扩展");
    TextRaw(drawList, fonts_.mono, 11.0f, ImVec2(x0 + 81.0f, min.y + 40.0f), kMuted,
            "thinline.aa.ext#7");

    const char* badge = "无法判断";
    const float badgeWidth = TextWidth(fonts_.small, 10.5f, badge) + 23.0f;
    Badge(drawList, fonts_.small, ImVec2(x1 - badgeWidth, min.y + 24.0f), badge, kAmberSoft, kAmberInk);
    Rule(drawList, x0, x1, min.y + 66.0f);

    const float previewWidth = 238.0f;
    const float previewLeft = x1 - previewWidth;
    const float previewTop = min.y + 77.0f;
    const float previewBottom = previewTop + 88.0f;

    TextRaw(drawList, fonts_.monoSmall, 8.4f, ImVec2(x0, min.y + 78.0f), kFaint, "REGISTRY ENTRY");
    TextRaw(drawList, fonts_.mono, 10.2f, ImVec2(x0 + 98.0f, min.y + 76.0f), kInk,
            "class=coverage-declaration");
    TextRaw(drawList, fonts_.monoSmall, 8.7f, ImVec2(x0, min.y + 96.0f), kFaint,
            "offset 0x1E   ·   96 bytes   ·   v0.9.3   ·   sha256 a6c0…e42f");
    TextRaw(drawList, fonts_.body, 11.9f, ImVec2(x0, min.y + 115.0f), kInk,
            "扩展声明覆盖模式，但没有写入正规化参数。");
    TextRaw(drawList, fonts_.body, 11.9f, ImVec2(x0, min.y + 134.0f), kMuted,
            "nearest 与 center 在 0.500 ± 0.012 内不可区分。");

    const ImVec2 pMin(previewLeft, previewTop);
    const ImVec2 pMax(previewLeft + previewWidth, previewBottom);
    drawList->AddRectFilled(pMin, pMax, C(0xFAF3E5), 7.0f);
    drawList->AddRect(pMin, pMax, C(0xE8D6B7), 7.0f, 0, 1.0f);
    TextRaw(drawList, fonts_.monoSmall, 7.8f, ImVec2(pMin.x + 10.0f, pMin.y + 8.0f), kAmberInk,
            "NEAREST  /  CENTER");
    TextRight(drawList, fonts_.monoSmall, 7.8f, pMax.x - 9.0f, pMin.y + 8.0f, kAmberInk, "B-02");
    const float axisY = pMin.y + 43.0f;
    drawList->AddLine(ImVec2(pMin.x + 13.0f, axisY), ImVec2(pMax.x - 13.0f, axisY), C(0xC6AA7C), 1.0f);
    const float centerX = pMin.x + previewWidth * 0.61f;
    drawList->AddRectFilled(ImVec2(centerX - 8.0f, axisY - 9.0f), ImVec2(centerX + 8.0f, axisY + 9.0f),
                            C(0xE0A34C, 75), 2.0f);
    drawList->AddCircleFilled(ImVec2(centerX - 4.0f, axisY), 3.2f, C(0xAE7627));
    drawList->AddCircle(ImVec2(centerX + 4.0f, axisY), 3.4f, C(0xAE7627), 8, 1.2f);
    drawList->AddLine(ImVec2(centerX, axisY - 14.0f), ImVec2(centerX, axisY + 14.0f), C(0xAE7627, 140), 1.0f);
    TextRaw(drawList, fonts_.monoSmall, 7.4f, ImVec2(pMin.x + 13.0f, pMax.y - 14.0f), C(0x8F7754), "0.488");
    TextRight(drawList, fonts_.monoSmall, 7.4f, pMax.x - 13.0f, pMax.y - 14.0f, C(0x8F7754), "0.512");

    Rule(drawList, x0, x1, min.y + 178.0f);
    const ImVec2 boundaryMin(x0, min.y + 190.0f);
    const ImVec2 boundaryMax(x1, max.y - 14.0f);
    drawList->AddRectFilled(boundaryMin, boundaryMax, kAmberSoft, 8.0f);
    drawList->AddRectFilled(boundaryMin, ImVec2(boundaryMin.x + 2.5f, boundaryMax.y), kAmber, 1.0f);

    TextRaw(drawList, fonts_.medium, 12.3f, ImVec2(boundaryMin.x + 14.0f, boundaryMin.y + 10.0f), kAmberInk,
            "边界 B-02");
    TextRaw(drawList, fonts_.monoSmall, 8.6f, ImVec2(boundaryMin.x + 79.0f, boundaryMin.y + 13.0f), C(0x9A7440),
            "ALPHA COVERAGE 0.500 ± 0.012");
    TextRaw(drawList, fonts_.body, 11.4f, ImVec2(boundaryMin.x + 14.0f, boundaryMin.y + 30.0f), C(0x73522A),
            "两路采样在不确定带内重合；现有探针不足，禁止自动归属。");

    TextRaw(drawList, fonts_.small, 9.6f, ImVec2(boundaryMin.x + 14.0f, boundaryMax.y - 23.0f), kAmberInk,
            "保留: E-06 原始 96 B chunk   /   E-07 decode-fail.log");

    const char* openText = "打开证据";
    const char* runText = "运行复现";
    const float runWidth = TextWidth(fonts_.small, 10.7f, runText) + 22.0f;
    const float openWidth = TextWidth(fonts_.small, 10.7f, openText) + 22.0f;
    const float runX = boundaryMax.x - 12.0f - runWidth;
    const float openX = runX - openWidth - 7.0f;
    const ImVec2 openMin(openX, boundaryMax.y - 34.0f);
    const ImVec2 openMax(openX + openWidth, boundaryMax.y - 8.0f);
    const ImVec2 runMin(runX, boundaryMax.y - 34.0f);
    const ImVec2 runMax(runX + runWidth, boundaryMax.y - 8.0f);
    const bool openHover = Hovered(openMin, openMax);
    const bool runHover = Hovered(runMin, runMax);
    if (Hit(openMin, openMax, "##open-boundary-evidence")) {
        evidenceModal_ = 2;
    }
    if (Hit(runMin, runMax, "##run-boundary-probe")) {
        failureModal_ = true;
    }
    drawList->AddRectFilled(openMin, openMax, openHover ? C(0xF4E7D2) : kAmberSoft, 6.0f);
    drawList->AddRect(openMin, openMax, C(0xD9BE92), 6.0f, 0, 1.0f);
    TextRaw(drawList, fonts_.small, 10.7f, ImVec2(openMin.x + 11.0f, openMin.y + 6.5f), kAmberInk, openText);
    drawList->AddRectFilled(runMin, runMax, runHover ? C(0xC28A38) : C(0xAE7627), 6.0f);
    TextRaw(drawList, fonts_.small, 10.7f, ImVec2(runMin.x + 11.0f, runMin.y + 6.5f), C(0xFFF8EB), runText);
}

void UiApp::DrawDecisionCard(const ImVec2& min, const ImVec2& max) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    Panel(drawList, min, max);
    const float x0 = min.x + 17.0f;
    const float x1 = max.x - 17.0f;

    TextRaw(drawList, fonts_.monoSmall, 8.8f, ImVec2(x0, min.y + 17.0f), kOlive, "DECISION LAYERS");
    TextRaw(drawList, fonts_.title, 19.0f, ImVec2(x0, min.y + 31.0f), kInk, "结论分层");
    TextRight(drawList, fonts_.monoSmall, 8.8f, x1, min.y + 38.0f, kFaint, "AUTO 02  /  HUMAN 01");

    const float autoTop = min.y + 66.0f;
    drawList->AddRectFilled(ImVec2(x0 - 7.0f, autoTop - 12.0f), ImVec2(x1 + 7.0f, min.y + 188.0f),
                            C(0xF4F7ED), 7.0f);
    drawList->AddRectFilled(ImVec2(x0, autoTop - 2.0f), ImVec2(x0 + 2.0f, autoTop + 115.0f), kLime, 1.0f);
    TextRaw(drawList, fonts_.monoSmall, 8.6f, ImVec2(x0 + 11.0f, autoTop), kOlive, "AUTOMATIC");
    TextRaw(drawList, fonts_.small, 9.8f, ImVec2(x0 + 77.0f, autoTop - 1.0f), kMuted, "自动结论 02");

    struct AutoRow {
        const char* index;
        const char* title;
        const char* chain;
        const char* confidence;
    };
    const AutoRow rows[] = {
        {"A-01", "R-17 重放可归因", "E-01 + E-03  →  步骤 01–03", "高"},
        {"A-02", "输入哈希未漂移", "E-01  →  步骤 01", "高"},
    };
    for (int i = 0; i < 2; ++i) {
        const float y = autoTop + 24.0f + static_cast<float>(i) * 45.0f;
        CheckIcon(drawList, ImVec2(x0 + 18.0f, y + 9.0f), 7.0f, kOliveSoft, kOlive);
        TextRaw(drawList, fonts_.monoSmall, 8.2f, ImVec2(x0 + 32.0f, y + 1.0f), kFaint, rows[i].index);
        TextRaw(drawList, fonts_.body, 12.1f, ImVec2(x0 + 72.0f, y - 1.0f), kInk, rows[i].title);
        TextRaw(drawList, fonts_.small, 10.7f, ImVec2(x0 + 72.0f, y + 16.0f), kMuted, rows[i].chain);
        TextRight(drawList, fonts_.small, 9.8f, x1 - 3.0f, y + 1.0f, kOlive, rows[i].confidence);
    }
    TextRaw(drawList, fonts_.small, 9.6f, ImVec2(x0 + 11.0f, autoTop + 111.0f), kFaint,
            "仅使用冻结证据，不引入人工判断");

    Rule(drawList, x0, x1, min.y + 191.0f);
    const float humanTop = min.y + 203.0f;
    drawList->AddRectFilled(ImVec2(x0 - 7.0f, humanTop - 12.0f), ImVec2(x1 + 7.0f, max.y - 11.0f),
                            C(0xFDF6EA), 7.0f);
    drawList->AddRectFilled(ImVec2(x0, humanTop - 2.0f), ImVec2(x0 + 2.0f, max.y - 17.0f), kAmber, 1.0f);
    TextRaw(drawList, fonts_.monoSmall, 8.6f, ImVec2(x0 + 11.0f, humanTop), kAmberInk, "HUMAN CONFIRMATION");
    TextRaw(drawList, fonts_.small, 9.8f, ImVec2(x0 + 130.0f, humanTop - 1.0f), kMuted, "人工确认 01");

    const char* humanTitle = humanConfirmed_ ? "B-02 已由人工保留为未知"
                                             : "接受 B-02 为永久未知？";
    const char* humanState = humanConfirmed_ ? "已确认" : (supplementRequested_ ? "已退回" : "待确认");
    TextRaw(drawList, fonts_.body, 12.1f, ImVec2(x0 + 11.0f, humanTop + 22.0f), kInk, humanTitle);
    Badge(drawList, fonts_.small, ImVec2(x1 - 52.0f, humanTop + 19.0f), humanState,
          humanConfirmed_ ? kOliveSoft : kAmberSoft, humanConfirmed_ ? kOlive : kAmberInk);
    TextRaw(drawList, fonts_.small, 9.7f, ImVec2(x0 + 11.0f, humanTop + 43.0f), kMuted,
            supplementRequested_ ? "补证请求已记录；自动结论保持不变。"
                                 : "人工结论单独记录，不覆盖自动判定。");

    const float buttonY = max.y - 55.0f;
    const float buttonH = 30.0f;
    const float primaryWidth = 106.0f;
    const float secondaryWidth = 88.0f;
    const float primaryX = x1 - primaryWidth;
    const float secondaryX = primaryX - secondaryWidth - 7.0f;
    const ImVec2 primaryMin(primaryX, buttonY);
    const ImVec2 primaryMax(primaryX + primaryWidth, buttonY + buttonH);
    const ImVec2 secondaryMin(secondaryX, buttonY);
    const ImVec2 secondaryMax(secondaryX + secondaryWidth, buttonY + buttonH);

    const bool primaryHover = Hovered(primaryMin, primaryMax);
    const bool secondaryHover = Hovered(secondaryMin, secondaryMax);
    if (humanConfirmed_) {
        if (Hit(primaryMin, primaryMax, "##reopen-unknown")) {
            humanConfirmed_ = false;
            supplementRequested_ = false;
            std::snprintf(toast_, sizeof(toast_), "已撤销人工确认，B-02 恢复为待确认");
            toastUntil_ = ImGui::GetTime() + 3.0;
        }
        drawList->AddRectFilled(primaryMin, primaryMax, primaryHover ? C(0xE2EED0) : kOliveSoft, 6.0f);
        drawList->AddRect(primaryMin, primaryMax, C(0xBED49B), 6.0f, 0, 1.0f);
        TextRaw(drawList, fonts_.small, 10.7f, ImVec2(primaryMin.x + 22.0f, primaryMin.y + 6.5f),
                kOlive, "撤销确认");
        if (Hit(secondaryMin, secondaryMax, "##review-confirmed")) {
            pendingReview_ = true;
            std::snprintf(toast_, sizeof(toast_), "复核请求已登记，目标 H-01");
            toastUntil_ = ImGui::GetTime() + 3.0;
        }
        drawList->AddRectFilled(secondaryMin, secondaryMax, secondaryHover ? C(0xEFEDE6) : kPanelAlt, 6.0f);
        drawList->AddRect(secondaryMin, secondaryMax, kLine, 6.0f, 0, 1.0f);
        TextRaw(drawList, fonts_.small, 10.7f, ImVec2(secondaryMin.x + 18.0f, secondaryMin.y + 6.5f),
                kMuted, "查看记录");
    } else {
        if (Hit(primaryMin, primaryMax, "##confirm-unknown")) {
            humanConfirmed_ = true;
            supplementRequested_ = false;
            std::snprintf(toast_, sizeof(toast_), "人工确认已记录：B-02 保留为未知，自动结论未改变");
            toastUntil_ = ImGui::GetTime() + 3.0;
        }
        drawList->AddRectFilled(primaryMin, primaryMax, primaryHover ? C(0x2D3436) : kInk, 6.0f);
        TextRaw(drawList, fonts_.small, 10.7f, ImVec2(primaryMin.x + 27.0f, primaryMin.y + 6.5f),
                C(0xF4F6EF), "确认未知");
        if (Hit(secondaryMin, secondaryMax, "##request-more-evidence")) {
            supplementRequested_ = true;
            std::snprintf(toast_, sizeof(toast_), "已退回补证：需提供扩展正规化参数或双采样探针");
            toastUntil_ = ImGui::GetTime() + 3.0;
        }
        drawList->AddRectFilled(secondaryMin, secondaryMax, secondaryHover ? C(0xEFEDE6) : kPanelAlt, 6.0f);
        drawList->AddRect(secondaryMin, secondaryMax, kLine, 6.0f, 0, 1.0f);
        TextRaw(drawList, fonts_.small, 10.7f, ImVec2(secondaryMin.x + 16.0f, secondaryMin.y + 6.5f),
                kMuted, "退回补证");
    }
    TextRaw(drawList, fonts_.monoSmall, 8.2f, ImVec2(x0 + 11.0f, max.y - 20.0f), kFaint,
            pendingReview_ ? "REVIEW REQUESTED  /  H-01" : "NO AUTO OVERRIDE  /  H-01 INDEPENDENT");
}

void UiApp::DrawEvidenceCard(const ImVec2& min, const ImVec2& max) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    Panel(drawList, min, max);
    const float x0 = min.x + 17.0f;
    const float x1 = max.x - 17.0f;

    TextRaw(drawList, fonts_.monoSmall, 8.8f, ImVec2(x0, min.y + 16.0f), kOlive, "EVIDENCE REGISTER");
    TextRaw(drawList, fonts_.title, 19.0f, ImVec2(x0, min.y + 29.0f), kInk, "关键证据");
    TextRight(drawList, fonts_.monoSmall, 8.8f, x1, min.y + 35.0f, kFaint, "04 FROZEN  /  07 TOTAL");

    struct EvidenceRow {
        const char* id;
        const char* file;
        const char* meta;
    };
    const EvidenceRow rows[] = {
        {"E-01", "thinline_aa_1080p.exr", "sha256 9f3a…5c81d  ·  步骤 01"},
        {"E-03", "replay-r17.log", "sha256 2d7b…9e04  ·  步骤 03"},
        {"E-06", "probe-0x1E.bin", "96 B  ·  边界 B-02"},
        {"E-07", "decode-fail.log", "exit 3  ·  边界 B-02"},
    };
    const float rowsTop = min.y + 58.0f;
    for (int i = 0; i < 4; ++i) {
        const float y = rowsTop + static_cast<float>(i) * 29.0f;
        const ImVec2 rMin(x0 - 6.0f, y - 4.0f);
        const ImVec2 rMax(x1 + 6.0f, y + 26.0f);
        const bool hover = Hovered(rMin, rMax);
        char id[32];
        std::snprintf(id, sizeof(id), "##evidence-row-%d", i);
        if (Hit(rMin, rMax, id)) {
            evidenceModal_ = i;
        }
        if (hover) {
            drawList->AddRectFilled(rMin, rMax, C(0xF3F1EA), 6.0f);
        }
        drawList->AddCircleFilled(ImVec2(x0 + 4.0f, y + 8.0f), 2.8f,
                                  i >= 2 ? kAmber : kOlive);
        TextRaw(drawList, fonts_.monoSmall, 8.4f, ImVec2(x0 + 14.0f, y + 1.0f), kFaint, rows[i].id);
        TextRaw(drawList, fonts_.mono, 10.0f, ImVec2(x0 + 51.0f, y - 1.0f), kInk, rows[i].file);
        TextRaw(drawList, fonts_.small, 9.3f, ImVec2(x0 + 51.0f, y + 12.0f), kMuted, rows[i].meta);
        TextRight(drawList, fonts_.small, 9.2f, x1, y + 2.0f, hover ? kOlive : kFaint, "冻结");
    }
}

void UiApp::DrawRunbookCard(const ImVec2& min, const ImVec2& max) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    Panel(drawList, min, max);
    const float x0 = min.x + 17.0f;
    const float x1 = max.x - 17.0f;

    TextRaw(drawList, fonts_.monoSmall, 8.4f, ImVec2(x0, min.y + 13.0f), kAmberInk, "FAILURE RUNBOOK");
    TextRaw(drawList, fonts_.medium, 14.0f, ImVec2(x0, min.y + 27.0f), kInk, "重放失败时");
    TextRight(drawList, fonts_.small, 9.7f, x1, min.y + 28.0f, kMuted, "保留 → 切换 → 导出");

    const char* copyText = "复制命令";
    const float copyWidth = TextWidth(fonts_.small, 10.0f, copyText) + 20.0f;
    const ImVec2 copyMin(x1 - copyWidth, min.y + 11.0f);
    const ImVec2 copyMax(x1, min.y + 37.0f);
    const bool copyHover = Hovered(copyMin, copyMax);
    if (Hit(copyMin, copyMax, "##copy-runbook")) {
        ImGui::SetClipboardText(
            "repair --freeze-input && replay --sample-mode=center\n"
            "export probe-0x1E.bin --include-driver\n");
        std::snprintf(toast_, sizeof(toast_), "可执行后续操作已复制");
        toastUntil_ = ImGui::GetTime() + 3.0;
    }
    drawList->AddRectFilled(copyMin, copyMax, copyHover ? C(0xF4E6CF) : kAmberSoft, 6.0f);
    drawList->AddRect(copyMin, copyMax, C(0xE2CAA4), 6.0f, 0, 1.0f);
    TextRaw(drawList, fonts_.small, 10.0f, ImVec2(copyMin.x + 10.0f, copyMin.y + 5.5f), kAmberInk, copyText);

    const ImVec2 row1(min.x + 8.0f, min.y + 50.0f);
    const ImVec2 row2(min.x + 8.0f, min.y + 70.0f);
    if (Hit(row1, ImVec2(max.x - 8.0f, min.y + 68.0f), "##runbook-line-1")) {
        failureModal_ = true;
    }
    if (Hit(row2, ImVec2(max.x - 8.0f, min.y + 88.0f), "##runbook-line-2")) {
        failureModal_ = true;
    }
    TextRaw(drawList, fonts_.monoSmall, 8.2f, ImVec2(x0, min.y + 51.0f), kAmberInk, "01");
    TextRaw(drawList, fonts_.monoSmall, 8.3f, ImVec2(x0 + 21.0f, min.y + 51.0f), C(0x3F4443),
            "repair --freeze-input && replay --sample-mode=center");
    TextRaw(drawList, fonts_.monoSmall, 8.2f, ImVec2(x0, min.y + 71.0f), kAmberInk, "02");
    TextRaw(drawList, fonts_.monoSmall, 8.3f, ImVec2(x0 + 21.0f, min.y + 71.0f), C(0x3F4443),
            "export probe-0x1E.bin --include-driver");
    TextRaw(drawList, fonts_.small, 9.0f, ImVec2(x0, max.y - 18.0f), kFaint,
            "OUTPUT → evidence/e-06-retry.bin   ·   原始 E-06 不被覆盖");
}

void UiApp::DrawEvidenceModal(const ImVec2& size) {
    g_BlockHits = false;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(ImVec2(0.0f, 0.0f), size, C(0x0B0D10, 162));

    struct EvidenceInfo {
        const char* id;
        const char* file;
        const char* kind;
        const char* hash;
        const char* step;
        const char* raw;
        const char* chain;
    };
    const EvidenceInfo evidence[] = {
        {"E-01", "thinline_aa_1080p.exr", "原始输入 / EXR",
         "9f3a72e0c1b47d80a1c0f0e53218d4bbcb45a0e81d6145acbc6112029a5c81d",
         "STEP 01 · READ", "1920×1080 · RGBA16F · FRAME 0142 · FROZEN 23:48:12Z",
         "E-01 → STEP 01 → R-17"},
        {"E-03", "replay-r17.log", "处理日志 / JSONL",
         "2d7beb8614e5d19f0a46ccb35da0dcc1bbdb51c8b4b0eb0d7efe2434c9e09e04",
         "STEP 02–03 · RESTORE / REPLAY", "mask=edge · strength=0.82 · samples=8 · order=center",
         "E-01 → STEP 02 → STEP 03 → E-03"},
        {"E-06", "probe-0x1E.bin", "原始扩展块 / BINARY",
         "a6c04d1d3f00be2e5ebf64c7f9e1d4f0f9a457b93f50e0b7615d74d3ba7ee42f",
         "BOUNDARY B-02 · UNRESOLVED", "offset=0x1E · size=96 · version=0.9.3 · status=UNRESOLVED",
         "E-06 → B-02 → H-01 待人工"},
        {"E-07", "decode-fail.log", "失败日志 / TEXT",
         "7b18a4f62ce6a539befe0e6c4da43b811d8c6db4f8d1df86b386d1f0fb225f1c",
         "BOUNDARY B-02 · PROBE FAILURE", "exit=3 · probe=T-03 · seed=0x1E · stderr retained",
         "E-06 → E-07 → B-02 → H-01 待人工"},
    };
    const int index = std::clamp(evidenceModal_, 0, 3);
    const EvidenceInfo& item = evidence[index];

    const ImVec2 panelMin(size.x * 0.5f - 310.0f, size.y * 0.5f - 226.0f);
    const ImVec2 panelMax(size.x * 0.5f + 310.0f, size.y * 0.5f + 226.0f);
    drawList->AddRectFilled(ImVec2(panelMin.x + 2.0f, panelMin.y + 6.0f),
                            ImVec2(panelMax.x + 2.0f, panelMax.y + 6.0f), C(0x000000, 45), 13.0f);
    drawList->AddRectFilled(panelMin, panelMax, C(0xFDFCF8), 13.0f);
    drawList->AddRect(panelMin, panelMax, C(0xD7D2C7), 13.0f, 0, 1.0f);

    const float x0 = panelMin.x + 26.0f;
    const float x1 = panelMax.x - 26.0f;
    TextRaw(drawList, fonts_.monoSmall, 8.8f, ImVec2(x0, panelMin.y + 20.0f), kOlive,
            "EVIDENCE  /  FROZEN CAPTURE");
    TextRaw(drawList, fonts_.title, 20.0f, ImVec2(x0, panelMin.y + 37.0f), kInk, item.id);
    TextRaw(drawList, fonts_.mono, 11.5f, ImVec2(x0 + 48.0f, panelMin.y + 45.0f), kMuted, item.file);
    Badge(drawList, fonts_.small, ImVec2(x1 - 60.0f, panelMin.y + 32.0f), "已冻结", kOliveSoft, kOlive);

    const ImVec2 closeXMin(x1 - 20.0f, panelMin.y + 67.0f);
    const ImVec2 closeXMax(x1, panelMin.y + 84.0f);
    if (Hit(closeXMin, closeXMax, "##close-evidence-x")) {
        evidenceModal_ = -1;
        return;
    }
    drawList->AddLine(closeXMin, closeXMax, kMuted, 1.2f);
    drawList->AddLine(ImVec2(closeXMin.x, closeXMax.y), ImVec2(closeXMax.x, closeXMin.y), kMuted, 1.2f);

    Rule(drawList, x0, x1, panelMin.y + 86.0f);
    TextRaw(drawList, fonts_.monoSmall, 8.2f, ImVec2(x0, panelMin.y + 101.0f), kFaint, "TYPE");
    TextRaw(drawList, fonts_.body, 11.5f, ImVec2(x0 + 76.0f, panelMin.y + 97.0f), kInk, item.kind);
    TextRaw(drawList, fonts_.monoSmall, 8.2f, ImVec2(x0, panelMin.y + 124.0f), kFaint, "SHA256");
    TextRaw(drawList, fonts_.mono, 9.8f, ImVec2(x0 + 76.0f, panelMin.y + 121.0f), kInk, item.hash);
    TextRaw(drawList, fonts_.monoSmall, 8.2f, ImVec2(x0, panelMin.y + 147.0f), kFaint, "STEP");
    TextRaw(drawList, fonts_.mono, 9.7f, ImVec2(x0 + 76.0f, panelMin.y + 144.0f), kOlive, item.step);
    TextRaw(drawList, fonts_.monoSmall, 8.2f, ImVec2(x0, panelMin.y + 170.0f), kFaint, "RAW");
    TextRaw(drawList, fonts_.mono, 9.2f, ImVec2(x0 + 76.0f, panelMin.y + 168.0f), kMuted, item.raw);

    const ImVec2 chainMin(x0, panelMin.y + 200.0f);
    const ImVec2 chainMax(x1, panelMin.y + 246.0f);
    drawList->AddRectFilled(chainMin, chainMax, C(0xF4F2EB), 7.0f);
    TextRaw(drawList, fonts_.monoSmall, 8.2f, ImVec2(chainMin.x + 12.0f, chainMin.y + 9.0f), kFaint,
            "PROVENANCE CHAIN");
    TextRaw(drawList, fonts_.small, 11.4f, ImVec2(chainMin.x + 12.0f, chainMin.y + 22.0f), kOlive, item.chain);

    TextRaw(drawList, fonts_.monoSmall, 8.2f, ImVec2(x0, panelMin.y + 264.0f), kFaint, "WHY IT IS KEPT");
    TextRaw(drawList, fonts_.body, 11.5f, ImVec2(x0, panelMin.y + 280.0f), kInk,
            index >= 2 ? "冻结原始扩展块与失败日志，确保无法判断的边界仍可回到字节级输入。"
                       : "冻结原始字节与处理日志，确保自动结论可复核、可重放、不可静默改写。");
    TextRaw(drawList, fonts_.small, 10.1f, ImVec2(x0, panelMin.y + 305.0f), kMuted,
            index == 2 ? "关联：B-02 / H-01 / E-07 decode-fail.log"
            : index == 3 ? "关联：E-06 probe-0x1E.bin / B-02 / H-01 待确认"
                         : "关联：R-17 / 步骤 01–04 / 自动结论 A-01 与 A-02");

    const char* copyRef = "复制引用";
    const char* closeText = "关闭";
    const float copyWidth = TextWidth(fonts_.small, 10.7f, copyRef) + 26.0f;
    const float closeWidth = TextWidth(fonts_.small, 10.7f, closeText) + 26.0f;
    const ImVec2 copyMin(x1 - copyWidth - closeWidth - 8.0f, panelMax.y - 50.0f);
    const ImVec2 copyMax(copyMin.x + copyWidth, panelMax.y - 18.0f);
    const ImVec2 closeMin(copyMax.x + 8.0f, panelMax.y - 50.0f);
    const ImVec2 closeMax(closeMin.x + closeWidth, panelMax.y - 18.0f);
    const bool copyHover = Hovered(copyMin, copyMax);
    const bool closeHover = Hovered(closeMin, closeMax);
    if (Hit(copyMin, copyMax, "##copy-evidence-ref")) {
        char reference[512];
        std::snprintf(reference, sizeof(reference), "%s %s sha256:%s step:%s", item.id, item.file, item.hash, item.step);
        ImGui::SetClipboardText(reference);
        std::snprintf(toast_, sizeof(toast_), "证据引用已复制");
        toastUntil_ = ImGui::GetTime() + 3.0;
    }
    if (Hit(closeMin, closeMax, "##close-evidence")) {
        evidenceModal_ = -1;
    }
    drawList->AddRectFilled(copyMin, copyMax, copyHover ? C(0x30373A) : kInk, 6.0f);
    TextRaw(drawList, fonts_.small, 10.7f, ImVec2(copyMin.x + 13.0f, copyMin.y + 7.0f), C(0xF4F5EF), copyRef);
    drawList->AddRectFilled(closeMin, closeMax, closeHover ? C(0xEDEAE2) : kPanel, 6.0f);
    drawList->AddRect(closeMin, closeMax, kLine, 6.0f, 0, 1.0f);
    TextRaw(drawList, fonts_.small, 10.7f, ImVec2(closeMin.x + 13.0f, closeMin.y + 7.0f), kMuted, closeText);
}

void UiApp::DrawFailureModal(const ImVec2& size) {
    g_BlockHits = false;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(ImVec2(0.0f, 0.0f), size, C(0x0B0D10, 162));

    const ImVec2 panelMin(size.x * 0.5f - 330.0f, size.y * 0.5f - 244.0f);
    const ImVec2 panelMax(size.x * 0.5f + 330.0f, size.y * 0.5f + 244.0f);
    drawList->AddRectFilled(ImVec2(panelMin.x + 2.0f, panelMin.y + 6.0f),
                            ImVec2(panelMax.x + 2.0f, panelMax.y + 6.0f), C(0x000000, 45), 13.0f);
    drawList->AddRectFilled(panelMin, panelMax, C(0xFDFCF8), 13.0f);
    drawList->AddRect(panelMin, panelMax, C(0xD7D2C7), 13.0f, 0, 1.0f);
    const float x0 = panelMin.x + 26.0f;
    const float x1 = panelMax.x - 26.0f;

    drawList->AddCircleFilled(ImVec2(x0 + 13.0f, panelMin.y + 28.0f), 13.0f, kRedSoft);
    drawList->AddText(fonts_.medium, 16.0f, ImVec2(x0 + 9.0f, panelMin.y + 18.0f), kRed, "!");
    TextRaw(drawList, fonts_.monoSmall, 8.8f, ImVec2(x0 + 35.0f, panelMin.y + 18.0f), kRed,
            "PROBE FAILURE  /  CAPTURE PRESERVED");
    TextRaw(drawList, fonts_.title, 20.0f, ImVec2(x0, panelMin.y + 48.0f), kInk, "边界探针失败");
    Badge(drawList, fonts_.small, ImVec2(x1 - 60.0f, panelMin.y + 47.0f), "exit 3", kRedSoft, kRed);
    TextRaw(drawList, fonts_.body, 11.8f, ImVec2(x0, panelMin.y + 79.0f), kMuted,
            "T-03 无法区分 nearest / center。原始 E-06 与 decode-fail.log 未被覆盖。");
    Rule(drawList, x0, x1, panelMin.y + 105.0f);

    struct RecoveryStep {
        const char* index;
        const char* title;
        const char* command;
        const char* result;
    };
    const RecoveryStep steps[] = {
        {"01", "冻结输入，防止哈希漂移", "repair --freeze-input", "hash locked"},
        {"02", "切换中心采样重跑", "replay --sample-mode=center", "R-18"},
        {"03", "导出探针与驱动信息", "export probe-0x1E.bin --include-driver", "evidence/e-06-retry.bin"},
    };
    for (int i = 0; i < 3; ++i) {
        const float y = panelMin.y + 121.0f + static_cast<float>(i) * 56.0f;
        drawList->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + 28.0f, y + 28.0f), kAmberSoft, 6.0f);
        TextRaw(drawList, fonts_.monoSmall, 9.0f, ImVec2(x0 + 8.0f, y + 9.0f), kAmberInk, steps[i].index);
        TextRaw(drawList, fonts_.body, 11.8f, ImVec2(x0 + 40.0f, y - 1.0f), kInk, steps[i].title);
        TextRaw(drawList, fonts_.mono, 9.4f, ImVec2(x0 + 40.0f, y + 17.0f), kMuted, steps[i].command);
        TextRight(drawList, fonts_.monoSmall, 8.2f, x1, y + 3.0f, kOlive, steps[i].result);
    }
    TextRaw(drawList, fonts_.small, 9.8f, ImVec2(x0, panelMin.y + 298.0f), kMuted,
            "先执行 01；若 R-18 仍失败，保留输入哈希并把 02–03 的输出附到复核请求。");

    const char* copyText = "复制后续命令";
    const char* closeText = "关闭";
    const float copyWidth = TextWidth(fonts_.small, 10.7f, copyText) + 26.0f;
    const float closeWidth = TextWidth(fonts_.small, 10.7f, closeText) + 26.0f;
    const ImVec2 copyMin(x1 - copyWidth - closeWidth - 8.0f, panelMax.y - 51.0f);
    const ImVec2 copyMax(copyMin.x + copyWidth, panelMax.y - 19.0f);
    const ImVec2 closeMin(copyMax.x + 8.0f, panelMax.y - 51.0f);
    const ImVec2 closeMax(closeMin.x + closeWidth, panelMax.y - 19.0f);
    const bool copyHover = Hovered(copyMin, copyMax);
    const bool closeHover = Hovered(closeMin, closeMax);
    if (Hit(copyMin, copyMax, "##copy-failure-actions")) {
        ImGui::SetClipboardText(
            "repair --freeze-input && replay --sample-mode=center\n"
            "export probe-0x1E.bin --include-driver\n");
        std::snprintf(toast_, sizeof(toast_), "后续命令已复制；原始证据保持冻结");
        toastUntil_ = ImGui::GetTime() + 3.0;
    }
    if (Hit(closeMin, closeMax, "##close-failure")) {
        failureModal_ = false;
    }
    drawList->AddRectFilled(copyMin, copyMax, copyHover ? C(0xC28A38) : C(0xAE7627), 6.0f);
    TextRaw(drawList, fonts_.small, 10.7f, ImVec2(copyMin.x + 13.0f, copyMin.y + 7.0f), C(0xFFF8EB), copyText);
    drawList->AddRectFilled(closeMin, closeMax, closeHover ? C(0xEDEAE2) : kPanel, 6.0f);
    drawList->AddRect(closeMin, closeMax, kLine, 6.0f, 0, 1.0f);
    TextRaw(drawList, fonts_.small, 10.7f, ImVec2(closeMin.x + 13.0f, closeMin.y + 7.0f), kMuted, closeText);
}

void UiApp::DrawToast(const ImVec2& size) {
    if (toastUntil_ <= ImGui::GetTime()) {
        return;
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float width = std::min(460.0f, TextWidth(fonts_.body, 11.8f, toast_) + 48.0f);
    const ImVec2 min(size.x * 0.5f - width * 0.5f, size.y - 56.0f);
    const ImVec2 max(min.x + width, min.y + 36.0f);
    drawList->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 3.0f), ImVec2(max.x + 1.0f, max.y + 3.0f),
                            C(0x000000, 30), 8.0f);
    drawList->AddRectFilled(min, max, C(0x1A2023), 8.0f);
    drawList->AddRectFilled(ImVec2(min.x, min.y + 8.0f), ImVec2(min.x + 2.0f, max.y - 8.0f), kLime, 1.0f);
    TextRaw(drawList, fonts_.body, 11.8f, ImVec2(min.x + 20.0f, min.y + 9.0f), C(0xF0F2EC), toast_);
}

} // namespace aa
