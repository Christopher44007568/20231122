#include "state.hpp"

#include "sha256.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <map>
#include <sstream>
#include <stdexcept>

namespace offscreen {
namespace {

std::uint32_t parseU32(const std::string& text, const char* field) {
    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) {
        throw std::runtime_error(std::string("invalid ") + field + ": " + text);
    }
    return value;
}

std::uint32_t parseHex32(const std::string& text) {
    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(),
                                        value, 16);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) {
        throw std::runtime_error("invalid digest word: " + text);
    }
    return value;
}

std::vector<std::string> splitNonEmpty(std::string_view text, char delimiter) {
    std::vector<std::string> result;
    for (std::string part : split(text, delimiter)) {
        part = trim(part);
        if (!part.empty()) result.push_back(part);
    }
    return result;
}

std::string join(const std::vector<std::string>& values, const char* delimiter) {
    std::string result;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) result += delimiter;
        result += values[i];
    }
    return result;
}

}  // namespace

std::string VisualState::canonical() const {
    std::ostringstream out;
    out << "bg=" << colorString(bg) << ";accent=" << colorString(accent)
        << ";badge=" << badge << ";stripes=" << stripes
        << ";title=" << title << ";title_serial=" << titleSerial
        << ";submit_count=" << submitCount;
    return out.str();
}

std::string VisualState::sha256() const {
    return sha256Hex(canonical());
}

std::array<std::uint32_t, 4> VisualState::digestWords() const {
    const std::string digest = sha256();
    std::array<std::uint32_t, 4> words{};
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string part = digest.substr(i * 8, 8);
        words[i] = parseHex32(part);
    }
    return words;
}

std::string VisualState::json() const {
    std::ostringstream out;
    out << "{\"bg\":\"" << colorString(bg) << "\",\"accent\":\""
        << colorString(accent) << "\",\"badge\":" << badge
        << ",\"stripes\":" << stripes << ",\"title\":\""
        << jsonEscape(title) << "\",\"title_serial\":" << titleSerial
        << ",\"submit_count\":" << submitCount << '}';
    return out.str();
}

bool VisualState::operator==(const VisualState& other) const {
    return bg == other.bg && accent == other.accent && badge == other.badge &&
           stripes == other.stripes && titleSerial == other.titleSerial &&
           submitCount == other.submitCount && title == other.title;
}

std::string ProtocolState::json() const {
    std::ostringstream out;
    out << "{\"last_event_id\":" << lastEventId << ",\"clock\":" << clock
        << ",\"seen_events\":[";
    for (std::size_t i = 0; i < seenEvents.size(); ++i) {
        if (i != 0) out << ',';
        out << seenEvents[i];
    }
    out << "],\"submitted_tokens\":[";
    for (std::size_t i = 0; i < submittedTokens.size(); ++i) {
        if (i != 0) out << ',';
        out << '"' << jsonEscape(submittedTokens[i]) << '"';
    }
    out << "]}";
    return out.str();
}

std::string State::json() const {
    return "{\"visual\":" + visual.json() + ",\"protocol\":" + protocol.json() +
           ",\"visual_sha256\":\"" + visualHash() + "\""
           ",\"frame_input_digest\":[\"" + hex32(visual.digestWords()[0]) + "\",\"" +
           hex32(visual.digestWords()[1]) + "\",\"" +
           hex32(visual.digestWords()[2]) + "\",\"" +
           hex32(visual.digestWords()[3]) + "\"]}";
}

StateMachine::StateMachine() = default;
StateMachine::StateMachine(State state) : state_(std::move(state)) {}

