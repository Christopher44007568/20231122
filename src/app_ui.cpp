#include "app_ui.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace hdrprov {
namespace {

constexpr ImU32 kBg = IM_COL32(7, 11, 15, 255);
constexpr ImU32 kPanel = IM_COL32(13, 19, 25, 255);
constexpr ImU32 kPanelRaised = IM_COL32(17, 25, 32, 255);
constexpr ImU32 kBorder = IM_COL32(34, 46, 56, 255);
constexpr ImU32 kBorderSoft = IM_COL32(27, 37, 46, 255);
constexpr ImU32 kText = IM_COL32(221, 231, 237, 255);
constexpr ImU32 kTextDim = IM_COL32(119, 135, 147, 255);
constexpr ImU32 kTextFaint = IM_COL32(76, 92, 104, 255);
constexpr ImU32 kCyan = IM_COL32(72, 214, 232, 255);
constexpr ImU32 kCyanSoft = IM_COL32(31, 83, 94, 255);
constexpr ImU32 kGreen = IM_COL32(78, 215, 143, 255);
constexpr ImU32 kGreenSoft = IM_COL32(27, 74, 52, 255);
constexpr ImU32 kAmber = IM_COL32(244, 178, 74, 255);
constexpr ImU32 kAmberSoft = IM_COL32(85, 60, 25, 255);
constexpr ImU32 kRed = IM_COL32(246, 104, 104, 255);

struct Fonts {
    ImFont* body = nullptr;
    ImFont* heading = nullptr;
    ImFont* small = nullptr;
    ImFont* mono = nullptr;
};

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
    return IM_COL32(82, 32, 35, 255);
}

std::string statusText(RecordStatus status) {
    if (status == RecordStatus::Pass) return "PASS";
    if (status == RecordStatus::Undecidable) return "无法判断";
    return "FAILED";
}

std::string formatBytes(std::uint64_t value) {
    std::string text = std::to_string(value);
    for (int index = static_cast<int>(text.size()) - 3; index > 0; index -= 3) {
        text.insert(static_cast<std::size_t>(index), ",");
    }
    return text + " B";
}

std::string shortHash(const std::string& hash) {
    if (hash.size() <= 18) return hash;
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

bool beginPanel(const char* id, ImVec2 position, ImVec2 size, float padding = 16.0f) {
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(kPanel));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(kBorderSoft));
    return ImGui::BeginChild(
        id, size,
        ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
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
                const BatchResult& batch,
                UiAction& action,
                UiContext& context,
                double now) {
    (void)batch;
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
    draw->AddRectFilled({logoMin.x + 8, logoMin.y + 8}, {logoMin.x + 22, logoMin.y + 22},
                        kCyan, 3.0f);
    draw->AddRectFilled({logoMin.x + 14, logoMin.y + 14}, {logoMin.x + 28, logoMin.y + 28},
                        IM_COL32(11, 30, 36, 255), 3.0f);
    draw->AddRect({logoMin.x + 14, logoMin.y + 14}, {logoMin.x + 28, logoMin.y + 28},
                  kCyan, 3.0f, 0, 1.0f);

    addText(draw, fonts.heading, 21.0f, {68, 11}, kText, "HDR 纹理格式验证");
    addText(draw, fonts.small, 12.5f, {69, 40}, kTextDim,
            "SOURCE PROVENANCE  /  HDR ATLAS 2026.11.22");

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 7));
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kPanelRaised));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(IM_COL32(27, 42, 50, 255)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(kCyanSoft));
    ImGui::SetCursorScreenPos({size.x - 118, 19});
    if (ImGui::Button("重新验证", {96, 31})) action = UiAction::Revalidate;
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);

    const float pillWidth = 214.0f;
    const ImVec2 pillMin(size.x - 20 - pillWidth - 128, 17);
    draw->AddRectFilled(pillMin, {pillMin.x + pillWidth, pillMin.y + 35},
                        IM_COL32(17, 35, 30, 255), 17.5f);
    draw->AddCircleFilled({pillMin.x + 16, pillMin.y + 17.5f}, 4.0f, kGreen);
    addText(draw, fonts.small, 12.5f, {pillMin.x + 28, pillMin.y + 9}, kGreen,
            "批处理完成 · 异常后已继续");

    if (now < context.noticeUntil && !context.notice.empty()) {
        const ImVec2 noticeSize = textSize(fonts.small, 12.5f, context.notice);
        const float width = noticeSize.x + 30.0f;
        const ImVec2 noticeMin(size.x - 20 - width, 82);
        draw->AddRectFilled(noticeMin, {noticeMin.x + width, noticeMin.y + 29},
                            IM_COL32(22, 38, 44, 245), 8.0f);
        draw->AddRect(noticeMin, {noticeMin.x + width, noticeMin.y + 29}, kCyanSoft, 8.0f);
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
    addText(draw, fonts.small, 12.0f, {position.x + 18, position.y + 13},
            kTextDim, label);
    addText(draw, fonts.heading, 27.0f, {position.x + 17, position.y + 29},
            accent, value);
    addText(draw, fonts.small, 11.5f, {position.x + 18, position.y + 60},
            kTextFaint, detail);
}

}  // namespace
}  // namespace hdrprov

