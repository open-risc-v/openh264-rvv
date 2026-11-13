#!/usr/bin/env bash
# 构建上游 openh264（Makefile）→ <out>/prefix：静态 libopenh264.a + 头文件。
# USE_ASM=No：openh264 的汇编只有 x86/arm/loongarch，riscv 走纯 C（两侧同口径）。
set -euo pipefail
SRC=$1
OUT=$2
mkdir -p "$OUT"
rm -rf "$OUT/src" && cp -R "$SRC" "$OUT/src"
cd "$OUT/src"
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" \
  USE_ASM=No BUILDTYPE=Release PREFIX="$OUT/prefix" \
  install-static-lib > "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log"; exit 1; }
