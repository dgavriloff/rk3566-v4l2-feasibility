# RKVENC VEPU540 V4L2 Encoder Driver — Implementation Plan

**Target hardware**: RK3566 / RK3568 / RV1126 (VEPU540 encoder block)
**Driver location**: `drivers/media/platform/rockchip/rkvenc/`
**Framework**: V4L2 m2m, stateless encoder uAPI (based on VC8000E RFC, May 2025)
**Estimated total**: ~5,000-6,000 lines (H.264 + H.265)

---

## Phase 1: Register Definitions & Platform Skeleton

### Step 1.1: Translate VEPU540 register headers

Translate `hal_h264e_vepu541_reg.h` (~2487 lines) and `hal_h264e_vepu541_reg_l2.h` (~412 lines) from MPP's C bitfield structs into kernel-style register definitions.

**Input**: MPP register headers (HermanChen/mpp fork)
**Output**: `rkvenc_regs.h`

Work items:
- Define L1 register struct (`rkvenc_h264_regs`) covering ~209 registers at 0x0000-0x0340
- Define L2 register region (0x10004+) for RDO tuning, using indirect address/data port pattern
- Define status register readback struct (0x0210-0x0340): bitstream length, SSE, QP distribution, partition counts
- Use kernel `BIT()` and `GENMASK()` macros instead of raw bitfields for portability
- Map MPP `Vepu541H264eRegSet` field names to kernel naming conventions (`RKVENC_REG_*`)

Register regions to cover:
```
0x0000-0x003C  Control: version, start, clear, link table
0x0010-0x001C  Interrupt: enable, mask, clear, status
0x0030-0x015C  Configuration: resolution, picture type, source format, RC, ROI
0x0110-0x015C  DMA addresses: source buffers, reference frames, bitstream output
0x0164-0x016C  Motion estimation
0x0170-0x01EC  H.264 syntax: NAL, SPS, PPS, slice header fields
0x0210-0x0340  Status readback: bs_length, SSE, QP stats, MADI/MADP
```

### Step 1.2: Platform driver skeleton

**Output**: `rkvenc_drv.c`, `rkvenc.h`

Work items:
- `struct rkvenc_dev`: device, v4l2_device, video_device, v4l2_m2m_dev, mutex, clocks, regs base
- `struct rkvenc_ctx`: v4l2_fh, v4l2_ctrl_handler, codec params, format state
- `rkvenc_probe()`: clk_bulk_get for `ACLK_RKVENC`, `HCLK_RKVENC`, `CLK_RKVENC_CORE`; devm_ioremap_resource at `0xFDF40000`; pm_runtime_enable; v4l2_device_register; video_register_device
- `rkvenc_remove()`: reverse of probe
- `rkvenc_runtime_suspend/resume()`: clk_bulk_disable/enable, pm_domain interaction with `RK3568_PD_RKVENC`
- `of_device_id` table: `"rockchip,rk3568-rkvenc"` (covers RK3566/RK3568)

### Step 1.3: Device tree binding & node

**Output**: `rockchip,rk3568-rkvenc.yaml`, DT node patch

Work items:
- YAML binding: compatible, reg (0xFDF40000 size 0x400), interrupts (GIC_SPI 140), clocks (3), clock-names, power-domains (`RK3568_PD_RKVENC`), iommus
- DT node for `rk356x-base.dtsi`:
  ```
  rkvenc: video-codec@fdf40000 {
      compatible = "rockchip,rk3568-rkvenc";
      reg = <0x0 0xfdf40000 0x0 0x400>;
      interrupts = <GIC_SPI 140 IRQ_TYPE_LEVEL_HIGH>;
      clocks = <&cru ACLK_RKVENC>, <&cru HCLK_RKVENC>, <&cru CLK_RKVENC_CORE>;
      clock-names = "aclk", "hclk", "core";
      power-domains = <&power RK3568_PD_RKVENC>;
      resets = <&cru SRST_A_RKVENC>, <&cru SRST_H_RKVENC>, <&cru SRST_RKVENC_CORE>;
      reset-names = "axi", "ahb", "core";
      iommus = <&rkvenc_mmu>;
  };

  rkvenc_mmu: iommu@fdf40f00 {
      compatible = "rockchip,iommu";
      reg = <0x0 0xfdf40f00 0x0 0x40>,
            <0x0 0xfdf40f40 0x0 0x40>;
      interrupts = <GIC_SPI 141 IRQ_TYPE_LEVEL_HIGH>,
                   <GIC_SPI 142 IRQ_TYPE_LEVEL_HIGH>;
      clocks = <&cru ACLK_RKVENC>, <&cru HCLK_RKVENC>;
      clock-names = "aclk", "iface";
      power-domains = <&power RK3568_PD_RKVENC>;
      #iommu-cells = <0>;
  };
  ```

