/*
 * openh264 RVV 移植：SAD（逐像素绝对差和）内核。
 *
 * 口径：与上游 WelsSampleSad{4x4,8x8,16x16}_c 逐值等价（int32 返回值，uint8 样本，
 * 行跨距任意）。A/B 唯一变量是 -DHAVE_RVV=1。
 *
 * 三条从 libvpx SAD 迁移过来的经验：
 *   1) u8 上不能 abs(a-b)（会回绕），用 vmaxu/vminu 相减得到 d∈[0,255]；
 *   2) 归约只在块末尾做一次，不要每行一次；
 *   3) 不要假设 VLEN/MVL —— chunk 上限一律用 vsetvlmax 取。
 */
#include <stdint.h>
#include <riscv_vector.h>

static uint32_t SadU8 (const uint8_t* pSrc1, int32_t iStride1,
                       const uint8_t* pSrc2, int32_t iStride2,
                       int32_t iWidth, int32_t iHeight) {
  size_t vmax = __riscv_vsetvlmax_e16m1 ();
  vuint16m1_t vAcc = __riscv_vmv_v_x_u16m1 ((uint16_t)0, vmax);
  int32_t y;

  for (y = 0; y < iHeight; y++) {
    int32_t x = 0;
    while (x < iWidth) {
      size_t vl = (size_t) (iWidth - x) < vmax ? (size_t) (iWidth - x) : vmax;
      vuint8mf2_t vA = __riscv_vle8_v_u8mf2 (pSrc1 + x, vl);
      vuint8mf2_t vB = __riscv_vle8_v_u8mf2 (pSrc2 + x, vl);
      vuint8mf2_t vHi = __riscv_vmaxu_vv_u8mf2 (vA, vB, vl);
      vuint8mf2_t vLo = __riscv_vminu_vv_u8mf2 (vA, vB, vl);
      vAcc = __riscv_vwaddu_wv_u16m1 (vAcc, __riscv_vsub_vv_u8mf2 (vHi, vLo, vl), vl);
      x += (int32_t) vl;
    }
    pSrc1 += iStride1;
    pSrc2 += iStride2;
  }
  {
    /* GCC 14: vwredsumu 返回 u32m1 向量（结果在元素 0），要再取标量。 */
    vuint32m1_t vSum = __riscv_vwredsumu_vs_u16m1_u32m1 (
        vAcc, __riscv_vmv_v_x_u32m1 ((uint32_t)0, vmax), vmax);
    return (uint32_t) __riscv_vmv_x_s_u32m1_u32 (vSum);
  }
}

int32_t WelsSampleSad4x4_rvv (uint8_t* pSample1, int32_t iStride1,
                              uint8_t* pSample2, int32_t iStride2) {
  return (int32_t) SadU8 (pSample1, iStride1, pSample2, iStride2, 4, 4);
}

int32_t WelsSampleSad8x8_rvv (uint8_t* pSample1, int32_t iStride1,
                              uint8_t* pSample2, int32_t iStride2) {
  return (int32_t) SadU8 (pSample1, iStride1, pSample2, iStride2, 8, 8);
}

int32_t WelsSampleSad16x16_rvv (uint8_t* pSample1, int32_t iStride1,
                                uint8_t* pSample2, int32_t iStride2) {
  return (int32_t) SadU8 (pSample1, iStride1, pSample2, iStride2, 16, 16);
}

/* 矩形块复用同一实现（16x8 / 8x16 / 8x4 / 4x8），非方形也走向量路径。 */
int32_t WelsSampleSad16x8_rvv (uint8_t* pSample1, int32_t iStride1,
                               uint8_t* pSample2, int32_t iStride2) {
  return (int32_t) SadU8 (pSample1, iStride1, pSample2, iStride2, 16, 8);
}

int32_t WelsSampleSad8x16_rvv (uint8_t* pSample1, int32_t iStride1,
                               uint8_t* pSample2, int32_t iStride2) {
  return (int32_t) SadU8 (pSample1, iStride1, pSample2, iStride2, 8, 16);
}

int32_t WelsSampleSad8x4_rvv (uint8_t* pSample1, int32_t iStride1,
                              uint8_t* pSample2, int32_t iStride2) {
  return (int32_t) SadU8 (pSample1, iStride1, pSample2, iStride2, 8, 4);
}

int32_t WelsSampleSad4x8_rvv (uint8_t* pSample1, int32_t iStride1,
                              uint8_t* pSample2, int32_t iStride2) {
  return (int32_t) SadU8 (pSample1, iStride1, pSample2, iStride2, 4, 8);
}
