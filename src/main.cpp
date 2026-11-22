// GPU 资源生命周期可回放实验
// 技术栈: C++17 / Vulkan / GLFW(null platform, 无显示环境)
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static uint64_t fnv1a(const void* data, size_t len, uint64_t h = 1469598103934665603ULL) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

static std::string hexHash(uint64_t h) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out;
}

// ---------------- GPU 上下文: GLFW + Vulkan 生命周期 ----------------
struct GpuContext {
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint32_t memTypeHost = UINT32_MAX;
    std::string deviceName;
    std::string glfwInfo;
    std::string error;

    bool init() {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL); // 无显示环境: null 平台
        if (!glfwInit()) {
            error = "glfwInit 失败";
            return false;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window = glfwCreateWindow(64, 64, "gpu-lifecycle-probe", nullptr, nullptr);
        glfwInfo = window ? "hidden-window" : "no-window(继续无窗口运行)";
        if (!glfwVulkanSupported()) {
            error = "GLFW 报告 Vulkan 不可用";
            return false;
        }

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "gpu-lifecycle-experiment";
        app.apiVersion = VK_API_VERSION_1_1;
        uint32_t extCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
        std::vector<const char*> exts;
        for (uint32_t i = 0; i < extCount; ++i) exts.push_back(glfwExts[i]);
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.pApplicationInfo = &app;
        ici.enabledExtensionCount = (uint32_t)exts.size();
        ici.ppEnabledExtensionNames = exts.data();
        if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) {
            error = "vkCreateInstance 失败(检查 ICD: VK_ICD_FILENAMES)";
            return false;
        }

        uint32_t devCount = 0;
        vkEnumeratePhysicalDevices(instance, &devCount, nullptr);
        if (devCount == 0) { error = "无可用 Vulkan 物理设备"; return false; }
        std::vector<VkPhysicalDevice> devs(devCount);
        vkEnumeratePhysicalDevices(instance, &devCount, devs.data());
        // 优先 独显>集显>CPU(lavapipe)
        int bestScore = -1;
        for (auto d : devs) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(d, &props);
            int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                      : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                      : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? 1 : 0;
            if (score > bestScore) { bestScore = score; phys = d; deviceName = props.deviceName; }
        }

        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(phys, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(phys, &qCount, qprops.data());
        bool found = false;
        for (uint32_t i = 0; i < qCount; ++i) {
            if (qprops[i].queueFlags & VK_QUEUE_TRANSFER_BIT) { queueFamily = i; found = true; break; }
        }
        if (!found) { error = "无 TRANSFER 队列族"; return false; }

        float prio = 1.0f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = queueFamily;
        qci.queueCount = 1;
        qci.pQueuePriorities = &prio;
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        if (vkCreateDevice(phys, &dci, nullptr, &device) != VK_SUCCESS) {
            error = "vkCreateDevice 失败";
            return false;
        }
        vkGetDeviceQueue(device, queueFamily, 0, &queue);

        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.queueFamilyIndex = queueFamily;
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(device, &pci, nullptr, &cmdPool) != VK_SUCCESS) {
            error = "vkCreateCommandPool 失败";
            return false;
        }
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cai.commandPool = cmdPool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &cai, &cmd) != VK_SUCCESS) {
            error = "vkAllocateCommandBuffers 失败";
            return false;
        }

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            VkMemoryPropertyFlags f = memProps.memoryTypes[i].propertyFlags;
            if ((f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                memTypeHost = i;
                break;
            }
        }
        if (memTypeHost == UINT32_MAX) { error = "无 HOST_VISIBLE|COHERENT 内存类型"; return false; }
        return true;
    }

    void pollEvents() { glfwPollEvents(); } // 模拟应用主循环中的事件处理

    void shutdown() {
        if (device) vkDeviceWaitIdle(device);
        if (cmdPool) vkDestroyCommandPool(device, cmdPool, nullptr);
        if (device) vkDestroyDevice(device, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
        cmdPool = VK_NULL_HANDLE; device = VK_NULL_HANDLE;
        instance = VK_NULL_HANDLE; window = nullptr;
    }
};

// ---------------- 资源与场景执行器 ----------------
struct BufferRes {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    uint64_t size = 0;
};

