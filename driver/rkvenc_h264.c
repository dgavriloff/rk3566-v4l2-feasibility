// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip RKVENC (VEPU540/541) V4L2 encoder driver — H.264 encoding
 *
 * Translates the MPP HAL gen_regs pipeline into kernel register programming.
 * Each rkvenc_h264_setup_*() function corresponds to a setup_vepu541_*()
 * function in the MPP hal_h264e_vepu541.c.
 *
 * Reference: HermanChen/mpp hal_h264e_vepu541.c (~1872 lines)
 * Register defs: hal_h264e_vepu541_reg.h, hal_h264e_vepu541_reg_l2.h
 *
 * Copyright (c) 2026 rk3566-v4l2-feasibility contributors
 */

#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/minmax.h>

#include "rkvenc.h"

/*
 * ================================================================
 * Magic constant tables from MPP
 *
 * These are hardware-specific lookup tables used for RDO tuning.
 * Copied verbatim from hal_h264e_vepu541.c — no derivation
 * documentation exists, but they produce correct encode output.
 * ================================================================
 */

/*
 * KLUT weight table: mode decision weights for intra/inter cost
 * comparison. Written to L2 registers via indirect port.
 * 30 entries, indexed by internal HW state.
 */
static const u32 h264e_klut_weight[30] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000006, 0x00000001, 0x00000001, 0x00000001,
	0x00000001, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000,
};

/*
 * Lambda table: QP-to-lambda mapping for Lagrangian RDO.
 * 58 entries cover the full QP range (0-51) plus extended values.
 */
static const s32 h264e_lambda_default[58] = {
	0x00000003, 0x00000005, 0x00000006, 0x00000007,
	0x00000009, 0x0000000b, 0x0000000e, 0x00000012,
	0x00000016, 0x0000001c, 0x00000024, 0x0000002d,
	0x00000039, 0x00000048, 0x0000005b, 0x00000073,
	0x00000091, 0x000000b6, 0x000000e6, 0x00000122,
	0x0000016d, 0x000001cc, 0x00000244, 0x000002db,
	0x00000399, 0x00000489, 0x000005b6, 0x00000733,
	0x00000912, 0x00000b6d, 0x00000e66, 0x00001224,
	0x000016db, 0x00001ccc, 0x00002449, 0x00002db7,
	0x00003999, 0x00004892, 0x00005b6d, 0x00007333,
	0x00009124, 0x0000b6db, 0x0000e666, 0x00012249,
	0x00016db7, 0x0001cccc, 0x00024492, 0x0002db6e,
	0x00039999, 0x00048924, 0x0005b6db, 0x00073333,
	0x00091249, 0x000b6db7, 0x000e6666, 0x00000000,
	0x00000000, 0x00000000,
};

/*
 * Default anti-ringing filter params.
 * Written to L2 RDO_ATFLT registers for I and P frames.
 */
static const u32 h264e_atf_intra[2] = { 0x14, 0x0c };
static const u32 h264e_atf_inter[2] = { 0x28, 0x14 };

/*
 * ================================================================
 * Internal buffer management
 * ================================================================
 */

static int rkvenc_alloc_aux_buf(struct rkvenc_dev *rkvenc,
				struct rkvenc_aux_buf *buf, size_t size)
{
	buf->size = size;
	buf->cpu = dma_alloc_coherent(rkvenc->dev, size, &buf->dma,
				      GFP_KERNEL);
	if (!buf->cpu)
		return -ENOMEM;
	return 0;
}

static void rkvenc_free_aux_buf(struct rkvenc_dev *rkvenc,
				struct rkvenc_aux_buf *buf)
{
	if (buf->cpu) {
		dma_free_coherent(rkvenc->dev, buf->size, buf->cpu, buf->dma);
		buf->cpu = NULL;
		buf->dma = 0;
		buf->size = 0;
	}
}