namespace hdrprov {
namespace {

void drawLeftPanel(const Fonts& fonts,
                   UiContext& context,
                   ImVec2 position,
                   ImVec2 size);
void drawCenterPanel(const Fonts& fonts,
                     UiContext& context,
                     ImVec2 position,
                     ImVec2 size);
void drawWrapped(ImDrawList* draw,
                 ImFont* font,
                 float size,
                 ImVec2 position,
                 ImU32 color,
                 const std::string& text,
                 float maxWidth,
                 int maxLines,
                 float lineHeight);

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
        : (record.status == RecordStatus::Undecidable && !record.steps.empty()
               ? static_cast<int>(record.steps.size()) - 1
               : -1);
    addText(draw, fonts.body, 17.0f, origin, kText, "证据链");
    const std::string evidenceCount = std::to_string(record.evidence.size());
    chip(draw, fonts.small, {origin.x + size.x - 55, origin.y - 2}, evidenceCount,
         kCyan, kCyanSoft, 22.0f);
    addText(draw, fonts.small, 10.5f, {origin.x, origin.y + 27}, kTextFaint,
            "INPUT → STEP → CONCLUSION");
    draw->AddLine({origin.x, origin.y + 48}, {origin.x + size.x - 30, origin.y + 48},
                  kBorderSoft, 1.0f);

    const float stepTop = origin.y + 58.0f;
    const float stepHeight = 52.0f;
    draw->AddLine({origin.x + 9, stepTop + 12},
                  {origin.x + 9, stepTop + stepHeight * static_cast<float>(record.steps.size()) - 24},
                  IM_COL32(43, 59, 68, 255), 1.0f);
    for (std::size_t index = 0; index < record.steps.size(); ++index) {
        const Step& step = record.steps[index];
        const ImVec2 rowMin(origin.x, stepTop + static_cast<float>(index) * stepHeight);
        const ImVec2 rowMax(origin.x + size.x - 30.0f, rowMin.y + stepHeight - 3.0f);
        const bool selected = activeStep == static_cast<int>(index);
        ImGui::SetCursorScreenPos(rowMin);
        ImGui::PushID(static_cast<int>(index) + 1000);
        ImGui::InvisibleButton("step", {rowMax.x - rowMin.x, rowMax.y - rowMin.y});
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
        draw->AddCircleFilled({origin.x + 9, rowMin.y + 17}, 4.2f, state);
        draw->AddCircle({origin.x + 9, rowMin.y + 17}, 7.0f,
                        IM_COL32(244, 178, 74, 80), 20, 1.0f);
        addText(draw, fonts.mono, 9.5f, {origin.x + 23, rowMin.y + 6},
                kTextFaint, step.id);
        addText(draw, fonts.small, 12.0f, {origin.x + 52, rowMin.y + 5},
                kText, step.title);
        if (!step.evidenceIds.empty()) {
            float x = origin.x + 23.0f;
            for (const auto& id : step.evidenceIds) {
                const float width = textSize(fonts.mono, 9.2f, id).x + 8.0f;
                if (x + width > rowMax.x - 8) break;
                draw->AddRectFilled({x, rowMin.y + 27}, {x + width, rowMin.y + 42},
                                    IM_COL32(16, 30, 36, 255), 3.5f);
                addText(draw, fonts.mono, 9.2f, {x + 4, rowMin.y + 31},
                        selected ? kCyan : kTextDim, id);
                x += width + 3.0f;
            }
        }
    }