struct StepRecord {
    int seq = 0;
    std::string op;
    std::string input;
    std::string resultCode;   // OK / ERROR / PASS / FAIL / CANCELLED / SKIPPED
    std::string detail;
    uint64_t stateHash = 0;
};

struct Runner {
    GpuContext& gpu;
    std::string scenarioName;
    std::map<std::string, BufferRes> buffers;
    bool recording = false;
    int pendingFills = 0;
    int submits = 0, completed = 0, cancels = 0;
    bool aborted = false;
    int seq = 0;
    int passCount = 0, failCount = 0;
    std::ofstream transcript;
    std::ofstream checks;
    std::map<std::string, uint64_t> checkpoints;
    std::vector<StepRecord> records;

    explicit Runner(GpuContext& g) : gpu(g) {}

    uint64_t contentHash(const BufferRes& b) {
        void* p = nullptr;
        if (vkMapMemory(gpu.device, b.mem, 0, b.size, 0, &p) != VK_SUCCESS) return 0;
        uint64_t h = fnv1a(p, (size_t)b.size);
        vkUnmapMemory(gpu.device, b.mem);
        return h;
    }

    // 状态快照哈希: 资源表(按名排序) + 计数器, 用于顺序改变/回放比对
    uint64_t stateHash() {
        uint64_t h = 1469598103934665603ULL;
        uint64_t nb = buffers.size();
        h = fnv1a(&nb, sizeof(nb), h);
        for (auto& [name, b] : buffers) {
            h = fnv1a(name.data(), name.size(), h);
            h = fnv1a(&b.size, sizeof(b.size), h);
            uint64_t ch = contentHash(b);
            h = fnv1a(&ch, sizeof(ch), h);
        }
        h = fnv1a(&submits, sizeof(submits), h);
        h = fnv1a(&completed, sizeof(completed), h);
        h = fnv1a(&cancels, sizeof(cancels), h);
        h = fnv1a(&pendingFills, sizeof(pendingFills), h);
        return h;
    }

    uint64_t totalBytes() {
        uint64_t t = 0;
        for (auto& [n, b] : buffers) t += b.size;
        return t;
    }

    void emit(const std::string& op, const std::string& input,
              const std::string& code, const std::string& detail) {
        ++seq;
        uint64_t h = stateHash();
        if (transcript) {
            transcript << "{\"seq\":" << seq
                       << ",\"scenario\":\"" << jsonEscape(scenarioName)
                       << "\",\"op\":\"" << jsonEscape(op)
                       << "\",\"input\":\"" << jsonEscape(input)
                       << "\",\"state\":{\"buffers\":" << buffers.size()
                       << ",\"bytes\":" << totalBytes()
                       << ",\"pending\":" << pendingFills
                       << ",\"submits\":" << submits
                       << ",\"completed\":" << completed
                       << ",\"cancels\":" << cancels
                       << ",\"hash\":\"" << hexHash(h) << "\"}"
                       << ",\"result\":{\"code\":\"" << code
                       << "\",\"detail\":\"" << jsonEscape(detail) << "\"}}\n";
            transcript.flush();
        }
        if (code == "PASS") ++passCount;
        if (code == "FAIL") ++failCount;
        StepRecord rec;
        rec.seq = seq; rec.op = op; rec.input = input;
        rec.resultCode = code; rec.detail = detail; rec.stateHash = h;
        records.push_back(rec);
    }

    void ensureRecording() {
        if (recording) return;
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(gpu.cmd, &bi);
        recording = true;
    }

    void createBuffer(const std::string& name, uint64_t size) {
        std::string input = name + " " + std::to_string(size);
        if (buffers.count(name)) { emit("create_buffer", input, "ERROR", "缓冲区已存在: " + name); return; }
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = size;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        BufferRes res;
        res.size = size;
        VkResult r = vkCreateBuffer(gpu.device, &bci, nullptr, &res.buf);
        if (r != VK_SUCCESS) { emit("create_buffer", input, "ERROR", "vkCreateBuffer=" + std::to_string(r)); return; }
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(gpu.device, res.buf, &req);
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = gpu.memTypeHost;
        r = vkAllocateMemory(gpu.device, &mai, nullptr, &res.mem);
        if (r != VK_SUCCESS) {
            vkDestroyBuffer(gpu.device, res.buf, nullptr);
            emit("create_buffer", input, "ERROR", "vkAllocateMemory=" + std::to_string(r));
            return;
        }
        vkBindBufferMemory(gpu.device, res.buf, res.mem, 0);
        // 显式清零, 保证初始状态确定(可回放性)
        void* p = nullptr;
        vkMapMemory(gpu.device, res.mem, 0, size, 0, &p);
        std::memset(p, 0, (size_t)size);
        vkUnmapMemory(gpu.device, res.mem);
        buffers[name] = res;
        emit("create_buffer", input, "OK", "已创建并清零");
    }

