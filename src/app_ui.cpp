#include "app_ui.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace surfprov {
namespace {

constexpr ImU32 kBg = IM_COL32(7, 11, 15, 255);
constexpr ImU32 kPanel = IM_COL32(13, 19, 25, 255);
constexpr ImU32 kPanelRaised = IM_COL32(17, 25, 32, 255);
constexpr ImU32 kBorder = IM_COL32(34, 46, 56, 255);
constexpr ImU32 kBorderSoft = IM_COL32(27, 37, 46, 255);
constexpr ImU32 kText = IM_COL32(221, 231, 237, 255);
constexpr ImU32 kTextDim = IM_COL32(123, 139, 151, 255);
constexpr ImU32 kTextFaint = IM_COL32(76, 92, 104, 255);
constexpr ImU32 kCyan = IM_COL32(72, 214, 232, 255);
constexpr ImU32 kCyanSoft = IM_COL32(31, 83, 94, 255);
constexpr ImU32 kGreen = IM_COL32(78, 215, 143, 255);
constexpr ImU32 kGreenSoft = IM_COL32(27, 74, 52, 255);
constexpr ImU32 kAmber = IM_COL32(244, 178, 74, 255);
constexpr ImU32 kAmberSoft = IM_COL32(85, 60, 25, 255);
constexpr ImU32 kRed = IM_COL32(246, 104, 104, 255);
constexpr ImU32 kRedSoft = IM_COL32(82, 32, 35, 255);

struct Fonts {
    ImFont* body = nullptr;
    ImFont* heading = nullptr;
    ImFont* small = nullptr;
    ImFont* mono = nullptr;
};

void drawLeftPanel(const Fonts& fonts, UiContext& context, ImVec2 position,
                   ImVec2 size);
void drawCenterPanel(const Fonts& fonts, UiContext& context, ImVec2 position,
                     ImVec2 size);
void drawRightPanel(const Fonts& fonts, UiContext& context, ImVec2 position,
                    ImVec2 size);
void drawPreview(ImDrawList* draw, const Fonts& fonts, const Record& record,
                 std::uint64_t texture, UiContext& context, ImVec2 position,
                 ImVec2 size);
void drawMetaField(ImDrawList* draw, const Fonts& fonts, ImVec2 position,
                   const std::string& label, const std::string& value,
                   ImU32 color = kText);
void drawWrapped(ImDrawList* draw, ImFont* font, float size,
                 ImVec2 position, ImU32 color, const std::string& text,
                 float maxWidth, int maxLines, float lineHeight);

Fonts uiFonts() {
    Fonts fonts;
    auto& list = ImGui::GetIO().Fonts->Fonts;
    if (!list.empty()) fonts.body = list[0];
    if (list.Size > 1) fonts.heading = list[1];
    if (list.Size > 2) fonts.small = list[2];
    if (list.Size > 3) fonts.mono = list[3];
    if (fonts.body == nullptr) fonts.body = ImGui::GetFont();
    if (fonts.heading == nullptr) fonts.heading = fonts.body;
    if (fonts.small == nullptr) fonts.small = fonts.body;
    if (fonts.mono == nullptr) fonts.mono = fonts.body;
    return fonts;
}

ImVec2 textSize(ImFont* font, float size, const std::string& text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
}

void addText(ImDrawList* draw,
             ImFont* font,
             float size,
             ImVec2 position,
             ImU32 color,
             const std::string& text) {
    draw->AddText(font, size, position, color, text.c_str());
}

ImU32 statusColor(RecordStatus status) {
    if (status == RecordStatus::Pass) return kGreen;
    if (status == RecordStatus::Undecidable) return kAmber;
    return kRed;
}

ImU32 statusBackground(RecordStatus status) {
    if (status == RecordStatus::Pass) return kGreenSoft;
    if (status == RecordStatus::Undecidable) return kAmberSoft;
    return kRedSoft;
}

std::string statusText(RecordStatus status) {
    if (status == RecordStatus::Pass) return "PASS";
    if (status == RecordStatus::Undecidable) return "无法判断";
    return "隔离";
}

std::string formatBytes(std::uint64_t value) {
    std::string text = std::to_string(value);
    for (int index = static_cast<int>(text.size()) - 3; index > 0; index -= 3) {
        text.insert(static_cast<std::size_t>(index), ",");
    }
    return text + " B";
}

std::string shortHash(const std::string& hash) {
    if (hash.size() <= 20) return hash;
    return hash.substr(0, 10) + "…" + hash.substr(hash.size() - 6);
}

float chip(ImDrawList* draw,
           ImFont* font,
           ImVec2 position,
           const std::string& label,
           ImU32 foreground,
           ImU32 background,
           float height = 23.0f) {
    const ImVec2 size = textSize(font, 13.0f, label);
    const float width = size.x + 18.0f;
    draw->AddRectFilled(position, {position.x + width, position.y + height},
                        background, height * 0.5f);
    addText(draw, font, 13.0f,
            {position.x + 9.0f, position.y + (height - size.y) * 0.5f - 1.0f},
            foreground, label);
    return width;
}

void panel(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 fill = kPanel,
           ImU32 border = kBorderSoft) {
    draw->AddRectFilled(min, max, fill, 12.0f);
    draw->AddRect(min, max, border, 12.0f, 0, 1.0f);
}

bool beginPanel(const char* id, ImVec2 position, ImVec2 size,
                float padding = 15.0f) {
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(kPanel));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(kBorderSoft));
    return ImGui::BeginChild(
        id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void endPanel() {
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void drawTopBar(ImDrawList* draw,
                const Fonts& fonts,
                ImVec2 size,
                UiContext& context,
                UiAction& action,
                double now) {
    draw->AddRectFilledMultiColor(
        {0, 0}, {size.x, 68}, IM_COL32(10, 16, 22, 255),
        IM_COL32(12, 21, 27, 255), IM_COL32(8, 13, 18, 255),
        IM_COL32(8, 12, 17, 255));
    draw->AddLine({0, 68}, {size.x, 68}, kBorder, 1.0f);

    const ImVec2 logoMin(20, 17);
    draw->AddRectFilled(logoMin, {logoMin.x + 34, logoMin.y + 34},
                        IM_COL32(21, 46, 55, 255), 8.0f);
    draw->AddRect({logoMin.x + 0.5f, logoMin.y + 0.5f},
                  {logoMin.x + 33.5f, logoMin.y + 33.5f}, kCyanSoft, 8.0f, 0, 1.0f);
    draw->AddLine({logoMin.x + 8, logoMin.y + 25}, {logoMin.x + 16, logoMin.y + 9},
                  kCyan, 1.6f);
    draw->AddLine({logoMin.x + 16, logoMin.y + 9}, {logoMin.x + 26, logoMin.y + 19},
                  kCyan, 1.6f);
    draw->AddLine({logoMin.x + 26, logoMin.y + 19}, {logoMin.x + 18, logoMin.y + 27},
                  kAmber, 1.6f);
    draw->AddLine({logoMin.x + 18, logoMin.y + 27}, {logoMin.x + 8, logoMin.y + 25},
                  kAmber, 1.6f);

    addText(draw, fonts.heading, 21.0f, {68, 10}, kText, "曲面参数化展开");
    addText(draw, fonts.small, 12.3f, {69, 39}, kTextDim,
            "SURFACE UNFOLD PROVENANCE  /  SUF1 → SUF2  /  RIBBED SHEET");

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 7));
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kPanelRaised));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImGui::ColorConvertU32ToFloat4(IM_COL32(27, 42, 50, 255)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImGui::ColorConvertU32ToFloat4(kCyanSoft));
    ImGui::SetCursorScreenPos({size.x - 118, 19});
    if (ImGui::Button("重新验证", {96, 31})) action = UiAction::Revalidate;
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);

    const float pillWidth = 284.0f;
    const ImVec2 pillMin(size.x - 20 - pillWidth - 128, 17);
    draw->AddRectFilled(pillMin, {pillMin.x + pillWidth, pillMin.y + 35},
                        IM_COL32(17, 35, 30, 255), 17.5f);
    draw->AddCircleFilled({pillMin.x + 16, pillMin.y + 17.5f}, 4.0f,
                          context.batch.tempCleaned ? kGreen : kRed);
    addText(draw, fonts.small, 12.2f, {pillMin.x + 28, pillMin.y + 9},
            context.batch.tempCleaned ? kGreen : kRed,
            "3 OBJECTS · 2 SNAPSHOTS · TEMP CLEANED");

    if (now < context.noticeUntil && !context.notice.empty()) {
        const ImVec2 noticeSize = textSize(fonts.small, 12.5f, context.notice);
        const float width = noticeSize.x + 30.0f;
        const ImVec2 noticeMin(size.x - 20 - width, 82);
        draw->AddRectFilled(noticeMin, {noticeMin.x + width, noticeMin.y + 29},
                            IM_COL32(22, 38, 44, 245), 8.0f);
        draw->AddRect(noticeMin, {noticeMin.x + width, noticeMin.y + 29},
                      kCyanSoft, 8.0f);
        addText(draw, fonts.small, 12.5f, {noticeMin.x + 15, noticeMin.y + 8},
                kText, context.notice);
    }
}

