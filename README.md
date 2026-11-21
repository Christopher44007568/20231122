# 20231122
# GPU 崩溃前状态转储来源检查页

固定技术栈：**C++17 + Vulkan + GLFW**。对一次 GPU 崩溃恢复后的状态转储做
来源检查（provenance），让每条自动结论都能回溯到原始输入行和处理步骤，
并把无法自动判断的边界证据原样保留。

## 能力

- 记录一次**恢复后的重放**（帧数、覆盖率、首个分歧帧）。
- 记录一个**未知扩展**，自动判定为 `INDETERMINATE`，不猜测。
- 每条结论携带证据行号、原文引用和经过的流水线步骤。
- 保留**无法判断的边界**原始行，并给出人工确认项。
- 失败时给出**可执行后续操作**，CLI 用退出码区分阻塞失败。
- 报告与页面均把**自动结论**和**人工确认**分开呈现。

## 目录

- `src/core/` 与渲染/窗口无关的解析、分析、报告核心（静态库）。
- `src/cli/` 命令行工具 `gcdump`。
- `src/gui/` GLFW + Vulkan 检查页 `gcdump_page`。
- `samples/` 恢复样本、校验和损坏样本、畸形样本。
- `docs/dump-format.md` 输入格式；`docs/provenance.md` 溯源设计。
- `tests/` 核心单测与页面/Vulkan 冒烟测试。

## 依赖准备（无 root）

系统若未安装开发包，可在项目内本地解压（运行库和驱动通常已存在）：

```bash
mkdir -p third_party/debs && cd third_party/debs
env -u http_proxy -u https_proxy -u HTTP_PROXY -u HTTPS_PROXY \
  apt-get -o Acquire::http::Proxy=NONE -o Acquire::https::Proxy=NONE \
  download libvulkan-dev libglfw3-dev libglfw3
cd .. && mkdir -p local
for d in debs/*.deb; do dpkg-deb -x "$d" local/; done
ln -sfn /usr/lib/x86_64-linux-gnu/libvulkan.so.1 \
  local/usr/lib/x86_64-linux-gnu/libvulkan.so.1
```

CMake 会自动识别 `third_party/local`，也兼容系统级安装和 `find_package`。

## 构建与测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

不需要窗口时可关闭 GUI：`-DGCDUMP_BUILD_GUI=OFF`。

## 运行

```bash
# 文本/Markdown 报告（失败时退出码 2）
./build/gcdump samples/recovered_dump.gcdump
./build/gcdump --format markdown samples/corrupted_dump.gcdump

# Vulkan 检查页（交互窗口）
./build/src/gui/gcdump_page samples/recovered_dump.gcdump

# 无交互：仅渲染固定帧数，或导出页面位图（PPM）
./build/src/gui/gcdump_page samples/recovered_dump.gcdump --frames 30
./build/src/gui/gcdump_page samples/recovered_dump.gcdump \
  --screenshot artifacts/page.ppm
```

页面采用 CPU 位图（自带 5x7 点阵字体，无系统字体依赖）经 host-visible
Vulkan image 上传，再用 `vkCmdBlitImage` 呈现，避免对运行时着色器工具链
的依赖。

## 退出码

- `0` 正常（可能仍有人工确认项）。
- `2` 存在阻塞失败（见 `docs/provenance.md` 的 F1001/F2002/F2003/F2004）。
- `3` 用法或文件 IO 错误。