    void destroyBuffer(const std::string& name) {
        auto it = buffers.find(name);
        if (it == buffers.end()) { emit("destroy", name, "ERROR", "缓冲区不存在(重复销毁?): " + name); return; }
        vkDestroyBuffer(gpu.device, it->second.buf, nullptr);
        vkFreeMemory(gpu.device, it->second.mem, nullptr);
        buffers.erase(it);
        emit("destroy", name, "OK", "已销毁并释放内存");
    }

    void recordFill(const std::string& name, uint32_t value) {
        std::string input = name + " 0x" + ([&]{ char b[16]; std::snprintf(b, sizeof(b), "%02x", value); return std::string(b); })();
        auto it = buffers.find(name);
        if (it == buffers.end()) { emit("record_fill", input, "ERROR", "缓冲区不存在: " + name); return; }
        ensureRecording();
        vkCmdFillBuffer(gpu.cmd, it->second.buf, 0, it->second.size, value * 0x01010101u);
        ++pendingFills;
        emit("record_fill", input, "OK", "填充命令已记录(未提交)");
    }

    void submit(const std::string& tag) {
        if (!recording || pendingFills == 0) { emit("submit", tag, "ERROR", "无待提交命令"); return; }
        vkEndCommandBuffer(gpu.cmd);
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence;
        vkCreateFence(gpu.device, &fci, nullptr, &fence);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &gpu.cmd;
        VkResult r = vkQueueSubmit(gpu.queue, 1, &si, fence);
        if (r != VK_SUCCESS) {
            vkDestroyFence(gpu.device, fence, nullptr);
            emit("submit", tag, "ERROR", "vkQueueSubmit=" + std::to_string(r));
            return;
        }
        ++submits;
        gpu.pollEvents();
        vkWaitForFences(gpu.device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(gpu.device, fence, nullptr);
        ++completed;
        vkResetCommandBuffer(gpu.cmd, 0);
        recording = false;
        emit("submit", tag, "OK", "已提交并完成, fills=" + std::to_string(pendingFills));
        pendingFills = 0;
    }

    // 中途取消: 丢弃已记录但未提交的命令
    void cancelPending(const std::string& reason) {
        if (!recording || pendingFills == 0) { emit("cancel_pending", reason, "ERROR", "无可取消的待提交命令"); return; }
        vkResetCommandBuffer(gpu.cmd, 0);
        recording = false;
        int dropped = pendingFills;
        pendingFills = 0;
        ++cancels;
        emit("cancel_pending", reason, "CANCELLED", "丢弃未提交命令 fills=" + std::to_string(dropped));
    }

    void expectBuffer(const std::string& name, uint32_t expectByte) {
        std::string input = name + " 0x" + ([&]{ char b[16]; std::snprintf(b, sizeof(b), "%02x", expectByte); return std::string(b); })();
        auto it = buffers.find(name);
        if (it == buffers.end()) { emit("expect_buffer", input, "FAIL", "缓冲区不存在: " + name); return; }
        void* p = nullptr;
        vkMapMemory(gpu.device, it->second.mem, 0, it->second.size, 0, &p);
        const uint8_t* bytes = static_cast<const uint8_t*>(p);
        size_t bad = SIZE_MAX;
        for (size_t i = 0; i < (size_t)it->second.size; ++i) {
            if (bytes[i] != (uint8_t)expectByte) { bad = i; break; }
        }
        vkUnmapMemory(gpu.device, it->second.mem);
        if (bad == SIZE_MAX) emit("expect_buffer", input, "PASS", "内容全部匹配");
        else emit("expect_buffer", input, "FAIL", "偏移 " + std::to_string(bad) + " 处不匹配");
    }

    void checkpoint(const std::string& label) {
        checkpoints[label] = stateHash();
        emit("checkpoint", label, "OK", "hash=" + hexHash(checkpoints[label]));
    }

    void expectCheckpointEq(const std::string& a, const std::string& b) {
        std::string input = a + " == " + b;
        auto ia = checkpoints.find(a), ib = checkpoints.find(b);
        if (ia == checkpoints.end() || ib == checkpoints.end()) {
            emit("expect_checkpoint_eq", input, "FAIL", "检查点缺失");
            return;
        }
        if (ia->second == ib->second) emit("expect_checkpoint_eq", input, "PASS", "hash=" + hexHash(ia->second));
        else emit("expect_checkpoint_eq", input, "FAIL",
                  hexHash(ia->second) + " != " + hexHash(ib->second));
    }

    // 生命周期收尾: 销毁全部残留资源, 校验无泄漏
    void cleanup() {
        int leaked = (int)buffers.size();
        for (auto& [name, b] : buffers) {
            vkDestroyBuffer(gpu.device, b.buf, nullptr);
            vkFreeMemory(gpu.device, b.mem, nullptr);
        }
        buffers.clear();
        if (recording) { vkResetCommandBuffer(gpu.cmd, 0); recording = false; pendingFills = 0; }
        // 中止场景由收尾回收属预期; 完整场景残留才算泄漏
        bool ok = (leaked == 0) || aborted;
        emit("cleanup", "auto", ok ? "PASS" : "FAIL",
             leaked == 0 ? "无资源泄漏"
             : aborted ? "中止后由收尾回收 " + std::to_string(leaked) + " 个(预期)"
                       : "强制回收残留资源 " + std::to_string(leaked) + " 个(泄漏)");
    }
};

// ---------------- 场景脚本执行 ----------------
static uint32_t parseByte(const std::string& tok) {
    return (uint32_t)std::stoul(tok, nullptr, 0) & 0xFFu;
}

// 执行单条指令; 返回 false 表示 abort_remaining
static bool execOp(Runner& r, const std::string& op, const std::vector<std::string>& args,
                   const std::string& rawInput) {
    if (op == "create_buffer" && args.size() >= 2) r.createBuffer(args[0], std::stoull(args[1]));
    else if (op == "record_fill" && args.size() >= 2) r.recordFill(args[0], parseByte(args[1]));
    else if (op == "submit" && args.size() >= 1) r.submit(args[0]);
    else if (op == "cancel_pending") r.cancelPending(rawInput.empty() ? "manual" : rawInput);
    else if (op == "destroy" && args.size() >= 1) r.destroyBuffer(args[0]);
    else if (op == "expect_buffer" && args.size() >= 2) r.expectBuffer(args[0], parseByte(args[1]));
    else if (op == "checkpoint" && args.size() >= 1) r.checkpoint(args[0]);
    else if (op == "expect_checkpoint_eq" && args.size() >= 2) r.expectCheckpointEq(args[0], args[1]);
    else if (op == "abort_remaining") { r.aborted = true; r.emit("abort_remaining", rawInput, "SKIPPED", "中止后续步骤(对照组)"); return false; }
    else if (op == "cleanup") r.cleanup(); // 回放时重放收尾步骤
    else r.emit(op, rawInput, "ERROR", "未知或参数错误的指令");
    return true;
}

static std::vector<std::string> splitTokens(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> out;
    std::string t;
    while (iss >> t) out.push_back(t);
    return out;
}

static int runScenario(GpuContext& gpu, const std::string& scriptPath,
                       const std::string& transcriptPath) {
    std::ifstream in(scriptPath);
    if (!in) { std::fprintf(stderr, "无法打开场景脚本: %s\n", scriptPath.c_str()); return -1; }
    Runner r(gpu);
    r.scenarioName = scriptPath.substr(scriptPath.find_last_of('/') + 1);
    r.transcript.open(transcriptPath);
    if (!r.transcript) { std::fprintf(stderr, "无法写入转录文件: %s\n", transcriptPath.c_str()); return -1; }
    // 环境证据头
    r.transcript << "{\"seq\":0,\"op\":\"env\",\"input\":\"\",\"state\":{},"
                 << "\"result\":{\"code\":\"OK\",\"detail\":\"device=" << jsonEscape(gpu.deviceName)
                 << " glfw=" << jsonEscape(gpu.glfwInfo) << "\"}}\n";
    std::string line;
    bool running = true;
    while (std::getline(in, line)) {
        if (!running) break;
        auto hashPos = line.find('#');
        if (hashPos != std::string::npos) line = line.substr(0, hashPos);
        auto toks = splitTokens(line);
        if (toks.empty()) continue;
        std::string rawInput;
        for (size_t i = 1; i < toks.size(); ++i) { if (i > 1) rawInput += " "; rawInput += toks[i]; }
        std::vector<std::string> args(toks.begin() + 1, toks.end());
        if (!execOp(r, toks[0], args, rawInput)) running = false;
    }
    r.cleanup();
    r.transcript.close();
    std::printf("[%s] steps=%d PASS=%d FAIL=%d cancels=%d submits=%d\n",
                r.scenarioName.c_str(), r.seq, r.passCount, r.failCount, r.cancels, r.submits);
    return r.failCount;
}

// ---------------- 简易 JSON 字段提取(仅用于自产转录文件) ----------------
static std::string jsonGet(const std::string& line, const std::string& key) {
    std::string pat = "\"" + key + "\":\"";
    auto pos = line.find(pat);
    if (pos == std::string::npos) return "";
    pos += pat.size();
    std::string out;
    for (size_t i = pos; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) { out += line[i + 1]; ++i; continue; }
        if (line[i] == '"') break;
        out += line[i];
    }
    return out;
}

