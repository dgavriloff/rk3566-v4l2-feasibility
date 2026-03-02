# Research: Mainline Kernel Search for RK3566 Encoder Drivers

## Summary

There is **no dedicated rkvenc (VEPU540) H.264/H.265 encoder driver** in mainline Linux for the RK3566/RK3568. Only **JPEG encoding** is supported via the Hantro/Verisilicon driver. The full hardware video encoding capability (H.264, H.265) remains unaddressed in mainline.

---

## 1. What EXISTS in Mainline

### A. Hantro JPEG Encoder (VEPU2 instance) — MERGED

The RK3566/RK3568 has a dedicated Hantro VPU2 encode-only instance at address `0xfdee0000`, which is separate from the combined VPU decode/encode block. This is supported in mainline for **JPEG encoding only**.

**Key commits by Nicolas Frattaroli (v5 patch series, June 2022):**

| Commit | Description | Author | Date | Committer |
|--------|-------------|--------|------|-----------|
| `5484ea9229a1decd6662dce2d8ab2920289d69d4` | media: dt-binding: media: Add rk3568-vepu binding | Nicolas Frattaroli | 2022-06-12 | Mauro Carvalho Chehab |
| (driver commit, part of same series) | media: hantro: Add support for RK356x encoder | Nicolas Frattaroli | 2022-06-12 | Hans Verkuil |
| `03d86fb5a56919ccf47e1cc5861bb5452017ab93` | arm64: dts: rockchip: Add Hantro encoder node to rk356x | Nicolas Frattaroli | 2022-06-12 | Heiko Stuebner |

**Files in mainline:**

- `drivers/media/platform/verisilicon/rockchip_vpu_hw.c` — contains `rk3568_vepu_variant` struct, which declares `HANTRO_JPEG_ENCODER` as the only supported codec
- `drivers/media/platform/verisilicon/rockchip_vpu2_hw_jpeg_enc.c` — the actual JPEG encode hardware implementation
- `drivers/media/platform/verisilicon/hantro_drv.c` — line 721: `{ .compatible = "rockchip,rk3568-vepu", .data = &rk3568_vepu_variant, }`
- `Documentation/devicetree/bindings/media/rockchip,rk3568-vepu.yaml` — DT binding (also covers `rockchip,rk3588-vepu121`)
- `arch/arm64/boot/dts/rockchip/rk356x-base.dtsi` — lines 602-620: device tree node for `vepu` at `0xfdee0000` and its IOMMU

The `rk3568_vepu_variant` in `rockchip_vpu_hw.c` explicitly only supports JPEG:
```c
const struct hantro_variant rk3568_vepu_variant = {
    .enc_offset = 0x0,
    .enc_fmts = rockchip_vpu_enc_fmts,
    .num_enc_fmts = ARRAY_SIZE(rockchip_vpu_enc_fmts),
    .codec = HANTRO_JPEG_ENCODER,
    .codec_ops = rk3568_vepu_codec_ops,
    ...
};
```

### B. RK3588 VEPU121 Binding — MERGED

Commit `b92346d2dba0048bfce7114225250bef73f83ad2` by Emmanuel Gil Peyrot (2024-06-18), signed off by Sebastian Reichel and Sebastian Fricke (Collabora), added the `rockchip,rk3588-vepu121` compatible string to the existing `rk3568-vepu` binding. In `hantro_drv.c` line 723, this reuses `rk3568_vepu_variant` (i.e., still JPEG-only).

### C. Hantro VPU Decode-Only Instance for RK3568 — MERGED

`rk3568_vpu_variant` provides MPEG2, VP8, and H264 **decoding** (not encoding) through the Hantro/Verisilicon driver. Compatible string `rockchip,rk3568-vpu` in `hantro_drv.c` line 722.

### D. Power Domain and Clock Infrastructure for RKVENC — MERGED

The `RK3568_PD_RKVENC` power domain is defined in mainline:
- `include/dt-bindings/power/rk3568-power.h`
- `drivers/pmdomain/rockchip/pm-domains.c`
- `arch/arm64/boot/dts/rockchip/rk356x-base.dtsi` lines 545-552 (power domain node with QoS entries at `0xfe138080`, `0xfe138100`, `0xfe138180`)
- `drivers/clk/rockchip/clk-rk3568.c` — defines RKVENC-related clocks

This infrastructure exists but is **not consumed by any encoder driver** — it is only wired up for the power domain controller. No device tree node references `RK3568_PD_RKVENC` as its `power-domains` property for an actual encoder device.

### E. RKVDEC2 (Decoder) for RK3588/RK3576 — RECENTLY MERGED (Linux 7.0)

The rkvdec driver at `drivers/media/platform/rockchip/rkvdec/` was significantly expanded by Detlev Casanova (Collabora) in January 2026 with a 17-patch series adding VDPU381 (RK3588) and VDPU383 (RK3576) H.264 and HEVC **decoding** support. This is decoding only, no encoding.

Key files: `rkvdec-vdpu381-h264.c`, `rkvdec-vdpu381-hevc.c`, `rkvdec-vdpu383-h264.c`, `rkvdec-vdpu383-hevc.c`