void drawMetricCard(ImDrawList* draw,
                    const Fonts& fonts,
                    ImVec2 position,
                    ImVec2 size,
                    const std::string& label,
                    const std::string& value,
                    const std::string& detail,
                    ImU32 accent) {
    panel(draw, position, {position.x + size.x, position.y + size.y});
    draw->AddRectFilled(position, {position.x + 3, position.y + size.y},
                        accent, 12.0f, ImDrawFlags_RoundCornersLeft);
    addText(draw, fonts.small, 12.0f, {position.x + 18, position.y + 12},
            kTextDim, label);
    addText(draw, fonts.heading, 26.0f, {position.x + 17, position.y + 27},
            accent, value);
    addText(draw, fonts.small, 11.0f, {position.x + 18, position.y + 58},
            kTextFaint, detail);
}

}  // namespace
}  // namespace surfprov

namespace surfprov {

UiAction drawUi(UiContext& context, double nowSeconds) {
    if (context.batch.records.empty()) return UiAction::Quit;
    context.selected = std::clamp(
        context.selected, 0, static_cast<int>(context.batch.records.size()) - 1);
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##surface-unfold-provenance-root", nullptr, flags);
    ImGui::PopStyleVar();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled({0, 0}, io.DisplaySize, kBg);
    const Fonts fonts = uiFonts();

    UiAction action = UiAction::None;
    drawTopBar(draw, fonts, io.DisplaySize, context, action, nowSeconds);

    const float margin = 18.0f;
    const float gap = 12.0f;
    const float cardWidth =
        (io.DisplaySize.x - margin * 2.0f - gap * 3.0f) / 4.0f;
    drawMetricCard(draw, fonts, {margin, 82}, {cardWidth, 78}, "版本快照",
                   std::to_string(context.batch.validSnapshotCount) + " / " +
                       std::to_string(context.batch.snapshotCount),
                   "SUF1 + SUF2 均可解析", kCyan);
    drawMetricCard(draw, fonts, {margin + (cardWidth + gap), 82},
                   {cardWidth, 78}, "未知扩展",
                   std::to_string(context.batch.failedCount), ".uvx 已隔离 · 异常后继续",
                   kRed);
    drawMetricCard(draw, fonts, {margin + (cardWidth + gap) * 2.0f, 82},
                   {cardWidth, 78}, "无法判断",
                   std::to_string(context.batch.boundaryCount),
                   "B-03 · 双候选均在容差内", kAmber);
    drawMetricCard(draw, fonts, {margin + (cardWidth + gap) * 3.0f, 82},
                   {cardWidth, 78}, "临时状态",
                   context.batch.tempCleaned ? "CLEAN" : "DIRTY",
                   context.batch.tempCleaned ? ".tmp 已清理 · 输入可回放"
                                             : "清理未完成",
                   context.batch.tempCleaned ? kGreen : kRed);

    const float mainTop = 174.0f;
    const float mainHeight = io.DisplaySize.y - mainTop - 18.0f;
    const float leftWidth = std::clamp(io.DisplaySize.x * 0.245f, 282.0f, 336.0f);
    const float rightWidth = std::clamp(io.DisplaySize.x * 0.278f, 322.0f, 386.0f);
    const float centerWidth =
        io.DisplaySize.x - margin * 2.0f - leftWidth - rightWidth - gap * 2.0f;
    drawLeftPanel(fonts, context, {margin, mainTop}, {leftWidth, mainHeight});
    drawCenterPanel(fonts, context,
                    {margin + leftWidth + gap, mainTop},
                    {centerWidth, mainHeight});
    drawRightPanel(fonts, context,
                   {margin + leftWidth + centerWidth + gap * 2.0f, mainTop},
                   {rightWidth, mainHeight});

    if (!io.WantTextInput) {
        const int count = static_cast<int>(context.batch.records.size());
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            context.selected = (context.selected + 1) % count;
            context.selectedStep = -1;
            context.rawMode = context.batch.records[
                static_cast<std::size_t>(context.selected)].kind ==
                RecordKind::UnknownExtension;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            context.selected = (context.selected + count - 1) % count;
            context.selectedStep = -1;
            context.rawMode = context.batch.records[
                static_cast<std::size_t>(context.selected)].kind ==
                RecordKind::UnknownExtension;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
            context.rawMode = !context.rawMode;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R)) action = UiAction::Revalidate;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) action = UiAction::Quit;
    }
    ImGui::End();
    return action;
}

}  // namespace surfprov