void rkvenc_h264_free_aux_bufs(struct rkvenc_ctx *ctx)
{
	struct rkvenc_dev *rkvenc = ctx->dev;
	int i;

	rkvenc_free_aux_buf(rkvenc, &ctx->me_buf);
	for (i = 0; i < RKVENC_NUM_RECON_BUFS; i++) {
		rkvenc_free_aux_buf(rkvenc, &ctx->recon[i]);
		rkvenc_free_aux_buf(rkvenc, &ctx->mv_buf[i]);
	}
}

/*
 * Allocate internal buffers needed for encoding:
 * - 2 reconstruction frames (ping-pong)
 * - 2 collocated MV buffers
 * - 1 ME scratch buffer
 */
int rkvenc_h264_alloc_aux_bufs(struct rkvenc_ctx *ctx)
{
	struct rkvenc_dev *rkvenc = ctx->dev;
	unsigned int aligned_w = ALIGN(ctx->width, 64);
	unsigned int aligned_h = ALIGN(ctx->height, 64);
	size_t luma_size, chroma_size, recon_size;
	size_t mv_size, me_size;
	int ret, i;

	luma_size = aligned_w * aligned_h;
	chroma_size = luma_size / 2;	/* 4:2:0 */
	recon_size = luma_size + chroma_size;

	/* Collocated MV: 16 bytes per macroblock */
	mv_size = (ctx->mb_width * ctx->mb_height) * 16;

	/*
	 * ME RAM scratch buffer.
	 * Size depends on picture width and variant.
	 * Formula from MPP setup_vepu541_me():
	 *   VEPU540: me_size = width_in_ctu * 2 * 16 * 4
	 *   VEPU541: me_size = width_in_ctu * 4 * 16 * 4
	 * Conservative: use the larger VEPU541 formula.
	 */
	{
		unsigned int ctu_w = (ctx->width + 63) / 64;

		if (rkvenc->variant->is_vepu540)
			me_size = ctu_w * 2 * 16 * 4;
		else
			me_size = ctu_w * 4 * 16 * 4;
		me_size = ALIGN(me_size, 256);
	}

	for (i = 0; i < RKVENC_NUM_RECON_BUFS; i++) {
		ret = rkvenc_alloc_aux_buf(rkvenc, &ctx->recon[i], recon_size);
		if (ret)
			goto err_free;

		ret = rkvenc_alloc_aux_buf(rkvenc, &ctx->mv_buf[i], mv_size);
		if (ret)
			goto err_free;
	}

	ret = rkvenc_alloc_aux_buf(rkvenc, &ctx->me_buf, me_size);
	if (ret)
		goto err_free;

	dev_dbg(rkvenc->dev,
		"aux bufs: recon=%zu mv=%zu me=%zu (w=%u h=%u)\n",
		recon_size, mv_size, me_size, ctx->width, ctx->height);

	return 0;

err_free:
	rkvenc_h264_free_aux_bufs(ctx);
	return ret;
}

/*
 * ================================================================
 * Register programming pipeline
 *
 * Each function corresponds to a setup_vepu541_*() in the MPP HAL.
 * They populate ctx->regs[] (shadow copy) which is written to HW
 * as a contiguous block at the end.
 * ================================================================
 */

/*
 * setup_base: From MPP setup_vepu541_normal()
 * Base HW config, interrupts, clock gating.
 */
static void rkvenc_h264_setup_base(struct rkvenc_ctx *ctx)
{
	u32 *regs = ctx->regs;

	/* Clear shadow register file */
	memset(regs, 0, sizeof(ctx->regs));

	/* ENC_STRT: single-frame mode, enable clock gating */
	regs[1] = FIELD_PREP(RKVENC_STRT_CMD, RKVENC_STRT_CMD_SINGLE) |
		  RKVENC_STRT_CLK_GATE_EN;

	/* INT_EN: enable encode done + all error interrupts */
	regs[4] = RKVENC_INT_ENC_DONE | RKVENC_INT_ERROR_BITS;

	/* INT_MSK: unmask the same set */
	regs[5] = 0;
}

