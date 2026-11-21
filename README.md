# outline_diag — 屏幕空间描边诊断工具

针对屏幕空间描边在薄片、相交边、高曲率轮廓处的断线问题，提供四视图对照、
独立阈值调节和逐像素断线诊断。技术栈：C++17 / Vulkan / GLFW。

## 构建

```sh
cmake -B build          # 依赖默认从 ~/local 查找，可用 -DLOCAL_PREFIX=... 覆盖
cmake --build build -j
```

需要 Vulkan 头文件+loader、GLFW 3、glslangValidator（着色器在构建期编译为 SPIR-V）。

## 运行

交互模式（四视图 2x2）：

```sh
./build/outline_diag
```

- 左上：深度差热力图；右上：法线差热力图
- 左下：边界像素（几何真值，epsilon 阈值）；右下：最终描边遮罩（叠加在着色场景上）
- `1`/`2`：深度阈值 ÷1.25 / ×1.25；`3`/`4`：法线阈值 ÷1.25 / ×1.25
- `Tab`/`S`：切换样例场景（plane → thin_sheet → intersecting）
- `P`：把当前帧全部断线像素转储到 stdout 和 `diag_<scene>.txt`
- `Esc`：退出

批量验证模式（无窗口，三个样例场景各渲染一次并输出诊断）：

```sh
./build/outline_diag --dump artifacts \
    [--depth-thr 0.03] [--normal-thr 0.25] [--gt-depth-eps 0.004] [--gt-normal-eps 0.02]
```

每个场景输出 `<scene>.ppm`（四视图合成图）和 `<scene>_diag.txt`（断线像素报告）。

## 指标定义

G-buffer 为视空间法线（RGB）+ 视空间深度（A），逐像素取四邻域（L/R/D/U）：

- 深度差 `depthDiff`：`|z_n - z_c| / z_c` 的最大值（相对一阶差分）
- 法线差 `normalDiff`：`1 - dot(n_c, n_n)` 的最大值
- 边界像素（真值）：`depthGT > gtDepthEps || normalDiff > gtNormalEps`，其中
  `depthGT` 是 `1/z` 的二阶梯度（平面上恒为 0，只在真实不连续处响应），
  避免把斜面的一阶深度梯度误判为边界
- 最终描边：`depthDiff > depthThreshold || normalDiff > normalThreshold`
- 断线像素：边界像素为真但最终描边为假

## 诊断输出格式

每个断线像素一行，包含触发条件、阈值与邻域采样：

```
pixel(x,y) trigger=depth|normal|depth+normal suppressed_by=depth_threshold+normal_threshold \
  depthDiff=<v> (thr <t>) normalDiff=<v> (thr <t>) \
  dz[L,R,D,U]=[...] dn[L,R,D,U]=[...]
```

## 样例验证（artifacts/）

- `plane` / `thin_sheet` / `intersecting`：默认阈值下三组场景无断线
- `broken_lines/depth_strict`（`--depth-thr 5`）：远处地平面轮廓的相对深度差
  ≈1.0，被阈值截断 —— 展示相对深度度量的距离不对称性
- `broken_lines/normal_edge`（`--normal-thr 1.05`）：薄片 90° 棱边
  （normalDiff≈1.0）断线，此处深度差仅 ~0.001，纯深度检测必然漏检
- `broken_lines/intersection`（`--normal-thr 0.5`）：两立方体相交线
  （面夹角 ~55°，normalDiff≈0.43）断线，仅出现在 intersecting 场景