namespace surfprov {
namespace {

void drawRightPanel(const Fonts& fonts,
                    UiContext& context,
                    ImVec2 position,
                    ImVec2 size) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const Record& record =
        context.batch.records[static_cast<std::size_t>(context.selected)];
    if (!beginPanel("evidence-chain", position, size, 15.0f)) {
        endPanel();
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const int activeStep = context.selectedStep >= 0
        ? context.selectedStep
        : (record.boundary.present && !record.steps.empty()
               ? static_cast<int>(record.steps.size()) - 1
               : -1);
    addText(draw, fonts.body, 17.0f, origin, kText, "证据链");
    const std::string evidenceCount = std::to_string(record.evidence.size());
    chip(draw, fonts.small, {origin.x + size.x - 55, origin.y - 2},
         evidenceCount, kCyan, kCyanSoft, 22.0f);
    addText(draw, fonts.small, 10.3f, {origin.x, origin.y + 27}, kTextFaint,
            "INPUT → STEP → CONCLUSION");
    draw->AddLine({origin.x, origin.y + 48}, {origin.x + size.x - 30, origin.y + 48},
                  kBorderSoft, 1.0f);

    const float stepTop = origin.y + 58.0f;
    const float stepHeight = 43.0f;
    const float railX = origin.x + 9.0f;
    if (!record.steps.empty()) {
        draw->AddLine({railX, stepTop + 10},
                      {railX, stepTop + stepHeight *
                                      static_cast<float>(record.steps.size()) -
                                  22},
                      IM_COL32(43, 59, 68, 255), 1.0f);
    }
    for (std::size_t index = 0; index < record.steps.size(); ++index) {
        const Step& step = record.steps[index];
        const ImVec2 rowMin(origin.x,
                            stepTop + static_cast<float>(index) * stepHeight);
        const ImVec2 rowMax(origin.x + size.x - 30.0f,
                            rowMin.y + stepHeight - 3.0f);
        const bool selected = activeStep == static_cast<int>(index);
        ImGui::SetCursorScreenPos(rowMin);
        ImGui::PushID(static_cast<int>(index) + 2000);
        ImGui::InvisibleButton("step", {rowMax.x - rowMin.x,
                                        rowMax.y - rowMin.y});
        if (ImGui::IsItemClicked()) context.selectedStep = static_cast<int>(index);
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (selected || hovered) {
            draw->AddRectFilled(rowMin, rowMax,
                                selected ? IM_COL32(20, 36, 42, 255)
                                         : IM_COL32(18, 28, 34, 255),
                                6.0f);
        }
        ImU32 state = kGreen;
        if (step.state == "warn") state = kAmber;
        if (step.state == "error") state = kRed;
        if (step.state == "undecidable") state = kAmber;
        draw->AddCircleFilled({railX, rowMin.y + 15}, 4.0f, state);
        if (step.state == "undecidable") {
            draw->AddCircle({railX, rowMin.y + 15}, 7.0f,
                            IM_COL32(244, 178, 74, 90), 20, 1.0f);
        }
        addText(draw, fonts.mono, 9.2f, {origin.x + 23, rowMin.y + 5},
                kTextFaint, step.id);
        addText(draw, fonts.small, 11.5f, {origin.x + 51, rowMin.y + 4},
                kText, step.title);
        float chipX = origin.x + 23.0f;
        for (const auto& id : step.evidenceIds) {
            const float width = textSize(fonts.mono, 8.7f, id).x + 7.0f;
            if (chipX + width > rowMax.x - 8) break;
            draw->AddRectFilled({chipX, rowMin.y + 23},
                                {chipX + width, rowMin.y + 35},
                                IM_COL32(16, 30, 36, 255), 3.0f);
            addText(draw, fonts.mono, 8.7f, {chipX + 3.5f, rowMin.y + 26},
                    selected ? kCyan : kTextDim, id);
            chipX += width + 3.0f;
        }
    }

    const float detailTop =
        stepTop + stepHeight * static_cast<float>(record.steps.size()) + 7.0f;
    const float detailHeight = 101.0f;
    draw->AddRectFilled({origin.x, detailTop},
                        {origin.x + size.x - 30, detailTop + detailHeight},
                        IM_COL32(10, 16, 21, 255), 8.0f);
    draw->AddRect({origin.x, detailTop},
                  {origin.x + size.x - 30, detailTop + detailHeight},
                  kBorderSoft, 8.0f);
    if (activeStep >= 0 && activeStep < static_cast<int>(record.steps.size())) {
        const Step& step = record.steps[static_cast<std::size_t>(activeStep)];
        addText(draw, fonts.small, 8.9f, {origin.x + 11, detailTop + 8},
                kTextFaint, step.id + "  /  STEP DETAIL");
        drawWrapped(draw, fonts.small, 10.2f, {origin.x + 11, detailTop + 23},
                    kTextDim, step.detail, size.x - 54.0f, 4, 15.0f);
    } else {
        addText(draw, fonts.small, 8.9f, {origin.x + 11, detailTop + 8},
                kTextFaint, "TRACE RULE");
        addText(draw, fonts.small, 10.2f, {origin.x + 11, detailTop + 24},
                kTextDim, "每条结论只引用本对象的原始输入、");
        addText(draw, fonts.small, 10.2f, {origin.x + 11, detailTop + 40},
                kTextDim, "Sxx 处理步骤与 Exx 字节证据。");
        addText(draw, fonts.small, 10.2f, {origin.x + 11, detailTop + 60},
                kTextFaint, "点击步骤查看偏移、哈希与判定理由。");
    }

    const float footerTop = detailTop + detailHeight + 12.0f;
    draw->AddLine({origin.x, footerTop - 5},
                  {origin.x + size.x - 30, footerTop - 5}, kBorderSoft, 1.0f);
    draw->AddCircleFilled({origin.x + 5, footerTop + 12}, 3.5f,
                          context.batch.tempCleaned ? kGreen : kRed);
    addText(draw, fonts.small, 9.8f, {origin.x + 15, footerTop + 5},
            context.batch.tempCleaned ? kGreen : kRed,
            context.batch.tempCleaned ? "SESSION END  ·  TEMP CLEANED"
                                      : "SESSION END  ·  TEMP DIRTY");
    addText(draw, fonts.mono, 8.7f, {origin.x, footerTop + 24}, kTextFaint,
            ".tmp/unfold-scan-" + context.batch.runId.substr(7, 10));
    addText(draw, fonts.small, 9.0f, {origin.x, footerTop + 41}, kTextFaint,
            "input manifest  " + shortHash(context.batch.inputManifestSha256));
    if (record.boundary.present) {
        addText(draw, fonts.mono, 9.0f, {origin.x, footerTop + 59}, kAmber,
                "E06  0.002 tolerance  /  16 B raw");
    }
    endPanel();
}

std::vector<std::string> utf8Tokens(const std::string& text) {
    std::vector<std::string> tokens;
    for (std::size_t index = 0; index < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        std::size_t length = 1;
        if ((first & 0x80u) == 0) {
            length = 1;
        } else if ((first & 0xe0u) == 0xc0u) {
            length = 2;
        } else if ((first & 0xf0u) == 0xe0u) {
            length = 3;
        } else if ((first & 0xf8u) == 0xf0u) {
            length = 4;
        }
        length = std::min(length, text.size() - index);
        tokens.push_back(text.substr(index, length));
        index += length;
    }
    return tokens;
}

void drawWrapped(ImDrawList* draw,
                 ImFont* font,
                 float size,
                 ImVec2 position,
                 ImU32 color,
                 const std::string& text,
                 float maxWidth,
                 int maxLines,
                 float lineHeight = 18.0f) {
    const auto tokens = utf8Tokens(text);
    std::string line;
    int lineIndex = 0;
    for (const auto& token : tokens) {
        if (token == "\n") {
            addText(draw, font, size,
                    {position.x,
                     position.y + static_cast<float>(lineIndex) * lineHeight},
                    color, line);
            line.clear();
            ++lineIndex;
            if (lineIndex >= maxLines) return;
            continue;
        }
        const std::string candidate = line + token;
        if (textSize(font, size, candidate).x > maxWidth && !line.empty()) {
            addText(draw, font, size,
                    {position.x,
                     position.y + static_cast<float>(lineIndex) * lineHeight},
                    color, line);
            line = token;
            ++lineIndex;
            if (lineIndex >= maxLines) return;
        } else {
            line = candidate;
        }
    }
    if (!line.empty() && lineIndex < maxLines) {
        addText(draw, font, size,
                {position.x,
                 position.y + static_cast<float>(lineIndex) * lineHeight},
                color, line);
    }
}

}  // namespace
}  // namespace surfprov