/*
 * setup_prep: From MPP setup_vepu541_prep()
 * Input format, resolution, stride.
 */
static void rkvenc_h264_setup_prep(struct rkvenc_ctx *ctx)
{
	u32 *regs = ctx->regs;
	unsigned int w8 = (ctx->width + 7) / 8;
	unsigned int h8 = (ctx->height + 7) / 8;
	unsigned int wfill = (ALIGN(ctx->width, 8) - ctx->width) & 0x7;
	unsigned int hfill = (ALIGN(ctx->height, 8) - ctx->height) & 0x7;
	u32 stride = ALIGN(ctx->width, RKVENC_ALIGN_W);

	/* ENC_PIC: resolution in 8-pixel units */
	regs[12] = FIELD_PREP(RKVENC_PIC_WD8_M1, w8 - 1) |
		   FIELD_PREP(RKVENC_PIC_WFILL, wfill) |
		   FIELD_PREP(RKVENC_PIC_HD8_M1, h8 - 1) |
		   FIELD_PREP(RKVENC_PIC_HFILL, hfill);

	/* SRC_FMT: NV12 (semi-planar 4:2:0) */
	regs[14] = FIELD_PREP(RKVENC_SRC_CFMT, RKVENC_SRC_CFMT_YUV420SP);

	/* SRC_UDFY: luma and chroma strides */
	regs[15] = FIELD_PREP(RKVENC_SRC_Y_STRIDE, stride) |
		   FIELD_PREP(RKVENC_SRC_C_STRIDE, stride);
}

/*
 * setup_codec: From MPP setup_vepu541_codec()
 * H.264 syntax: NAL, SPS, PPS, slice header parameters.
 */
static void rkvenc_h264_setup_codec(struct rkvenc_ctx *ctx)
{
	u32 *regs = ctx->regs;
	const struct rkvenc_h264_params *p = &ctx->params;
	u32 cfg;

	/* NAL header: ref_idc and unit type */
	regs[92] = FIELD_PREP(RKVENC_NAL_REF_IDC,
			      p->is_idr ? 3 : 2) |
		   FIELD_PREP(RKVENC_NAL_UNIT_TYPE,
			      p->is_idr ? RKVENC_H264_NAL_IDR :
					  RKVENC_H264_NAL_NON_IDR);

	/* SPS: profile, level, max frame num, max POC */
	regs[93] = FIELD_PREP(RKVENC_SPS_PROFILE_IDC, p->profile_idc) |
		   FIELD_PREP(RKVENC_SPS_LEVEL_IDC, p->level_idc) |
		   FIELD_PREP(RKVENC_SPS_MAX_FRM_NUM, 0) | /* log2_max_frame_num=4 */
		   FIELD_PREP(RKVENC_SPS_MAX_POC_LSB, 0);  /* log2_max_poc_lsb=4 */

	/* SPS2: picture size in MBs, chroma format, direct 8x8 */
	regs[94] = FIELD_PREP(RKVENC_SPS_PIC_W_MBS, ctx->mb_width - 1) |
		   FIELD_PREP(RKVENC_SPS_PIC_H_MBS, ctx->mb_height - 1) |
		   FIELD_PREP(RKVENC_SPS_CHROMA_FMT, 1) | /* 4:2:0 */
		   (p->transform_8x8 ? RKVENC_SPS_DIRECT_8X8 : 0);

	/* Frame numbering */
	regs[100] = FIELD_PREP(RKVENC_FRM_NUM, p->frame_num) |
		    FIELD_PREP(RKVENC_IDR_PIC_ID, p->idr_pic_id);

	regs[101] = FIELD_PREP(RKVENC_POC_LSB, p->poc_lsb);

	/*
	 * reg105: Critical codec config register.
	 * Entropy mode, transform, prediction, deblocking, slice type.
	 */
	cfg = 0;
	if (p->entropy_mode)
		cfg |= RKVENC_CFG_ETPY_MODE;
	if (p->transform_8x8)
		cfg |= RKVENC_CFG_TRNS_8X8;
	cfg |= RKVENC_CFG_FRM_MBS_ONLY;	/* frame_mbs_only=1 always */

	cfg |= FIELD_PREP(RKVENC_CFG_SLICE_TYPE, p->slice_type);
	cfg |= FIELD_PREP(RKVENC_CFG_CABAC_INIT_IDC, p->cabac_init_idc);
	cfg |= FIELD_PREP(RKVENC_CFG_NUM_REF0_IDX, 0); /* 1 reference */

	/* pic_init_qp_minus26 is a signed 6-bit field */
	cfg |= FIELD_PREP(RKVENC_CFG_PIC_INIT_QP_M26,
			  (u32)((s8)(p->qp - 26)) & 0x3F);

	/* chroma_qp_index_offset: default 0 */
	cfg |= FIELD_PREP(RKVENC_CFG_CB_QP_OFFSET, 0);

	if (p->transform_8x8)
		cfg |= RKVENC_CFG_DIRECT_8X8_INF;

	regs[105] = cfg;

	/* Deblocking filter parameters */
	regs[106] = FIELD_PREP(RKVENC_DBF_DIS_IDC, p->dbf_dis_idc) |
		    FIELD_PREP(RKVENC_DBF_ALPHA_DIV2,
			       (u32)((s8)p->dbf_alpha) & 0x3F) |
		    FIELD_PREP(RKVENC_DBF_BETA_DIV2,
			       (u32)((s8)p->dbf_beta) & 0x3F);
}

