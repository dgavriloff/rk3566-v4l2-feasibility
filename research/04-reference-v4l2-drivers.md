# Research: Reference V4L2 Encoder Drivers in Mainline Linux

## 1. Samsung S5P-MFC Encoder (Most Complete Reference)

**Location:** `drivers/media/platform/samsung/s5p-mfc/`
**Total driver:** 14,000 lines (encoder + decoder combined, 33 files)

### Encoder-Specific Line Counts

| Component | File(s) | Lines |
|-----------|---------|-------|
| V4L2 ioctl ops, controls, VB2 ops | `s5p_mfc_enc.c` + `.h` | 2,776 |
| HW register programming (encoder) | `s5p_mfc_opr_v6.c` (encoder portions) | ~1,417 |
| Encoder register definitions | `regs-mfc*.h` (encoder lines) | ~236 |
| Encoder register map struct | `s5p_mfc_opr.h` (e_* fields) | ~110 |
| Encoder param structs | `s5p_mfc_common.h` (enc_params) | ~200 |
| Shared infrastructure (probe, IRQ, PM) | `s5p_mfc.c`, `s5p_mfc_pm.c`, etc. | ~400 shared |
| **Total encoder-specific** | | **~4,700-5,100** |

### Key Source Files

1. **`s5p_mfc.c`** (1,753 lines) — Platform driver probe, IRQ handler dispatch, file open/release. IRQ handler dispatches `FRAME_DONE_RET` to encoder's `post_frame_start()`.

2. **`s5p_mfc_enc.c`** (2,757 lines) — Core encoder file:
   - Format table: 6 raw input formats, 5 compressed output formats (H.264, MPEG4, H.263, VP8, HEVC)
   - 90+ V4L2 controls (GOP, multi-slice, bitrate, RC, QP, profile/level, loop filter, VUI, hierarchical QP)
   - Codec ops: `enc_pre_seq_start`, `enc_post_seq_start`, `enc_pre_frame_start`, `enc_post_frame_start`
   - V4L2 ioctl ops: querycap, enum_fmt, g/try/s_fmt, reqbufs, querybuf, qbuf, dqbuf, streamon/off, s/g_parm, encoder_cmd
   - VB2 queue ops: queue_setup, buf_init, buf_prepare, start/stop_streaming, buf_queue

3. **`s5p_mfc_opr_v6.c`** (2,713 lines total, ~1,417 encoder) — HW register programming:
   - `s5p_mfc_set_enc_params()` (~190 lines): Common encoder registers
   - `s5p_mfc_set_enc_params_h264()` (~290 lines): H.264-specific registers
   - `s5p_mfc_set_enc_params_hevc()` (~270 lines): HEVC-specific
   - Per-codec functions for MPEG4, H.263, VP8

4. **`s5p_mfc_pm.c`** (105 lines) — Power management: up to 4 clocks, pm_runtime integration

### V4L2 M2M Integration

Samsung MFC does **NOT** use `v4l2_m2m` helper framework. Instead:
- Two separate `video_device` nodes (decoder + encoder)
- Manual VB2 queue management with `vb2_dma_contig_memops`
- Round-robin context scheduling via `ctx_work_bits`
- Single-bit `hw_lock` serializes HW access
- IRQ-driven state machine: `MFCINST_INIT → GOT_INST → HEAD_PRODUCED → RUNNING → FINISHING → FINISHED`

### Encoding Parameters

Parameters stored in `s5p_mfc_enc_params` within `s5p_mfc_ctx`. The `s_ctrl` callback uses a massive switch statement. When HW is ready, `s5p_mfc_set_enc_params()` programs registers.

### Buffer Management
- **Source**: Multi-planar (2-3 planes for NV12M/YUV420M). After encoding, buffers move to `ref_queue`.
- **Destination**: Single plane for compressed stream.
- **Internal**: Reference DPB, ME, TMV, scratch buffers allocated via `s5p_mfc_alloc_priv_buf()`.

### DT Binding
`samsung,s5p-mfc.yaml`: compatible (mfc-v5 through v10), reg, clocks (1-3), interrupts (1), iommus (1-2), power-domains, memory-region (firmware).

---

## 2. Verisilicon/Hantro Encoder Support