namespace surfprov {
namespace {

void drawCenterPanel(const Fonts& fonts,
                     UiContext& context,
                     ImVec2 position,
                     ImVec2 size) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const Record& record =
        context.batch.records[static_cast<std::size_t>(context.selected)];
    if (!beginPanel("record-detail", position, size, 16.0f)) {
        endPanel();
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float contentWidth = size.x - 32.0f;
    addText(draw, fonts.heading, 19.0f, origin, kText, record.name);
    addText(draw, fonts.mono, 10.4f, {origin.x, origin.y + 26}, kTextFaint,
            record.sourcePath + "  /  " + record.sourceRevision);
    const ImU32 accent = statusColor(record.status);
    const float statusWidth =
        textSize(fonts.small, 12.0f, statusText(record.status)).x + 18.0f;
    chip(draw, fonts.small, {origin.x + contentWidth - statusWidth, origin.y + 2},
         statusText(record.status), accent, statusBackground(record.status),
         23.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Button,
                          context.rawMode
                              ? ImGui::ColorConvertU32ToFloat4(kCyanSoft)
                              : ImGui::ColorConvertU32ToFloat4(kPanelRaised));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImGui::ColorConvertU32ToFloat4(
                              IM_COL32(30, 49, 58, 255)));
    ImGui::SetCursorScreenPos({origin.x + contentWidth - 186, origin.y + 31});
    if (ImGui::Button("原始证据", {78, 27})) context.rawMode = true;
    ImGui::SameLine();
    if (ImGui::Button("展开预览", {78, 27})) context.rawMode = false;
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    const float previewTop = origin.y + 66.0f;
    const float previewHeight = 218.0f;
    drawPreview(draw, fonts, record,
                context.previews[static_cast<std::size_t>(context.selected)],
                context, {origin.x, previewTop},
                {contentWidth, previewHeight});

