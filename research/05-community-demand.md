# Research: Community Demand for RK3566 Hardware Encoding

## Summary

**30+ unique threads/issues** across GitHub, hardware forums, and community sites demonstrating significant and sustained demand. The demand spans multiple use cases and has intensified since the Home Assistant Green (RK3566) became a mainstream consumer device.

---

## 1. GitHub Issues & Discussions

### go2rtc
- **[#768](https://github.com/AlexxIT/go2rtc/issues/768)**: Add presets for hardware acceleration on Rockchip devices. Motivated by Home Assistant Green (RK3566). Use case: camera stream transcoding.
- **[PR #1203](https://github.com/AlexxIT/go2rtc/pull/1203)**: Implement Rockchip hardware acceleration presets.
- **[#1348](https://github.com/AlexxIT/go2rtc/issues/1348)**: Transcode HA camera entity to H264.

### Frigate
- **[#9952](https://github.com/blakeblackshear/frigate/discussions/9952)**: RK3566 ffmpeg h264_rkmpp_decoder not working. Use case: security camera NVR.
- **[#18878](https://github.com/blakeblackshear/frigate/discussions/18878)**: VPU Hardware Decoding fails on RK3566. Shows fragmented support state.

### Immich
- **[#13579](https://github.com/immich-app/immich/issues/13579)**: RKMPP not using hardware decoding when no OpenCL. OrangePi 3B (RK3566). Use case: photo/video library transcoding.

### rockchip-linux/mpp
- **[#253](https://github.com/rockchip-linux/mpp/issues/253)**: Encoding YUYV to H264/H265 gets artifacts. Affects RK3568/RK3588.
- **[#446](https://github.com/rockchip-linux/mpp/issues/446)**: GStreamer mpph265enc issues on RK3566. Regression broke encoding.
- **[#311](https://github.com/rockchip-linux/mpp/issues/311)**: "can not found match soc name: rk3566". Radxa CM3 segfault.
- **[#259](https://github.com/rockchip-linux/mpp/issues/259)**: Performance comparison rv1126 vs rk3566/rk3568.
- **[#601](https://github.com/rockchip-linux/mpp/issues/601)**: HEVC encoder low_delay questions.

### Jellyfin
- **[jellyfin-ffmpeg #34](https://github.com/jellyfin/jellyfin-ffmpeg/issues/34)**: Enable/integrate Rockchip hwaccel support.
- **[jellyfin #12174](https://github.com/jellyfin/jellyfin/issues/12174)**: Hardware Acceleration DOES NOT WORK on RK3588 with Docker.

### Other
- **[nyanmisaka/ffmpeg-rockchip](https://github.com/nyanmisaka/ffmpeg-rockchip)**: Entire project exists for Rockchip hardware transcoding. De facto standard for Jellyfin, Frigate, go2rtc, Immich.
- **[OpenHD/FPVue_RK3566](https://github.com/OpenHD/FPVue_RK3566)**: FPV drone video on RK3566.
- **[Joshua-Riek/ubuntu-rockchip #482](https://github.com/Joshua-Riek/ubuntu-rockchip/discussions/482)**: RK3566 mainline, including video codec gaps.
- **[HeyMeco Gist](https://gist.github.com/HeyMeco/d016493a72baf94e9c69d072eaadf943)**: Benchmark: 4K HEVC encoding at 6Mbps on RK3568 via hevc_rkmpp, ~0.615x speed.

---

## 2. Forum Discussions

### Pine64
- **[Thread #17002](https://forum.pine64.org/showthread.php?tid=17002)**: "Hardware h264 video encoding" on Quartz64 (RK3566).

### Radxa
- **["Please open H264 hardware encoding/decoding for RK3566"](https://forum.radxa.com/t/rock3c-please-open-h264-hardware-encoding-decoding-for-rk3566/17151)** (~June 2023): Direct request for Rock3C.
- **["RK3399 vs RK3568 for H.264 encoding"](https://forum.radxa.com/t/rk3399-vs-rk3568-for-h-264-encoding/10987)**: Comparing SoCs for encoding workloads.
- **["Has anyone actually succeeded in getting hardware video encoding working?"](https://forum.radxa.com/t/has-anyone-actually-succeeded-in-getting-hardware-video-encoding-working/15894)**: Frustrated user, demonstrates difficulty.
- **["RK3566/RK3568 jpeg encoding + rga2 versioning issue"](https://forum.radxa.com/t/rk3566-rk3568-jpeg-encoding-rga2-versioning-issue/12980)** (~Nov 2022).
- **["How to use VPU for encode decode"](https://forum.radxa.com/t/how-to-use-vpu-for-encode-decode/10903)** (~Jul 2022): CM3 (RK3566), building FFmpeg with MPP.
- **["FFmpeg-Rockchip for hyper fast video transcoding"](https://forum.radxa.com/t/ffmpeg-introduce-ffmpeg-rockchip-for-hyper-fast-video-transcoding-via-cli/19508)**: Multi-page discussion.

### Armbian
- **["Need help with h264"](https://forum.armbian.com/topic/49973-neep-help-with-h264/)**: Radxa Zero 3W (RK3566).
- **["RK3566 and Armbian"](https://forum.armbian.com/topic/18255-rk3566-and-armbian/)**: General RK3566 support including multimedia.

### Other Forums
- **[Emby](https://emby.media/community/index.php?/topic/125680-hw-acceleration-decode-encode-with-rk3568/)**: "HW acceleration (decode+encode) with RK3568?" (2 pages).
- **[Jellyfin Forum](https://forum.jellyfin.org/t-changing-ffmpeg-encoder-from-libx264-to-rkmpp)**: Changing encoder to rkmpp.
- **[OBS Forums](https://obsproject.com/forum/threads/v4l2-multiplanar-support-rockchip-mpp-encoder-rga.161205/)**: V4L2 multiplanar + Rockchip MPP for live streaming.
- **[LibreELEC](https://forum.libreelec.tv/thread/29953-le13-testing-for-rk3288-rk3328-rk3399-rk3566-rk3568-rk3576-rk3588/)**: LE13 testing for RK3566.
- **[Home Assistant Community](https://community.home-assistant.io/t/how-to-use-a-rockchip-rk-frigate-image-on-home-assistant-green/728493)**: Frigate on Home Assistant Green (RK3566).
- **[Home Assistant Community](https://community.home-assistant.io/t/solved-reolink-h-265-h-264-transcoding-in-go2rtc-complete-working-guide/966420)**: Reolink H.265→H.264 transcoding guide.
- **[LinuxQuestions.org](https://www.linuxquestions.org/questions/slarm64-132/rockchip-hardware-video-decoding-mainline-kernel-vpu-rk3328-rk3399-rk3566-rk3588-4175722202/)**: Mainline kernel video decoding discussion.

---

## 3. Wiki/Documentation References

- **[Pine64 Wiki: Mainline Hardware Encoding](https://wiki.pine64.org/wiki/Mainline_Hardware_Encoding)**: H.264 encoding on RK3566 **NOT supported in mainline**. Listed as "one of the big missing pieces."
- **[Pine64 Wiki: Mainline Hardware Decoding](https://wiki.pine64.org/wiki/Mainline_Hardware_Decoding)**: RK3566 uses rkvdec2, no mainline driver yet.
- **[Jellyfin Docs: Rockchip VPU](https://jellyfin.org/docs/general/post-install/transcoding/hardware-acceleration/rockchip/)**: RK356x "may be supported but unable to test." Only RK3588 officially recommended.
- **[Collabora Blog: RK3588 upstream](https://www.collabora.com/news-and-blog/news-and-events/rockchip-rk3588-upstream-support-progress-future-plans.html)**: Hardware video encoding is "big missing piece." RK3566 not actively targeted.
- **[Kernel Patchwork: JPEG Encoder](https://patchwork.kernel.org/project/linux-media/cover/20220427224438.335327-1-frattaroli.nicolas@gmail.com/)**: Only encoder upstreamed for RK3566/RK3568 is JPEG (April 2022).

---

## 4. Quantified Summary

| Metric | Count |
|--------|-------|
| **Unique threads/issues** | **30+** |
| GitHub issues/discussions | 18 |
| Hardware/community forums | 15 |
| Reddit threads | 0 (indexing limitation) |
| Wiki/docs noting the gap | 5 |

### Use Cases (ranked by frequency)

1. **Media server transcoding (Jellyfin/Emby/Plex)** — ~10 threads
2. **Security camera / NVR (Frigate, go2rtc)** — ~8 threads
3. **Home Assistant camera streaming** — ~5 threads
4. **Photo/video library (Immich)** — ~2 threads
5. **FPV/drone video** — ~2 threads
6. **Live streaming (OBS, RTMP)** — ~2 threads
7. **General embedded/industrial encoding** — ~3 threads

### Key Observations

- **Home Assistant Green is the major demand driver.** RK3566-based, mainstream product, potentially hundreds of thousands of users wanting camera support.
- **The gap between HW capability and SW support is the main frustration.** RK3566 datasheet confirms 1080p@60fps H.264/H.265, but mainline support is nonexistent.
- **nyanmisaka/ffmpeg-rockchip** is the de facto standard — community-maintained, not upstream.
- **Collabora's mainline work targets RK3588/RK3576**, not RK3566.
- **Multiple frustration threads** express difficulty enabling HW encoding on Rockchip boards.