    const float detailTop = stepTop + stepHeight * static_cast<float>(record.steps.size()) + 8.0f;
    const float detailHeight = 91.0f;
    draw->AddRectFilled({origin.x, detailTop}, {origin.x + size.x - 30, detailTop + detailHeight},
                        IM_COL32(10, 16, 21, 255), 8.0f);
    draw->AddRect({origin.x, detailTop}, {origin.x + size.x - 30, detailTop + detailHeight},
                  kBorderSoft, 8.0f);
    if (activeStep >= 0 &&
        activeStep < static_cast<int>(record.steps.size())) {
        const Step& step = record.steps[static_cast<std::size_t>(activeStep)];
        addText(draw, fonts.small, 9.2f, {origin.x + 11, detailTop + 9},
                kTextFaint, step.id + "  /  STEP DETAIL");
        drawWrapped(draw, fonts.small, 11.0f, {origin.x + 11, detailTop + 25},
                    kTextDim, step.detail, size.x - 54.0f, 3, 16.0f);
    } else {
        addText(draw, fonts.small, 9.2f, {origin.x + 11, detailTop + 9},
                kTextFaint, "TRACE RULE");
        draw->AddCircleFilled({origin.x + 15, detailTop + 30}, 3.0f, kCyan);
        addText(draw, fonts.small, 11.0f, {origin.x + 24, detailTop + 23},
                kTextDim, "每个结论只引用本记录的输入与步骤。");
        addText(draw, fonts.small, 11.0f, {origin.x + 24, detailTop + 41},
                kTextDim, "点击步骤查看处理证据与偏移。");
    }

    const float footerTop = detailTop + detailHeight + 12.0f;
    draw->AddLine({origin.x, footerTop - 5}, {origin.x + size.x - 30, footerTop - 5},
                  kBorderSoft, 1.0f);
    draw->AddCircleFilled({origin.x + 5, footerTop + 13}, 3.5f,
                          context.batch.tempCleaned ? kGreen : kRed);
    addText(draw, fonts.small, 10.2f, {origin.x + 15, footerTop + 6},
            context.batch.tempCleaned ? kGreen : kRed,
            context.batch.tempCleaned ? "SESSION END  ·  TEMP CLEANED"
                                      : "SESSION END  ·  TEMP DIRTY");
    addText(draw, fonts.mono, 9.0f, {origin.x, footerTop + 26}, kTextFaint,
            ".tmp/hdr-scan-" + context.batch.runId.substr(5, 10));
    const std::string hashText = "input manifest  " + shortHash(context.batch.runId);
    addText(draw, fonts.small, 9.2f, {origin.x, footerTop + 43}, kTextFaint, hashText);
    endPanel();
}

}  // namespace

