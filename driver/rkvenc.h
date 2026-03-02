/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Rockchip RKVENC (VEPU540/541) V4L2 encoder driver
 *
 * Copyright (c) 2026 rk3566-v4l2-feasibility contributors
 */

#ifndef _RKVENC_H_
#define _RKVENC_H_

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/types.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "rkvenc_regs.h"

#define RKVENC_NAME			"rockchip-rkvenc"

/*
 * Hardware limits
 * Dimensions are in pixels; hardware operates on 16x16 MBs.
 */
#define RKVENC_MIN_WIDTH		96
#define RKVENC_MIN_HEIGHT		96
#define RKVENC_MAX_WIDTH		1920
#define RKVENC_MAX_HEIGHT		1088    /* MB-aligned 1080 */
#define RKVENC_MB_DIM			16
#define RKVENC_ALIGN_W			16
#define RKVENC_ALIGN_H			16

/* Maximum supported QP */
#define RKVENC_H264_QP_MAX		51
#define RKVENC_H264_QP_DEFAULT		26

/* GOP defaults */
#define RKVENC_GOP_DEFAULT		30
#define RKVENC_GOP_MAX			1000

/* Clock indices for clk_bulk API */
#define RKVENC_CLK_ACLK			0
#define RKVENC_CLK_HCLK			1
#define RKVENC_CLK_CORE			2
#define RKVENC_NUM_CLKS			3

/* Number of reconstruction frame buffers (ping-pong) */
#define RKVENC_NUM_RECON_BUFS		2

/**
 * struct rkvenc_variant - Per-SoC hardware variant data
 * @is_vepu540: true for VEPU540 (RK3566/RK3568), false for VEPU541 (RV1126)
 * @me_ram_size_default: default ME RAM size for this variant
 */
struct rkvenc_variant {
	bool is_vepu540;
	unsigned int me_ram_size_default;
};

/**
 * struct rkvenc_aux_buf - Driver-internal DMA buffer
 * @cpu: kernel virtual address
 * @dma: DMA/IOVA address
 * @size: buffer size in bytes
 */
struct rkvenc_aux_buf {
	void *cpu;
	dma_addr_t dma;
	size_t size;
};

/**
 * struct rkvenc_h264_params - H.264 encoding parameters for current frame
 * @profile_idc: H.264 profile (66=Baseline, 77=Main, 100=High)
 * @level_idc: H.264 level
 * @entropy_mode: 0=CAVLC, 1=CABAC
 * @transform_8x8: enable 8x8 transform (High profile)
 * @qp: target QP for this frame
 * @qp_min: minimum QP
 * @qp_max: maximum QP
 * @slice_type: 0=P, 2=I
 * @is_idr: true if this frame is an IDR
 * @frame_num: H.264 frame_num
 * @idr_pic_id: increments on each IDR
 * @poc_lsb: picture order count LSB
 * @cabac_init_idc: CABAC context init index (0-2)
 * @dbf_dis_idc: deblocking filter disable indicator
 * @dbf_alpha: deblocking alpha offset / 2
 * @dbf_beta: deblocking beta offset / 2
 */
struct rkvenc_h264_params {
	u8 profile_idc;
	u8 level_idc;
	u8 entropy_mode;
	bool transform_8x8;
	u8 qp;
	u8 qp_min;
	u8 qp_max;
	u8 slice_type;
	bool is_idr;
	u16 frame_num;
	u16 idr_pic_id;
	u16 poc_lsb;
	u8 cabac_init_idc;
	u8 dbf_dis_idc;
	s8 dbf_alpha;
	s8 dbf_beta;
};

/**
 * struct rkvenc_dev - Per-device driver state
 * @dev: platform device
 * @regs: MMIO register base
 * @clks: bulk clock array [aclk, hclk, core]
 * @variant: hardware variant data
 * @v4l2_dev: V4L2 device
 * @vdev: video device
 * @m2m_dev: V4L2 M2M device
 * @lock: serializes device-level operations
 */
struct rkvenc_dev {
	struct device *dev;
	void __iomem *regs;
	struct clk_bulk_data clks[RKVENC_NUM_CLKS];
	const struct rkvenc_variant *variant;

	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct v4l2_m2m_dev *m2m_dev;

	struct mutex lock; /* serializes HW access */
};