    const float metadataTop = previewTop + previewHeight + 12.0f;
    const float metadataHeight = 78.0f;
    draw->AddRectFilled({origin.x, metadataTop},
                        {origin.x + contentWidth, metadataTop + metadataHeight},
                        IM_COL32(10, 16, 21, 255), 9.0f);
    draw->AddRect({origin.x, metadataTop},
                  {origin.x + contentWidth, metadataTop + metadataHeight},
                  kBorderSoft, 9.0f);
    const float columnWidth = (contentWidth - 30.0f) / 3.0f;
    const std::string format =
        record.header.magicOk ? record.header.magic : "UNKNOWN";
    drawMetaField(draw, fonts, {origin.x + 13, metadataTop + 10}, "FORMAT",
                  format + "  v" + std::to_string(record.header.version), kCyan);
    drawMetaField(draw, fonts, {origin.x + 13 + columnWidth, metadataTop + 10},
                  "GEOMETRY",
                  std::to_string(record.vertices.size()) + " V / " +
                      std::to_string(record.triangles.size()) + " T");
    drawMetaField(draw, fonts,
                  {origin.x + 13 + columnWidth * 2.0f, metadataTop + 10},
                  "SEAMS / EXTENSION",
                  std::to_string(record.header.seamCount) + " / ." +
                      record.extension,
                  record.header.seamCount > 0 ? kAmber : kText);
    draw->AddLine({origin.x + 13, metadataTop + 43},
                  {origin.x + contentWidth - 13, metadataTop + 43},
                  kBorderSoft, 1.0f);
    drawMetaField(draw, fonts, {origin.x + 13, metadataTop + 52}, "INPUT BYTES",
                  formatBytes(record.fileSize));
    drawMetaField(draw, fonts,
                  {origin.x + 13 + columnWidth, metadataTop + 52},
                  "INPUT SHA-256", shortHash(record.sha256));
    drawMetaField(draw, fonts,
                  {origin.x + 13 + columnWidth * 2.0f, metadataTop + 52},
                  "CHANGED PIXELS",
                  record.changedPixels >= 0
                      ? std::to_string(record.changedPixels)
                      : "baseline",
                  record.changedPixels > 0 ? kAmber : kTextDim);

    float conclusionTop = metadataTop + metadataHeight + 12.0f;
    if (record.boundary.present) {
        const float boundaryHeight = 92.0f;
        draw->AddRectFilled({origin.x, conclusionTop},
                            {origin.x + contentWidth,
                             conclusionTop + boundaryHeight},
                            IM_COL32(42, 31, 16, 255), 9.0f);
        draw->AddRect({origin.x, conclusionTop},
                      {origin.x + contentWidth,
                       conclusionTop + boundaryHeight},
                      IM_COL32(104, 72, 26, 255), 9.0f, 0, 1.2f);
        addText(draw, fonts.small, 9.6f, {origin.x + 13, conclusionTop + 8},
                kAmber, "UNDECIDABLE BOUNDARY  /  " + record.boundary.id);
        addText(draw, fonts.body, 13.5f, {origin.x + 13, conclusionTop + 24},
                kText, record.boundary.headline);
        addText(draw, fonts.mono, 10.0f, {origin.x + 13, conclusionTop + 47},
                kTextDim,
                "orientation=" + std::to_string(record.boundary.orientationField) +
                    "   tolerance=" + std::to_string(record.boundary.tolerance));
        addText(draw, fonts.mono, 10.0f,
                {origin.x + 13 + contentWidth * 0.44f, conclusionTop + 47},
                kAmber,
                "forward " + std::to_string(record.boundary.candidateForward) +
                    "   reverse " +
                    std::to_string(record.boundary.candidateReverse));
        addText(draw, fonts.small, 10.2f, {origin.x + 13, conclusionTop + 67},
                kTextFaint,
                "两个候选都在容差内且符号相反 → 不选择单值方向");
        conclusionTop += boundaryHeight + 10.0f;
    }

