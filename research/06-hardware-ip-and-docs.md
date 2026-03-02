# Research: RK3566 Encoder IP Identification & Documentation

## 1. Encoder IP Block Identification

The RK3566 (and sibling RK3568) contains **two separate hardware video encoder blocks**:

### Block A: VEPU121 (Hantro/Verisilicon)

| Property | Value |
|----------|-------|
| Register base | `0xFDEE0000` (size `0x800`) |
| IOMMU | `0xFDEE0800` (size `0x40`) |
| Interrupt | GIC_SPI 64 (level high) |
| Power domain | `RK3568_PD_RGA` |
| Clocks | `ACLK_JENC`, `HCLK_JENC` |
| Supported codecs | JPEG baseline, H.264 (up to HP L4.1), VP8 |
| Architecture | Hantro/Verisilicon stateless encoder. Nearly identical to RK3399 |
| Mainline compatible | `"rockchip,rk3568-vepu"` |
| Vendor compatible | `"rockchip,vpu-encoder-v2"` |

### Block B: RKVENC / VEPU540 (Rockchip proprietary)

| Property | Value |
|----------|-------|
| Register base | `0xFDF40000` (size `0x400`) |
| IOMMU | `0xFDF40F00` (dual-channel: +`0xFDF40F40`, each `0x40`) |
| Interrupts | GIC_SPI 140 (encoder), 141/142 (MMU channels) |
| Power domain | `RK3568_PD_RKVENC` (dedicated) |
| Clocks | `ACLK_RKVENC`, `HCLK_RKVENC`, `CLK_RKVENC_CORE` |
| Supported codecs | H.264, H.265/HEVC (1080p60) |
| Architecture | Rockchip proprietary, not Hantro/Verisilicon |
| Vendor compatible | `"rockchip,rkv-encoder-v1"` |
| Mainline status | **No driver, no DT node, no patches** |

### Confirmation from MPP

In `hal_h264e_vepu541.c`:
```c
RockchipSocType soc_type = mpp_get_soc_type();
if (soc_type == ROCKCHIP_SOC_RK3566 || soc_type == ROCKCHIP_SOC_RK3568)
    p->is_vepu540 = 1;
```

SoC compatibility list for the VEPU541 HAL:
- `ROCKCHIP_SOC_RV1126`
- `ROCKCHIP_SOC_RK3566`
- `ROCKCHIP_SOC_RK3567`
- `ROCKCHIP_SOC_RK3568`

### Cross-SoC Encoder IP Map

| SoC | VEPU121 (Hantro) | RKVENC variant | Notes |
|-----|-------------------|----------------|-------|
| RK3399 | Hantro H1 | None | Bootlin H1 reference |
| RK3288 | Hantro H1 | None | |
| RK3328 | Hantro H1 | None | |
| RV1126 | ? | VEPU540/541 | Shares HAL with RK3566 |
| **RK3566** | **VEPU121** | **VEPU540** | Both blocks present |
| **RK3568** | **VEPU121** | **VEPU540** | Both blocks present |
| RK3588 | VEPU121 (multiple) | VEPU580 | Next-gen rkvenc |

---

## 2. Hardware Documentation

### RK3568 TRM Part 2 (CRITICAL DOCUMENT)

