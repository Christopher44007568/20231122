#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

#include "analyze.h"
#include "gpu.h"
#include "log.h"
#include "report.h"
#include "util.h"

namespace {

struct Config {
    std::string out_dir = "artifacts";
    int frames = 8;
    int crash_at = 5;    // frame whose record is truncated mid-write
    int lost_end = 6;    // frame whose end timestamp is never written
    uint64_t budget_ns = 100000000;  // 100 ms per frame
};

Config parse_args(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; i++) {
        auto need = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        std::string a = argv[i];
        if (a == "--out") c.out_dir = need("--out");
        else if (a == "--frames") c.frames = std::stoi(need("--frames"));
        else if (a == "--crash-at") c.crash_at = std::stoi(need("--crash-at"));
        else if (a == "--lost-end") c.lost_end = std::stoi(need("--lost-end"));
        else if (a == "--budget-ns") c.budget_ns = std::stoull(need("--budget-ns"));
        else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            std::exit(2);
        }
    }
    return c;
}

bool write_file(const std::string& path, const std::string& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
    return true;
}

std::string read_file(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return "";
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::string d(static_cast<size_t>(n), '\0');
    if (n > 0) std::fread(&d[0], 1, static_cast<size_t>(n), f);
    std::fclose(f);
    return d;
}

util::JsonObj sample_fields(int frame, const char* origin, const gpu::FrameSample& s) {
    util::JsonObj f;
    f["v"] = "1";
    f["kind"] = "sample";
    f["frame"] = std::to_string(frame);
    f["origin"] = origin;
    f["ts_begin"] = std::to_string(s.ts_begin);
    f["ts_end"] = std::to_string(s.ts_end);
    f["avail"] = std::to_string(s.availability);
    return f;
}

}  // namespace