    const float conclusionHeight = origin.y + size.y - 31.0f - conclusionTop;
    draw->AddRectFilled({origin.x, conclusionTop},
                        {origin.x + contentWidth, conclusionTop + conclusionHeight},
                        IM_COL32(11, 18, 22, 255), 10.0f);
    draw->AddRect({origin.x, conclusionTop},
                  {origin.x + contentWidth, conclusionTop + conclusionHeight},
                  accent, 10.0f, 0, 1.2f);
    draw->AddRectFilled({origin.x, conclusionTop},
                        {origin.x + 3, conclusionTop + conclusionHeight},
                        accent, 10.0f, ImDrawFlags_RoundCornersLeft);
    addText(draw, fonts.small, 9.4f, {origin.x + 14, conclusionTop + 10},
            accent, "结论  /  TRACEABLE CONCLUSION");
    addText(draw, fonts.heading, 15.0f, {origin.x + 14, conclusionTop + 27},
            kText, record.conclusion.headline);
    drawWrapped(draw, fonts.small, 11.0f, {origin.x + 14, conclusionTop + 52},
                kTextDim, record.conclusion.detail, contentWidth - 28.0f,
                std::max(2, static_cast<int>((conclusionHeight - 93.0f) / 17.0f)),
                17.0f);

    float refX = origin.x + 14.0f;
    const float refY = conclusionTop + conclusionHeight - 27.0f;
    addText(draw, fonts.small, 8.8f, {refX, refY + 5}, kTextFaint, "STEPS");
    refX += 38.0f;
    for (const auto& id : record.conclusion.stepIds) {
        const float width = textSize(fonts.mono, 9.3f, id).x + 9.0f;
        draw->AddRectFilled({refX, refY}, {refX + width, refY + 17},
                            IM_COL32(21, 33, 40, 255), 4.0f);
        addText(draw, fonts.mono, 9.3f, {refX + 4.5f, refY + 4}, kTextDim, id);
        refX += width + 3.0f;
    }
    addText(draw, fonts.small, 8.8f, {refX + 6, refY + 5}, kTextFaint,
            "EVIDENCE");
    refX += 56.0f;
    for (const auto& id : record.conclusion.evidenceIds) {
        const float width = textSize(fonts.mono, 9.3f, id).x + 9.0f;
        draw->AddRectFilled({refX, refY}, {refX + width, refY + 17},
                            IM_COL32(17, 39, 43, 255), 4.0f);
        addText(draw, fonts.mono, 9.3f, {refX + 4.5f, refY + 4}, kCyan, id);
        refX += width + 3.0f;
    }
    endPanel();
}

}  // namespace
}  // namespace surfprov