Notably, the rkvdec driver does **not** yet support RK3566/RK3568 VDPU346 variant, though Collabora has stated this is planned.

---

## 2. What is in Staging

There are **no** Rockchip encoder or decoder drivers in `drivers/staging/media/`. The staging directory contains drivers for other vendors (Atomisp, Tegra, Starfive, Sunxi, Meson, etc.) but nothing Rockchip-related. The Hantro driver was de-staged by Ezequiel Garcia in commit `fbb6c848dd89` (2022-07-18), and the rkvdec driver was de-staged in Linux 6.17.

---

## 3. What Has Been PROPOSED But NOT Merged

### A. H.264 Stateless Encoder uAPI + Hantro H1 Driver (RFC)

- **Author:** Andrzej Pietrasiewicz (Collabora)
- **Date:** November 16, 2023
- **Patchwork:** [RFC 0/6](https://patchwork.linuxtv.org/project/linux-media/cover/20231116154816.70959-1-andrzej.p@collabora.com/)
- **Status:** RFC only, not merged. Adds uAPI for stateless H.264 encoding and a Hantro H1 encoder driver. Tested on STM32MP25. Only supports constrained baseline profile, I and P frames.

### B. VP8 Stateless Encoder for Hantro H1 (RFC)

- **Author:** Hugues Fruchet (ST Microelectronics)
- **Date:** October 4, 2023
- **Patchwork:** [RFC 0/2](https://patchwork.linuxtv.org/project/linux-media/cover/20231004103720.3540436-1-hugues.fruchet@foss.st.com/)
- **Status:** RFC, not merged.

### C. Bootlin H.264 Encoder Out-of-Tree Work

- **Author:** Paul Kocialkowski (Bootlin)
- **Repository:** https://github.com/bootlin/linux branch `hantro/h264-encoding-v5.11`
- **Userspace:** https://github.com/bootlin/v4l2-hantro-h264-encoder
- **Status:** Never submitted to mainline. Predates the stateless encoding uAPI discussion. Demonstrated at FOSDEM 2024 presentation.

### D. VC8000E H.264 V4L2 Stateless Encoder (RFC/v1)

- **Authors:** Marco Felsch (Pengutronix), Michael Tretter, Paul Kocialkowski
- **Date:** May 2, 2025 (v1)
- **Patchwork:** [v1 VC8000E](https://patchew.org/linux/20250502150513.4169098-1-m.felsch@pengutronix.de/)
- **Status:** Very early state, uAPI still being figured out. Targets NXP i.MX8MP (VC8000E), not RK3568 directly, but relevant because the Verisilicon encoder IP is related.

### E. RKVENC (VEPU540) — No Patches Submitted

There are **no known patch submissions** for a dedicated rkvenc / VEPU540 driver targeting H.264/H.265 encoding on RK3566/RK3568. In a 2022 mailing list discussion, Michael Grzeschik (Pengutronix) mentioned working on an rkvenc driver and extending the rkvdec driver to become more generic, but no patches materialized publicly.

---

## 4. Key Blockers

The fundamental blocker preventing H.264/H.265/VP8 stateless encoding from reaching mainline is the **lack of a finalized V4L2 stateless encoder uAPI**. All existing encoder RFC patches are waiting on consensus for how to expose stateless encoders to userspace. The JPEG encoder was able to land because JPEG encoding was already supported by the existing V4L2 API without needing new stateless controls.

---

## 5. Hardware Reality on RK3566/RK3568

The SoC contains two distinct encoder IP blocks:

| Block | Address | Mainline Status | Capabilities |
|-------|---------|----------------|--------------|
| VEPU2 (Hantro encode-only instance) | `0xfdee0000` | **JPEG only** via Verisilicon driver | JPEG, likely VP8 + H.264 per TRM |
| VEPU540 (RKVENC) | Under `RK3568_PD_RKVENC` power domain | **No driver** | H.264, H.265 (per downstream MPP) |

For actual H.264/H.265 encoding today, users must rely on Rockchip's proprietary MPP (Media Process Platform) with Rockchip's BSP kernel.

---

## Sources
- [PINE64 Mainline Hardware Encoding Wiki](https://wiki.pine64.org/wiki/Mainline_Hardware_Encoding)
- [PINE64 Mainline Hardware Decoding Wiki](https://wiki.pine64.org/wiki/Mainline_Hardware_Decoding)
- [Patchwork: Enable JPEG Encoder on RK3566/RK3568 v5](https://patchwork.kernel.org/project/linux-media/cover/20220612155346.16288-1-frattaroli.nicolas@gmail.com/)
- [Collabora: RK3588 upstream support progress](https://www.collabora.com/news-and-blog/news-and-events/rockchip-rk3588-upstream-support-progress-future-plans.html)
- [Collabora: RK3588/RK3576 video decoders merged](https://www.collabora.com/news-and-blog/news-and-events/rk3588-and-rk3576-video-decoders-support-merged-in-the-upstream-linux-kernel.html)
- [FOSDEM 2024: V4L2 Stateless Video Encoding](https://archive.fosdem.org/2024/schedule/event/fosdem-2024-3090-v4l2-stateless-video-encoding-hardware-support-and-uapi/)