UiAction drawUi(UiContext& context, double nowSeconds) {
    if (context.batch.records.empty()) return UiAction::Quit;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##hdr-provenance-root", nullptr, flags);
    ImGui::PopStyleVar();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled({0, 0}, io.DisplaySize, kBg);
    const Fonts fonts = uiFonts();

    UiAction action = UiAction::None;
    drawTopBar(draw, fonts, io.DisplaySize, context.batch, action, context, nowSeconds);

    const float margin = 18.0f;
    const float gap = 12.0f;
    const float cardWidth =
        (io.DisplaySize.x - margin * 2.0f - gap * 3.0f) / 4.0f;
    drawMetricCard(draw, fonts, {margin, 82}, {cardWidth, 80}, "输入对象",
                   "03", "2 快照 + 1 截断记录", kCyan);
    drawMetricCard(draw, fonts, {margin + (cardWidth + gap), 82}, {cardWidth, 80},
                   "格式通过", "02", "可回到原始输入", kGreen);
    drawMetricCard(draw, fonts, {margin + (cardWidth + gap) * 2.0f, 82},
                   {cardWidth, 80}, "无法判断", "01",
                   "边界证据 E05 已保留", kAmber);
    drawMetricCard(draw, fonts, {margin + (cardWidth + gap) * 3.0f, 82},
                   {cardWidth, 80}, "异常后继续", "1",
                   context.batch.tempCleaned ? "v2 已继续 · 临时已清理"
                                             : "清理未完成",
                   kGreen);

    const float mainTop = 176.0f;
    const float mainHeight = io.DisplaySize.y - mainTop - 18.0f;
    const float leftWidth = std::clamp(io.DisplaySize.x * 0.245f, 282.0f, 332.0f);
    const float rightWidth = std::clamp(io.DisplaySize.x * 0.275f, 320.0f, 382.0f);
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
                static_cast<std::size_t>(context.selected)].status ==
                RecordStatus::Undecidable;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            context.selected = (context.selected + count - 1) % count;
            context.selectedStep = -1;
            context.rawMode = context.batch.records[
                static_cast<std::size_t>(context.selected)].status ==
                RecordStatus::Undecidable;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R)) action = UiAction::Revalidate;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) action = UiAction::Quit;
    }
    ImGui::End();
    return action;
}

}  // namespace hdrprov

namespace hdrprov {
namespace {

const Evidence* selectedEvidence(const Record& record, int selectedStep) {
    if (selectedStep >= 0 && selectedStep < static_cast<int>(record.steps.size())) {
        for (const auto& id : record.steps[selectedStep].evidenceIds) {
            for (const auto& evidence : record.evidence) {
                if (evidence.id == id) return &evidence;
            }
        }
    }
    if (record.status == RecordStatus::Undecidable && !record.evidence.empty()) {
        return &record.evidence.back();
    }
    return record.evidence.empty() ? nullptr : &record.evidence.front();
}

std::string hexLine(const Evidence& evidence, std::size_t offset, std::size_t count) {
    std::string hex;
    std::string ascii;
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t source = offset + index;
        if (source >= evidence.excerpt.size()) {
            hex += "   ";
            ascii += ' ';
            continue;
        }
        char part[4];
        std::snprintf(part, sizeof(part), "%02x ", evidence.excerpt[source]);
        hex += part;
        const unsigned char value = evidence.excerpt[source];
        ascii += value >= 32 && value < 127 ? static_cast<char>(value) : '.';
    }
    return hex + " " + ascii;
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
    if (context.rawMode) {
        const Evidence* evidence = selectedEvidence(record, context.selectedStep);
        addText(draw, fonts.small, 10.5f, {position.x + 13, position.y + 10},
                kTextFaint, "RAW EVIDENCE");
        if (evidence != nullptr) {
            const std::string range =
                evidence->id + "  ·  offset " + std::to_string(evidence->offset) +
                "  ·  " + formatBytes(evidence->length);
            addText(draw, fonts.mono, 11.0f, {position.x + 13, position.y + 27},
                    kCyan, range);
            addText(draw, fonts.mono, 10.0f, {position.x + 13, position.y + 44},
                    kTextFaint, "sha256 " + shortHash(evidence->sha256));
            const std::size_t maxBytes = std::min<std::size_t>(
                evidence->excerpt.size(), 144);
            for (std::size_t row = 0; row * 16 < maxBytes && row < 8; ++row) {
                char offset[16];
                std::snprintf(offset, sizeof(offset), "%08zx:",
                              static_cast<std::size_t>(evidence->offset) + row * 16);
                addText(draw, fonts.mono, 10.8f,
                        {position.x + 13, position.y + 68 + static_cast<float>(row) * 18.0f},
                        kTextFaint, offset);
                addText(draw, fonts.mono, 10.8f,
                        {position.x + 82, position.y + 68 + static_cast<float>(row) * 18.0f},
                        kTextDim, hexLine(*evidence, row * 16, 16));
            }
        }
        if (record.status == RecordStatus::Undecidable) {
            const float calloutTop = position.y + size.y - 69.0f;
            draw->AddRectFilled({position.x + 10, calloutTop},
                                {position.x + size.x - 10, position.y + size.y - 9},
                                IM_COL32(42, 31, 16, 235), 7.0f);
            draw->AddRect({position.x + 10, calloutTop},
                          {position.x + size.x - 10, position.y + size.y - 9},
                          IM_COL32(102, 72, 31, 255), 7.0f);
            addText(draw, fonts.small, 10.2f, {position.x + 20, calloutTop + 9},
                    kAmber, "BOUNDARY  ·  声明 8,192 B / 实际 409 B  ·  缺 7,783 B");
            addText(draw, fonts.small, 10.2f, {position.x + 20, calloutTop + 28},
                    kTextDim, "无尾部长度或校验字段 → 截断原因无法判断");
            addText(draw, fonts.small, 10.2f, {position.x + 20, calloutTop + 45},
                    kTextFaint, "EOF 前 24 B 原样保留，结论引用 E05");
        }
        return;
    }