/*
 * setup_rdo: From MPP setup_vepu541_rdo_pred()
 * RDO prediction weights for intra/inter mode decision.
 */
static void rkvenc_h264_setup_rdo(struct rkvenc_ctx *ctx)
{
	/*
	 * RDO tuning is programmed via L2 indirect registers.
	 * This is handled in rkvenc_h264_write_l2() below.
	 * The L1 register file has no RDO-specific fields.
	 */
}

/*
 * setup_rc: From MPP setup_vepu541_rc_base()
 * Rate control: QP range, CTU bit target.
 */
static void rkvenc_h264_setup_rc(struct rkvenc_ctx *ctx)
{
	u32 *regs = ctx->regs;
	const struct rkvenc_h264_params *p = &ctx->params;

	/*
	 * For the initial driver, rate control is simple:
	 * - Set target QP from userspace control
	 * - Set min/max QP bounds
	 * - CTU-level bit target set to 0 (hardware uses QP-only mode)
	 */
	regs[124] = FIELD_PREP(RKVENC_RC_QP_TARGET, p->qp) |
		    FIELD_PREP(RKVENC_RC_QP_MIN, p->qp_min) |
		    FIELD_PREP(RKVENC_RC_QP_MAX, p->qp_max) |
		    FIELD_PREP(RKVENC_RC_QP_I_MIN, p->qp_min) |
		    FIELD_PREP(RKVENC_RC_QP_I_MAX, p->qp_max);

	/* CTU target: 0 = QP-only mode, no CTU-level RC */
	regs[125] = 0;

	/* QP delta thresholds: all zero for QP-only mode */
	regs[126] = 0;
	regs[127] = 0;
	regs[128] = 0;
	regs[129] = 0;
	regs[130] = 0;
	regs[131] = 0;
}

/*
 * setup_buffers: From MPP setup_vepu541_io_buf()
 * Program DMA addresses for source, output, and internal buffers.
 */
static void rkvenc_h264_setup_buffers(struct rkvenc_ctx *ctx,
				      struct vb2_v4l2_buffer *src_buf,
				      struct vb2_v4l2_buffer *dst_buf)
{
	u32 *regs = ctx->regs;
	dma_addr_t src_dma, dst_dma;
	u32 stride = ALIGN(ctx->width, RKVENC_ALIGN_W);

