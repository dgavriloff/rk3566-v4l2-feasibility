/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Kernel type stubs for userspace simulation.
 *
 * Provides just enough of the linux kernel headers to let the real driver
 * and register-programming code compile in userspace.  MMIO writes
 * go to a flat memory buffer; reads come back from the same buffer
 * (with optional HW-model overrides for status/version registers).
 */

#ifndef _SIM_KSTUBS_H_
#define _SIM_KSTUBS_H_

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- basic kernel types ---- */
typedef uint8_t   u8;
typedef int8_t    s8;
typedef uint16_t  u16;
typedef int16_t   s16;
typedef uint32_t  u32;
typedef int32_t   s32;
typedef uint64_t  u64;
typedef int64_t   s64;

typedef u64 dma_addr_t;

/* ---- bitops (linux/bits.h, linux/bitfield.h) ---- */
#ifndef BIT
#define BIT(n)           (1U << (n))
#endif

#ifndef GENMASK
#define GENMASK(h, l)    (((~0U) << (l)) & ((~0U) >> (31 - (h))))
#endif

/*
 * FIELD_PREP / FIELD_GET — simplified versions.
 * Real kernel uses type-safe macros; this is good enough for sim.
 */
#ifndef FIELD_PREP
#define __bf_shf(mask)   (__builtin_ctzll(mask))
#define FIELD_PREP(mask, val)  (((u64)(val) << __bf_shf(mask)) & (mask))
#define FIELD_GET(mask, val)   (((val) & (mask)) >> __bf_shf(mask))
#endif

/* ---- min / max / clamp ---- */
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

static inline u32 clamp_t_u32(u32 val, u32 lo, u32 hi)
{
	if (val < lo) return lo;
	if (val > hi) return hi;
	return val;
}
#define clamp_t(type, val, lo, hi)  clamp_t_u32(val, lo, hi)
#define clamp(val, lo, hi)          clamp_t_u32(val, lo, hi)

/* ---- alignment ---- */
#ifndef ALIGN
#define ALIGN(x, a)  (((x) + ((a) - 1)) & ~((a) - 1))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))
#endif

#define lower_32_bits(x)  ((u32)(x))
#define upper_32_bits(x)  ((u32)((u64)(x) >> 32))

/* ---- printk stubs ---- */
#define dev_info(dev, fmt, ...)   fprintf(stderr, "[INFO] " fmt, ##__VA_ARGS__)
#define dev_err(dev, fmt, ...)    fprintf(stderr, "[ERR ] " fmt, ##__VA_ARGS__)
#define dev_warn(dev, fmt, ...)   fprintf(stderr, "[WARN] " fmt, ##__VA_ARGS__)
#define dev_dbg(dev, fmt, ...)    fprintf(stderr, "[DBG ] " fmt, ##__VA_ARGS__)

/* ---- GFP flags ---- */
#define GFP_KERNEL  0

/* ---- MMIO simulation ----
 *
 * The simulated register file is a flat array in sim_hw.c.
 * writel/readl redirect through function pointers so the HW model
 * can intercept specific registers (version, status).
 */

/* Provided by sim_hw.c */
extern void     sim_writel(u32 val, volatile void *addr);
extern u32      sim_readl(const volatile void *addr);
extern void    *sim_ioremap(size_t size);
extern void     sim_iounmap(void *base);

#define writel(val, addr)   sim_writel((val), (addr))
#define readl(addr)         sim_readl(addr)

/*
 * ---- Stubs for structs we reference but don't use in sim ----
 */

/* Minimal device stub */
struct device {
	const char *name;
};

/* Minimal clk stub */
struct clk_bulk_data {
	const char *id;
};

/* Minimal V4L2 / VB2 stubs */
struct v4l2_device { int dummy; };
struct video_device { int dummy; };
struct v4l2_m2m_dev;
struct v4l2_fh { void *m2m_ctx; void *ctrl_handler; };
struct v4l2_ctrl_handler { int error; };
struct v4l2_ctrl { s32 val; };
struct v4l2_pix_format_mplane {
	u32 pixelformat;
	u32 width;
	u32 height;
	u32 field;
	u32 colorspace;
	u32 num_planes;
	struct {
		u32 sizeimage;
		u32 bytesperline;
	} plane_fmt[4];
};

struct vb2_buffer {
	u64 timestamp;
};
struct vb2_v4l2_buffer {
	struct vb2_buffer vb2_buf;
	u32 flags;
};
struct vb2_queue { int dummy; };

struct mutex { int dummy; };

/* VB2 / DMA stubs */
static inline dma_addr_t
vb2_dma_contig_plane_dma_addr(struct vb2_buffer *vb, int plane)
{
	/* Return a fake DMA address based on the buffer pointer */
	return (dma_addr_t)(uintptr_t)vb + (plane * 0x100000);
}

static inline size_t vb2_plane_size(struct vb2_buffer *vb, int plane)
{
	/* Return a generous 2MB for simulation */
	return 2 * 1024 * 1024;
}

static inline void vb2_set_plane_payload(struct vb2_buffer *vb,
					 unsigned int plane, unsigned long sz)
{
	(void)vb; (void)plane; (void)sz;
}

/* pm_runtime stubs */
static inline int pm_runtime_resume_and_get(struct device *dev)
{
	return 0;
}
static inline void pm_runtime_put(struct device *dev) {}

/* DMA alloc stub — uses malloc + fake DMA addr */
static inline void *dma_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t *dma_handle, int flag)
{
	void *p = calloc(1, size);
	if (p && dma_handle)
		*dma_handle = (dma_addr_t)(uintptr_t)p;
	return p;
}

static inline void dma_free_coherent(struct device *dev, size_t size,
				     void *cpu, dma_addr_t dma)
{
	free(cpu);
}

/* V4L2 control stub */
static inline s32 v4l2_ctrl_g_ctrl(struct v4l2_ctrl *ctrl)
{
	return ctrl ? ctrl->val : 0;
}

/* V4L2 M2M stubs */
#define V4L2_PIX_FMT_NV12   0x3231564E  /* 'NV12' */
#define V4L2_PIX_FMT_H264   0x34363248  /* 'H264' */
#define V4L2_FIELD_NONE      1
#define V4L2_COLORSPACE_REC709  1
#define V4L2_BUF_FLAG_KEYFRAME  0x00000008
#define V4L2_BUF_FLAG_PFRAME    0x00000010
#define V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE  0
#define V4L2_MPEG_VIDEO_H264_PROFILE_MAIN      2
#define V4L2_MPEG_VIDEO_H264_PROFILE_HIGH      4
#define V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC  0
#define V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC  1

#endif /* _SIM_KSTUBS_H_ */