    if (texture != 0 && record.preview.width > 0 && record.preview.height > 0) {
        const float availableWidth = size.x - 24.0f;
        const float availableHeight = size.y - 22.0f;
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
        draw->AddRectFilledMultiColor(
            {position.x + 1, position.y + size.y - 44},
            {position.x + size.x - 1, position.y + size.y - 1},
            IM_COL32(7, 12, 16, 0), IM_COL32(7, 12, 16, 0),
            IM_COL32(7, 12, 16, 210), IM_COL32(7, 12, 16, 210));
        addText(draw, fonts.small, 10.0f, {position.x + 13, position.y + 9},
                IM_COL32(180, 225, 232, 230), "HDR PREVIEW  ·  TONE MAPPED");
        const std::string dimensions =
            std::to_string(record.preview.width) + " × " +
            std::to_string(record.preview.height) + "  ·  RGBE";
        addText(draw, fonts.mono, 10.5f,
                {position.x + size.x - textSize(fonts.mono, 10.5f, dimensions).x - 13,
                 position.y + 9},
                IM_COL32(205, 230, 235, 220), dimensions);
        addText(draw, fonts.small, 10.5f, {position.x + 13, position.y + size.y - 25},
                kTextDim, record.revision + " 来源解码  ·  clamp / Reinhard / sRGB");
    } else {
        draw->AddRectFilled({position.x + 1, position.y + 1},
                            {position.x + size.x - 1, position.y + size.y - 1},
                            IM_COL32(11, 17, 21, 255), 10.0f);
        const ImVec2 center(position.x + size.x * 0.5f, position.y + size.y * 0.45f);
        draw->AddCircle(center, 31.0f, kAmber, 32, 2.0f);
        addText(draw, fonts.heading, 25.0f, {center.x - 7, center.y - 18},
                kAmber, "!");
        addText(draw, fonts.body, 15.0f, {center.x - 76, center.y + 44},
                kText, "NO PIXEL PREVIEW");
        addText(draw, fonts.small, 11.0f, {center.x - 103, center.y + 67},
                kTextDim, "输入在预期负载结束前 EOF");
        for (int block = 0; block < 8; ++block) {
            const float x = position.x + 24 + static_cast<float>(block) * 24.0f;
            const float height = block < 7 ? 26.0f : 8.0f;
            draw->AddRectFilled({x, position.y + size.y - 28 - height},
                                {x + 16, position.y + size.y - 28},
                                block < 7 ? kCyanSoft : kAmber, 2.0f);
        }
    }
}

void drawMetaField(ImDrawList* draw,
                   const Fonts& fonts,
                   ImVec2 position,
                   float width,
                   const std::string& label,
                   const std::string& value,
                   ImU32 valueColor = kText) {
    (void)width;
    addText(draw, fonts.small, 9.5f, position, kTextFaint, label);
    addText(draw, fonts.mono, 11.2f, {position.x, position.y + 15}, valueColor,
            value);
}

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
    addText(draw, fonts.heading, 20.0f, origin, kText, record.name);
    const std::string subtitle = record.revision + "  /  " + record.sourcePath;
    addText(draw, fonts.mono, 10.8f, {origin.x, origin.y + 28}, kTextFaint, subtitle);
    const ImU32 accent = statusColor(record.status);
    const float statusWidth = textSize(fonts.small, 12.0f, statusText(record.status)).x + 18.0f;
    chip(draw, fonts.small, {origin.x + size.x - 32 - statusWidth, origin.y + 3},
         statusText(record.status), accent, statusBackground(record.status), 23.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Button,
                          context.rawMode ? ImGui::ColorConvertU32ToFloat4(kCyanSoft)
                                          : ImGui::ColorConvertU32ToFloat4(kPanelRaised));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImGui::ColorConvertU32ToFloat4(IM_COL32(30, 49, 58, 255)));
    ImGui::SetCursorScreenPos({origin.x + size.x - 192, origin.y + 31});
    if (ImGui::Button("原始字节", {78, 27})) context.rawMode = true;
    ImGui::SameLine();
    if (ImGui::Button("预览", {52, 27})) context.rawMode = false;
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    const float previewTop = origin.y + 67.0f;
    const float previewHeight = std::clamp(size.y * 0.31f, 166.0f, 214.0f);
    const ImVec2 previewPosition(origin.x, previewTop);
    const ImVec2 previewSize(size.x - 32.0f, previewHeight);
    drawPreview(draw, fonts, record,
                context.previews[static_cast<std::size_t>(context.selected)],
                context, previewPosition, previewSize);

    const float metadataTop = previewTop + previewHeight + 14.0f;
    const float metadataHeight = 91.0f;
    draw->AddRectFilled({origin.x, metadataTop},
                        {origin.x + size.x - 32.0f, metadataTop + metadataHeight},
                        IM_COL32(10, 16, 21, 255), 9.0f);
    draw->AddRect({origin.x, metadataTop},
                  {origin.x + size.x - 32.0f, metadataTop + metadataHeight},
                  kBorderSoft, 9.0f);
    const float columnWidth = (size.x - 32.0f - 32.0f) / 3.0f;
    drawMetaField(draw, fonts, {origin.x + 14, metadataTop + 12}, columnWidth,
                  "FORMAT", record.header.format.empty() ? "—" : record.header.format,
                  kCyan);
    drawMetaField(draw, fonts, {origin.x + 14 + columnWidth, metadataTop + 12},
                  columnWidth, "RESOLUTION",
                  record.header.resolution.empty() ? "—" : record.header.resolution);
    drawMetaField(draw, fonts, {origin.x + 14 + columnWidth * 2, metadataTop + 12},
                  columnWidth, "INPUT SHA-256", shortHash(record.sha256));
    draw->AddLine({origin.x + 14, metadataTop + 47},
                  {origin.x + size.x - 46, metadataTop + 47}, kBorderSoft, 1.0f);
    drawMetaField(draw, fonts, {origin.x + 14, metadataTop + 57}, columnWidth,
                  "HEADER / PAYLOAD",
                  formatBytes(record.header.headerBytes) + " / " +
                      formatBytes(record.header.pixelBytes));
    drawMetaField(draw, fonts, {origin.x + 14 + columnWidth, metadataTop + 57},
                  columnWidth, "SOFTWARE",
                  record.header.software.empty() ? "—" : record.header.software);
    drawMetaField(draw, fonts, {origin.x + 14 + columnWidth * 2, metadataTop + 57},
                  columnWidth, "EXPOSURE", record.header.exposure.empty() ? "—"
                                                                         : record.header.exposure);

    const float conclusionTop = metadataTop + metadataHeight + 14.0f;
    const float conclusionHeight = origin.y + size.y - 31.0f - conclusionTop;
    draw->AddRectFilled({origin.x, conclusionTop},
                        {origin.x + size.x - 32.0f, conclusionTop + conclusionHeight},
                        IM_COL32(11, 18, 22, 255), 10.0f);
    draw->AddRect({origin.x, conclusionTop},
                  {origin.x + size.x - 32.0f, conclusionTop + conclusionHeight},
                  accent, 10.0f, 0, 1.2f);
    draw->AddRectFilled({origin.x, conclusionTop},
                        {origin.x + 3, conclusionTop + conclusionHeight},
                        accent, 10.0f, ImDrawFlags_RoundCornersLeft);
    addText(draw, fonts.small, 9.5f, {origin.x + 14, conclusionTop + 12},
            accent, "结论  /  TRACEABLE CONCLUSION");
    addText(draw, fonts.heading, 17.0f, {origin.x + 14, conclusionTop + 30},
            kText, record.conclusion.headline);
    drawWrapped(draw, fonts.small, 11.8f, {origin.x + 14, conclusionTop + 58},
                kTextDim, record.conclusion.detail, size.x - 62.0f,
                std::max(2, static_cast<int>((conclusionHeight - 102.0f) / 18.0f)), 18.0f);

    float refX = origin.x + 14.0f;
    const float refY = conclusionTop + conclusionHeight - 31.0f;
    addText(draw, fonts.small, 9.0f, {refX, refY + 5}, kTextFaint, "STEPS");
    refX += 38.0f;
    for (const auto& id : record.conclusion.stepIds) {
        const float width = textSize(fonts.mono, 9.8f, id).x + 10.0f;
        draw->AddRectFilled({refX, refY}, {refX + width, refY + 18},
                            IM_COL32(21, 33, 40, 255), 4.0f);
        addText(draw, fonts.mono, 9.8f, {refX + 5, refY + 4}, kTextDim, id);
        refX += width + 4.0f;
    }
    addText(draw, fonts.small, 9.0f, {refX + 7, refY + 5}, kTextFaint, "EVIDENCE");
    refX += 58.0f;
    for (const auto& id : record.conclusion.evidenceIds) {
        const float width = textSize(fonts.mono, 9.8f, id).x + 10.0f;
        draw->AddRectFilled({refX, refY}, {refX + width, refY + 18},
                            IM_COL32(17, 39, 43, 255), 4.0f);
        addText(draw, fonts.mono, 9.8f, {refX + 5, refY + 4}, kCyan, id);
        refX += width + 4.0f;
    }
    endPanel();
}

}  // namespace
}  // namespace hdrprov