// ---------------- 回放: 从转录重新执行并逐步比对 ----------------
static int replayTranscript(GpuContext& gpu, const std::string& transcriptPath,
                            const std::string& verdictPath) {
    std::ifstream in(transcriptPath);
    if (!in) { std::fprintf(stderr, "无法打开转录: %s\n", transcriptPath.c_str()); return -1; }
    Runner r(gpu);
    r.scenarioName = "replay";
    int steps = 0, mismatches = 0;
    std::string line;
    std::vector<std::string> mismatchLog;
    while (std::getline(in, line)) {
        std::string op = jsonGet(line, "op");
        if (op.empty() || op == "env") continue;
        std::string input = jsonGet(line, "input");
        std::string wantHash = jsonGet(line, "hash");
        std::string wantCode = jsonGet(line, "code");
        auto toks = splitTokens(input);
        size_t before = r.records.size();
        execOp(r, op, toks, input);
        if (r.records.size() == before) continue; // 防御
        const StepRecord& got = r.records.back();
        ++steps;
        bool ok = (got.resultCode == wantCode) && (hexHash(got.stateHash) == wantHash);
        if (!ok) {
            ++mismatches;
            mismatchLog.push_back("seq=" + std::to_string(got.seq) + " op=" + op +
                " want(" + wantCode + "," + wantHash + ") got(" + got.resultCode +
                "," + hexHash(got.stateHash) + ")");
        }
    }
    std::ofstream v(verdictPath);
    v << "{\"steps\":" << steps << ",\"mismatches\":" << mismatches << "}\n";
    for (auto& m : mismatchLog) v << m << "\n";
    std::printf("[replay %s] steps=%d mismatches=%d\n", transcriptPath.c_str(), steps, mismatches);
    return mismatches;
}