	/* Source Y plane DMA address */
	src_dma = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	regs[16] = lower_32_bits(src_dma);

	/* Source CbCr plane: Y plane size offset for NV12 */
	regs[17] = lower_32_bits(src_dma + stride * ALIGN(ctx->height, RKVENC_ALIGN_H));

	/* V plane: not used for NV12 (interleaved UV) */
	regs[18] = 0;

	/* Bitstream output DMA address */
	dst_dma = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	regs[23] = lower_32_bits(dst_dma);

	/* Bitstream buffer size */
	regs[24] = vb2_plane_size(&dst_buf->vb2_buf, 0);

	/* ME scratch buffer */
	regs[29] = lower_32_bits(ctx->me_buf.dma);
}

/*
 * setup_ref: From MPP setup_vepu541_recn_refr()
 * Reconstruction and reference frame buffer addresses.
 * Uses ping-pong pattern: current recon becomes next reference.
 */
static void rkvenc_h264_setup_ref(struct rkvenc_ctx *ctx)
{
	u32 *regs = ctx->regs;
	unsigned int recon_idx = ctx->recon_idx;
	unsigned int ref_idx = recon_idx ^ 1;	/* the other buffer */
	unsigned int aligned_w = ALIGN(ctx->width, 64);
	unsigned int aligned_h = ALIGN(ctx->height, 64);
	size_t luma_size = aligned_w * aligned_h;

	/* Reconstruction frame: Y and CbCr addresses */
	regs[19] = lower_32_bits(ctx->recon[recon_idx].dma);
	regs[20] = lower_32_bits(ctx->recon[recon_idx].dma + luma_size);

	/* Reference frame: previous reconstruction (for P-frames) */
	if (ctx->params.is_idr) {
		/* I-frame: no reference needed, point to self */
		regs[21] = regs[19];
		regs[22] = regs[20];
	} else {
		regs[21] = lower_32_bits(ctx->recon[ref_idx].dma);
		regs[22] = lower_32_bits(ctx->recon[ref_idx].dma + luma_size);
	}

	/* Collocated MV output (current) and reference (previous) */
	regs[25] = lower_32_bits(ctx->mv_buf[recon_idx].dma);
	regs[26] = lower_32_bits(ctx->mv_buf[ref_idx].dma);

	/* Toggle for next frame */
	ctx->recon_idx ^= 1;
}

/*
 * setup_me: From MPP setup_vepu541_me()
 * Motion estimation configuration.
 */
static void rkvenc_h264_setup_me(struct rkvenc_ctx *ctx)
{
	u32 *regs = ctx->regs;
	unsigned int ctu_w = (ctx->width + 63) / 64;
	unsigned int me_ram;

	/*
	 * ME RAM size calculation differs between VEPU540 and VEPU541.
	 * VEPU540 (RK3566/3568): smaller internal SRAM.
	 */
	if (ctx->dev->variant->is_vepu540)
		me_ram = ctu_w * 2 * 16;
	else
		me_ram = ctu_w * 4 * 16;

	/*
	 * ME search range: 7 = ±128 pixels for both H and V.
	 * Suitable for 1080p. Could be tuned per level.
	 */
	regs[89] = FIELD_PREP(RKVENC_ME_SRCH_H, 7) |
		   FIELD_PREP(RKVENC_ME_SRCH_V, 5) |
		   FIELD_PREP(RKVENC_ME_RAM_SIZE, me_ram);
}

/*
 * Write L2 indirect registers: lambda tables, klut weights,
 * anti-ringing filters.
 *
 * L2 registers are accessed through an address/data port pair.
 * Write target address to RKVENC_L2_ADDR_PORT, then data to
 * RKVENC_L2_DATA_PORT.
 */