---

### STEP BACK 1: Platform driver validation

Before writing any V4L2 or encoding logic, verify the platform skeleton works:

- [ ] **Build test**: Does the module compile against a target kernel (6.x) without errors?
- [ ] **DT binding check**: `dt_binding_check` passes on the YAML binding?
- [ ] **Probe test** (requires hardware): Does the driver probe successfully? Do clocks enable? Does `pm_runtime_get_sync` succeed? Can you read the version register (`reg000` at 0x0000) and confirm `h264_enc=1`, `h265_enc=1`?
- [ ] **Register sanity**: Read `reg000` (VERSION). Confirm `rkvenc_ver` matches expected value. This proves register base mapping is correct.
- [ ] **IOMMU**: Does the IOMMU at 0xFDF40F00 attach correctly? Can you allocate a DMA buffer and get a valid IOVA?

**Why**: If the platform layer is wrong (wrong clock names, wrong register base, wrong power domain), everything built on top will fail in confusing ways. Catch it here.

---

## Phase 2: V4L2 M2M Encoder Framework

### Step 2.1: V4L2 device setup

**Output**: `rkvenc_v4l2.c`

Work items:
- Register `video_device` with `V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING`
- OUTPUT queue (raw input): `V4L2_PIX_FMT_NV12` (primary), `V4L2_PIX_FMT_YUV420M` (secondary)
- CAPTURE queue (compressed output): `V4L2_PIX_FMT_H264` (initially), `V4L2_PIX_FMT_HEVC` (Phase 4)
- Implement format negotiation: `enum_fmt`, `g_fmt`, `s_fmt`, `try_fmt`
- Resolution constraints: 96x96 minimum, 1920x1088 maximum (MB-aligned), step 16
- VB2 queue ops: `queue_setup`, `buf_prepare`, `buf_queue`, `start_streaming`, `stop_streaming`
- Use `vb2_dma_contig_memops` for DMA buffer allocation

### Step 2.2: V4L2 controls

Work items (MVP control set):
- `V4L2_CID_MPEG_VIDEO_H264_PROFILE` (Baseline, Main, High)
- `V4L2_CID_MPEG_VIDEO_H264_LEVEL` (1.0 - 5.1)
- `V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE` (CAVLC, CABAC)
- `V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM` (High profile)
- `V4L2_CID_MPEG_VIDEO_GOP_SIZE`
- `V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME`
- `V4L2_CID_MPEG_VIDEO_H264_QUANTIZATION` (or per-frame QP for stateless)
- `V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP`
- `V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP`
- `V4L2_CID_MPEG_VIDEO_H264_MIN_QP`
- `V4L2_CID_MPEG_VIDEO_H264_MAX_QP`

Stateless-specific controls (per VC8000E RFC pattern):
- Encode parameters passed per-frame via request API (`V4L2_BUF_FLAG_REQUEST_FD`)
- Frame type (I/P), reference frame index, QP

### Step 2.3: Request API integration

Work items:
- Implement `v4l2_m2m_request_validate` and `v4l2_m2m_request_queue`
- Per-frame parameter passing via `v4l2_ctrl_request_setup`/`complete`
- Stateless model: userspace is responsible for reference frame management, driver just programs what it's told

---

### STEP BACK 2: V4L2 framework validation

Before touching hardware registers for encoding, verify the V4L2 layer is correct:

- [ ] **v4l2-compliance**: Run `v4l2-compliance -d /dev/videoN` — it should pass all non-streaming tests (querycap, enum_fmt, g/s_fmt, controls, reqbufs)
- [ ] **Format negotiation**: Does setting NV12 on OUTPUT and H264 on CAPTURE work? Do resolution constraints enforce MB alignment?
- [ ] **Buffer allocation**: Can you allocate VB2 buffers on both queues? Do DMA addresses look valid?
- [ ] **Control validation**: Do profile/level/QP controls accept valid ranges and reject invalid ones?
- [ ] **Request API**: Can you create a request, attach controls, queue buffers with request FDs?

**Why**: V4L2 compliance issues found after hardware encoding is wired up are much harder to debug. The V4L2 layer should be airtight before adding HW complexity.

---

## Phase 3: H.264 Hardware Encoding

### Step 3.1: Internal buffer allocation

Work items:
- Reconstruction frame buffers: **2 frames minimum** (ping-pong: one written as reconstruction, one read as reference). Allocated at `start_streaming`, freed at `stop_streaming`. Size per buffer:
  - Luma: `align(width, 64) * align(height, 64)`
  - Chroma (4:2:0): `align(width, 64) * align(height, 64) / 2`
  - Collocated MV data: `(width/16) * (height/16) * 16` bytes
  - Allocate with `dma_alloc_coherent()` — must be IOMMU-mapped
- Bitstream output buffer: from userspace via CAPTURE VB2 queue. If `bsf_ovflw` fires, buffer was too small.
- ME (motion estimation) scratch buffer: size depends on resolution AND variant — **VEPU540 (RK3566/RK3568) uses different ME RAM calculation than VEPU541 (RV1126)**. See MPP `setup_vepu541_me()` for both formulas.
- Reference frame management: ping-pong pattern. After each encode, swap reconstruction and reference pointers. For I-frames, no reference needed. For P-frames, previous reconstruction is the reference.

### Step 3.2: Register programming pipeline

Translate the MPP `gen_regs` pipeline into kernel functions. Each maps to a `setup_vepu541_*()` MPP function:

1. **`rkvenc_h264_setup_base()`** — from `setup_vepu541_normal()`
   - Enable interrupt on encode complete (`reg003.enc_done_en = 1`)
   - Clock gating, timeout config
   - Clear status registers

2. **`rkvenc_h264_setup_prep()`** — from `setup_vepu541_prep()`
   - Input pixel format (NV12/YUV420)
   - Source stride, resolution (in MBs)
   - Color space conversion if needed
   - Rotation (skip for MVP)

3. **`rkvenc_h264_setup_codec()`** — from `setup_vepu541_codec()`
   - NAL unit type (IDR for I-frames, non-IDR for P-frames)
   - Profile/level IDC
   - Entropy mode (CAVLC/CABAC)
   - 8x8 transform enable
   - Deblocking filter params
   - POC, frame_num, idr_pic_id
   - Reference picture list (single ref for MVP)

4. **`rkvenc_h264_setup_rdo()`** — from `setup_vepu541_rdo_pred()`
   - RDO prediction weights
   - Lambda tables (`h264e_lambda_default[58]`) — copy as-is from MPP
   - KLUT weight table (`h264e_klut_weight[30]`) — copy as-is

5. **`rkvenc_h264_setup_rc()`** — from `setup_vepu541_rc_base()`
   - QP value (from userspace control)
   - QP min/max range
   - CTU-level QP thresholds (for HW-assisted RC)
   - MB-level bit budget (simplified: just set target QP)

6. **`rkvenc_h264_setup_buffers()`** — from `setup_vepu541_io_buf()`
   - Source luma/chroma DMA addresses (from OUTPUT VB2 buffer)
   - Bitstream output DMA address (from CAPTURE VB2 buffer)
   - Reference frame DMA address
   - Reconstruction frame DMA address

7. **`rkvenc_h264_setup_ref()`** — from `setup_vepu541_recn_refr()`
   - Reconstruction buffer address (ping-pong between 2 buffers)
   - Reference buffer address (previous reconstruction)
   - Swap after each frame

8. **`rkvenc_h264_setup_me()`** — from `setup_vepu541_me()`
   - ME range (dependent on level)
   - ME RAM size calculation
   - `is_vepu540` branch: different ME RAM formula for RK3566/RK3568 vs RV1126

### Step 3.3: Encode kick & completion