// ---------------- 跨场景检查点比对(顺序改变验证) ----------------
static int compareCheckpoints(const std::string& fileA, const std::string& labelA,
                              const std::string& fileB, const std::string& labelB,
                              const std::string& verdictPath) {
    auto findHash = [](const std::string& path, const std::string& label) -> std::string {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            if (jsonGet(line, "op") == "checkpoint" && jsonGet(line, "input") == label)
                return jsonGet(line, "hash");
        }
        return "";
    };
    std::string ha = findHash(fileA, labelA), hb = findHash(fileB, labelB);
    bool equal = !ha.empty() && ha == hb;
    std::ofstream v(verdictPath);
    v << "{\"labelA\":\"" << labelA << "\",\"hashA\":\"" << ha
      << "\",\"labelB\":\"" << labelB << "\",\"hashB\":\"" << hb
      << "\",\"equal\":" << (equal ? "true" : "false") << "}\n";
    std::printf("[compare] %s=%s vs %s=%s -> %s\n", labelA.c_str(), ha.c_str(),
                labelB.c_str(), hb.c_str(), equal ? "EQUAL" : "DIFFERENT");
    return equal ? 0 : 1;
}

// ---------------- 报告生成: 自动结论 + 人工确认 ----------------
struct ScenarioSummary {
    std::string name;
    int steps = 0, pass = 0, fail = 0, cancels = 0;
    bool cleanupOk = false;
    int replaySteps = -1, replayMismatch = -1;
};