int main(int argc, char** argv) {
    Config cfg = parse_args(argc, argv);
    std::string run_id = util::now_iso8601() + "-p" + std::to_string(getpid());
    std::string log_path = cfg.out_dir + "/gpu_ts.log";
    std::string evidence_dir = cfg.out_dir + "/evidence";
    std::string report_path = cfg.out_dir + "/report.html";

    std::string mkdir_cmd = "mkdir -p '" + evidence_dir + "'";
    if (std::system(mkdir_cmd.c_str()) != 0) return 1;
    std::remove(log_path.c_str());  // fresh run

    std::vector<report::Step> steps;
    gpu::GpuContext gpu;

    // P1: environment init.
    std::string err;
    if (!gpu.init(err)) {
        std::fprintf(stderr, "gpu init failed: %s\n", err.c_str());
        return 1;
    }
    steps.push_back({"P1-init", "初始化 GLFW + Vulkan",
        "GLFW 隐藏 NO_API 窗口; Vulkan 实例/设备/时间戳查询池 → 设备 " + gpu.device().name});
    std::printf("[P1] device=%s period=%.1fns validBits=%u\n", gpu.device().name.c_str(),
                gpu.device().timestamp_period_ns, gpu.device().timestamp_valid_bits);

    // P2: live sampling; the crash_at frame is only partially committed.
    tslog::RecordLog log;
    if (!log.open_append(log_path)) {
        std::fprintf(stderr, "cannot open log %s\n", log_path.c_str());
        return 1;
    }
    util::JsonObj meta;
    meta["v"] = "1";
    meta["kind"] = "meta";
    meta["run_id"] = run_id;
    meta["device"] = gpu.device().name;
    meta["ts_period_ns"] = std::to_string(gpu.device().timestamp_period_ns);
    meta["ts_valid_bits"] = std::to_string(gpu.device().timestamp_valid_bits);
    meta["budget_ns"] = std::to_string(cfg.budget_ns);
    log.append(meta);

    bool crashed = false;
    for (int frame = 0; frame < cfg.frames; frame++) {
        gpu::FrameSample s;
        if (!gpu.sample_frame(frame == cfg.lost_end, s, err)) {
            std::fprintf(stderr, "sample failed: %s\n", err.c_str());
            return 1;
        }
        if (frame == cfg.crash_at) {
            // Crash mid-write: only a prefix of the record reaches disk.
            util::JsonObj f = sample_fields(frame, "live", s);
            f["seq"] = std::to_string(log.next_seq());
            f["prev"] = log.head_hash();
            f["hash"] = tslog::RecordLog::chain_hash(log.head_hash(), f);
            size_t full = util::json_build(f).size() + 1;
            uint64_t at = log.append_partial(sample_fields(frame, "live", s), full * 2 / 5);
            std::printf("[P2] CRASH simulated at frame %d, offset %llu (%zu/%zu bytes)\n",
                        frame, (unsigned long long)at, full * 2 / 5, full);
            crashed = true;
            break;
        }
        log.append(sample_fields(frame, "live", s));
    }
    log.close();
    steps.push_back({"P2-sample", "实时采样并追加日志",
        "vkCmdWriteTimestamp 夹住 GPU 拷贝工作 → 追加记录到 " + log_path +
        (crashed ? "; 最后一帧写入中途崩溃(部分字节落盘)" : "")});

    // P4: recovery scan — locate the first bad tail, preserve it as evidence,
    // then roll the log back to the last committed record.
    analyze::TruncationEvent truncation;
    tslog::ScanResult scan = tslog::RecordLog::scan(log_path);
    if (scan.truncated) {
        truncation.present = true;
        truncation.byte_offset = scan.bad_offset;
        truncation.salvaged_bytes = scan.bad_bytes;
        truncation.reason = scan.bad_reason;
        truncation.expected_seq = scan.next_seq;
        truncation.evidence_file = evidence_dir + "/truncated_tail.bin";
        write_file(truncation.evidence_file, scan.bad_bytes);
        util::JsonObj evt;
        evt["event"] = "truncation";
        evt["offset"] = std::to_string(scan.bad_offset);
        evt["expected_seq"] = std::to_string(scan.next_seq);
        evt["reason"] = scan.bad_reason;
        evt["salvaged_len"] = std::to_string(scan.bad_bytes.size());
        evt["salvaged_fnv"] = util::hex16(util::fnv1a64(scan.bad_bytes));
        write_file(evidence_dir + "/truncation_event.json", util::json_build(evt) + "\n");
        if (::truncate(log_path.c_str(), static_cast<off_t>(scan.bad_offset)) != 0) {
            std::fprintf(stderr, "truncate failed\n");
            return 1;
        }
        std::printf("[P4] truncation at offset %llu: %s (%zu salvaged bytes)\n",
                    (unsigned long long)scan.bad_offset, scan.bad_reason.c_str(),
                    scan.bad_bytes.size());
    }
    steps.push_back({"P4-recover-scan", "恢复扫描与截断",
        "逐行校验结构+哈希链 → 首个坏尾偏移 " +
        (truncation.present ? std::to_string(truncation.byte_offset) : "无") +
        "; 坏尾字节打捞到 evidence/ 并截断日志到最后一条已提交记录"});

    // P5: replay the frames lost to the crash, tagged as replay origin.
    if (truncation.present) {
        tslog::RecordLog rlog;
        if (!rlog.open_append(log_path)) return 1;
        util::JsonObj note;
        note["v"] = "1";
        note["kind"] = "replay-note";
        note["note"] = "replay frames " + std::to_string(cfg.crash_at) + ".." +
                       std::to_string(cfg.frames - 1) + " after truncation@" +
                       std::to_string(truncation.byte_offset);
        rlog.append(note);
        for (int frame = cfg.crash_at; frame < cfg.frames; frame++) {
            gpu::FrameSample s;
            if (!gpu.sample_frame(frame == cfg.lost_end, s, err)) {
                std::fprintf(stderr, "replay sample failed: %s\n", err.c_str());
                return 1;
            }
            util::JsonObj f = sample_fields(frame, "replay", s);
            f["replays"] = "truncation@" + std::to_string(truncation.byte_offset);
            rlog.append(f);
        }
        rlog.close();
        std::printf("[P5] replayed frames %d..%d\n", cfg.crash_at, cfg.frames - 1);
    }
    steps.push_back({"P5-replay", "恢复后重放",
        truncation.present
            ? "重跑丢失帧的 GPU 采样, 以 origin=replay 追加, 并回链截断事件"
            : "无截断, 跳过重放"});
    gpu.shutdown();

    // P6: analyze the recovered stream.
    tslog::ScanResult final_scan = tslog::RecordLog::scan(log_path);
    if (final_scan.truncated) {
        std::fprintf(stderr, "log still corrupt after recovery: %s\n",
                     final_scan.bad_reason.c_str());
        return 1;
    }
    analyze::Analysis analysis = analyze::run(final_scan.good, truncation,
                                              gpu.device().timestamp_period_ns, cfg.budget_ns);
    steps.push_back({"P6-validate", "逐条判定(状态机)",
        "按字节流顺序检查 可用性→单调性→预算; 输出每条结论与首个错误状态"});

    // P7: render the inspection page.
    steps.push_back({"P7-report", "生成检查页", "结论+证据+步骤 → " + report_path});
    report::Data rd;
    rd.run_id = run_id;
    rd.generated_at = util::now_iso8601();
    rd.device = gpu.device();
    rd.budget_ns = cfg.budget_ns;
    rd.log_path = log_path;
    rd.log_sha = util::hex16(util::fnv1a64(read_file(log_path)));
    rd.truncation = truncation;
    rd.truncation_evidence_path = truncation.evidence_file;
    rd.truncation_salvaged_hex = util::hex_dump(truncation.salvaged_bytes);
    rd.analysis = analysis;
    rd.records = final_scan.good;
    rd.steps = steps;
    write_file(report_path, report::render_html(rd));

    std::printf("[P7] report: %s\n", report_path.c_str());
    if (analysis.first_error_index >= 0) {
        const auto& c = analysis.conclusions[analysis.first_error_index];
        std::printf("首个错误状态: C%d seq=%llu offset=%llu %s\n", c.id,
                    (unsigned long long)c.seq, (unsigned long long)c.byte_offset,
                    c.subject.c_str());
    } else {
        std::printf("无错误状态\n");
    }
    std::printf("OK=%zu ERROR=%zu UNDECIDABLE=%zu\n", analysis.count_ok,
                analysis.count_error, analysis.count_undecidable);
    return 0;
}
