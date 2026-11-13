// openh264 A/B 探针：编码器（ISVCEncoder）编 10 帧 128×96 I420 → 解码（WelsCreateDecoder）。
// 哈希码流字节与解码 YUV；两侧必须逐字节一致。
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec_api.h"
#include "codec_app_def.h"
#include "codec_def.h"

#define W 128
#define H 96
#define NFRAMES 10

static uint64_t fnv1a(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}

static void fill_i420(uint8_t *buf, int f)
{
    uint8_t *y = buf, *u = buf + W * H, *v = u + (W / 2) * (H / 2);
    for (int r = 0; r < H; r++)
        for (int c = 0; c < W; c++) {
            unsigned int n = (unsigned int)(c * 2654435761u + r * 40503u + f * 7919u);
            n ^= n >> 13;
            int val = (c * 2 + r + f * 3) & 0xff;
            if (((c >> 4) ^ (r >> 4)) & 1) val = 255 - val;
            val ^= (int)(n & 0x07);
            y[r * W + c] = (uint8_t)val;
        }
    for (int r = 0; r < H / 2; r++)
        for (int c = 0; c < W / 2; c++) {
            u[r * (W / 2) + c] = (uint8_t)((c * 3 + r + f) & 0xff);
            v[r * (W / 2) + c] = (uint8_t)((c + r * 5 + f * 2) & 0xff);
        }
}

int main(int argc, char **argv)
{
    const char *outp = argc > 1 ? argv[1] : NULL;
    ISVCEncoder *enc = NULL;
    if (WelsCreateSVCEncoder(&enc) != 0 || !enc) { fprintf(stderr, "编码器创建失败\n"); return 2; }

    SEncParamExt p;
    enc->GetDefaultParams(&p);
    p.iUsageType = CAMERA_VIDEO_REAL_TIME;
    p.iPicWidth = W;
    p.iPicHeight = H;
    p.iTargetBitrate = 200000;
    p.iRCMode = RC_BITRATE_MODE;
    p.fMaxFrameRate = 30.0f;
    p.iTemporalLayerNum = 1;
    p.iSpatialLayerNum = 1;
    p.iMultipleThreadIdc = 1;
    p.bEnableDenoise = false;
    p.bEnableSceneChangeDetect = false;
    p.bEnableBackgroundDetection = false;
    p.bEnableAdaptiveQuant = false;
    p.bEnableFrameSkip = false;
    p.bEnableLongTermReference = false;
    p.iEntropyCodingModeFlag = 0;
    p.iComplexityMode = LOW_COMPLEXITY;
    p.sSpatialLayers[0].iVideoWidth = W;
    p.sSpatialLayers[0].iVideoHeight = H;
    p.sSpatialLayers[0].fFrameRate = 30.0f;
    p.sSpatialLayers[0].iSpatialBitrate = 200000;
    p.sSpatialLayers[0].uiProfileIdc = PRO_BASELINE;

    if (enc->InitializeExt(&p) != 0) { fprintf(stderr, "编码器初始化失败\n"); return 3; }

    ISVCDecoder *dec = NULL;
    if (WelsCreateDecoder(&dec) != 0 || !dec) { fprintf(stderr, "解码器创建失败\n"); return 4; }
    SDecodingParam dp;
    memset(&dp, 0, sizeof dp);
    dp.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
    if (dec->Initialize(&dp) != 0) { fprintf(stderr, "解码器初始化失败\n"); return 5; }

    const int ysz = W * H, csz = (W / 2) * (H / 2);
    uint8_t *frame = (uint8_t *)malloc(ysz + 2 * csz);
    if (!frame) return 6;

    uint64_t hbits = 1469598103934665603ULL, hpix = 1469598103934665603ULL;
    unsigned long bytes = 0, dec_frames = 0;

    for (int f = 0; f < NFRAMES; f++) {
        fill_i420(frame, f);
        SSourcePicture pic;
        memset(&pic, 0, sizeof pic);
        pic.iPicWidth = W; pic.iPicHeight = H;
        pic.iColorFormat = videoFormatI420;
        pic.iStride[0] = W; pic.iStride[1] = W / 2; pic.iStride[2] = W / 2;
        pic.pData[0] = frame; pic.pData[1] = frame + ysz; pic.pData[2] = frame + ysz + csz;
        pic.uiTimeStamp = (int64_t)f * 33;

        SFrameBSInfo bsi;
        memset(&bsi, 0, sizeof bsi);
        if (enc->EncodeFrame(&pic, &bsi) != cmResultSuccess) {
            fprintf(stderr, "编码失败 f=%d\n", f); return 7;
        }
        /* 逐 NAL 累计：pBsBuf 里是该层所有 NAL 的连续字节串 */
        for (int L = 0; L < bsi.iLayerNum; L++) {
            SLayerBSInfo *li = &bsi.sLayerInfo[L];
            int nal_total = 0;
            for (int n = 0; n < li->iNalCount; n++) nal_total += li->pNalLengthInByte[n];
            const unsigned char *base = li->pBsBuf;
            bytes += (unsigned long)nal_total;
            hbits = (hbits ^ (uint64_t)nal_total) * 1099511628211ULL;
            for (int i = 0; i < nal_total; i++) { hbits ^= base[i]; hbits *= 1099511628211ULL; }

            unsigned char *dst[3] = {NULL, NULL, NULL};
            SBufferInfo bi;
            memset(&bi, 0, sizeof bi);
            if (dec->DecodeFrameNoDelay(base, nal_total, dst, &bi) != dsErrorFree) {
                fprintf(stderr, "解码失败 f=%d L=%d\n", f, L); return 8;
            }
            if (bi.iBufferStatus == 1) {
                dec_frames++;
                for (int p = 0; p < 3; p++) {
                    int pw = (p == 0) ? W : W / 2, ph = (p == 0) ? H : H / 2;
                    const uint8_t *pl = dst[p];
                    for (int r = 0; r < ph; r++) {
                        const uint8_t *row = pl + r * bi.UsrData.sSystemBuffer.iStride[p];
                        for (int c = 0; c < pw; c++) { hpix ^= row[c]; hpix *= 1099511628211ULL; }
                    }
                }
            }
        }
    }

    char line[256];
    snprintf(line, sizeof line, "frames=%d bytes=%lu fnv_bit=%016llx dec_frames=%lu fnv_px=%016llx\n",
             NFRAMES, bytes, (unsigned long long)hbits, dec_frames, (unsigned long long)hpix);
    if (outp) { FILE *fp = fopen(outp, "wb"); if (fp) { fwrite(line, 1, strlen(line), fp); fclose(fp); } }
    fputs(line, stdout);

    dec->Uninitialize();
    WelsDestroyDecoder(dec);
    enc->Uninitialize();
    WelsDestroySVCEncoder(enc);
    free(frame);
    return dec_frames >= (unsigned long)NFRAMES ? 0 : 9;
}
