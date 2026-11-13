# 交叉编译基线（riscv64）— openh264

## 口径
- 只编 baseline，`MARCH=rv64gc`，镜像 `rvv-cross:2`（探针是 C++，要 riscv g++）
- `USE_ASM=No`：openh264 的汇编只有 x86/arm/loongarch，riscv 走纯 C/C++，native 侧同样关汇编
- 探针 `test/pkg_h264.cpp`：ISVCEncoder 编 128×96 ×10 帧（CAMERA_REAL_TIME、CBR 200 kbps、
  LOW_COMPLEXITY、单线程、关降噪/场景检测/自适应量化/frame skip），
  逐 NAL 送 WelsCreateDecoder 解码；码流与解码 YUV 一起哈希

## 坑
- 头文件装在 `prefix/include/wels/`，`-I` 要同时给 `include` 和 `include/wels`
- `EncodeFrame` 的签名是 `(SSourcePicture*, SFrameBSInfo*)`，码流在 `sLayerInfo[].pBsBuf`
  + `pNalLengthInByte[]`；`SSourcePicture` 没有 `iFrameRate`，用 `uiTimeStamp`

## 结果
| 侧 | 输出 |
|---|---|
| riscv64 | `frames=10 bytes=8619 fnv_bit=316f1b141efb69a7 dec_frames=10 fnv_px=762b92ca7ae625a5` |
| native | 同左 |

**全字段一致**。

## 复现
```bash
cd ~/work/openh264-rvv
./test/run_ab.sh && ./scripts/cross-baseline.sh
```
