# Research: Linux Kernel Mailing List Search for Rockchip Encoder Patches

## Executive Summary

There is **no mainline Linux kernel driver for Rockchip H.264/H.265/VP8 video encoding** (i.e., the rkvenc hardware blocks). The only Rockchip encoding support that has been merged upstream is **JPEG encoding** via the Hantro/Verisilicon driver for the VEPU121 hardware block. A broader stateless encoding uAPI is still under active development but has not yet been finalized, which is a prerequisite for upstreaming full video encoding drivers.

---

## Search-by-Search Results

### 1. "rkvenc" (92 results on lore.kernel.org)

The term "rkvenc" appears primarily in **RK3582 SoC support patches** (U-Boot), not Linux kernel media driver work.

- **[PATCH v1-v4] rockchip: Add initial RK3582 support** — Jonas Karlman (Dec 2024 - Jan 2026)
  - Type: Patch series (U-Boot, not Linux kernel media driver)
  - The RK3582 is a cut-down RK3588S with certain IP blocks fused off. The patches mark disabled blocks (including rkvenc cores) as "status=fail" in the device tree.
  - Confirms "rkvenc" is a real hardware block name, but **no Linux kernel driver submitted**.

- **Hantro H1 Encoding Upstreaming** — Fabio Estevam, Paul Kocialkowski, et al. (Jan 2025)
  - Type: Discussion
  - Notes that Rockchip VEPUs exist but relies on libMPP (proprietary userspace). No kernel patches for rkvenc.

- **Stateless Encoding uAPI Discussion** — Paul Kocialkowski et al. (Jul-Nov 2023)
  - Type: Discussion/RFC
  - Broad design discussion. No Rockchip-specific encoder patches; focuses on Hantro H1 and Allwinner Video Engine.

### 2. "vepu" AND "rockchip" (~900 results)

- **[PATCH v5-v8] RK3588 VEPU121/VPU121 support** — Sebastian Reichel (Jun-Aug 2024)
  - Adds DT bindings and hantro driver support for VEPU121 on RK3588.
  - **VEPU121 supports JPEG encoding only.**
  - Status: **MERGED** into mainline (kernel 6.12).

- **[PATCH v2-v4] Enable JPEG encoding on rk3588** — Emmanuel Gil Peyrot (Mar-Apr 2024)
  - Superseded by Sebastian Reichel's series.

### 3. "rockchip" AND "encoder" AND "v4l2" (~7,000 results)

Mostly general kernel release notes. Only directly relevant: RGA (Raster Graphics Accelerator) patches, not video encoding.

### 4. "rk3566 encoder" (~600 results)

Dominated by VOP2 display driver patches and rkvdec decoder patches. No encoder work.

### 5. "hantro encoder rk3566"

- **[PATCH v1-v5] Enable JPEG Encoder on RK3566/RK3568** — Nicolas Frattaroli (Apr-Jun 2022)
  - 3 patches across 5 versions.
  - JPEG encoding only.
  - Status: **MERGED** into mainline.

- **Hantro JPEG Encoding Padding Bug** — Nicolas Frattaroli (Apr 2022)
  - Bug report identified padding issues.

- **[PATCH v2] media: verisilicon: Fix crash when probing encoder** — Benjamin Gaignard (May 2023)
  - Bug fix for Verisilicon encoder probe crashes.

### 6. "vepu540" (19 results)

Very small result set. VEPU540 mentioned in discussions but **no driver patches**:

- **[RFC PATCH 03/11] media: uapi: add nal unit header fields** — Marco Felsch / Nicolas Dufresne (May 2025)
  - VEPU540 mentioned tangentially. No driver code.

- Multiple replies in the JPEG Encoder thread (2022) mention VEPU540 as hardware that exists but has no upstream support.

### 7. "vepu2 rockchip" (94 results)

Primarily covers VDPU2 (decoder) work:

- **[PATCH v1-v3] media: hantro: Enable H.264 on Rockchip VDPU2** — Ezequiel Garcia (Jun-Jul 2021)
  - **Decoder** support, not encoder. Status: **MERGED**.

---

## What HAS Been Merged (encoding)

| Feature | SoC | Hardware IP | Codec | Author | Merged |
|---------|-----|-------------|-------|--------|--------|
| JPEG Encoder | RK3566/RK3568 | Hantro H1 (VEPU) | JPEG only | Nicolas Frattaroli | ~2022 |
| JPEG Encoder | RK3588 | VEPU121 | JPEG only | Sebastian Reichel | Aug 2024 (kernel 6.12) |

## What Has NOT Been Submitted or Merged

- **No rkvenc driver** — Only referenced in U-Boot DT patches
- **No VEPU540 driver** — Mentioned in discussions, no patches
- **No VEPU580 driver** — No patches found
- **No H.264/H.265/VP8 encoding** on any Rockchip SoC via V4L2

## Blocking Factor: Stateless Encoding uAPI

The main blocker is the **V4L2 stateless encoding uAPI is still being designed**:

1. **Paul Kocialkowski's uAPI Proposal** (Jul 2023): Initial design discussion. Key unresolved: rate control strategy, reference frame management, bitstream header generation.
2. **Andrzej Pietrasiewicz's H.264 RFC** (Nov 2023): 6-patch RFC. Still under discussion.
3. **Marco Felsch's VC8000E RFC** (May 2025): 11-patch RFC, "very very early state." Working GStreamer integration but not production-ready.
4. **Hantro H1 Encoding Discussion** (Jan 2025): Proposed BPF for rate control. Rockchip VEPUs acknowledged but only served by proprietary libMPP.

## Rockchip Encoding Hardware Blocks (from discussions)

- **VEPU1 / Hantro H1**: RK3288, RK3328, PX30. JPEG merged. H.264/VP8 blocked by missing uAPI.
- **VEPU121**: RK3588. JPEG merged (Aug 2024). Hantro variant.
- **VEPU2**: Older Rockchip SoCs. No encoding patches.
- **VEPU540**: Newer SoCs (RK3566/RK3568). No driver patches.
- **VEPU580**: RK3588. No patches found.
- **rkvenc / rkvenc2**: RK3588 dedicated video encoding block. No kernel driver. Only in U-Boot DT.
