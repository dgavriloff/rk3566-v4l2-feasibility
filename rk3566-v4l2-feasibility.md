# RK3566 V4L2 H.264 Encoder Driver: Feasibility Report

**Date**: 2026-03-01
**Scope**: Evaluate feasibility and value of building a V4L2 kernel driver for H.264 hardware encoding on the Rockchip RK3566

---

## Executive Summary

Building a V4L2 H.264 encoder driver for the RK3566 is **technically feasible but strategically premature**. The RK3566 contains two encoder blocks: a Hantro VEPU121 (JPEG-only in mainline) and a Rockchip-proprietary RKVENC/VEPU540 (no mainline driver at all). The VEPU540 is the production encoder capable of 1080p60 H.264/H.265. The MPP reference code provides an excellent register-level reference with clean bitfield structures, and the RK3568 TRM Part 2 is publicly available. However, the **V4L2 stateless encoding uAPI itself is still in RFC stage** (most recent: Marco Felsch's VC8000E RFC, May 2025), meaning any driver written today would target an unstable API. Community demand is strong (30+ threads across GitHub, forums, and wikis, driven heavily by Home Assistant Green users), and ~5,000-6,000 lines of new kernel code would be needed. The recommendation is **conditional go**: begin prototyping against the emerging stateless encoder uAPI now, with the expectation of submitting upstream once the uAPI stabilizes, likely in late 2026 or 2027.

---

## Existing Work

### Mainline Kernel Status

| Component | Status | Details |
|-----------|--------|---------|
| VEPU121 JPEG encoding (RK3566/RK3568) | **Merged** (~kernel 6.0) | Nicolas Frattaroli, v5 patch series, June 2022. Files: `drivers/media/platform/verisilicon/rockchip_vpu2_hw_jpeg_enc.c`, DT binding `rockchip,rk3568-vepu.yaml` |
| VEPU121 JPEG encoding (RK3588) | **Merged** (kernel 6.12) | Sebastian Reichel (Collabora), Aug 2024. Reuses `rk3568_vepu_variant` |
| RKVENC/VEPU540 H.264/H.265 encoding | **No driver, no patches submitted** | Power domain infrastructure (`RK3568_PD_RKVENC`) exists in mainline, but no device consumes it |
| VEPU121 H.264 encoding | **Not supported** | Hardware is theoretically capable, but Bootlin's H1 encoder code produced unusable output when tested against VEPU121 on RK3566 |
| Hantro/Verisilicon video encoding (any codec beyond JPEG) | **Not supported on any SoC** | The mainline Verisilicon driver supports `HANTRO_JPEG_ENCODER` only |

### Mailing List Activity

| Patch/RFC | Author | Date | Status |
|-----------|--------|------|--------|
| H.264 stateless encoder uAPI + Hantro H1 driver (RFC) | Andrzej Pietrasiewicz (Collabora) | Nov 2023 | RFC only, not merged |
| VP8 stateless encoder for Hantro H1 (RFC) | Hugues Fruchet (STMicro) | Oct 2023 | RFC only, not merged |
| VC8000E H.264 V4L2 Stateless Encoder (RFC/v1) | Marco Felsch (Pengutronix) | May 2025 | "Very early state", targets NXP i.MX8MP |
| Hantro H1 encoding upstreaming discussion | Paul Kocialkowski (Bootlin) | Jan 2025 | Discussion only; proposed BPF for rate control |

**No one has submitted RKVENC/VEPU540 encoder patches to LKML.** Michael Grzeschik (Pengutronix) mentioned working on an rkvenc driver in a Dec 2022 mailing list thread, but no patches materialized.

### Out-of-Tree Work

| Project | Author | Status |
|---------|--------|--------|
| [bootlin/linux `hantro/h264-encoding-v5.11`](https://github.com/bootlin/linux) | Paul Kocialkowski (Bootlin) | Out-of-tree, never submitted, targets Hantro H1 on RK3399 |
| [bootlin/v4l2-hantro-h264-encoder](https://github.com/bootlin/v4l2-hantro-h264-encoder) | Bootlin | Userspace companion for above |
| [nyanmisaka/ffmpeg-rockchip](https://github.com/nyanmisaka/ffmpeg-rockchip) | nyanmisaka | FFmpeg fork using MPP library (userspace, not V4L2). De facto standard for Jellyfin/Frigate/go2rtc on Rockchip |

No out-of-tree V4L2 kernel drivers for the RKVENC/VEPU540 were found on GitHub.

---

## Hardware IP Analysis

### Dual Encoder Architecture

The RK3566 contains **two independent encoder IP blocks**:

#### Block A: VEPU121 (Hantro/Verisilicon)
- **Address**: `0xFDEE0000`
- **Power domain**: `RK3568_PD_RGA`
- **Capabilities**: JPEG, H.264 (up to High Profile L4.1), VP8
- **Architecture**: Hantro/Verisilicon stateless encoder. Register layout nearly identical to RK3399's H1 encoder
- **Mainline**: JPEG encoding only (`rockchip,rk3568-vepu` compatible)
- **Relevance**: Could theoretically be extended for H.264, but test results against VEPU121 produced unusable output. Lower priority target

#### Block B: RKVENC/VEPU540 (Rockchip proprietary)
- **Address**: `0xFDF40000`
- **IOMMU**: `0xFDF40F00` (dual-channel)
- **Power domain**: `RK3568_PD_RKVENC` (dedicated)
- **Clocks**: `ACLK_RKVENC`, `HCLK_RKVENC`, `CLK_RKVENC_CORE`
- **Capabilities**: H.264, H.265/HEVC at 1080p60
- **Architecture**: Rockchip-proprietary, no relation to Hantro/Verisilicon
- **Mainline**: No driver, no DT node, no patches. Only the power domain exists
- **Relevance**: **This is the target for a new driver**

### Cross-SoC IP Sharing

| SoC | VEPU121 (Hantro) | RKVENC variant | Shared HAL code |
|-----|-------------------|----------------|-----------------|
| RK3399/RK3288/RK3328 | Hantro H1 | None | Bootlin H1 reference exists |
| RV1126 | Unknown | VEPU540/541 | Same `hal_h264e_vepu541.c` HAL |
| **RK3566/RK3568** | **VEPU121** | **VEPU540** | `hal_h264e_vepu541.c` with `is_vepu540=1` |
| RK3588 | VEPU121 (multiple) | VEPU580 | Different HAL (`hal_h264e_vepu580.c`) |

The RK3566 VEPU540 shares its HAL code with the RV1126. The VEPU541 HAL handles both variants via a single `is_vepu540` flag with only minor differences (ME RAM calculation, AXI response register layout). **A driver written for RK3566 VEPU540 would likely also support RK3568 and RV1126 with minimal changes.**

### Documentation Availability

| Document | Available | URL |
|----------|-----------|-----|
| RK3568 TRM Part 2 (Chapter 10: Video Encoder/Decoder, p.463) | **Yes** | [Radxa mirror (PDF)](https://dl.radxa.com/rock3/docs/hw/datasheet/Rockchip%20RK3568%20TRM%20Part2%20V1.1-20210301.pdf) |
| RK3568 TRM Part 1 | **Yes** | [Radxa mirror](https://dl.radxa.com/rock3/docs/hw/datasheet/Rockchip%20RK3568%20TRM%20Part1%20V1.1-20210301.pdf), [Internet Archive](https://archive.org/details/rockchip-rk-3568-trm-part-1-v-1.3-20220930) |
| RK3566 Datasheet V1.0 | **Yes** | [Pine64](https://files.pine64.org/doc/quartz64/Rockchip%20RK3566%20Datasheet%20V1.0-20201210.pdf) |
| VEPU540 register definitions (C headers) | **Yes** | MPP source: `hal_h264e_vepu541_reg.h` (~2487 lines), `hal_h264e_vepu541_reg_l2.h` (~412 lines) |
| Vendor kernel DT definitions | **Yes** | `rockchip-linux/kernel` branch `develop-4.19`, `rk3568.dtsi` |

**Assessment**: Hardware documentation is unusually good for a Rockchip IP block. Between the TRM Part 2 and the MPP register headers, the entire register interface is documented. No reverse engineering should be needed.

---

## MPP Reference Quality

### Source Access

The original `rockchip-linux/mpp` repository was **DMCA'd by FFmpeg** in December 2025 for LGPL code copying violations. The code remains accessible via [HermanChen/mpp](https://github.com/HermanChen/mpp) and hundreds of other forks.

### Code Structure Assessment

The MPP follows a clean layered architecture:

```
Codec layer (H.264 SPS/PPS/slice/DPB generation) → generic
    ↓
Rate control (frame-level QP, CBR/VBR/AVBR models) → generic
    ↓
HAL layer (register programming per VEPU variant) → hardware-specific
    ↓
Device layer (ioctl to /dev/mpp_service) → OS abstraction
```

### Register Map Quality: Excellent

The register headers use clean C bitfield structures with address offset comments. Example from `hal_h264e_vepu541_reg.h`:

```c
// reg001 @ 0x0004 - ENC_STRT
struct {
    RK_U32  lkt_num      : 8;
    RK_U32  rkvenc_cmd   : 2;   // 1=single frame, 2=link table
    RK_U32  reserved0    : 6;
    RK_U32  clk_gate_en  : 1;
    RK_U32  resetn_hw_en : 1;
    RK_U32  enc_done_tmvp_en : 1;
    RK_U32  reserved1    : 13;
} reg001;
```

Three register regions:
1. **L1 registers** (0x0000-0x0340): Main configuration, written as contiguous block
2. **L2 registers** (0x10004+): RDO tuning, written via indirect address/data ports
3. **Status registers** (0x001C, 0x0210-0x0340): Read back after encode completes

### Pipeline Structure: Clear

```
init → prepare → get_task → gen_regs → start → wait → ret_task → deinit
```

The `gen_regs` function calls sub-functions in a well-defined order:
1. `setup_vepu541_normal()` — base config, interrupts, clocks
2. `setup_vepu541_prep()` — input format, CSC, stride, rotation
3. `setup_vepu541_codec()` — H.264 syntax: NAL type, entropy mode, reference management
4. `setup_vepu541_rdo_pred()` — RDO prediction weights
5. `setup_vepu541_rc_base()` — rate control: QP range, CTU bit target
6. `setup_vepu541_io_buf()` — I/O buffer DMA addresses
7. `setup_vepu541_roi()` — ROI map
8. `setup_vepu541_recn_refr()` — reconstruction/reference frames
9. `setup_vepu541_split()` — slice splitting
10. `setup_vepu541_me()` — motion estimation parameters
11. `setup_vepu541_intra_refresh()` — intra refresh
12. `vepu541_set_osd()` — OSD overlay
13. `setup_vepu541_l2()` — L2 RDO tuning

### H.264 Features Supported by VEPU540

| Feature | Status | Notes |
|---------|--------|-------|
| Profiles | Baseline, Main, High | High profile with 8x8 transform and CABAC |
| Entropy coding | CAVLC + CABAC | CABAC for Main/High |
| Deblocking filter | Yes | Configurable alpha/beta offsets |
| Multiple references | Yes | Per PPS configuration |
| Long-term references | Yes | Up to 3 MMCO operations |
| Weighted prediction | Yes | Via register flag |
| Slice splitting | Yes | By MB rows or byte count |
| Intra refresh | Yes | Column-based rolling refresh |
| ROI encoding | Yes | 8 regions, absolute/relative QP |
| OSD overlay | Yes | 8 regions, 256-color palette |
| B-frames | **No** | Hardware has capability bit but HAL only programs I/P slices |
| Rate control (HW-assisted) | Yes | CTU-level QP with 5 threshold levels, 8 QP zones |
| Levels | 1.0 through 6.2 | Full level table in codec layer |

### Weaknesses

- HAL `.c` files have sparse comments — register headers are documented, but the "why" behind tuning values is missing
- Several magic lookup tables without derivation documentation (`h264e_klut_weight[30]`, `h264e_lambda_default[58]`)
- Some code duplication (MMCO handling repeated 3x)
- L2 tuning parameters have limited explanation

### Overall Assessment

**The MPP source is highly suitable as a kernel driver reference.** The register headers can be nearly directly translated to kernel register structures. The pipeline maps cleanly to V4L2 stateless encoder operations. The VEPU540 variant handling is minimal (single `is_vepu540` flag, handful of conditionals).

---

## Implementation Scope Estimate

### Reference: Existing V4L2 Encoder Drivers

| Driver | Location | Encoder LOC | Codecs | Framework |
|--------|----------|-------------|--------|-----------|
| Samsung S5P-MFC | `drivers/media/platform/samsung/s5p-mfc/` | ~4,700-5,100 | H.264, HEVC, MPEG4, H.263, VP8 | Custom scheduling |
| MediaTek VENC | `drivers/media/platform/mediatek/vcodec/encoder/` | ~4,590 | H.264, VP8 | v4l2_m2m framework |
| Hantro/Verisilicon (encoder) | `drivers/media/platform/verisilicon/` | ~600 | JPEG only | Stateless |

The Samsung MFC at ~14,000 total lines supports 6+ HW generations and 5 codecs. The MediaTek VENC at ~4,590 lines (H.264+VP8 only) is a better size comparison.

### Estimated New Code for RK3566 RKVENC Driver

#### Minimum Viable: H.264 Stateless Encoder

| Component | Estimated Lines | Reference |
|-----------|----------------|-----------|
| Platform driver (probe, remove, DT, PM) | 300-400 | `mtk_vcodec_enc_drv.c` |
| V4L2 encoder (ioctls, VB2, controls) | 1,200-1,500 | `mtk_vcodec_enc.c` |
| HW register programming | 800-1,200 | `hal_h264e_vepu541.c` (subset) |
| Register definitions header | 400-600 | `hal_h264e_vepu541_reg.h` (translated) |
| Common headers / data structures | 200-300 | |
| Power/clock management | 80-120 | `s5p_mfc_pm.c` |
| DT binding YAML + DT node | 80-100 | |
| Kconfig + Makefile | 20-30 | |
| **Total MVP** | **~3,100-4,250** | |

#### Full-Featured: H.264 + H.265

| Component | Estimated Lines | Notes |
|-----------|----------------|-------|
| Platform driver | 400-500 | Multi-codec dispatch |
| V4L2 encoder | 2,000-2,500 | Full control set for both codecs |
| HW register programming | 1,500-2,000 | Two codec paths |
| Register definitions | 600-800 | Both codec register sets |
| Common headers | 300-500 | Per-codec param structs |
| Power/clock management | 100-150 | |
| DT binding + node | 80-100 | |
| Kconfig + Makefile | 20-30 | |
| **Total Full** | **~5,000-6,600** | |

### Key Design Decisions

1. **Must be stateless**: The emerging V4L2 stateless encoding uAPI is the only path that will be accepted upstream for new encoder drivers
2. **Should use v4l2_m2m framework**: Eliminates hundreds of lines of scheduling boilerplate
3. **Placement**: `drivers/media/platform/rockchip/rkvenc/` (paralleling `rkvdec/`)
4. **Rate control**: Likely needs userspace implementation; the January 2025 LKML discussion proposed BPF as an alternative, but no consensus yet

---

## Community Demand

### Quantified Summary

| Source | Unique Threads/Issues |
|--------|----------------------|
| GitHub issues/discussions | 18+ |
| Hardware/community forums | 15+ |
| Wiki/documentation pages noting the gap | 5 |
| **Total** | **30+** |

### Top Use Cases (by frequency)

1. **Media server transcoding (Jellyfin/Emby/Plex)** — ~10 threads — Users want hardware H.264/HEVC encoding for real-time stream transcoding on SBC media servers
2. **Security camera / NVR (Frigate, go2rtc)** — ~8 threads — H.265-to-H.264 transcoding for camera feeds
3. **Home Assistant camera streaming** — ~5 threads — Home Assistant Green (RK3566) users wanting hardware-accelerated camera processing
4. **Photo/video library (Immich)** — ~2 threads — Self-hosted photo library transcoding
5. **FPV/drone video** — ~2 threads — Low-latency video on compact RK3566 boards
6. **Live streaming (OBS, RTMP)** — ~2 threads — GStreamer/RTMP pipelines
7. **General embedded encoding** — ~3 threads — Industrial camera-to-H264 pipelines

### Key Demand Drivers

- **Home Assistant Green**: Ships with RK3566, sold to mainstream home automation users. Single biggest demand driver for hardware encoding on this SoC. The go2rtc project (built into Home Assistant since 2024.11) has active issues requesting Rockchip hardware encoding presets ([go2rtc #768](https://github.com/AlexxIT/go2rtc/issues/768), [go2rtc PR #1203](https://github.com/AlexxIT/go2rtc/pull/1203))
- **nyanmisaka/ffmpeg-rockchip**: Community-maintained FFmpeg fork is the de facto standard referenced by Jellyfin, Frigate, go2rtc, and Immich. Entire projects exist because mainline lacks this support
- **Radxa forum frustration**: One thread literally titled ["Has anyone actually succeeded in getting hardware video encoding working?"](https://forum.radxa.com/t/has-anyone-actually-succeeded-in-getting-hardware-video-encoding-working/15894)
- **Pine64 wiki**: Explicitly lists hardware video encoding as ["one of the big missing pieces"](https://wiki.pine64.org/wiki/Mainline_Hardware_Encoding) for RK3566

### Notable References

- [Jellyfin Rockchip docs](https://jellyfin.org/docs/general/post-install/transcoding/hardware-acceleration/rockchip/): RK356x listed as "may be supported but unable to test"
- [Collabora RK3588 roadmap](https://www.collabora.com/news-and-blog/news-and-events/rockchip-rk3588-upstream-support-progress-future-plans.html): Hardware video encoding listed as "big missing piece" across all Rockchip SoCs; RK3566 not being actively targeted
- [Radxa: "Please open H264 hardware encoding/decoding for RK3566"](https://forum.radxa.com/t/rock3c-please-open-h264-hardware-encoding-decoding-for-rk3566/17151)
- [Pine64 forum: "Hardware h264 video encoding"](https://forum.pine64.org/showthread.php?tid=17002)
- [Emby forum: "HW acceleration (decode+encode) with RK3568?"](https://emby.media/community/index.php?/topic/125680-hw-acceleration-decode-encode-with-rk3568/) (2-page thread)

---

## Risk Factors

### High Risk

1. **Unstable uAPI**: The V4L2 stateless encoding uAPI is still in RFC stage. Writing a driver against it means potentially major rewrites as the API evolves. The most recent RFC (VC8000E, May 2025) self-describes as "very very early state." No timeline for finalization.

2. **Rate control design unsettled**: The kernel community hasn't agreed on where rate control should live. Options discussed: purely userspace, kernel-side, or BPF programs. This is a fundamental architectural question that affects driver design.

3. **MPP DMCA status**: The primary reference implementation (`rockchip-linux/mpp`) was DMCA'd by FFmpeg in December 2025. While forks exist, the legal status of using MPP as a reference is murky. The DMCA was about copied FFmpeg code, not about register definitions, but a cautious developer might want legal review.

### Medium Risk

4. **No existing rkvenc framework**: Unlike the Hantro VEPU121 (which has an existing mainline driver to extend), the RKVENC/VEPU540 requires a completely new driver from scratch. There is no existing Rockchip encoder driver pattern to follow.

5. **Interrupt handling complexity**: The VEPU540 has dual MMU channels and multiple interrupt sources. The MPP userspace uses polling, so the interrupt behavior needs to be validated against actual hardware.

6. **Testing hardware access**: Need physical RK3566 or RK3568 boards for development and testing. Cannot be done in emulation.

### Low Risk

7. **Register documentation**: Between the TRM and MPP headers, the register interface is well-documented. Low reverse-engineering risk.

8. **Code scope**: At ~5,000-6,000 lines for a full H.264+H.265 driver, this is a substantial but manageable project for an experienced kernel developer.

9. **Community review**: Rockchip V4L2 patches are actively reviewed by Collabora, Pengutronix, and Bootlin developers. Getting review attention should not be a problem.

---

## Recommendation

### Verdict: Conditional Go

**Start prototyping now, target upstream submission when the stateless encoding uAPI stabilizes.**

#### Reasoning

1. **The demand is real and growing.** 30+ community threads, driven by the Home Assistant Green (mainstream consumer product) and self-hosted media server use cases. This isn't niche hobbyist territory.

2. **The reference materials are excellent.** The MPP register headers, the RK3568 TRM Part 2, and the clean pipeline structure in the MPP HAL make this more approachable than most embedded encoder bring-up projects.

3. **The uAPI is the blocker, not the hardware.** The V4L2 stateless encoder uAPI must stabilize before any encoder driver can be merged upstream. Working on the hardware-specific parts now means you'll be ready to submit when the API is ready.

4. **Multi-SoC benefit.** A VEPU540 driver would cover RK3566, RK3568, and RV1126 — three SoCs with one driver. This increases the value proposition.

#### Suggested Approach

1. **Phase 1 (now)**: Build an out-of-tree prototype targeting the latest stateless encoder uAPI RFC. Use the VC8000E RFC series (May 2025) as the API template. Focus on H.264 Baseline/Main profile, I/P frames only.

2. **Phase 2 (when uAPI stabilizes)**: Adapt to the finalized API. Add H.265 support. Add full control set (rate control, ROI, slice splitting). Write DT bindings.

3. **Phase 3 (upstream)**: Submit RFC to linux-media, incorporating feedback. Target inclusion once the stateless encoder framework lands.

#### Alternative: VEPU121 H.264 Path

A lower-risk alternative is extending the existing Hantro/Verisilicon driver with H.264 encoding support for the VEPU121 block. Bootlin's out-of-tree work provides a starting point, and the Hantro driver framework already exists in mainline. However, the VEPU121 reportedly produced unusable H.264 output on RK3566 when tested, and even if fixed, the VEPU121 is a simpler encoder that likely won't match the VEPU540's quality and performance at 1080p60. This path has lower risk but also lower reward.

---

## Appendix: Key File References

### Mainline Kernel (torvalds/linux)
- `drivers/media/platform/verisilicon/rockchip_vpu_hw.c` — `rk3568_vepu_variant` (JPEG only)
- `drivers/media/platform/verisilicon/rockchip_vpu2_hw_jpeg_enc.c` — JPEG encode HW impl
- `drivers/media/platform/verisilicon/hantro_drv.c:721` — `rockchip,rk3568-vepu` compatible
- `arch/arm64/boot/dts/rockchip/rk356x-base.dtsi:602-620` — VEPU DT node
- `include/dt-bindings/power/rk3568-power.h` — `RK3568_PD_RKVENC` power domain
- `drivers/pmdomain/rockchip/pm-domains.c` — RKVENC power domain definition
- `drivers/media/platform/rockchip/rkvdec/` — Decoder driver (pattern reference)

### MPP Reference (HermanChen/mpp fork)
- `mpp/hal/rkenc/h264e/hal_h264e_vepu541.c` (~1872 lines) — Full HAL implementation
- `mpp/hal/rkenc/h264e/hal_h264e_vepu541_reg.h` (~2487 lines) — Register bitfield definitions
- `mpp/hal/rkenc/h264e/hal_h264e_vepu541_reg_l2.h` (~412 lines) — L2 register definitions
- `mpp/hal/rkenc/h265e/hal_h265e_vepu541.c` — H.265 HAL implementation
- `mpp/hal/rkenc/h265e/hal_h265e_vepu541_reg.h` — H.265 register definitions
- `mpp/hal/rkenc/common/vepu541_common.c` — Shared ROI/OSD code
- `mpp/soc.cmake` — SoC-to-encoder IP mapping

### Documentation
- [RK3568 TRM Part 2 V1.1 (PDF)](https://dl.radxa.com/rock3/docs/hw/datasheet/Rockchip%20RK3568%20TRM%20Part2%20V1.1-20210301.pdf) — Chapter 10, p.463
- [RK3568 TRM Part 1 V1.1 (PDF)](https://dl.radxa.com/rock3/docs/hw/datasheet/Rockchip%20RK3568%20TRM%20Part1%20V1.1-20210301.pdf)

### Mailing List References
- [RFC: H.264 stateless encoder uAPI (Nov 2023)](https://patchwork.linuxtv.org/project/linux-media/cover/20231116154816.70959-1-andrzej.p@collabora.com/)
- [RFC: VP8 H1 stateless encoding (Oct 2023)](https://patchwork.linuxtv.org/project/linux-media/cover/20231004103720.3540436-1-hugues.fruchet@foss.st.com/)
- [v1: VC8000E H.264 V4L2 Stateless Encoder (May 2025)](https://patchew.org/linux/20250502150513.4169098-1-m.felsch@pengutronix.de/)
- [JPEG Encoder RK3566/RK3568 v5 (Jun 2022)](https://patchwork.kernel.org/project/linux-media/cover/20220612155346.16288-1-frattaroli.nicolas@gmail.com/)
- [FOSDEM 2024: V4L2 Stateless Video Encoding](https://archive.fosdem.org/2024/schedule/event/fosdem-2024-3090-v4l2-stateless-video-encoding-hardware-support-and-uapi/)
- [Hantro H1 encoding upstreaming discussion (Jan 2025)](https://lore.kernel.org/linux-media/)
- [rkvenc mention in AV1 decoder thread (Dec 2022)](https://lore.kernel.org/linux-rockchip/20221220134003.GC26315@pengutronix.de/)

### Community Demand References
- [go2rtc #768: Rockchip hardware acceleration](https://github.com/AlexxIT/go2rtc/issues/768)
- [Frigate #9952: RK3566 ffmpeg h264_rkmpp_decoder](https://github.com/blakeblackshear/frigate/discussions/9952)
- [Immich #13579: RKMPP not using hardware decoding](https://github.com/immich-app/immich/issues/13579)
- [Jellyfin FFmpeg #34: Rockchip hwaccel support](https://github.com/jellyfin/jellyfin-ffmpeg/issues/34)
- [Radxa forum: "Please open H264 hardware encoding/decoding for RK3566"](https://forum.radxa.com/t/rock3c-please-open-h264-hardware-encoding-decoding-for-rk3566/17151)
- [Pine64 forum: Hardware h264 video encoding](https://forum.pine64.org/showthread.php?tid=17002)
- [Pine64 wiki: Mainline Hardware Encoding](https://wiki.pine64.org/wiki/Mainline_Hardware_Encoding)
- [Collabora: RK3588 upstream status](https://www.collabora.com/news-and-blog/news-and-events/rockchip-rk3588-upstream-support-progress-future-plans.html)
- [MPP DMCA takedown (Dec 2025)](https://github.com/github/dmca/blob/master/2025/12/2025-12-18-ffmpeg.md)