namespace surfprov {
namespace {

const Evidence* selectedEvidence(const Record& record, int selectedStep) {
    if (selectedStep >= 0 && selectedStep < static_cast<int>(record.steps.size())) {
        for (const auto& id : record.steps[selectedStep].evidenceIds) {
            for (const auto& evidence : record.evidence) {
                if (evidence.id == id) return &evidence;
            }
        }
    }
    if (record.boundary.present) {
        for (auto iterator = record.evidence.rbegin();
             iterator != record.evidence.rend(); ++iterator) {
            if (iterator->id == "E06") return &*iterator;
        }
    }
    return record.evidence.empty() ? nullptr : &record.evidence.front();
}

std::string hexLine(const Evidence& evidence, std::size_t offset,
                    std::size_t count, std::string& ascii) {
    std::string hex;
    ascii.clear();
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t source = offset + index;
        if (source >= evidence.excerpt.size()) {
            hex += "   ";
            ascii += ' ';
            continue;
        }
        char part[4];
        std::snprintf(part, sizeof(part), "%02x ",
                      static_cast<unsigned>(evidence.excerpt[source]));
        hex += part;
        const unsigned char value = evidence.excerpt[source];
        ascii += value >= 32 && value < 127 ? static_cast<char>(value) : '.';
    }
    return hex;
}

void drawPreview(ImDrawList* draw,
                 const Fonts& fonts,
                 const Record& record,
                 std::uint64_t texture,
                 UiContext& context,
                 ImVec2 position,
                 ImVec2 size) {
    panel(draw, position, {position.x + size.x, position.y + size.y},
          IM_COL32(8, 13, 17, 255), kBorderSoft);
    if (context.rawMode || record.preview.rgba.empty()) {
        const Evidence* evidence = selectedEvidence(record, context.selectedStep);
        addText(draw, fonts.small, 10.4f, {position.x + 13, position.y + 10},
                kTextFaint, "RAW EVIDENCE  /  ORIGINAL BYTES");
        if (evidence != nullptr) {
            const std::string range =
                evidence->id + " · offset " + std::to_string(evidence->offset) +
                " · " + formatBytes(evidence->length);
            addText(draw, fonts.mono, 11.0f, {position.x + 13, position.y + 27},
                    kCyan, range);
            addText(draw, fonts.mono, 9.8f, {position.x + 13, position.y + 44},
                    kTextFaint, "sha256 " + shortHash(evidence->sha256));
            const std::size_t maxBytes =
                std::min<std::size_t>(evidence->excerpt.size(), 192);
            const float reserved =
                record.kind == RecordKind::UnknownExtension ? 76.0f : 12.0f;
            const int maxRows = std::max(
                1, static_cast<int>((size.y - 68.0f - reserved) / 16.0f));
            for (std::size_t row = 0;
                 row * 16 < maxBytes && row < static_cast<std::size_t>(maxRows);
                 ++row) {
                char offset[20];
                std::snprintf(offset, sizeof(offset), "%08zx:",
                              static_cast<std::size_t>(evidence->offset) + row * 16);
                std::string ascii;
                const std::string hex = hexLine(*evidence, row * 16, 16, ascii);
                const float y = position.y + 68 + static_cast<float>(row) * 16.0f;
                addText(draw, fonts.mono, 10.2f, {position.x + 13, y},
                        kTextFaint, offset);
                addText(draw, fonts.mono, 10.2f, {position.x + 82, y},
                        kTextDim, hex);
                addText(draw, fonts.mono, 10.2f, {position.x + 420, y},
                        kTextFaint, ascii);
            }
        }
        if (record.kind == RecordKind::UnknownExtension) {
            const float calloutTop = position.y + size.y - 64.0f;
            draw->AddRectFilled({position.x + 10, calloutTop},
                                {position.x + size.x - 10, position.y + size.y - 9},
                                IM_COL32(55, 25, 28, 238), 7.0f);
            draw->AddRect({position.x + 10, calloutTop},
                          {position.x + size.x - 10, position.y + size.y - 9},
                          IM_COL32(112, 48, 52, 255), 7.0f);
            addText(draw, fonts.small, 10.3f, {position.x + 20, calloutTop + 8},
                    kRed, "UNKNOWN EXTENSION  ·  magic 55 56 58 00  ·  .uvx");
            addText(draw, fonts.small, 10.3f, {position.x + 20, calloutTop + 26},
                    kTextDim, "未从扩展名推断格式；原始 96 B 与头前缀保留");
            addText(draw, fonts.small, 10.3f, {position.x + 20, calloutTop + 43},
                    kTextFaint, "E01 完整输入 → E02 关键前缀 → S03 异常隔离");
        }
        return;
    }

    if (texture != 0 && record.preview.width > 0 && record.preview.height > 0) {
        const float availableWidth = size.x - 20.0f;
        const float availableHeight = size.y - 20.0f;
        const float scale = std::min(
            availableWidth / static_cast<float>(record.preview.width),
            availableHeight / static_cast<float>(record.preview.height));
        const float width = static_cast<float>(record.preview.width) * scale;
        const float height = static_cast<float>(record.preview.height) * scale;
        const ImVec2 imageMin(position.x + (size.x - width) * 0.5f,
                              position.y + (size.y - height) * 0.5f);
        const ImVec2 imageMax(imageMin.x + width, imageMin.y + height);
        draw->AddImage(static_cast<ImTextureID>(texture), imageMin, imageMax);
        draw->AddRect(imageMin, imageMax, IM_COL32(52, 75, 84, 180), 3.0f);
        addText(draw, fonts.small, 10.0f, {imageMin.x + 10, imageMin.y + 7},
                IM_COL32(190, 232, 238, 230), "3D SURFACE  /  DECODED");
        const std::string uvLabel = "UV UNFOLD  /  " +
            std::to_string(record.header.seamCount) + " SEAMS";
        addText(draw, fonts.small, 10.0f,
                {imageMin.x + width * 0.63f,
                 imageMin.y + 7},
                IM_COL32(190, 232, 238, 230), uvLabel);
        const std::string revision = record.revision + "  ·  " +
            std::to_string(record.vertices.size()) + " V  /  " +
            std::to_string(record.triangles.size()) + " T";
        addText(draw, fonts.mono, 9.7f,
                {imageMax.x - textSize(fonts.mono, 9.7f, revision).x - 10,
                 imageMax.y - 16},
                IM_COL32(205, 230, 235, 210), revision);
    }
}

void drawMetaField(ImDrawList* draw,
                   const Fonts& fonts,
                   ImVec2 position,
                   const std::string& label,
                   const std::string& value,
                   ImU32 color) {
    addText(draw, fonts.small, 9.3f, position, kTextFaint, label);
    addText(draw, fonts.mono, 10.8f, {position.x, position.y + 14}, color, value);
}

}  // namespace
}  // namespace surfprov