- **Available**: Yes, publicly accessible
- **URL**: [Radxa mirror (PDF)](https://dl.radxa.com/rock3/docs/hw/datasheet/Rockchip%20RK3568%20TRM%20Part2%20V1.1-20210301.pdf)
- **Relevant**: Chapter 10 "Multi-format Video Encoder and Decoder" (Page 463)
  - VEPU121 Detail Registers Description (Page 704)
  - VEPU540 (RKVENC) documentation also present
- **Applicability**: RK3566 shares same video encoder IP as RK3568

### RK3568 TRM Part 1

- **URL**: [Radxa mirror](https://dl.radxa.com/rock3/docs/hw/datasheet/Rockchip%20RK3568%20TRM%20Part1%20V1.1-20210301.pdf)
- Also on [Internet Archive](https://archive.org/details/rockchip-rk-3568-trm-part-1-v-1.3-20220930)

### RK3566 Datasheets

- [V1.0 (Pine64)](https://files.pine64.org/doc/quartz64/Rockchip%20RK3566%20Datasheet%20V1.0-20201210.pdf)
- [V1.1 (Boardcon)](https://www.boardcon.com/download/Rockchip_RK3566_Datasheet_V1.1.pdf)
- [Brief (Rockchip)](https://www.rock-chips.com/uploads/pdf/2022.8.26/192/RK3566%20Brief%20Datasheet.pdf)

### Mainline Kernel DT Binding

- `Documentation/devicetree/bindings/media/rockchip,rk3568-vepu.yaml` — present in mainline

---

## 3. Register Definitions in Open Source

### MPP Repository

**Note**: Original `rockchip-linux/mpp` was DMCA'd by FFmpeg (Dec 2025). Available via [HermanChen/mpp](https://github.com/HermanChen/mpp) and other forks.

Key register files for RK3566/RK3568:

```
mpp/hal/rkenc/
  h264e/
    hal_h264e_vepu541_reg.h      (~2487 lines) — H.264 register definitions (~209 regs)
    hal_h264e_vepu541_reg_l2.h   (~412 lines)  — L2 RDO tuning registers
    hal_h264e_vepu541.c          (~1872 lines)  — Full HAL implementation
  h265e/
    hal_h265e_vepu541_reg.h      — H.265 register definitions
    hal_h265e_vepu54x_reg_l2.h   — H.265 L2 registers
    hal_h265e_vepu541.c          — H.265 HAL implementation
  common/
    vepu541_common.c             — Shared ROI/OSD code
```

The `Vepu541H264eRegSet` structure contains ~209 registers:
- Control (0x0000-0x003C): version, start, clear, link table
- Interrupt (0x0010-0x001C): enable, mask, clear, status
- Configuration (0x0030-0x015C): resolution, picture, source format, RC, ROI
- Address (0x0110-0x015C): source buffers, reference frames, bitstream
- Motion estimation (0x0164-0x016C)
- Syntax (0x0170-0x01EC): NAL, SPS, PPS, slice headers
- Status (0x210-0x340): bitstream length, SSE, QP distribution

### Vendor Kernel Device Tree

`rockchip-linux/kernel` branch `develop-4.19`, `arch/arm64/boot/dts/rockchip/rk3568.dtsi` has complete DT definitions for both encoder blocks.

---

## 4. Documentation Quality Assessment

**Assessment: Unusually good for a Rockchip IP block.** Between the TRM Part 2 and the MPP register headers, the entire register interface is documented. No reverse engineering should be needed.

| Source | Coverage | Quality |
|--------|----------|---------|
| RK3568 TRM Part 2 | Full encoder chapter | Official Rockchip documentation |
| MPP register headers | Every bitfield named and commented | Essentially a TRM in C struct form |
| Vendor kernel DT | Complete device definitions | Authoritative |
| Mainline kernel DT binding | VEPU121 only | Only covers JPEG encoder block |

---

## 5. Out-of-Tree Driver Attempts

No complete out-of-tree V4L2 drivers found for RKVENC/VEPU540. Related work:

| Project | Target | Status |
|---------|--------|--------|
| [bootlin/linux `hantro/h264-encoding-v5.11`](https://github.com/bootlin/linux) | Hantro H1 on RK3399 | Out-of-tree, never submitted |
| [bootlin/v4l2-hantro-h264-encoder](https://github.com/bootlin/v4l2-hantro-h264-encoder) | Userspace for above | Available |
| RK3588 VEPU121 JPEG | JPEG on RK3588 | Merged kernel 6.12 |
| RK3588/RK3576 decoders | rkvdec2 | Merged Feb 2026 |
| Collabora VDPU346 for RK356x | Decoder | Planned |

---

## 6. The Two-Block Problem

Any driver effort must choose between:

1. **VEPU121 (Hantro)**: Mainline driver exists (JPEG only). Extending for H.264 would leverage existing `verisilicon` driver. Bootlin's H1 work provides reference. But: test results on RK3566 produced unusable H.264 output. Lower-quality encoder.

2. **RKVENC/VEPU540**: The encoder you actually want for production H.264/H.265 at 1080p60. Completely new driver from scratch. No existing framework to extend. But: excellent documentation, excellent MPP reference, and covers multiple SoCs (RK3566/RK3568/RV1126).

### What a mainline RKVENC driver needs:

- Completely new V4L2 stateless encoder driver
- New device tree bindings
- New device tree nodes for rk356x
- Register-level programming from MPP HAL + TRM
- Userspace rate control implementation
- V4L2 stateless encoding uAPI to stabilize first
