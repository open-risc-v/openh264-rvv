#!/usr/bin/env bash
# G3 内核 A/B（openh264）：HAVE_RVV 编译期开关把 pfSampleSad 换成 RVV 内核，
# 两套构建只差这一个宏；其余 -march/优化级别/构建参数完全一致。
#
#   ./scripts/g3-wire-ab.sh       # 编 on/off 并在 qemu 下比对哈希
#
# 产物：build/g3-ab/{on,off}/{prefix,harness}
set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE=${IMAGE:-rvv-cross:2}
MARCH=${MARCH:-rv64gcv}
OUT=build/g3-ab

for side in off on; do
  if [ "$side" = on ]; then
    RVFLAG="-DHAVE_RVV=1"
  else
    RVFLAG=""
  fi
  echo "=== 构建 ${side}（额外 CFLAGS: '${RVFLAG:-<无>}'）==="
  docker run --rm -v "$PWD":/work -w /work -e SIDE="$side" -e MARCH="$MARCH" \
    -e RVFLAG="$RVFLAG" -e OUT="$OUT" "${IMAGE}" bash -lc '
set -e
rm -rf "${OUT}/${SIDE}"
mkdir -p "${OUT}/${SIDE}"
cp -R src "${OUT}/${SIDE}/src"
cd "${OUT}/${SIDE}/src"
make -j"$(nproc)" USE_ASM=No BUILDTYPE=Release PREFIX="/work/${OUT}/${SIDE}/prefix" \
  CC=riscv64-linux-gnu-gcc CXX=riscv64-linux-gnu-g++ AR=riscv64-linux-gnu-ar \
  CFLAGS="-march=${MARCH} -O2 ${RVFLAG}" CXXFLAGS="-march=${MARCH} -O2 ${RVFLAG}" \
  install-static-lib > /tmp/bld.log 2>&1 \
  || { echo "build ${SIDE} FAIL"; tail -40 /tmp/bld.log; exit 1; }
cd /work
cmake -S . -B "${OUT}/${SIDE}/harness" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/work/cmake/toolchain-riscv64-linux.cmake \
  -DCMAKE_C_FLAGS="-march=${MARCH} -static" -DCMAKE_CXX_FLAGS="-march=${MARCH} -static" \
  -DCMAKE_EXE_LINKER_FLAGS="-static" -DPKG_PREFIX="/work/${OUT}/${SIDE}/prefix" \
  > /tmp/h.log 2>&1 || { tail -20 /tmp/h.log; exit 1; }
cmake --build "${OUT}/${SIDE}/harness" -j > /tmp/hb.log 2>&1 || { tail -20 /tmp/hb.log; exit 1; }
mkdir -p "${OUT}/${SIDE}/out"
qemu-riscv64 -L /usr/riscv64-linux-gnu "${OUT}/${SIDE}/harness/h264_harness" \
  "${OUT}/${SIDE}/out/hash.txt" > "${OUT}/${SIDE}/out/stdout.txt"
echo "--- ${SIDE} ---"; cat "${OUT}/${SIDE}/out/hash.txt"
'
done

echo '=== 比对 ==='
if diff -q "$OUT/off/out/hash.txt" "$OUT/on/out/hash.txt" > /dev/null; then
  echo 'PASS：开/关 RVV SAD 的码流与解码像素完全一致'
else
  echo 'FAIL：两侧哈希不同'
  diff "$OUT/off/out/hash.txt" "$OUT/on/out/hash.txt" || true
  exit 1
fi