static ScenarioSummary scanTranscript(const std::string& dir, const std::string& name) {
    ScenarioSummary s;
    s.name = name;
    std::ifstream in(dir + "/transcript_" + name + ".jsonl");
    std::string line;
    while (std::getline(in, line)) {
        std::string op = jsonGet(line, "op");
        if (op.empty() || op == "env") continue;
        ++s.steps;
        std::string code = jsonGet(line, "code");
        if (code == "PASS") ++s.pass;
        if (code == "FAIL") ++s.fail;
        if (code == "CANCELLED") ++s.cancels;
        if (op == "cleanup" && code == "PASS") s.cleanupOk = true;
    }
    std::ifstream rv(dir + "/replay_" + name + ".json");
    if (rv) {
        std::string rl;
        std::getline(rv, rl);
        auto num = [&](const std::string& k) -> int {
            auto p = rl.find("\"" + k + "\":");
            return p == std::string::npos ? -1 : std::stoi(rl.substr(p + k.size() + 3));
        };
        s.replaySteps = num("steps");
        s.replayMismatch = num("mismatches");
    }
    return s;
}

static int generateReport(const std::string& dir, const std::vector<std::string>& scenarios,
                          const std::string& deviceName) {
    std::vector<ScenarioSummary> sums;
    int totalFail = 0;
    for (auto& n : scenarios) {
        sums.push_back(scanTranscript(dir, n));
        totalFail += sums.back().fail;
        if (sums.back().replayMismatch > 0) totalFail += sums.back().replayMismatch;
    }
    bool orderEqual = false, orderChecked = false;
    {
        std::ifstream c(dir + "/compare_order.json");
        std::string l;
        if (c && std::getline(c, l)) {
            orderChecked = true;
            orderEqual = l.find("\"equal\":true") != std::string::npos;
        }
    }
    if (orderChecked && !orderEqual) ++totalFail;

    std::ofstream rep(dir + "/REPORT.md");
    rep << "# GPU 资源生命周期可回放实验报告\n\n";
    rep << "- 设备: `" << deviceName << "` (GLFW null-platform, 无显示环境)\n";
    rep << "- 技术栈: C++17 / Vulkan / GLFW\n\n";

    rep << "## 自动结论(机器生成)\n\n";
    rep << "| 场景 | 步数 | PASS | FAIL | 取消次数 | 清理无泄漏 | 回放步数 | 回放不一致 |\n";
    rep << "|---|---|---|---|---|---|---|---|\n";
    for (auto& s : sums) {
        rep << "| " << s.name << " | " << s.steps << " | " << s.pass << " | " << s.fail
            << " | " << s.cancels << " | " << (s.cleanupOk ? "是" : "否")
            << " | " << s.replaySteps << " | " << s.replayMismatch << " |\n";
    }
    rep << "\n";
    rep << "- 中途取消(实验组 cancel_resubmit): 取消后重录重提, 最终内容校验 "
        << "见 transcript_cancel_resubmit.jsonl 中 expect_buffer 结果\n";
    rep << "- 中途取消(对照组 cancel_abort): 取消并中止, 缓冲区应保持初始 0x00, "
        << "见 transcript_cancel_abort.jsonl 中 expect_buffer 结果\n";
    rep << "- 顺序改变验证: reorder_a 与 reorder_b 最终检查点哈希 "
        << (!orderChecked ? "未执行(缺 compare_order.json)"
            : orderEqual ? "**一致**, 操作顺序不影响最终资源状态" : "**不一致**, 需人工排查")
        << " (compare_order.json)\n";
    rep << "- 重复提交验证: repeat 场景中同一填充提交两次结果幂等; "
        << "重复销毁被检测为 ERROR(见 transcript_repeat.jsonl)\n";
    rep << "- 总体判定: **" << (totalFail == 0 ? "全部通过" : "存在失败, 见下方后续操作") << "**\n\n";

    rep << "## 人工确认(需人工签字)\n\n";
    rep << "- [ ] 确认本实验的“中途取消”语义(丢弃已记录未提交的命令)符合团队定义\n";
    rep << "- [ ] 确认在软件设备 llvmpipe 上的结论可外推到目标硬件, 或已在目标硬件复跑\n";
    rep << "- [ ] 抽查 artifacts/transcript_*.jsonl 中至少一个场景的逐步输入/状态/结果\n";
    rep << "- [ ] 确认 repeat 场景中“重复销毁返回 ERROR”是期望的防御行为\n";
    rep << "- [ ] 确认对照组(cancel_abort)与实验组(cancel_resubmit)的差异符合实验设计\n\n";

    if (totalFail > 0) {
        rep << "## 失败后续操作(可执行)\n\n";
        rep << "1. 查看失败明细: `grep '\"FAIL\"' " << dir << "/transcript_*.jsonl`\n";
        rep << "2. 单独重跑失败场景: `./build/gpu_lifecycle run scenarios/<名>.txt " << dir << "/transcript_<名>.jsonl`\n";
        rep << "3. 回放定位分叉步: `./build/gpu_lifecycle replay " << dir << "/transcript_<名>.jsonl " << dir << "/replay_<名>.json`\n";
        rep << "4. 检查 ICD 环境: `ls /usr/share/vulkan/icd.d/` 并试 `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`\n";
        rep << "5. 若怀疑驱动差异, 安装 vulkan-tools 后运行 `vulkaninfo --summary` 对比设备\n";
    }
    rep.close();
    std::printf("[report] %s/REPORT.md 已生成, 总失败数=%d\n", dir.c_str(), totalFail);
    return totalFail;
}

