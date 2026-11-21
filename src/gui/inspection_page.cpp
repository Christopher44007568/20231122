#include "inspection_page.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace gcdump::gui {

namespace {

// Minimal 5x7 bitmap font, one byte per row, bit 4 = leftmost pixel.
const std::unordered_map<char, std::array<uint8_t, 7>>& font() {
    static const std::unordered_map<char, std::array<uint8_t, 7>> glyphs = {
        {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
        {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
        {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
        {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
        {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
        {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
        {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
        {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        {'I', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
        {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
        {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
        {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
        {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
        {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
        {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
        {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
        {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
        {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
        {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
        {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
        {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
        {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
        {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
        {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
        {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
        {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
        {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
        {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
        {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
        {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
        {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
        {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
        {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
        {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
        {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
        {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
        {':', {0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00}},
        {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
        {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
        {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
        {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
        {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},
        {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
        {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
        {'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
        {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
        {')', {0x04, 0x02, 0x01, 0x01, 0x01, 0x02, 0x04}},
        {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
        {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    };
    return glyphs;
}

const Rgba kBg{13, 15, 20};
const Rgba kPanel{22, 26, 34};
const Rgba kText{220, 226, 236};
const Rgba kMuted{140, 150, 165};
const Rgba kGreen{92, 210, 130};
const Rgba kRed{235, 105, 105};
const Rgba kAmber{240, 185, 90};
const Rgba kCyan{105, 190, 235};

Rgba verdictColor(Verdict v) {
    switch (v) {
    case Verdict::AutoConfirmed: return kGreen;
    case Verdict::AutoRejected: return kRed;
    case Verdict::Indeterminate: return kAmber;
    case Verdict::NeedsManualReview: return kCyan;
    }
    return kText;
}

} // namespace

void InspectionPage::fillRect(uint32_t x, uint32_t y, uint32_t w,
                              uint32_t h, Rgba color) {
    for (uint32_t row = 0; row < h; ++row) {
        for (uint32_t col = 0; col < w; ++col) {
            uint32_t px = x + col;
            uint32_t py = y + row;
            if (px >= bitmap_.width || py >= bitmap_.height) continue;
            size_t idx = (static_cast<size_t>(py) * bitmap_.width + px) * 4;
            bitmap_.pixels[idx] = color.r;
            bitmap_.pixels[idx + 1] = color.g;
            bitmap_.pixels[idx + 2] = color.b;
            bitmap_.pixels[idx + 3] = color.a;
        }
    }
}

void InspectionPage::drawChar(char c, uint32_t x, uint32_t y, Rgba color,
                              uint32_t scale) {
    char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto it = font().find(upper);
    const auto& glyph = it == font().end() ? font().at(' ') : it->second;
    for (uint32_t row = 0; row < kCharH; ++row) {
        for (uint32_t col = 0; col < kCharW; ++col) {
            if (glyph[row] & (1 << (kCharW - 1 - col)))
                fillRect(x + col * scale, y + row * scale, scale, scale,
                         color);
        }
    }
}

void InspectionPage::drawText(std::string_view text, uint32_t x, uint32_t y,
                              Rgba color, uint32_t scale) {
    uint32_t cx = x;
    for (char c : text) {
        drawChar(c, cx, y, color, scale);
        cx += (kCharW + 1) * scale;
        if (cx + kCharW * scale > bitmap_.width) break;
    }
}

std::array<float, 4> InspectionPage::statusColor() const {
    bool blocking = !report_.failures.empty();
    bool boundary = report_.manualBoundaryLine != 0;
    if (blocking) return {0.55f, 0.16f, 0.16f, 1.0f};
    if (boundary) return {0.55f, 0.42f, 0.12f, 1.0f};
    return {0.10f, 0.30f, 0.18f, 1.0f};
}

InspectionPage::InspectionPage(Report report) : report_(std::move(report)) {
    const uint32_t width = 1180;
    struct Item {
        bool rule = false;
        std::string text;
        Rgba color = kText;
        uint32_t indent = 0;
        uint32_t gapBefore = 0;
    };
    std::vector<Item> items;
    auto add = [&](std::string text, Rgba color, uint32_t indent = 0) {
        items.push_back({false, std::move(text), color, indent, 0});
    };
    auto section = [&](std::string title, Rgba color) {
        items.push_back({true, std::move(title), color, 0, lineHeight()});
    };

    add("GPU CRASH PRE-STATE DUMP PROVENANCE PAGE", kCyan);
    add("DUMP " + report_.dumpId, kMuted);
    add("SOURCE " + report_.sourcePath, kMuted);
    add("GREEN=AUTO CONFIRMED  RED=AUTO REJECTED  AMBER=INDETERMINATE  "
        "BLUE=MANUAL REVIEW",
        kMuted);

    section("1 AUTOMATIC CONCLUSIONS", kGreen);
    for (const Finding& f : report_.findings) {
        add(std::string("[") + to_string(f.verdict) + "] " + f.id + " " +
                f.subject,
            verdictColor(f.verdict));
        add("SUMMARY " + f.summary, kText, kPad);
        for (const EvidenceRef& e : f.evidence)
            add("EVIDENCE L" + std::to_string(e.sourceLine) + " " + e.quote,
                kMuted, kPad);
        if (!f.steps.empty())
            add("PIPELINE STEPS " + std::to_string(f.steps.front()) + ".." +
                    std::to_string(f.steps.back()),
                kMuted, kPad);
    }
    if (report_.findings.empty()) add("NONE EMITTED", kMuted);

    section("2 MANUAL CONFIRMATION", kCyan);
    for (const ManualCheck& mc : report_.manualChecks) {
        add("CHECK " + mc.item, kCyan);
        add("REASON " + mc.reason, kText, kPad);
        for (const EvidenceRef& e : mc.evidence)
            add("EVIDENCE L" + std::to_string(e.sourceLine) + " " + e.quote,
                kMuted, kPad);
        for (const FollowUpAction& a : mc.actions)
            add("ACTION " + a.action, kAmber, kPad);
    }
    if (report_.manualChecks.empty()) add("NONE", kMuted);

    section("3 FAILURES AND EXECUTABLE FOLLOW-UP", kRed);
    for (const FailureReport& fail : report_.failures) {
        add(fail.code + " " + fail.message, kRed);
        for (const EvidenceRef& e : fail.evidence)
            add("EVIDENCE L" + std::to_string(e.sourceLine) + " " + e.quote,
                kMuted, kPad);
        for (const FollowUpAction& a : fail.actions)
            add(std::string(a.blocksReliability ? "[BLOCKING] " : "") +
                    "ACTION " + a.action,
                kAmber, kPad);
    }
    if (report_.failures.empty()) add("NO BLOCKING FAILURES", kGreen);

    if (report_.manualBoundaryLine) {
        section("4 RETAINED BOUNDARY EVIDENCE - CANNOT DECIDE", kAmber);
        if (const SourceLine* l = report_.findLine(report_.manualBoundaryLine))
            add("L" + std::to_string(l->sourceLine) + " " + l->raw, kAmber);
    }

    section("5 RAW SOURCE DUMP - UNMODIFIED INPUT", kMuted);
    for (const SourceLine& l : report_.lines)
        add("L" + std::to_string(l.sourceLine) + " " + l.raw, kMuted);

    const uint32_t charCell = (kCharW + 1) * kScale;
    const size_t maxChars = (width - kPad * 2) / charCell;
    auto wrappedRows = [&](const Item& it) {
        size_t usable =
            maxChars - (it.indent + charCell - 1) / charCell;
        return std::max<size_t>(1, (it.text.size() + usable - 1) / usable);
    };
    uint32_t height = kPad;
    for (const Item& it : items) {
        height += it.gapBefore;
        height += it.rule
                      ? lineHeight() * 2
                      : static_cast<uint32_t>(wrappedRows(it)) * lineHeight();
    }
    height += kPad;

    bitmap_ = Bitmap(width, height);
    fillRect(0, 0, width, height, kBg);
    Rgba top = report_.failures.empty()
                   ? (report_.manualBoundaryLine ? kAmber : kGreen)
                   : kRed;
    fillRect(0, 0, width, 4, top);

    uint32_t y = kPad;
    for (const Item& it : items) {
        y += it.gapBefore;
        if (it.rule) {
            fillRect(kPad, y, width - kPad * 2, 2, it.color);
            y += lineHeight();
            drawText(it.text, kPad, y, it.color);
            y += lineHeight();
            continue;
        }
        size_t usable =
            maxChars - (it.indent + charCell - 1) / charCell;
        std::string_view remaining = it.text;
        while (!remaining.empty()) {
            size_t take = std::min(usable, remaining.size());
            drawText(remaining.substr(0, take), kPad + it.indent, y, it.color);
            remaining.remove_prefix(take);
            y += lineHeight();
        }
    }
}

} // namespace gcdump::gui