namespace hdrprov {
namespace {

std::vector<std::string> wrapText(ImFont* font,
                                  float size,
                                  const std::string& text,
                                  float maxWidth) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        std::string word = text.substr(start, end - start);
        std::string candidate = line.empty() ? word : line + " " + word;
        if (textSize(font, size, candidate).x > maxWidth && !line.empty()) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        start = end + (end < text.size() ? 1 : 0);
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

void drawWrapped(ImDrawList* draw,
                 ImFont* font,
                 float size,
                 ImVec2 position,
                 ImU32 color,
                 const std::string& text,
                 float maxWidth,
                 int maxLines,
                 float lineHeight = 20.0f) {
    const auto lines = wrapText(font, size, text, maxWidth);
    for (std::size_t index = 0; index < lines.size() &&
                                static_cast<int>(index) < maxLines; ++index) {
        addText(draw, font, size,
                {position.x, position.y + static_cast<float>(index) * lineHeight},
                color, lines[index]);
    }
}

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
    ImGui::InvisibleButton(
        "record", size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) {
        context.selected = static_cast<int>(index);
        context.selectedStep = -1;
        context.rawMode = record.status == RecordStatus::Undecidable;
    }
    ImGui::PopID();

    const ImU32 fill = selected
        ? IM_COL32(20, 39, 45, 255)
        : (hovered ? IM_COL32(20, 29, 36, 255) : kPanelRaised);
    draw->AddRectFilled(position, {position.x + size.x, position.y + size.y},
                        fill, 10.0f);
    draw->AddRect(position, {position.x + size.x, position.y + size.y},
                  selected ? kCyan : (hovered ? IM_COL32(48, 68, 79, 255) : kBorderSoft),
                  10.0f, 0, selected ? 1.6f : 1.0f);

