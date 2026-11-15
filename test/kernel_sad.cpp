// openh264 SAD 内核级 A/B：同一份输入，分别调 C 版与 RVV 版，逐值比对 + 计时。
// 用法：kernel_sad <size 4|8|16> <reps> [v]
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sad_common.h"

#define MAXW 16
#define STRIDE 32

static uint8_t buf1[STRIDE * MAXW] __attribute__ ((aligned (16)));
static uint8_t buf2[STRIDE * MAXW] __attribute__ ((aligned (16)));

static double now_ns () {
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (double) ts.tv_sec * 1e9 + (double) ts.tv_nsec;
}

typedef int32_t (*SadFn) (uint8_t*, int32_t, uint8_t*, int32_t);

int main (int argc, char** argv) {
  int iSize = argc > 1 ? atoi (argv[1]) : 16;
  int iReps = argc > 2 ? atoi (argv[2]) : 100000;
  int iVerbose = argc > 3 ? atoi (argv[3]) : 0;

  SadFn pC = NULL, pRvv = NULL;
  switch (iSize) {
    case 4:  pC = WelsSampleSad4x4_c;   pRvv = WelsSampleSad4x4_rvv;   break;
    case 8:  pC = WelsSampleSad8x8_c;   pRvv = WelsSampleSad8x8_rvv;   break;
    case 16: pC = WelsSampleSad16x16_c; pRvv = WelsSampleSad16x16_rvv; break;
    case 84: pC = WelsSampleSad8x4_c;   pRvv = WelsSampleSad8x4_rvv;   break;
    case 48: pC = WelsSampleSad4x8_c;   pRvv = WelsSampleSad4x8_rvv;   break;
    default: fprintf (stderr, "bad size\n"); return 2;
  }

  /* 确定性输入：两个不同的 LCG 序列，保证不平凡 */
  {
    uint32_t s1 = 12345, s2 = 98765;
    int i;
    for (i = 0; i < STRIDE * MAXW; i++) {
      s1 = s1 * 1103515245u + 12345u; buf1[i] = (uint8_t) (s1 >> 16);
      s2 = s2 * 1664525u + 1013904223u; buf2[i] = (uint8_t) (s2 >> 16);
    }
  }

  int32_t iC = pC (buf1, STRIDE, buf2, STRIDE);
  int32_t iR = pRvv (buf1, STRIDE, buf2, STRIDE);
  if (iVerbose) printf ("sad%dx%d c=%d rvv=%d\n", iSize, iSize, (int) iC, (int) iR);
  if (iC != iR) { printf ("MISMATCH c=%d rvv=%d\n", (int) iC, (int) iR); return 3; }

  double t0 = now_ns ();
  int32_t iAcc = 0;
  int k;
  for (k = 0; k < iReps; k++) iAcc += pC (buf1, STRIDE, buf2, STRIDE);
  double tC = now_ns () - t0;

  t0 = now_ns ();
  int32_t iAcc2 = 0;
  for (k = 0; k < iReps; k++) iAcc2 += pRvv (buf1, STRIDE, buf2, STRIDE);
  double tR = now_ns () - t0;

  printf ("sad%dx%d c_ns=%.1f rvv_ns=%.1f ratio=%.3f value=%d\n",
          iSize, iSize, tC / iReps, tR / iReps, tC / tR, (int) iAcc);
  return 0;
}
