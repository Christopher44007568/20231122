#include "pipeline.h"
#include "codecs.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace tcpv;

namespace {

void usage() {
  std::printf(
      "tcpv - 纹理压缩质量预览（可复核流程）\n"
      "\n"
      "用法:\n"
      "  tcpv run [--texture K] [--size WxH] [--codec BC1|BC3|BC7]...\n"
      "           [--cancel-after N] [--window] [--threshold DB]\n"
      "           [--runs-dir D] [--run-id ID]\n"
      "  tcpv confirm --run ID --item N (--approve|--reject) --reviewer NAME\n"
      "  tcpv verify --run ID\n"
      "  tcpv report --run ID\n"
      "  tcpv doctor\n"
      "\n"
      "纹理: gradient | checker | noise | photo (默认 gradient)\n"
      "编解码器: BC1/BC3 (旧格式), BC7 (新格式); 默认全部\n");
}

bool parseSize(const std::string& s, uint32_t& w, uint32_t& h) {
  return std::sscanf(s.c_str(), "%ux%u", &w, &h) == 2 && w >= 4 && h >= 4;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  std::string cmd = argv[1];

  if (cmd == "doctor") return cmdDoctor();

  if (cmd == "run") {
    RunOptions opt;
    for (int i = 2; i < argc; i++) {
      std::string a = argv[i];
      auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
      if (a == "--texture") opt.texture = next();
      else if (a == "--size") {
        if (!parseSize(next(), opt.width, opt.height)) {
          std::fprintf(stderr, "bad --size, expect WxH (>=4x4)\n");
          return 2;
        }
      } else if (a == "--codec") {
        bool ok = false;
        CodecId c = codecFromName(next(), ok);
        if (!ok) {
          std::fprintf(stderr, "unknown codec\n");
          return 2;
        }
        opt.codecs.push_back(c);
      } else if (a == "--cancel-after") opt.cancelAfterDraws = std::atoi(next().c_str());
      else if (a == "--window") opt.windowed = true;
      else if (a == "--threshold") opt.psnrThreshold = std::atof(next().c_str());
      else if (a == "--runs-dir") opt.runsDir = next();
      else if (a == "--run-id") opt.runId = next();
      else {
        std::fprintf(stderr, "unknown option: %s\n", a.c_str());
        return 2;
      }
    }
    return runPreview(opt);
  }

  auto getOpt = [&](const std::string& name, const std::string& dflt) {
    for (int i = 2; i + 1 < argc; i++)
      if (argv[i] == name) return std::string(argv[i + 1]);
    return dflt;
  };
  auto hasFlag = [&](const std::string& name) {
    for (int i = 2; i < argc; i++)
      if (argv[i] == name) return true;
    return false;
  };

  if (cmd == "confirm") {
    std::string runId = getOpt("--run", "");
    int item = std::atoi(getOpt("--item", "0").c_str());
    std::string reviewer = getOpt("--reviewer", "anonymous");
    bool approve = hasFlag("--approve");
    bool reject = hasFlag("--reject");
    if (runId.empty() || item <= 0 || approve == reject) {
      std::fprintf(stderr, "confirm needs --run ID --item N and exactly one "
                           "of --approve/--reject\n");
      return 2;
    }
    return cmdConfirm(getOpt("--runs-dir", "runs"), runId, item, approve,
                      reviewer);
  }
  if (cmd == "verify") {
    std::string runId = getOpt("--run", "");
    if (runId.empty()) return 2;
    return cmdVerify(getOpt("--runs-dir", "runs"), runId);
  }
  if (cmd == "report") {
    std::string runId = getOpt("--run", "");
    if (runId.empty()) return 2;
    return cmdReport(getOpt("--runs-dir", "runs"), runId);
  }

  usage();
  return 2;
}
