# openh264 G3：第一个 RVV 内核（SAD）

上游 openh264 没有任何 RISC-V 后端（`USE_ASM=No` 时全走 C）。第一个内核选 SAD：
语义最简、便于逐值等价，且它就在运动搜索的热路径上（`pfSampleSad[]` 函数指针）。

## 实现与接线

- 内核：`codec/common/riscv/sad_rvv.c`，一个通用 `SadU8(w,h)` + 7 个尺寸包装。
- 接线：`codec/common/src/sad_common.cpp` 在 `HAVE_RVV` 时把该文件并入同一 TU
  （不新增构建系统源文件条目）；`codec/encoder/core/src/sample.cpp` 的
  `WelsInitSampleSadFunc` 在 `HAVE_RVV` 时覆盖 `pfSampleSad[]` 指针。
- A/B：两套 `make` 只差 `CFLAGS/CXXFLAGS` 里多一个 `-DHAVE_RVV=1`，`-march=rv64gcv -O2`
  与其余参数完全相同（`scripts/g3-wire-ab.sh`）。

实现要点沿用 libvpx SAD 的三条经验：u8 上不能 `abs(a-b)`（回绕），用 `vmaxu/vminu` 相减；
归约只在块末尾做一次；**不假设 VLEN/MVL**，chunk 上限用 `vsetvlmax_e16m1()`。

## 功能

qemu 下两套跑既有 G2 探针（10 帧 → 编码 → 解码回环）：
`frames=10 bytes=8619 fnv_bit=316f1b141efb69a7 fnv_px=762b92ca7ae625a5` 两侧完全一致，
且与 G2 基线一致。产物符号核对：`on` 侧静态库含 14 个 `*_rvv` 符号，`off` 侧 0 个。

## 内核级真机数字（K3，`test/kernel_sad.cpp`，300k 次/档）

| 块 | C ns/call | RVV ns/call | 比值 |
|---|---|---|---|
| 4x4 | 21.8 | 34.9 | **0.62×（负优化）** |
| 4x8 | 45.5 | 57.4 | **0.79×（负优化）** |
| 8x4 | 45.4 | 34.6 | 1.31× |
| 8x8 | 78.1 | 57.3 | 1.36× |
| 16x16 | 302.6 | 103.9 | **2.91×** |

**结论：决定因素是"宽度"，不是面积。** 宽度 4 的块每次只有 4 字节进向量，
`vsetvl` + 尾部归约的固定开销盖过算术量（4x4 只有 16 个样本）；宽度 ≥8 才是正收益。
所以 `pfSampleSad[]` 只覆盖 16x16 / 16x8 / 8x16 / 8x8 / 8x4，**4x4 与 4x8 保留 C 版**。
这个取舍是量出来的，不是猜的。

## 编码器级真机 A/B（K3，5 轮交替、每轮内侧 3 次取最优，整条"编码 10 帧 + 解码回环"）

| 轮 | off（纯 C） | on（RVVSAD，已收窄到宽度≥8） |
|---|---|---|
| 1 | 35498 us | 34875 us |
| 2 | 35563 | 34810 |
| 3 | 35414 | 34883 |
| 4 | 35532 | 34781 |
| 5 | 35491 | 34935 |

**≈1.018×（快约 1.8%）**，5 轮同向、波动 ≤4 us（0.1%）。

为什么不亮眼：openh264 的运动搜索在亚分区上大量使用 **4x4 SAD**，而 4x4 恰恰是
负优化的那一档（已保留 C）。整链里能吃到收益的只有 16x16/8x8 这些块。
下一步要想在 openh264 上拿到明显收益，得换方向：把多个 4x4 块**批量拼进一个向量**
（一次算 4 个块），或改做 deblocking / IDCT 这类按行处理的模块。

## 待办

- `pfSampleSatd[]`（SATD 比 SAD 更贵，openh264 的 RD 代价里占比更高）尚未做。
- 4x4 批量化（一次 4 块）尚未做。
- 本内核是编译期开关 `HAVE_RVV`，没有运行时 CPU 探测（对 Gentoo rv64gcv 目标够用）。