**Location:** `drivers/media/platform/verisilicon/`
**Total driver:** 21,035 lines (42 files)

**Encoder support: JPEG only.** No H.264/H.265/VP8 video encoding.

Encoder files:
- `hantro_h1_jpeg_enc.c` (166 lines)
- `rockchip_vpu2_hw_jpeg_enc.c` (197 lines)
- `hantro_jpeg.c` (245 lines)
- `hantro_jpeg.h` (15 lines)

Only `HANTRO_MODE_JPEG_ENC` mode. Stateless JPEG encoder, not a stateful video encoder.

---

## 3. MediaTek VENC

**Location:** `drivers/media/platform/mediatek/vcodec/encoder/`
**Encoder-specific:** 4,590 lines (14 files)
**Common shared with decoder:** 1,186 lines

Key files:
- `mtk_vcodec_enc.c` (1,426 lines) — V4L2 ioctl ops, controls, format negotiation
- `mtk_vcodec_enc_drv.c` (488 lines) — Platform driver probe, IRQ, video device registration
- `mtk_vcodec_enc_pm.c` (105 lines) — Clock/power management
- `venc_vpu_if.c` (377 lines) — VPU firmware communication
- `venc/venc_h264_if.c` (819 lines) — H.264 encoder interface
- `venc/venc_vp8_if.c` (441 lines) — VP8 encoder interface

**Notable differences from Samsung:**
- **Uses v4l2_m2m framework**: `v4l2_m2m_ctx_init()`, `v4l2_m2m_ioctl_*` helpers
- **VPU-based**: Encoding runs on co-processor; kernel sends commands
- **Fewer controls**: ~12 (bitrate, B-frames, RC, H264 profile/level, GOP, VP8 profile, force key frame)
- **Codecs**: H.264 and VP8 only

---

## 4. Rockchip Encoder

**Location:** `drivers/media/platform/rockchip/`

**Encoder support: NONE.** Directory contains:
- `rga/` — 2D graphics accelerator
- `rkcif/` — Camera interface
- `rkisp1/` — Image signal processor
- `rkvdec/` — Video DECODER only (H.264, HEVC, VP9)

No mainline encoder driver for any Rockchip SoC. JPEG encoder for Rockchip exists in Hantro/Verisilicon driver (`rockchip_vpu2_hw_jpeg_enc.c`), JPEG-only.

---

## 5. Implementation Scope Estimate

### Minimum Viable: H.264 Only

| Component | Estimated Lines | Notes |
|-----------|----------------|-------|
| Platform driver (probe, remove, DT, PM) | 300-400 | Based on `mtk_vcodec_enc_drv.c` |
| V4L2 encoder (ioctls, VB2, controls) | 1,200-1,500 | Based on `mtk_vcodec_enc.c` |
| HW register programming | 800-1,200 | H.264-only from `opr_v6.c` encoder functions |
| Register definitions header | 200-400 | Based on Rockchip TRM |
| Common headers / data structures | 200-300 | |
| Power/clock management | 80-120 | Similar to `s5p_mfc_pm.c` |
| DT binding YAML | 60-80 | |
| Kconfig + Makefile | 20-30 | |
| **Total MVP** | **~2,900-4,000** | |

### Full-Featured: H.264 + H.265

| Component | Estimated Lines | Notes |
|-----------|----------------|-------|
| Platform driver | 400-500 | Multi-codec dispatch, suspend/resume |
| V4L2 encoder | 2,000-2,500 | Full control set for both codecs |
| HW register programming | 1,500-2,000 | Two codec paths, reference buffer mgmt |
| Register definitions | 400-600 | |
| Common headers | 300-500 | Per-codec param structs |
| Power/clock management | 100-150 | |
| DT binding YAML | 80-100 | |
| Kconfig + Makefile | 20-30 | |
| **Total Full** | **~4,800-6,400** | |

### Design Recommendations

1. **Use v4l2_m2m framework** (like MediaTek, not Samsung's custom scheduling)
2. **Stateless API** required for upstream acceptance
3. **Placement**: `drivers/media/platform/rockchip/rkvenc/`
4. **Realistic target**: ~5,000-6,000 lines for upstream-quality H.264+HEVC
