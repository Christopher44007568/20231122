#include "record.hpp"

#include "sha256.hpp"

#include <charconv>
#include <stdexcept>

namespace offscreen {
namespace {

Event parseEvent(std::string_view line, std::size_t lineNumber) {
    const auto fields = split(line, '\t');
    if (fields.size() < 2 || fields.size() > 4) {
        throw std::runtime_error("bad record line " + std::to_string(lineNumber));
    }
    Event event;
    const std::string id = trim(fields[0]);
    const auto result = std::from_chars(id.data(), id.data() + id.size(), event.id);
    if (result.ec != std::errc() || result.ptr != id.data() + id.size()) {
        throw std::runtime_error("bad event id on line " + std::to_string(lineNumber));
    }
    event.op = fields[1];
    event.arg0 = fields.size() > 2 ? fields[2] : "";
    event.arg1 = fields.size() > 3 ? fields[3] : "";
    event.raw = std::string(line);
    event.line = lineNumber;
    return event;
}

}  // namespace

Record loadRecord(const std::filesystem::path& path) {
    const std::string text = readText(path);
    Record record;
    record.path = path;
    record.byteCount = text.size();
    record.sha256 = sha256Hex(text);

    std::size_t begin = 0;
    std::size_t lineNumber = 1;
    while (begin < text.size()) {
        const std::size_t newline = text.find('\n', begin);
        if (newline == std::string::npos) {
            record.truncatedTail = true;
            record.incompleteTail = text.substr(begin);
            record.completePrefixBytes = begin;
            break;
        }
        std::string_view line(text.data() + begin, newline - begin);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty() && line.front() != '#') {
            record.events.push_back(parseEvent(line, lineNumber));
        }
        begin = newline + 1;
        ++lineNumber;
        record.completePrefixBytes = begin;
    }
    return record;
}

std::string serializeEvent(const Event& event) {
    std::string line = std::to_string(event.id) + "\t" + event.op + "\t" +
                       event.arg0 + "\t" + event.arg1 + "\n";
    return line;
}

std::string serializeRecord(const std::vector<Event>& events) {
    std::string out =
        "# offscreen-replay-record v1\n"
        "# fields: event_id<TAB>operation<TAB>arg0<TAB>arg1\n";
    for (const Event& event : events) out += serializeEvent(event);
    return out;
}

void writeRecord(const std::filesystem::path& path,
                 const std::vector<Event>& events) {
    writeText(path, serializeRecord(events));
}

std::string makeTruncatedRecord(const std::vector<Event>& events,
                                std::size_t keepThrough,
                                std::size_t partialBytes) {
    if (keepThrough >= events.size()) {
        throw std::runtime_error("truncation point is beyond the record");
    }
    std::string out =
        "# offscreen-replay-record v1\n"
        "# fields: event_id<TAB>operation<TAB>arg0<TAB>arg1\n"
        "# deliberately truncated after the checkpoint; no final newline\n";
    for (std::size_t i = 0; i <= keepThrough; ++i) out += serializeEvent(events[i]);
    std::string partial = serializeEvent(events[keepThrough + 1]);
    if (!partial.empty() && partial.back() == '\n') partial.pop_back();
    if (partialBytes < partial.size()) partial.resize(partialBytes);
    out += partial;
    return out;
}

}  // namespace offscreen
