#include "experiment.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>

namespace fs = std::filesystem;
using journal::join;
using journal::kv;

std::string aggregateStateHash(const ChunkMap& chunks, uint64_t totalBytes) {
    std::string acc;
    for (const auto& entry : chunks) {
        acc += std::to_string(entry.first.first) + "," + std::to_string(entry.first.second)
             + ":" + entry.second + ";";
    }
    acc += "#" + std::to_string(chunks.size()) + "/" + std::to_string(totalBytes);
    return terrain::hex64(terrain::fnv1a64(acc.data(), acc.size()));
}

TerrainLoader::TerrainLoader(VkContext& vk, terrain::Snapshot snap, uint32_t seed)
    : vk_(vk), snap_(snap), seed_(seed) {}

bool TerrainLoader::isLoaded(int cx, int cy) const {
    return chunks_.count({cx, cy}) != 0;
}

std::string TerrainLoader::stateHash() const {
    return aggregateStateHash(chunks_, totalBytes_);
}

std::string TerrainLoader::loadChunk(int cx, int cy, const std::string& tmpDir) {
    const auto heights = terrain::generateHeightmap(seed_, snap_, cx, cy);
    const size_t bytes = heights.size() * sizeof(float);
    const std::string dataHash = terrain::hex64(terrain::fnv1a64(heights.data(), bytes));
    // temp raw dump: transient state, removed during end-of-run cleanup
    if (!tmpDir.empty()) {
        const std::string p = tmpDir + "/chunk_" + std::to_string(cx) + "_" + std::to_string(cy) + ".raw";
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(heights.data()), static_cast<std::streamsize>(bytes));
    }
    const size_t slot = vk_.upload(heights.data(), bytes);
    const auto back = vk_.readback(slot);
    const std::string readbackHash = terrain::hex64(terrain::fnv1a64(back.data(), back.size()));
    if (readbackHash != dataHash)
        throw VkException(VK_ERROR_UNKNOWN, "readback hash mismatch for chunk");
    chunks_[{cx, cy}] = dataHash;
    totalBytes_ += bytes;
    return dataHash;
}

namespace {

std::string stateJson(const TerrainLoader& loader) {
    return "{" + join({kv("loaded", static_cast<long long>(loader.loadedCount())),
                       kv("bytes", static_cast<unsigned long long>(loader.totalBytes())),
                       kv("state_hash", loader.stateHash())}) + "}";
}

void writeStep(Journal& j, long long& seq, const std::string& step,
               const std::string& input, const std::string& stateBefore,
               const std::string& result, const std::string& stateAfter,
               bool ok, const std::string& error) {
    j.writeLine("{" + join({
        kv("seq", seq++),
        kv("step", step),
        "\"input\":" + input,
        "\"state_before\":" + stateBefore,
        "\"result\":" + result,
        "\"state_after\":" + stateAfter,
        kv("ok", ok),
        kv("error", error)}) + "}");
}

std::vector<std::pair<int, int>> buildOrder(const RunConfig& cfg) {
    std::vector<std::pair<int, int>> order;
    for (int cy = 0; cy < cfg.gridH; ++cy)
        for (int cx = 0; cx < cfg.gridW; ++cx)
            order.emplace_back(cx, cy);
    if (cfg.order == "shuffled") {
        std::mt19937 rng(cfg.seed ^ 0x5EEDu);
        std::shuffle(order.begin(), order.end(), rng);
    }
    return order;
}

} // namespace

