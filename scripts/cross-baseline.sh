#!/usr/bin/env bash
# 交叉编译基线版本：openh264 用 Makefile，riscv 走纯 C/C++（USE_ASM=No，上游汇编只有
# x86/arm/loongarch），探针用 qemu-user 跑，输出与 native 基线逐字段比对。
set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE=${IMAGE:-rvv-cross:2}     # :2 = 在 :1 上补了 riscv64-linux-gnu-g++（探针是 C++）
MARCH=${MARCH:-rv64gc}
OUT=build/riscv64-b

docker run --rm -v "$PWD":/work -w /work "${IMAGE}" bash -lc "
set -e
rm -rf ${OUT}
mkdir -p ${OUT} && cp -R baseline ${OUT}/src && rm -rf ${OUT}/src/.git
cd ${OUT}/src
make -j\$(nproc) USE_ASM=No BUILDTYPE=Release PREFIX=/work/${OUT}/prefix \\
  CC=riscv64-linux-gnu-gcc CXX=riscv64-linux-gnu-g++ AR=riscv64-linux-gnu-ar \\
  CFLAGS='-march=${MARCH} -O2' CXXFLAGS='-march=${MARCH} -O2' \\
  install-static-lib > /tmp/h264-bld.log 2>&1 || { tail -30 /tmp/h264-bld.log; exit 1; }

cd /work
cmake -S . -B ${OUT}/harness -DCMAKE_BUILD_TYPE=Release \\
  -DCMAKE_TOOLCHAIN_FILE=/work/cmake/toolchain-riscv64-linux.cmake \\
  -DCMAKE_C_FLAGS='-march=${MARCH}' -DCMAKE_CXX_FLAGS='-march=${MARCH}' \\
  -DPKG_PREFIX=/work/${OUT}/prefix > /tmp/h.log 2>&1 || { tail -30 /tmp/h.log; exit 1; }
cmake --build ${OUT}/harness -j > /tmp/hb.log 2>&1 || { tail -20 /tmp/hb.log; exit 1; }

mkdir -p ${OUT}/out
qemu-riscv64 -L /usr/riscv64-linux-gnu ${OUT}/harness/h264_harness ${OUT}/out/hash.txt > ${OUT}/out/stdout.txt
grep -v '^[[]OpenH264[]]' ${OUT}/out/stdout.txt > ${OUT}/out/clean.txt || true
echo \"--- riscv64（${MARCH}）---\"; cat ${OUT}/out/clean.txt
echo \"--- native 基线 ---\"; cat build/ab-data/out/01_h264_roundtrip/a/hash.txt
"
