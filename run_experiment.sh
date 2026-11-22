#!/usr/bin/env bash
# GPU 资源生命周期可回放实验: 构建 -> 执行 -> 回放 -> 比对 -> 报告
set -u
cd "$(dirname "$0")"
export TMPDIR="$PWD/.tmp"
mkdir -p .tmp artifacts

SCENARIOS="baseline cancel_resubmit cancel_abort reorder_a reorder_b repeat"
FAIL=0

echo "== 构建 =="
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null || { echo "cmake 配置失败"; exit 1; }
cmake --build build -j"$(nproc)" || { echo "编译失败"; exit 1; }

echo "== 执行场景(记录逐步输入/状态/结果) =="
for s in $SCENARIOS; do
    ./build/gpu_lifecycle run "scenarios/$s.txt" "artifacts/transcript_$s.jsonl" || FAIL=1
done

echo "== 回放校验(确定性验证) =="
for s in $SCENARIOS; do
    ./build/gpu_lifecycle replay "artifacts/transcript_$s.jsonl" "artifacts/replay_$s.json" || FAIL=1
done

echo "== 顺序改变验证 =="
./build/gpu_lifecycle compare artifacts/transcript_reorder_a.jsonl final \
    artifacts/transcript_reorder_b.jsonl final artifacts/compare_order.json || FAIL=1

echo "== 生成报告 =="
./build/gpu_lifecycle report artifacts "$(echo $SCENARIOS | tr ' ' ',')" || FAIL=1

if [ "$FAIL" -ne 0 ]; then
    echo "实验存在失败, 见 artifacts/REPORT.md 的失败后续操作"
    exit 1
fi
echo "实验全部通过, 报告: artifacts/REPORT.md"