/**
 * struct rkvenc_ctx - Per-open-file encoder context
 * @fh: V4L2 file handle (contains ctrl_handler, m2m_ctx)
 * @dev: back-pointer to device
 * @ctrl_handler: V4L2 control handler
 *
 * @src_fmt: current OUTPUT (raw) format
 * @dst_fmt: current CAPTURE (compressed) format
 * @width: frame width in pixels
 * @height: frame height in pixels
 * @mb_width: width in macroblocks
 * @mb_height: height in macroblocks
 *
 * @params: current H.264 encoding parameters
 * @gop_size: GOP size (key frame interval)
 * @frames_since_idr: frame count since last IDR
 *
 * @recon: reconstruction frame buffers (ping-pong)
 * @recon_idx: index of the reconstruction buffer being written (0 or 1)
 * @me_buf: motion estimation scratch buffer
 * @mv_buf: collocated MV buffers
 *
 * @regs: shadow copy of L1 register file
 *
 * Controls:
 * @ctrl_profile: H.264 profile
 * @ctrl_level: H.264 level
 * @ctrl_entropy: entropy coding mode
 * @ctrl_8x8: 8x8 transform mode
 * @ctrl_gop: GOP size
 * @ctrl_qp_i: I-frame QP
 * @ctrl_qp_p: P-frame QP
 * @ctrl_qp_min: minimum QP
 * @ctrl_qp_max: maximum QP
 * @ctrl_force_key: force key frame
 */
struct rkvenc_ctx {
	struct v4l2_fh fh;
	struct rkvenc_dev *dev;
	struct v4l2_ctrl_handler ctrl_handler;

	/* Format state */
	struct v4l2_pix_format_mplane src_fmt;
	struct v4l2_pix_format_mplane dst_fmt;
	u32 width;
	u32 height;
	u32 mb_width;
	u32 mb_height;

	/* Encoding state */
	struct rkvenc_h264_params params;
	u32 gop_size;
	u32 frames_since_idr;

	/* Internal DMA buffers */
	struct rkvenc_aux_buf recon[RKVENC_NUM_RECON_BUFS];
	unsigned int recon_idx;
	struct rkvenc_aux_buf me_buf;
	struct rkvenc_aux_buf mv_buf[RKVENC_NUM_RECON_BUFS];

	/* Register shadow copy */
	u32 regs[RKVENC_REG_NUM];

	/* Control pointers */
	struct v4l2_ctrl *ctrl_profile;
	struct v4l2_ctrl *ctrl_level;
	struct v4l2_ctrl *ctrl_entropy;
	struct v4l2_ctrl *ctrl_8x8;
	struct v4l2_ctrl *ctrl_gop;
	struct v4l2_ctrl *ctrl_qp_i;
	struct v4l2_ctrl *ctrl_qp_p;
	struct v4l2_ctrl *ctrl_qp_min;
	struct v4l2_ctrl *ctrl_qp_max;
	struct v4l2_ctrl *ctrl_force_key;
};

static inline void rkvenc_write(struct rkvenc_dev *rkvenc, u32 reg, u32 val)
{
	writel(val, rkvenc->regs + reg);
}

static inline u32 rkvenc_read(struct rkvenc_dev *rkvenc, u32 reg)
{
	return readl(rkvenc->regs + reg);
}

/*
 * Write a contiguous block of shadow registers to hardware.
 * Used to program the L1 register file in a single burst.
 */
static inline void rkvenc_write_regs(struct rkvenc_dev *rkvenc,
				     const u32 *shadow,
				     unsigned int start_reg,
				     unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		writel(shadow[start_reg + i],
		       rkvenc->regs + RKVENC_REG(start_reg + i));
}

/* Function declarations — rkvenc_v4l2.c */
int rkvenc_v4l2_register(struct rkvenc_dev *rkvenc);
void rkvenc_v4l2_unregister(struct rkvenc_dev *rkvenc);

/* Function declarations — rkvenc_h264.c */
int rkvenc_h264_alloc_aux_bufs(struct rkvenc_ctx *ctx);
void rkvenc_h264_free_aux_bufs(struct rkvenc_ctx *ctx);
int rkvenc_h264_encode_frame(struct rkvenc_ctx *ctx,
			     struct vb2_v4l2_buffer *src_buf,
			     struct vb2_v4l2_buffer *dst_buf);

#endif /* _RKVENC_H_ */
