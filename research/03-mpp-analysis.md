# Research: Rockchip MPP Encoder Code Analysis

## Repository Status

The original `rockchip-linux/mpp` was DMCA'd by FFmpeg in December 2025 due to LGPL code copying. Code accessible via:
- **GitHub fork**: [HermanChen/mpp](https://github.com/HermanChen/mpp) (develop branch)
- **Gitee mirrors**: [rockchip-mirror/mpp](https://gitee.com/rockchip-mirror/mpp)

---

## 1. Encoder IP Block Identification

**The RK3566 uses VEPU540 (primary) + VEPU2 (secondary).**

From `mpp/soc.cmake`:
```cmake
add_soc_config("RK356[678]"
    "VDPU34X,VDPU720,VDPU2,VEPU541,VEPU2"
    "H263D,H264D,H265D,JPEGD,MPEG2D,MPEG4D,VP8D,VP9D,AVSD"
    "H264E,JPEGE,H265E")
```

Within `hal_h264e_vepu541_init`:
```c
RockchipSocType soc_type = mpp_get_soc_type();
if (soc_type == ROCKCHIP_SOC_RK3566 || soc_type == ROCKCHIP_SOC_RK3568)
    p->is_vepu540 = 1;
```

The VEPU541 HAL handles both VEPU540 (RK3566/RK3568) and VEPU541 (RV1126 etc.) with a runtime flag. The SoC compatibility list:
- `ROCKCHIP_SOC_RV1126`
- `ROCKCHIP_SOC_RK3566`
- `ROCKCHIP_SOC_RK3567`
- `ROCKCHIP_SOC_RK3568`

Device type: `VPU_CLIENT_RKVENC`.

---

## 2. Repository Structure

```
mpp/
  mpp/           - Core library
    hal/         - Hardware Abstraction Layer
      rkenc/     - RKVENC family (VEPU5xx series)
        h264e/   - H.264 encoder HALs for each VEPU variant
        h265e/   - H.265 encoder HALs for each VEPU variant
        common/  - Shared VEPU5xx code
        jpege/   - JPEG encoder HAL
      vpu/       - VPU/Hantro family (VEPU1, VEPU2)
        h264e/   - H.264 encoder HAL for VEPU1/VEPU2
      common/    - Shared HAL utilities
    codec/       - Codec-level logic
      enc/h264/  - H.264 SPS/PPS/Slice/DPB generation
      enc/h265/  - H.265 encoder codec logic
      rc/        - Rate control algorithms
    base/        - Buffer, frame, packet management
  osal/          - OS Abstraction Layer
    driver/      - Kernel driver interface (mpp_service)
  kmpp/          - Kernel-mode MPP (optional in-kernel encoder)
```

### Key Files for RK3566 VEPU540

| File | Lines | Purpose |
|------|-------|---------|
| `mpp/hal/rkenc/h264e/hal_h264e_vepu541_reg.h` | ~2487 | Complete register bitfield definitions |
| `mpp/hal/rkenc/h264e/hal_h264e_vepu541_reg_l2.h` | ~412 | RDO/quality tuning registers |
| `mpp/hal/rkenc/h264e/hal_h264e_vepu541.c` | ~1872 | Full HAL: init/gen_regs/start/wait/ret_task |
| `mpp/hal/rkenc/common/vepu541_common.c` | ~400 | ROI, OSD shared code |
| `mpp/hal/rkenc/common/vepu5xx_common.c` | ~500 | Format conversion, OSD, shared utilities |

---

## 3. Hardware Register Access

Registers use **clean C bitfield structures** with address offset comments. Example:

```c
typedef struct Vepu541H264eRegSet_t {
    // reg000 @ 0x0000 - VERSION (read only)
    struct {
        RK_U32  sub_ver      : 8;
        RK_U32  h264_enc     : 1;
        RK_U32  h265_enc     : 1;
        RK_U32  reserved0    : 2;
        RK_U32  pic_size     : 4;
        RK_U32  osd_cap      : 2;
        RK_U32  filtr_cap    : 2;
        RK_U32  bfrm_cap     : 1;
        RK_U32  fbc_cap      : 1;
        RK_U32  reserved1    : 2;
        RK_U32  rkvenc_ver   : 8;
    } reg000;

    // reg001 @ 0x0004 - ENC_STRT
    struct {
        RK_U32  lkt_num      : 8;
        RK_U32  rkvenc_cmd   : 2;
        RK_U32  reserved0    : 6;
        RK_U32  clk_gate_en  : 1;
        RK_U32  resetn_hw_en : 1;
        RK_U32  enc_done_tmvp_en : 1;
        RK_U32  reserved1    : 13;
    } reg001;
    // ... ~208 registers total
} Vepu541H264eRegSet;
```

**Three register regions:**
1. **L1 registers** (0x0000-0x0340): Main configuration, written as contiguous block
2. **L2 registers** (0x10004+): RDO tuning, via indirect access (address + data port)
3. **Status registers** (0x001C, 0x0210-0x0340): Read back after encode

---

## 4. Encoding Pipeline

```
init → prepare → get_task → gen_regs → start → wait → ret_task → deinit
```

### `init` (hal_h264e_vepu541_init)
- Opens `/dev/mpp_service` via `mpp_dev_init()` with `VPU_CLIENT_RKVENC`
- Detects VEPU540 variant for RK3566/RK3568
- Allocates reconstruction frame buffers
- Sets up OSD, initializes default HW quality parameters

### `gen_regs` (hal_h264e_vepu541_gen_regs)
Core register programming. Calls in order:
1. `setup_vepu541_normal()` — base HW config, interrupts, clocks
2. `setup_vepu541_prep()` — input format, CSC, stride, rotation
3. `setup_vepu541_codec()` — H.264 syntax: NAL type, entropy mode
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

### `start` (hal_h264e_vepu541_start)
```c
mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &wr_cfg);  // L2 regs
mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_WR, &wr_cfg);  // L1 regs
mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_RD, &rd_cfg);   // IRQ status readback
mpp_dev_ioctl(ctx->dev, MPP_DEV_REG_RD, &rd_cfg);   // Statistics readback
mpp_dev_ioctl(ctx->dev, MPP_DEV_CMD_SEND, NULL);     // Kick hardware
```

### `wait` (hal_h264e_vepu541_wait)
```c
ret = mpp_dev_ioctl(ctx->dev, MPP_DEV_CMD_POLL, NULL);  // Block until done
hal_h264e_vepu541_status_check(hal);                      // Check errors
task->hw_length += ctx->regs_ret.st_bsl.bs_lgth;         // Bitstream length
```

---

## 5. H.264 Features Supported

| Feature | Register | Support |
|---------|----------|---------|
| Entropy (CAVLC/CABAC) | `reg105.etpy_mode` | Both |
| 8x8 transform | `reg105.trns_8x8` | Yes (High profile) |
| Constrained intra pred | `reg105.csip_flag` | Yes |
| Weighted prediction | `reg105.wght_pred` | Yes |
| Deblocking filter | `reg105.dbf_cp_flg` | Yes, with alpha/beta offsets |
| Multiple references | `reg105.num_ref0_idx` | Yes |
| Long-term references | `reg109.ltrf_flg` | Yes |
| MMCO | `reg109.mmco_type0/1/2` | Up to 3 operations |
| Slice splitting | `reg205-reg208` | By MB rows or bytes |
| Intra refresh | Dedicated function | Column-based rolling |
| ROI encoding | `reg013.roi_enc` | 8 regions |
| OSD overlay | 8 regions | 256-color palette |
| B-frames | `reg000.bfrm_cap` | HW capability exists but **HAL only does I/P** |
| HW rate control | CTU-level QP | 5 threshold levels, 8 zones |
| Profiles | Baseline, Main, High | Full support |
| Levels | 1.0 through 6.2 | Full table |

---

## 6. Code Quality Assessment

### Strengths
1. **Clean register definitions**: Address offsets, named bitfields, descriptive comments. Nearly directly translatable to kernel structs.
2. **Well-structured pipeline**: `MppEncHalApi` vtable cleanly separates init, register gen, HW kick, polling, result collection.
3. **Clear separation of concerns**: Codec (SPS/PPS/slice) → HAL (registers) → Device (ioctl).
4. **Minimal variant handling**: `is_vepu540` flag with only a few conditional branches.
5. **Comprehensive statistics readback**: SSE, QP per-block, MADI/MADP complexity, partition counts.

### Weaknesses
1. **Sparse HAL comments**: Register headers documented, but "why" behind tuning values missing.
2. **Magic constant tables**: `h264e_klut_weight[30]`, `h264e_lambda_default[58]` — no derivation.
3. **Some code duplication**: MMCO handling repeated 3x.
4. **L2 tuning lacks explanation**: Mixes anti-ringing, anti-flicker, RDO weights with limited docs.

### LOC Breakdown

| Layer | LOC | HW-Specific? |
|-------|-----|-------------|
| `hal_h264e_vepu541.c` | 1872 | Yes |
| `hal_h264e_vepu541_reg.h` | 2487 | Yes |
| `hal_h264e_vepu541_reg_l2.h` | 412 | Yes |
| `vepu541_common.c/h` | ~400 | Yes (shared VEPU5xx) |
| `vepu5xx_common.c/h` | ~500 | Shared across family |
| `h264e_sps.c/pps.c/slice.c` | ~2000 | Generic H.264 syntax |
| `rc_model_v2.c` | ~2000 | Generic rate control |
| `mpp_service.c` | ~400 | Generic ioctl wrapper |

---

## 7. Kernel Interface

1. Open `/dev/mpp_service`
2. Single ioctl: `ioctl(fd, MPP_IOC_CFG_V1, &mpp_req)` where `MPP_IOC_CFG_V1 = _IOW('v', 1, unsigned int)`
3. `MppReqV1` struct carries command type and data pointer
4. Command flow: `INIT_CLIENT_TYPE → SET_REG_WRITE (L2) → SET_REG_WRITE (L1) → SET_REG_READ → SET_REG_ADDR_OFFSET → POLL_HW_FINISH → read status`

---

## 8. Feasibility Verdict

**This codebase is highly suitable as a kernel driver reference.** The register headers are essentially a hardware TRM in C struct form. The pipeline maps cleanly to V4L2 stateless operations. The only significant challenge is translating FD-based DMA buffer management to kernel IOMMU mappings and implementing actual interrupt handling (MPP userspace just polls).

## Sources
- [HermanChen/mpp fork](https://github.com/HermanChen/mpp)
- [Rockchip MPP Wiki](https://opensource.rock-chips.com/wiki_Mpp)
- [DMCA takedown report](https://linuxiac.com/github-takes-down-rockchip-mpp-repository-after-ffmpeg-copyright-claim/)