StepOutcome StateMachine::apply(const Event& event) {
    StepOutcome outcome;
    if (std::find(state_.protocol.seenEvents.begin(), state_.protocol.seenEvents.end(),
                  event.id) != state_.protocol.seenEvents.end()) {
        outcome.status = "duplicate_event_dropped";
        outcome.detail = "event id was already applied; no state mutation";
        return outcome;
    }

    const VisualState before = state_.visual;
    state_.protocol.seenEvents.push_back(event.id);
    std::sort(state_.protocol.seenEvents.begin(), state_.protocol.seenEvents.end());
    state_.protocol.lastEventId = event.id;
    ++state_.protocol.clock;

    if (event.op == "PATCH") {
        if (event.arg0 == "bg") state_.visual.bg = parseColor(event.arg1);
        else if (event.arg0 == "accent") state_.visual.accent = parseColor(event.arg1);
        else throw std::runtime_error("unknown PATCH target: " + event.arg0);
        outcome.status = "applied";
        outcome.detail = "patched " + event.arg0 + " to " + event.arg1;
    } else if (event.op == "ADD") {
        const std::uint32_t amount = parseU32(event.arg1, "ADD amount");
        if (event.arg0 == "badge") state_.visual.badge += amount;
        else if (event.arg0 == "stripe") state_.visual.stripes += amount;
        else throw std::runtime_error("unknown ADD target: " + event.arg0);
        outcome.status = "applied";
        outcome.detail = "added " + std::to_string(amount) + " " + event.arg0;
    } else if (event.op == "RENAME") {
        if (event.arg0 != "title") {
            throw std::runtime_error("unknown RENAME target: " + event.arg0);
        }
        state_.visual.title = event.arg1;
        ++state_.visual.titleSerial;
        outcome.status = "applied";
        outcome.detail = "renamed title";
    } else if (event.op == "SUBMIT") {
        const auto found = std::find(state_.protocol.submittedTokens.begin(),
                                     state_.protocol.submittedTokens.end(), event.arg0);
        if (found != state_.protocol.submittedTokens.end()) {
            outcome.status = "duplicate_submit_deduped";
            outcome.detail = "submission token " + event.arg0 +
                             " was already accepted; visual state unchanged";
        } else {
            state_.protocol.submittedTokens.push_back(event.arg0);
            std::sort(state_.protocol.submittedTokens.begin(),
                      state_.protocol.submittedTokens.end());
            ++state_.visual.submitCount;
            outcome.status = "applied";
            outcome.detail = "accepted submission " + event.arg0;
        }
    } else {
        throw std::runtime_error("unknown operation: " + event.op);
    }
    outcome.visualChanged = !(before == state_.visual);
    return outcome;
}

std::string serializeCheckpoint(const State& state, const std::string& frameSha256) {
    std::ostringstream out;
    out << "format=offscreen-checkpoint/v1\n"
        << "last_event_id=" << state.protocol.lastEventId << '\n'
        << "clock=" << state.protocol.clock << '\n'
        << "bg=" << colorString(state.visual.bg).substr(1) << '\n'
        << "accent=" << colorString(state.visual.accent).substr(1) << '\n'
        << "badge=" << state.visual.badge << '\n'
        << "stripes=" << state.visual.stripes << '\n'
        << "title=" << state.visual.title << '\n'
        << "title_serial=" << state.visual.titleSerial << '\n'
        << "submit_count=" << state.visual.submitCount << '\n'
        << "seen_events=";
    for (std::size_t i = 0; i < state.protocol.seenEvents.size(); ++i) {
        if (i != 0) out << ',';
        out << state.protocol.seenEvents[i];
    }
    out << "\nsubmitted_tokens=" << join(state.protocol.submittedTokens, ",") << '\n'
        << "visual_sha256=" << state.visualHash() << '\n'
        << "protocol_sha256=" << sha256Hex(state.protocol.json()) << '\n'
        << "frame_sha256=" << frameSha256 << '\n';
    return out.str();
}

Checkpoint loadCheckpoint(const std::filesystem::path& path) {
    const std::string text = readText(path);
    std::map<std::string, std::string> values;
    for (const std::string& line : split(text, '\n')) {
        if (line.empty() || line.front() == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("invalid checkpoint line: " + line);
        }
        values[line.substr(0, equals)] = line.substr(equals + 1);
    }
    if (values.at("format") != "offscreen-checkpoint/v1") {
        throw std::runtime_error("unsupported checkpoint format");
    }
    State state;
    state.protocol.lastEventId = parseU32(values.at("last_event_id"), "last_event_id");
    state.protocol.clock = parseU32(values.at("clock"), "clock");
    state.visual.bg = parseColor(values.at("bg"));
    state.visual.accent = parseColor(values.at("accent"));
    state.visual.badge = parseU32(values.at("badge"), "badge");
    state.visual.stripes = parseU32(values.at("stripes"), "stripes");
    state.visual.title = values.at("title");
    state.visual.titleSerial = parseU32(values.at("title_serial"), "title_serial");
    state.visual.submitCount = parseU32(values.at("submit_count"), "submit_count");
    for (const std::string& id : splitNonEmpty(values.at("seen_events"), ',')) {
        state.protocol.seenEvents.push_back(parseU32(id, "seen event"));
    }
    state.protocol.submittedTokens =
        splitNonEmpty(values.at("submitted_tokens"), ',');
    if (state.visualHash() != values.at("visual_sha256")) {
        throw std::runtime_error("checkpoint visual hash verification failed");
    }
    if (sha256Hex(state.protocol.json()) != values.at("protocol_sha256")) {
        throw std::runtime_error("checkpoint protocol hash verification failed");
    }
    Checkpoint checkpoint;
    checkpoint.frameSha256 = values.at("frame_sha256");
    // Seed the machine through its public checkpoint-only constructor path.
    checkpoint.machine = StateMachine(std::move(state));
    return checkpoint;
}

}  // namespace offscreen