    ImU32 accent = statusColor(record.status);
    draw->AddRectFilled(position, {position.x + 3, position.y + size.y},
                        selected ? accent : IM_COL32(0, 0, 0, 0),
                        10.0f, ImDrawFlags_RoundCornersLeft);
    char order[8];
    std::snprintf(order, sizeof(order), "%02d", static_cast<int>(index + 1));
    addText(draw, fonts.mono, 10.5f, {position.x + 14, position.y + 13},
            kTextFaint, order);
    addText(draw, fonts.body, 15.0f, {position.x + 42, position.y + 9},
            kText, record.name);
    const std::string revision = record.revision == "v1" || record.revision == "v2"
        ? "VERSION SNAPSHOT"
        : "TRUNCATED RECORD";
    addText(draw, fonts.small, 10.5f, {position.x + 42, position.y + 30},
            kTextFaint, revision + "  /  " + record.revision);

    float chipX = position.x + size.x - 14;
    const float chipWidth = textSize(fonts.small, 12.0f, statusText(record.status)).x + 18.0f;
    chipX -= chipWidth;
    chip(draw, fonts.small, {chipX, position.y + 12}, statusText(record.status),
         accent, statusBackground(record.status), 22.0f);

    const std::string sizeText = record.status == RecordStatus::Undecidable
        ? formatBytes(record.fileSize) + " / " + formatBytes(record.declaredSize)
        : formatBytes(record.fileSize);
    addText(draw, fonts.mono, 11.5f, {position.x + 14, position.y + 60},
            statusColor(record.status), sizeText);
    addText(draw, fonts.small, 10.5f, {position.x + 14, position.y + 82},
            kTextFaint, "sha256  " + shortHash(record.sha256));
    if (record.status == RecordStatus::Undecidable) {
        addText(draw, fonts.small, 10.5f, {position.x + 14, position.y + 101},
                kAmber, "EOF 前关键字节已保留 → E05");
    } else {
        addText(draw, fonts.small, 10.5f, {position.x + 14, position.y + 101},
                kTextFaint,
                std::to_string(record.header.width) + " × " +
                    std::to_string(record.header.height) + "  /  " +
                    record.header.format);
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
    addText(draw, fonts.small, 10.5f, {origin.x, origin.y + 27},
            kTextFaint, "PROCESS ORDER  /  只读输入");
    draw->AddLine({origin.x, origin.y + 48}, {origin.x + size.x - 30, origin.y + 48},
                  kBorderSoft, 1.0f);

    const float cardTop = origin.y + 58.0f;
    const float cardHeight = 122.0f;
    const float cardWidth = size.x - 30.0f;
    for (std::size_t index = 0; index < context.batch.records.size(); ++index) {
        drawRecordCard(draw, fonts, context, index,
                       {origin.x, cardTop + static_cast<float>(index) * (cardHeight + 9.0f)},
                       {cardWidth, cardHeight});
    }

    const float bottom = origin.y + size.y - 30.0f;
    const float boxTop = bottom - 126.0f;
    draw->AddRectFilled({origin.x, boxTop}, {origin.x + cardWidth, bottom},
                        IM_COL32(10, 16, 21, 255), 9.0f);
    draw->AddRect({origin.x, boxTop}, {origin.x + cardWidth, bottom},
                  kBorderSoft, 9.0f);
    addText(draw, fonts.small, 10.5f, {origin.x + 12, boxTop + 10},
            kTextFaint, "BATCH INTEGRITY");
    addText(draw, fonts.body, 13.0f, {origin.x + 12, boxTop + 28},
            kText, "异常对象已隔离");
    draw->AddCircleFilled({origin.x + 15, boxTop + 60}, 3.2f, kGreen);
    addText(draw, fonts.small, 11.5f, {origin.x + 25, boxTop + 52},
            kTextDim, "TruncatedRecordError → 边界");
    draw->AddCircleFilled({origin.x + 15, boxTop + 80}, 3.2f, kGreen);
    addText(draw, fonts.small, 11.5f, {origin.x + 25, boxTop + 72},
            kTextDim, "snap-v2 已继续处理");
    draw->AddCircleFilled({origin.x + 15, boxTop + 100}, 3.2f,
                          context.batch.tempCleaned ? kGreen : kRed);
    addText(draw, fonts.small, 11.5f, {origin.x + 25, boxTop + 92},
            context.batch.tempCleaned ? kGreen : kRed,
            context.batch.tempCleaned ? "临时状态 · 已清理" : "临时状态 · 未清理");
    endPanel();
}

}  // namespace
}  // namespace hdrprov