namespace surfprov {
namespace {

void drawWrapped(ImDrawList* draw,
                 ImFont* font,
                 float size,
                 ImVec2 position,
                 ImU32 color,
                 const std::string& text,
                 float maxWidth,
                 int maxLines,
                 float lineHeight);

void drawRecordCard(ImDrawList* draw,
                    const Fonts& fonts,
                    UiContext& context,
                    std::size_t index,
                    ImVec2 position,
                    ImVec2 size) {
    const Record& record = context.batch.records[index];
    const bool selected = static_cast<int>(index) == context.selected;
    ImGui::SetCursorScreenPos(position);
    ImGui::PushID(static_cast<int>(index));
    ImGui::InvisibleButton("record", size,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) {
        context.selected = static_cast<int>(index);
        context.selectedStep = -1;
        context.rawMode = record.kind == RecordKind::UnknownExtension;
    }
    ImGui::PopID();

    const ImU32 fill = selected
        ? IM_COL32(20, 39, 45, 255)
        : (hovered ? IM_COL32(20, 29, 36, 255) : kPanelRaised);
    draw->AddRectFilled(position, {position.x + size.x, position.y + size.y},
                        fill, 10.0f);
    draw->AddRect(position, {position.x + size.x, position.y + size.y},
                  selected ? kCyan
                           : (hovered ? IM_COL32(48, 68, 79, 255) : kBorderSoft),
                  10.0f, 0, selected ? 1.6f : 1.0f);
    const ImU32 accent = statusColor(record.status);
    draw->AddRectFilled(position, {position.x + 3, position.y + size.y},
                        selected ? accent : IM_COL32(0, 0, 0, 0), 10.0f,
                        ImDrawFlags_RoundCornersLeft);

    char order[8];
    std::snprintf(order, sizeof(order), "%02d", static_cast<int>(index + 1));
    addText(draw, fonts.mono, 10.2f, {position.x + 14, position.y + 13},
            kTextFaint, order);
    addText(draw, fonts.body, 15.0f, {position.x + 42, position.y + 8},
            kText, record.name);
    const std::string kind = record.kind == RecordKind::UnknownExtension
        ? "UNKNOWN EXTENSION"
        : "VERSION SNAPSHOT";
    addText(draw, fonts.small, 10.2f, {position.x + 42, position.y + 29},
            kTextFaint, kind + "  /  " + record.revision);

    const float chipWidth =
        textSize(fonts.small, 12.0f, statusText(record.status)).x + 18.0f;
    chip(draw, fonts.small,
         {position.x + size.x - 14.0f - chipWidth, position.y + 11},
         statusText(record.status), accent, statusBackground(record.status),
         22.0f);

    addText(draw, fonts.mono, 11.1f, {position.x + 14, position.y + 56},
            kCyan, "extension ." + record.extension);
    addText(draw, fonts.mono, 11.1f,
            {position.x + 14 + textSize(fonts.mono, 11.1f,
                                        "extension ." + record.extension).x + 16,
             position.y + 56},
            kTextDim, formatBytes(record.fileSize));
    addText(draw, fonts.small, 10.2f, {position.x + 14, position.y + 77},
            kTextFaint, "sha256  " + shortHash(record.sha256));

    if (record.boundary.present) {
        addText(draw, fonts.small, 10.3f, {position.x + 14, position.y + 96},
                kAmber, record.boundary.id + "  ·  接缝方向无优势候选");
    } else if (record.kind == RecordKind::UnknownExtension) {
        addText(draw, fonts.small, 10.3f, {position.x + 14, position.y + 96},
                kRed, "UnknownFormatError  ·  原始字节保留");
    } else {
        addText(draw, fonts.small, 10.3f, {position.x + 14, position.y + 96},
                kTextFaint, "SUF 头 / 几何 / UV 全部通过");
    }
}

void drawLeftPanel(const Fonts& fonts,
                   UiContext& context,
                   ImVec2 position,
                   ImVec2 size) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!beginPanel("sources", position, size, 15.0f)) {
        endPanel();
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    addText(draw, fonts.body, 17.0f, origin, kText, "来源对象");
    const std::string count = std::to_string(context.batch.records.size());
    chip(draw, fonts.small, {origin.x + size.x - 54, origin.y - 2}, count,
         kCyan, kCyanSoft, 22.0f);
    addText(draw, fonts.small, 10.4f, {origin.x, origin.y + 27},
            kTextFaint, "PROCESS ORDER  /  原始输入只读");
    draw->AddLine({origin.x, origin.y + 48}, {origin.x + size.x - 30, origin.y + 48},
                  kBorderSoft, 1.0f);

    const float cardTop = origin.y + 58.0f;
    const float cardHeight = 121.0f;
    const float cardWidth = size.x - 30.0f;
    for (std::size_t index = 0; index < context.batch.records.size(); ++index) {
        drawRecordCard(draw, fonts, context, index,
                       {origin.x,
                        cardTop + static_cast<float>(index) * (cardHeight + 9.0f)},
                       {cardWidth, cardHeight});
    }

    const float bottom = origin.y + size.y - 30.0f;
    const float boxTop = bottom - 135.0f;
    draw->AddRectFilled({origin.x, boxTop}, {origin.x + cardWidth, bottom},
                        IM_COL32(10, 16, 21, 255), 9.0f);
    draw->AddRect({origin.x, boxTop}, {origin.x + cardWidth, bottom},
                  kBorderSoft, 9.0f);
    addText(draw, fonts.small, 10.3f, {origin.x + 12, boxTop + 10},
            kTextFaint, "BATCH INTEGRITY");
    draw->AddCircleFilled({origin.x + 15, boxTop + 35}, 3.2f,
                          context.batch.continuedAfterException ? kGreen : kRed);
    addText(draw, fonts.small, 11.2f, {origin.x + 25, boxTop + 27},
            kTextDim, "异常后继续 → snap-v2 已处理");
    draw->AddCircleFilled({origin.x + 15, boxTop + 57}, 3.2f,
                          context.batch.boundaryCount == 1 ? kAmber : kRed);
    addText(draw, fonts.small, 11.2f, {origin.x + 25, boxTop + 49},
            kAmber, "B-03 边界 → 保留 E05 / E06");
    draw->AddCircleFilled({origin.x + 15, boxTop + 79}, 3.2f,
                          context.batch.tempCleaned ? kGreen : kRed);
    addText(draw, fonts.small, 11.2f, {origin.x + 25, boxTop + 71},
            context.batch.tempCleaned ? kGreen : kRed,
            context.batch.tempCleaned ? "临时状态 → 已清理" : "临时状态 → 未清理");
    addText(draw, fonts.mono, 9.1f, {origin.x + 12, boxTop + 99},
            kTextFaint, "manifest " + shortHash(context.batch.inputManifestSha256));
    addText(draw, fonts.mono, 9.1f, {origin.x + 12, boxTop + 114},
            kTextFaint, ".tmp/unfold-scan-*");
    endPanel();
}

}  // namespace
}  // namespace surfprov