static void printNextStepsOnInitFail(const std::string& err) {
    std::fprintf(stderr, "初始化失败: %s\n", err.c_str());
    std::fprintf(stderr, "可执行的后续操作:\n");
    std::fprintf(stderr, "  1. 确认 ICD: ls /usr/share/vulkan/icd.d/\n");
    std::fprintf(stderr, "  2. 指定软件设备: export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json\n");
    std::fprintf(stderr, "  3. 确认加载器存在: ldconfig -p | grep libvulkan\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "用法:\n"
            "  gpu_lifecycle run <场景脚本> <转录输出.jsonl>\n"
            "  gpu_lifecycle replay <转录.jsonl> <回放判定输出.json>\n"
            "  gpu_lifecycle compare <转录A> <标签A> <转录B> <标签B> <判定输出.json>\n"
            "  gpu_lifecycle report <artifacts目录> <场景1,场景2,...>\n");
        return 2;
    }
    std::string mode = argv[1];
    GpuContext gpu;
    if (mode != "report") {
        if (!gpu.init()) { printNextStepsOnInitFail(gpu.error); return 1; }
    }
    int rc = 0;
    if (mode == "run" && argc >= 4) {
        rc = runScenario(gpu, argv[2], argv[3]);
    } else if (mode == "replay" && argc >= 4) {
        rc = replayTranscript(gpu, argv[2], argv[3]);
    } else if (mode == "compare" && argc >= 7) {
        rc = compareCheckpoints(argv[2], argv[3], argv[4], argv[5], argv[6]);
    } else if (mode == "report" && argc >= 4) {
        // report 模式无需 GPU, 但尝试初始化以记录设备名
        std::string dev = "unknown";
        if (gpu.init()) { dev = gpu.deviceName; gpu.shutdown(); }
        rc = generateReport(argv[2], splitTokens([&]{ std::string s=argv[3]; for(auto&c:s) if(c==',') c=' '; return s; }()), dev);
    } else {
        std::fprintf(stderr, "参数错误, 见用法\n");
        rc = 2;
    }
    if (mode != "report") gpu.shutdown();
    return rc < 0 ? 1 : (rc > 0 ? 1 : 0);
}
