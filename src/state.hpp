#pragma once

#include "record.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace offscreen {

struct VisualState {
    std::uint32_t bg = 0x101820;
    std::uint32_t accent = 0xe0a020;
    std::uint32_t badge = 0;
    std::uint32_t stripes = 0;
    std::uint32_t titleSerial = 0;
    std::uint32_t submitCount = 0;
    std::string title;

    std::string canonical() const;
    std::string sha256() const;
    std::array<std::uint32_t, 4> digestWords() const;
    std::string json() const;
    bool operator==(const VisualState& other) const;
};

struct ProtocolState {
    std::uint32_t lastEventId = 0xffffffffu;
    std::uint32_t clock = 0;
    std::vector<std::uint32_t> seenEvents;
    std::vector<std::string> submittedTokens;

    std::string json() const;
};

struct State {
    VisualState visual;
    ProtocolState protocol;

    std::string visualHash() const { return visual.sha256(); }
    std::string json() const;
};

struct StepOutcome {
    std::string status;
    std::string detail;
    bool visualChanged = false;
};

class StateMachine {
public:
    StateMachine();
    explicit StateMachine(State state);

    StepOutcome apply(const Event& event);
    const State& state() const { return state_; }
    State snapshot() const { return state_; }

private:
    State state_;
};

struct Checkpoint {
    StateMachine machine;
    std::string frameSha256;
};

std::string serializeCheckpoint(const State& state, const std::string& frameSha256);
Checkpoint loadCheckpoint(const std::filesystem::path& path);

}  // namespace offscreen