Work items:
- **Kick**: Write L2 registers (via indirect port), write L1 registers (contiguous block write to MMIO), set `reg001.rkvenc_cmd = 1` (single frame encode)
- **IRQ handler**: Read interrupt status register (`reg007` at 0x001C), dispatch based on status bits:
  ```
  Bit 0: enc_done     — frame complete (success path)
  Bit 1: lkt_done     — link table complete (not used in single-frame mode)
  Bit 2: sclr_done    — safe-clear complete (abort acknowledgment)
  Bit 3: slc_done     — slice complete (for multi-slice notification)
  Bit 4: bsf_ovflw    — bitstream FIFO overflow (output buffer too small — ERROR)
  Bit 5: brsp_ostd    — AXI bus response outstanding error (ERROR)
  Bit 6: wbus_err     — AXI write bus error (ERROR)
  Bit 7: rbus_err     — AXI read bus error (ERROR)
  Bit 8: wdg          — watchdog timeout (ERROR)
  ```
  On `enc_done`: read status, call `v4l2_m2m_buf_done()` + `v4l2_m2m_job_finish()`
  On error bits: mark buffer `VB2_BUF_STATE_ERROR`, attempt recovery
  Always: write `reg006` (INT_CLR at 0x0018) to acknowledge interrupt
- **Completion**: Read `reg132.st_bsl.bs_lgth` (at 0x0210) for actual bitstream byte count, set on CAPTURE vb2_buffer `bytesused`. Read SSE/QP/MADI/MADP status registers for debug/statistics.
- **Error handling**: On `bsf_ovflw`: buffer was too small, return error, userspace should allocate larger. On `wbus_err`/`rbus_err`/`wdg`: reset encoder via `reg002.force_clr`, return error on current frame, allow next frame to proceed.

### Step 3.4: H.264 stream header generation

Work items:
- Decide: generate SPS/PPS in kernel or userspace?
- **Recommendation**: Generate in kernel for the initial driver. The VEPU540 does NOT generate SPS/PPS — it only produces slice data. The kernel driver must prepend SPS/PPS to IDR frames.
- Translate from MPP's `h264e_sps.c` / `h264e_pps.c` — simplified versions for supported parameter subset
- Alternatively: require userspace to provide SPS/PPS via a control, and only generate slice NALU in HW

---

### STEP BACK 3: First encode validation

This is the critical milestone — first hardware-produced H.264 output:

- [ ] **Single I-frame encode**: Feed one NV12 frame, get back H.264 data. Does the hardware interrupt fire? Is `bs_lgth > 0`?
- [ ] **Bitstream validation**: Run output through `ffprobe` or `h264_analyze`. Is it a valid H.264 NAL unit? Are SPS/PPS/slice headers parseable?
- [ ] **Visual check**: Decode the single I-frame with `ffmpeg -i output.h264 -frames:v 1 frame.png`. Does it look like the input?
- [ ] **I+P sequence**: Encode 2+ frames (I then P). Does the P-frame reference the I-frame correctly? Does `ffmpeg` decode the sequence without errors?
- [ ] **Error register check**: Are all error status bits clear after successful encode? Document any unexpected status values.
- [ ] **Timing**: How long does a single 1080p encode take? Compare against expected throughput (1080p60 = ~16.6ms per frame).
- [ ] **Memory**: Are DMA buffers freed correctly on stop_streaming? No leaks after repeated start/stop cycles?

**Why**: This is the highest-risk phase. If the register programming is wrong, the hardware will either hang, produce garbage, or silently corrupt memory. Validate thoroughly before adding features.

---

## Phase 4: H.265/HEVC Encoding

### Step 4.1: H.265 register definitions

**Output**: Extend `rkvenc_regs.h` or add `rkvenc_h265_regs.h`

Work items:
- Translate `hal_h265e_vepu541_reg.h` from MPP
- The VEPU540 shares most registers between H.264 and H.265 — the codec-specific differences are in the syntax element registers (0x0170-0x01EC region)
- CTU size configuration (H.265 uses 64x64 CTU vs H.264 16x16 MB)

### Step 4.2: H.265 register programming

**Output**: `rkvenc_h265.c`