RunResult runLoading(VkContext& vk, const RunConfig& cfg) {
    RunResult rr;
    fs::create_directories(cfg.runDir);
    const std::string tmpDir = cfg.runDir + "/tmp";
    fs::create_directories(tmpDir);
    Journal j;
    if (!j.open(cfg.runDir + "/journal.jsonl"))
        throw std::runtime_error("cannot open journal in " + cfg.runDir);
    long long seq = 0;
    TerrainLoader loader(vk, cfg.snapshot, cfg.seed);

    writeStep(j, seq, "run_begin",
              "{" + join({kv("snapshot", terrain::snapshotName(cfg.snapshot)),
                          kv("order", cfg.order),
                          kv("seed", static_cast<unsigned long long>(cfg.seed)),
                          kv("grid_w", static_cast<long long>(cfg.gridW)),
                          kv("grid_h", static_cast<long long>(cfg.gridH))}) + "}",
              stateJson(loader), "{}", stateJson(loader), true, "");

    const auto order = buildOrder(cfg);
    for (size_t i = 0; i < order.size(); ++i) {
        const auto [cx, cy] = order[i];
        // Fault injection: one chunk first attempts to load through a device
        // created with an unknown extension. The failure is recorded, then
        // processing continues with the remaining objects.
        if (static_cast<int>(i) == cfg.poisonIndex) {
            const std::string before = stateJson(loader);
            std::string err;
            bool ok = false;
            try {
                VkContext probe;
                probe.loadGlobal();
                probe.createInstance({});
                probe.pickPhysicalDevice();
                probe.createDevice({kUnknownExtension});
                ok = true; // unexpected: extension actually exists
            } catch (const VkException& e) {
                err = e.what();
            }
            if (ok) err = "unexpected_success";
            ++rr.exceptions;
            writeStep(j, seq, "chunk_load_ext_probe",
                      "{" + join({kv("cx", static_cast<long long>(cx)),
                                  kv("cy", static_cast<long long>(cy)),
                                  kv("extension", kUnknownExtension)}) + "}",
                      before,
                      "{" + kv("status", ok ? "unexpected_success" : "exception_caught") + "}",
                      stateJson(loader), ok, ok ? "" : err);
        }
        const std::string before = stateJson(loader);
        std::string dataHash;
        try {
            dataHash = loader.loadChunk(cx, cy, tmpDir);
        } catch (const VkException& e) {
            ++rr.exceptions;
            writeStep(j, seq, "chunk_load",
                      "{" + join({kv("cx", static_cast<long long>(cx)),
                                  kv("cy", static_cast<long long>(cy)),
                                  kv("order_index", static_cast<long long>(i))}) + "}",
                      before, "{" + kv("status", "failed") + "}",
                      stateJson(loader), false, e.what());
            continue; // exception: skip this object, keep processing the rest
        }
        writeStep(j, seq, "chunk_load",
                  "{" + join({kv("snapshot", terrain::snapshotName(cfg.snapshot)),
                              kv("seed", static_cast<unsigned long long>(cfg.seed)),
                              kv("cx", static_cast<long long>(cx)),
                              kv("cy", static_cast<long long>(cy)),
                              kv("order_index", static_cast<long long>(i))}) + "}",
                  before,
                  "{" + join({kv("status", "ok"),
                              kv("data_hash", dataHash)}) + "}",
                  stateJson(loader), true, "");
    }

    // Repeated submission of an already-loaded chunk must be a no-op.
    {
        const auto [cx, cy] = order.front();
        const std::string before = stateJson(loader);
        std::string status = "duplicate_suppressed";
        if (!loader.isLoaded(cx, cy)) {
            loader.loadChunk(cx, cy, tmpDir);
            status = "unexpected_reload";
        }
        const std::string after = stateJson(loader);
        ++rr.duplicates;
        writeStep(j, seq, "chunk_load_repeat",
                  "{" + join({kv("cx", static_cast<long long>(cx)),
                              kv("cy", static_cast<long long>(cy)),
                              kv("attempt", 2LL)}) + "}",
                  before, "{" + kv("status", status) + "}", after,
                  before == after && status == "duplicate_suppressed", "");
    }

    // Repeated Vulkan upload of identical bytes must hash identically.
    {
        const auto [cx, cy] = order.front();
        const auto heights = terrain::generateHeightmap(cfg.seed, cfg.snapshot, cx, cy);
        const size_t bytes = heights.size() * sizeof(float);
        const size_t slot = vk.upload(heights.data(), bytes);
        const auto back = vk.readback(slot);
        const std::string h = terrain::hex64(terrain::fnv1a64(back.data(), back.size()));
        const bool match = h == loader.chunks().at({cx, cy});
        writeStep(j, seq, "repeated_upload_verify",
                  "{" + join({kv("cx", static_cast<long long>(cx)),
                              kv("cy", static_cast<long long>(cy))}) + "}",
                  stateJson(loader),
                  "{" + join({kv("status", match ? "hash_match" : "hash_mismatch"),
                              kv("data_hash", h)}) + "}",
                  stateJson(loader), match, "");
    }

    rr.finalStateHash = loader.stateHash();
    rr.totalBytes = loader.totalBytes();
    rr.loaded = loader.loadedCount();
    writeStep(j, seq, "run_end", "{}", stateJson(loader),
              "{" + join({kv("loaded", static_cast<long long>(rr.loaded)),
                          kv("bytes", static_cast<unsigned long long>(rr.totalBytes))}) + "}",
              stateJson(loader), true, "");

    // End-of-run cleanup: remove transient raw dumps and verify removal.
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
    rr.cleanupOk = !ec && !fs::exists(tmpDir);
    writeStep(j, seq, "cleanup",
              "{" + kv("tmp_dir", tmpDir) + "}", stateJson(loader),
              "{" + kv("tmp_dir_removed", rr.cleanupOk) + "}",
              stateJson(loader), rr.cleanupOk, ec ? ec.message() : "");
    j.close();
    return rr;
}