static void rkvenc_h264_write_l2(struct rkvenc_ctx *ctx)
{
	struct rkvenc_dev *rkvenc = ctx->dev;
	unsigned int i;
	bool is_intra = ctx->params.is_idr;

	/* Write KLUT weight table */
	for (i = 0; i < RKVENC_L2_KLUT_COUNT; i++) {
		rkvenc_write(rkvenc, RKVENC_L2_ADDR_PORT,
			     RKVENC_L2_KLUT_START + i * 4);
		rkvenc_write(rkvenc, RKVENC_L2_DATA_PORT,
			     h264e_klut_weight[i]);
	}

	/* Write lambda table */
	for (i = 0; i < RKVENC_L2_LAMBDA_COUNT; i++) {
		rkvenc_write(rkvenc, RKVENC_L2_ADDR_PORT,
			     RKVENC_L2_LAMBDA_START + i * 4);
		rkvenc_write(rkvenc, RKVENC_L2_DATA_PORT,
			     (u32)h264e_lambda_default[i]);
	}

	/* Anti-ringing filter: different params for I vs P */
	rkvenc_write(rkvenc, RKVENC_L2_ADDR_PORT, RKVENC_L2_RDO_ATFLT_I0);
	rkvenc_write(rkvenc, RKVENC_L2_DATA_PORT,
		     is_intra ? h264e_atf_intra[0] : h264e_atf_inter[0]);

	rkvenc_write(rkvenc, RKVENC_L2_ADDR_PORT, RKVENC_L2_RDO_ATFLT_I1);
	rkvenc_write(rkvenc, RKVENC_L2_DATA_PORT,
		     is_intra ? h264e_atf_intra[1] : h264e_atf_inter[1]);
}

/*
 * ================================================================
 * Main encode function
 *
 * Called from device_run() in rkvenc_v4l2.c.
 * Programs all registers and kicks the hardware.
 * Completion is handled by the IRQ handler in rkvenc_drv.c.
 * ================================================================
 */
int rkvenc_h264_encode_frame(struct rkvenc_ctx *ctx,
			     struct vb2_v4l2_buffer *src_buf,
			     struct vb2_v4l2_buffer *dst_buf)
{
	struct rkvenc_dev *rkvenc = ctx->dev;

	/*
	 * Phase 1: Build register shadow copy.
	 * This follows the MPP gen_regs pipeline order exactly.
	 */
	rkvenc_h264_setup_base(ctx);	/* setup_vepu541_normal */
	rkvenc_h264_setup_prep(ctx);	/* setup_vepu541_prep */
	rkvenc_h264_setup_codec(ctx);	/* setup_vepu541_codec */
	rkvenc_h264_setup_rdo(ctx);	/* setup_vepu541_rdo_pred */
	rkvenc_h264_setup_rc(ctx);	/* setup_vepu541_rc_base */
	rkvenc_h264_setup_buffers(ctx, src_buf, dst_buf);
					/* setup_vepu541_io_buf */
	rkvenc_h264_setup_ref(ctx);	/* setup_vepu541_recn_refr */
	rkvenc_h264_setup_me(ctx);	/* setup_vepu541_me */

	/*
	 * Phase 2: Write L2 indirect registers.
	 * Must be done BEFORE the L1 block write (MPP does the same).
	 */
	rkvenc_h264_write_l2(ctx);

	/*
	 * Phase 3: Write L1 register block to hardware.
	 *
	 * The MPP writes registers as a contiguous block. We skip reg000
	 * (read-only version) and reg001 (encode start — written last).
	 * Write reg002 through reg208 first.
	 */
	rkvenc_write_regs(rkvenc, ctx->regs, 2, RKVENC_REG_NUM - 2);

	/*
	 * Phase 4: Kick encode by writing reg001 (ENC_STRT).
	 * This triggers the hardware to begin encoding. The IRQ handler
	 * will fire on completion.
	 */
	rkvenc_write(rkvenc, RKVENC_ENC_STRT, ctx->regs[1]);

	return 0;
}