Work items:
- `rkvenc_h265_setup_codec()` — VPS/SPS/PPS syntax elements
- CTU-level configuration vs MB-level
- H.265 reference picture management (RPS vs H.264's frame_num/POC)
- H.265 deblocking + SAO filter configuration
- Reuse: `setup_base`, `setup_prep`, `setup_buffers`, `setup_me` are shared with H.264

### Step 4.3: V4L2 H.265 controls

Work items:
- `V4L2_PIX_FMT_HEVC` on CAPTURE queue
- `V4L2_CID_MPEG_VIDEO_HEVC_PROFILE` (Main)
- `V4L2_CID_MPEG_VIDEO_HEVC_LEVEL`
- `V4L2_CID_MPEG_VIDEO_HEVC_TIER`
- QP controls for HEVC

---

### STEP BACK 4: H.265 validation

- [ ] **Single I-frame**: Same drill as H.264 — encode one frame, validate with `ffprobe`/`hevc_analyze`
- [ ] **Visual check**: Decode and compare against input
- [ ] **I+P sequence**: Multi-frame encode with correct reference handling
- [ ] **Codec switching**: Can you switch between H.264 and H.265 without reloading the driver? (Stop stream, change format, start stream)
- [ ] **Regression**: Re-run all H.264 tests to confirm H.265 additions didn't break anything

**Why**: The shared register space between H.264 and H.265 means a H.265 bug could silently regress H.264. Test both after every change.

---

## Phase 5: Production Hardening

### Step 5.1: Interrupt-driven completion

Replace any polling with proper interrupt handling:
- IRQ handler reads status, clears interrupt, schedules bottom half
- Bottom half completes V4L2 buffer processing
- Timeout watchdog: if no interrupt within 100ms, assume HW hang and reset

### Step 5.2: Error recovery

- HW error detection via `enc_err_status` register bits
- Bus error / IOMMU fault handling
- Graceful recovery: reset encoder block, return error on current frame, allow next frame to proceed
- Repeated failures: disable HW, return errors on all queued buffers

### Step 5.3: VEPU540 vs VEPU541 variant handling

Differences are minimal — same register layout, same feature set. Only two known divergences:

1. **ME RAM calculation**: VEPU540 has smaller internal ME SRAM, different sizing formula in `setup_vepu541_me()`
2. **AXI response register**: Minor bit layout difference in diagnostic fields (core encode path unaffected)

Implementation via `of_device_id` variant data:
```c
static const struct rkvenc_variant rk3568_variant = {
    .is_vepu540 = true,
};
static const struct rkvenc_variant rv1126_variant = {
    .is_vepu540 = false,
};
static const struct of_device_id rkvenc_dt_match[] = {
    { .compatible = "rockchip,rk3568-rkvenc", .data = &rk3568_variant },
    { .compatible = "rockchip,rv1126-rkvenc", .data = &rv1126_variant },
    { },
};
```

Everything else (L1/L2 register layout, interrupt model, H.264 codec features, DMA addressing) is identical. The VEPU580 (RK3588) is a completely different IP and should NOT be mixed into this driver.

### Step 5.4: Extended feature set

- Slice splitting (by MB rows or byte count): `reg205-reg208`
- Deblocking filter alpha/beta offset controls
- Long-term reference frame support (MMCO)
- Intra refresh (column-based rolling)
- ROI encoding (8 regions, absolute/relative QP): `reg013.roi_enc`

---

### STEP BACK 5: Production readiness validation

- [ ] **Stress test**: Encode 10,000+ frames continuously. No hangs, no memory leaks, no corruption.
- [ ] **Resolution sweep**: Test 96x96, 320x240, 640x480, 1280x720, 1920x1080. All produce valid output.
- [ ] **Format sweep**: NV12, YUV420M inputs. H.264 Baseline/Main/High, H.265 Main outputs.
- [ ] **Error injection**: Unmap a DMA buffer mid-encode. Does the driver recover gracefully? Does it not panic?
- [ ] **Suspend/resume**: pm_runtime suspend during idle, resume on next encode. Works?
- [ ] **Multi-open**: Two userspace processes open the device. Second gets `-EBUSY` or queues correctly.
- [ ] **v4l2-compliance full**: `v4l2-compliance -d /dev/videoN -s` — all streaming tests pass.
- [ ] **Valgrind/KASAN**: No memory errors under KASAN-enabled kernel.

**Why**: Out-of-tree users will run this on always-on NVR/media servers. It must not crash after 3 days of continuous encoding.

---

## Phase 6: Out-of-Tree Packaging & User Delivery

### Step 6.1: DKMS packaging

- `dkms.conf` for automatic module build on kernel updates
- Kconfig option: `CONFIG_VIDEO_ROCKCHIP_RKVENC`
- Dependencies: `VIDEO_DEV`, `VIDEO_V4L2`, `V4L2_MEM2MEM_DEV`
- Out-of-tree Makefile that builds against installed kernel headers

### Step 6.2: DT overlay

- Provide a DT overlay for boards that don't have the rkvenc node
- Test on: Pine64 Quartz64, Radxa ROCK 3A/3C, Home Assistant Green
- Include IOMMU node if not already present

### Step 6.3: Userspace integration testing

- FFmpeg: test with `ffmpeg -f v4l2 -input_format nv12 -i /dev/videoN -c:v h264_v4l2m2m output.mp4`
- GStreamer: test with `v4l2h264enc` element
- go2rtc: verify hardware encoder detection and transcode pipeline
- Document any userspace patches needed for stateless encoder support

---

### STEP BACK 6: End-to-end user validation

- [ ] **Fresh board setup**: Start from a stock Armbian/DietPi image. Install DKMS package. Does the module load? Does `/dev/videoN` appear?
- [ ] **FFmpeg transcode**: `ffmpeg -i input.mp4 -c:v h264_v4l2m2m -b:v 2M output.mp4` — produces valid, playable output?
- [ ] **Real workload**: Set up Jellyfin or Frigate with hardware encoding enabled. Does transcoding work? Is it faster than software encoding?
- [ ] **Thermal**: Run continuous 1080p30 encoding for 1 hour. SoC temperature stable? No thermal throttling causing failures?
- [ ] **Multiple boards**: Test on at least 2 different RK3566/RK3568 boards to catch board-specific DT issues.

**Why**: The whole point is delivering a working solution to users. If it doesn't work in their actual setup, nothing else matters.

---

## Phase 7: Upstream Preparation

### Step 7.1: uAPI alignment

- Track V4L2 stateless encoding uAPI evolution on linux-media
- Adapt driver to match finalized API when available
- Participate in uAPI discussions with real hardware experience from this driver

### Step 7.2: Upstream submission

- `checkpatch.pl` clean
- `dt_binding_check` clean
- `v4l2-compliance` full pass
- RFC cover letter explaining hardware, design decisions, test results
- Submit to linux-media, CC rockchip, devicetree lists
- Iterate on review from Collabora/Pengutronix/Bootlin

---

## File Structure

```
drivers/media/platform/rockchip/rkvenc/
├── Kconfig
├── Makefile
├── rkvenc_drv.c          — Platform driver, probe/remove, PM
├── rkvenc.h              — Shared structs, device/context definitions
├── rkvenc_regs.h         — VEPU540 register definitions (from MPP translation)
├── rkvenc_v4l2.c         — V4L2 ioctls, VB2 ops, controls, format negotiation
├── rkvenc_h264.c         — H.264 register programming pipeline
├── rkvenc_h265.c         — H.265 register programming pipeline (Phase 4)
└── rkvenc_h264_sps_pps.c — H.264 SPS/PPS NALU generation

Documentation/devicetree/bindings/media/
└── rockchip,rk3568-rkvenc.yaml
```

## Dependencies & Risks

| Dependency | Status | Mitigation |
|------------|--------|------------|
| V4L2 stateless encoding uAPI | RFC, unstable | Build against latest RFC; accept refactoring cost |
| Physical RK3566/RK3568 board | Required from Phase 1 STEP BACK onward | Pine64 Quartz64-A ($60), Radxa ROCK 3A ($35) |
| MPP register reference | Available via forks | DMCA targeted FFmpeg code, not register headers |
| IOMMU driver | In mainline | `rockchip-iommu` driver works for rkvdec, should work for rkvenc |
| Power domain | In mainline | `RK3568_PD_RKVENC` defined in `pm-domains.c` |