int replayJournal(VkContext& vk, const std::string& journalPath, const std::string& reportPath) {
    const auto lines = journal::readLines(journalPath);
    ChunkMap chunks;
    uint64_t totalBytes = 0;
    int checked = 0;
    int mismatched = 0;
    std::string recordedFinal;
    for (const auto& line : lines) {
        std::string step;
        if (!journal::extractString(line, "step", step)) continue;
        if (step == "run_end") {
            journal::extractString(line, "state_hash", recordedFinal);
            continue;
        }
        if (step != "chunk_load") continue;
        std::string status;
        if (!journal::extractString(line, "status", status) || status != "ok") continue;
        std::string snapName, recordedHash;
        long long seed = 0, cx = 0, cy = 0;
        journal::extractString(line, "snapshot", snapName);
        journal::extractInt(line, "seed", seed);
        journal::extractInt(line, "cx", cx);
        journal::extractInt(line, "cy", cy);
        journal::extractString(line, "data_hash", recordedHash);
        const auto heights = terrain::generateHeightmap(
            static_cast<uint32_t>(seed), terrain::snapshotFromName(snapName),
            static_cast<int>(cx), static_cast<int>(cy));
        const size_t bytes = heights.size() * sizeof(float);
        const size_t slot = vk.upload(heights.data(), bytes);
        const auto back = vk.readback(slot);
        const std::string recomputed = terrain::hex64(terrain::fnv1a64(back.data(), back.size()));
        ++checked;
        if (recomputed != recordedHash) ++mismatched;
        chunks[{static_cast<int>(cx), static_cast<int>(cy)}] = recomputed;
        totalBytes += bytes;
    }
    const std::string recomputedFinal = aggregateStateHash(chunks, totalBytes);
    const bool finalOk = !recordedFinal.empty() && recomputedFinal == recordedFinal;
    if (!finalOk) ++mismatched;
    std::ofstream rep(reportPath);
    rep << "{" << join({
        kv("journal", journalPath),
        kv("checked", static_cast<long long>(checked)),
        kv("mismatched", static_cast<long long>(mismatched)),
        kv("recorded_final_state_hash", recordedFinal),
        kv("recomputed_final_state_hash", recomputedFinal),
        kv("final_state_match", finalOk),
        kv("ok", mismatched == 0 && checked > 0)}) << "}\n";
    return mismatched;
}
