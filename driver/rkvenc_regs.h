/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Rockchip RKVENC (VEPU540/541) register definitions
 *
 * Translated from Rockchip MPP hal_h264e_vepu541_reg.h and
 * hal_h264e_vepu541_reg_l2.h. Register map verified against
 * RK3568 TRM Part 2 V1.1 Chapter 10.
 *
 * Register file: 209 registers (reg000-reg208), each 32-bit.
 * Addresses: regN at offset N * 4 (0x0000 - 0x0340).
 *
 * Three regions:
 *   L1: 0x0000-0x0340  Main config, written as contiguous block
 *   L2: 0x10004+       RDO tuning, via indirect address/data port
 *   Status: 0x0210-0x0340  Read-back after encode complete
 */

#ifndef _RKVENC_REGS_H_
#define _RKVENC_REGS_H_

#include <linux/bitfield.h>
#include <linux/bits.h>

/* Total number of L1 registers */
#define RKVENC_REG_NUM			209
#define RKVENC_REG_START		0
#define RKVENC_REG_END			208

/* Helper: register index to byte offset */
#define RKVENC_REG(n)			((n) * 4)

/*
 * ============================================================
 * Control registers (reg000 - reg007)
 * ============================================================
 */

/* reg000 @ 0x0000 — VERSION (read-only) */
#define RKVENC_VERSION			RKVENC_REG(0)
#define   RKVENC_VER_SUB		GENMASK(7, 0)
#define   RKVENC_VER_H264_ENC		BIT(8)
#define   RKVENC_VER_H265_ENC		BIT(9)
#define   RKVENC_VER_PIC_SIZE		GENMASK(15, 12)
#define   RKVENC_VER_OSD_CAP		GENMASK(17, 16)
#define   RKVENC_VER_FILTR_CAP		GENMASK(19, 18)
#define   RKVENC_VER_BFRM_CAP		BIT(20)
#define   RKVENC_VER_FBC_CAP		BIT(21)
#define   RKVENC_VER_RKVENC		GENMASK(31, 24)

/* reg001 @ 0x0004 — ENC_STRT */
#define RKVENC_ENC_STRT			RKVENC_REG(1)
#define   RKVENC_STRT_LKT_NUM		GENMASK(7, 0)
#define   RKVENC_STRT_CMD		GENMASK(9, 8)
#define     RKVENC_STRT_CMD_SINGLE	1
#define     RKVENC_STRT_CMD_LINK	2
#define   RKVENC_STRT_CLK_GATE_EN	BIT(16)
#define   RKVENC_STRT_RESETN_HW_EN	BIT(17)
#define   RKVENC_STRT_ENC_DONE_TMVP	BIT(18)

/* reg002 @ 0x0008 — ENC_CLR */
#define RKVENC_ENC_CLR			RKVENC_REG(2)
#define   RKVENC_CLR_SAFE		BIT(0)
#define   RKVENC_CLR_FORCE		BIT(1)

/* reg003 @ 0x000C — LKT_ADDR */
#define RKVENC_LKT_ADDR			RKVENC_REG(3)

/*
 * ============================================================
 * Interrupt registers (reg004 - reg007)
 * Bits are identical across EN, MSK, CLR, STA registers.
 * ============================================================
 */

#define RKVENC_INT_EN			RKVENC_REG(4)   /* 0x0010 */
#define RKVENC_INT_MSK			RKVENC_REG(5)   /* 0x0014 */
#define RKVENC_INT_CLR			RKVENC_REG(6)   /* 0x0018 */
#define RKVENC_INT_STA			RKVENC_REG(7)   /* 0x001C */

#define   RKVENC_INT_ENC_DONE		BIT(0)
#define   RKVENC_INT_LKT_DONE		BIT(1)
#define   RKVENC_INT_SCLR_DONE		BIT(2)
#define   RKVENC_INT_SLC_DONE		BIT(3)
#define   RKVENC_INT_BSF_OVFLW		BIT(4)
#define   RKVENC_INT_BRSP_OSTD		BIT(5)
#define   RKVENC_INT_WBUS_ERR		BIT(6)
#define   RKVENC_INT_RBUS_ERR		BIT(7)
#define   RKVENC_INT_WDG		BIT(8)

#define RKVENC_INT_ERROR_BITS		(RKVENC_INT_BSF_OVFLW | \
					 RKVENC_INT_BRSP_OSTD | \
					 RKVENC_INT_WBUS_ERR  | \
					 RKVENC_INT_RBUS_ERR  | \
					 RKVENC_INT_WDG)

#define RKVENC_INT_ALL_BITS		(RKVENC_INT_ENC_DONE  | \
					 RKVENC_INT_LKT_DONE  | \
					 RKVENC_INT_SCLR_DONE | \
					 RKVENC_INT_SLC_DONE  | \
					 RKVENC_INT_ERROR_BITS)

/*
 * ============================================================
 * Configuration registers (reg008 - reg031)
 * AXI config, picture size, source format
 * ============================================================
 */

/* reg008-011 @ 0x0020-0x002C — AXI/bus config (timing, outstanding) */
#define RKVENC_AXI_CFG0			RKVENC_REG(8)
#define RKVENC_AXI_CFG1			RKVENC_REG(9)
#define RKVENC_AXI_CFG2			RKVENC_REG(10)
#define RKVENC_AXI_CFG3			RKVENC_REG(11)

/* reg012 @ 0x0030 — ENC_PIC: picture dimensions */
#define RKVENC_ENC_PIC			RKVENC_REG(12)
#define   RKVENC_PIC_WD8_M1		GENMASK(8, 0)     /* width in 8-px units - 1 */
#define   RKVENC_PIC_WFILL		GENMASK(11, 9)    /* right fill pixels (0-7) */
#define   RKVENC_PIC_HD8_M1		GENMASK(24, 16)   /* height in 8-px units - 1 */
#define   RKVENC_PIC_HFILL		GENMASK(27, 25)   /* bottom fill pixels (0-7) */

/* reg013 @ 0x0034 — ENC_RSL: encoding options */
#define RKVENC_ENC_RSL			RKVENC_REG(13)
#define   RKVENC_RSL_VS_LOAD_THD	GENMASK(7, 0)
#define   RKVENC_RSL_RFP_LOAD_THD	GENMASK(15, 8)
#define   RKVENC_RSL_ROI_EN		BIT(24)

/* reg014 @ 0x0038 — SRC_FMT: input pixel format */
#define RKVENC_SRC_FMT			RKVENC_REG(14)
#define   RKVENC_SRC_CFMT		GENMASK(3, 0)
#define     RKVENC_SRC_CFMT_YUV420P	0
#define     RKVENC_SRC_CFMT_YUV420SP	1  /* NV12 */
#define     RKVENC_SRC_CFMT_YUV422P	2
#define     RKVENC_SRC_CFMT_YUV422SP	3  /* NV16 */
#define     RKVENC_SRC_CFMT_YUYV	5
#define     RKVENC_SRC_CFMT_UYVY	6
#define     RKVENC_SRC_CFMT_BGR888	7
#define     RKVENC_SRC_CFMT_BGRA8888	8
#define     RKVENC_SRC_CFMT_RGB888	9
#define     RKVENC_SRC_CFMT_RGBA8888	10
#define   RKVENC_SRC_RANGE		BIT(4)    /* 0=limited, 1=full */
#define   RKVENC_SRC_SWAP_U_V		BIT(5)
#define   RKVENC_SRC_ALPHA_SWAP		BIT(6)
#define   RKVENC_SRC_RW_ENDIAN		BIT(7)

/* reg015 @ 0x003C — SRC_UDFY: source UV/stride */
#define RKVENC_SRC_UDFY			RKVENC_REG(15)
#define   RKVENC_SRC_Y_STRIDE		GENMASK(15, 0)
#define   RKVENC_SRC_C_STRIDE		GENMASK(31, 16)

/*
 * ============================================================
 * Source / reference / output DMA address registers
 * reg016-031 @ 0x0040-0x007C
 * reg068-087 @ 0x0110-0x015C
 *
 * These are 32-bit DMA addresses (IOMMU-translated IOVAs).
 * Written as part of the contiguous L1 register block.
 * ============================================================
 */

/* Source frame DMA addresses */
#define RKVENC_SRC_ADDR_Y		RKVENC_REG(16)  /* 0x0040 */
#define RKVENC_SRC_ADDR_U		RKVENC_REG(17)  /* 0x0044 */
#define RKVENC_SRC_ADDR_V		RKVENC_REG(18)  /* 0x0048 */

/* Reconstruction frame addresses (driver-internal buffers) */
#define RKVENC_RECON_ADDR_Y		RKVENC_REG(19)  /* 0x004C */
#define RKVENC_RECON_ADDR_U		RKVENC_REG(20)  /* 0x0050 */

/* Reference frame addresses (previous reconstruction) */
#define RKVENC_REF_ADDR_Y		RKVENC_REG(21)  /* 0x0054 */
#define RKVENC_REF_ADDR_U		RKVENC_REG(22)  /* 0x0058 */

/* Bitstream output DMA address and size */
#define RKVENC_BS_ADDR			RKVENC_REG(23)  /* 0x005C */
#define RKVENC_BS_SIZE			RKVENC_REG(24)  /* 0x0060 */

/* Collocated MV buffer (for temporal prediction) */
#define RKVENC_MV_OUT_ADDR		RKVENC_REG(25)  /* 0x0064 */
#define RKVENC_MV_REF_ADDR		RKVENC_REG(26)  /* 0x0068 */

/* ROI map DMA address */
#define RKVENC_ROI_ADDR			RKVENC_REG(27)  /* 0x006C */

/* Additional address registers */
#define RKVENC_OSD_ADDR			RKVENC_REG(28)  /* 0x0070 */
#define RKVENC_ME_ADDR			RKVENC_REG(29)  /* 0x0074 */

/*
 * reg030-067 @ 0x0078-0x010C — Reserved / additional config
 * reg068-087 @ 0x0110-0x015C — Additional DMA config & addresses
 *   These include OSD palette, secondary address configs, etc.
 *   Not needed for MVP H.264 encoding.
 */

/*
 * ============================================================
 * Motion estimation registers (reg089-091)
 * ============================================================
 */

#define RKVENC_ME_CFG0			RKVENC_REG(89)  /* 0x0164 */
#define   RKVENC_ME_SRCH_H		GENMASK(3, 0)   /* H search range */
#define   RKVENC_ME_SRCH_V		GENMASK(7, 4)   /* V search range */
#define   RKVENC_ME_RAM_SIZE		GENMASK(21, 8)  /* ME RAM size */

#define RKVENC_ME_CFG1			RKVENC_REG(90)  /* 0x0168 */
#define RKVENC_ME_CFG2			RKVENC_REG(91)  /* 0x016C */

/*
 * ============================================================
 * H.264 syntax registers (reg092-123) @ 0x0170-0x01EC
 *
 * These program the H.264 NAL/SPS/PPS/slice header parameters
 * that the hardware uses during encoding.
 * ============================================================
 */

/*
 * reg092-104: NAL header, SPS fields, PPS fields
 * Exact layout varies by firmware, but the critical encoding
 * config is in reg105 and reg109.
 */
#define RKVENC_SYNT_NAL			RKVENC_REG(92)  /* 0x0170 */
#define   RKVENC_NAL_REF_IDC		GENMASK(1, 0)
#define   RKVENC_NAL_UNIT_TYPE		GENMASK(6, 2)

#define RKVENC_SYNT_SPS			RKVENC_REG(93)  /* 0x0174 */
#define   RKVENC_SPS_PROFILE_IDC	GENMASK(7, 0)
#define   RKVENC_SPS_LEVEL_IDC		GENMASK(15, 8)
#define   RKVENC_SPS_MAX_FRM_NUM	GENMASK(19, 16)  /* log2_max_frame_num - 4 */
#define   RKVENC_SPS_MAX_POC_LSB	GENMASK(23, 20)  /* log2_max_poc_lsb - 4 */

#define RKVENC_SYNT_SPS2		RKVENC_REG(94)  /* 0x0178 */
#define   RKVENC_SPS_PIC_W_MBS		GENMASK(8, 0)   /* pic_width_in_mbs - 1 */
#define   RKVENC_SPS_PIC_H_MBS		GENMASK(24, 16) /* pic_height_in_mbs - 1 */
#define   RKVENC_SPS_DIRECT_8X8	BIT(28)
#define   RKVENC_SPS_CHROMA_FMT		GENMASK(31, 30)

/* reg095-104: Additional SPS/PPS fields */
#define RKVENC_SYNT_PPS			RKVENC_REG(95)  /* 0x017C */
#define RKVENC_SYNT_SLI0		RKVENC_REG(96)  /* 0x0180 */
#define RKVENC_SYNT_SLI1		RKVENC_REG(97)  /* 0x0184 */
#define RKVENC_SYNT_SLI2		RKVENC_REG(98)  /* 0x0188 */
#define RKVENC_SYNT_SLI3		RKVENC_REG(99)  /* 0x018C */

/* reg100-104: Frame numbering, POC, IDR */
#define RKVENC_SYNT_FRM0		RKVENC_REG(100) /* 0x0190 */
#define   RKVENC_FRM_NUM		GENMASK(15, 0)
#define   RKVENC_IDR_PIC_ID		GENMASK(31, 16)

#define RKVENC_SYNT_FRM1		RKVENC_REG(101) /* 0x0194 */
#define   RKVENC_POC_LSB		GENMASK(15, 0)

#define RKVENC_SYNT_FRM2		RKVENC_REG(102) /* 0x0198 */
#define RKVENC_SYNT_FRM3		RKVENC_REG(103) /* 0x019C */
#define RKVENC_SYNT_FRM4		RKVENC_REG(104) /* 0x01A0 */

/*
 * reg105 @ 0x01A4 — Critical H.264 codec config register
 * Contains entropy mode, transform, prediction, deblocking flags
 */
#define RKVENC_SYNT_CFG			RKVENC_REG(105) /* 0x01A4 */
#define   RKVENC_CFG_ETPY_MODE		BIT(0)        /* 0=CAVLC, 1=CABAC */
#define   RKVENC_CFG_TRNS_8X8		BIT(1)        /* 8x8 transform (High profile) */
#define   RKVENC_CFG_CSIP_FLAG		BIT(2)        /* constrained intra pred */
#define   RKVENC_CFG_WGHT_PRED		BIT(3)        /* weighted prediction */
#define   RKVENC_CFG_DBF_CP_FLG	BIT(4)        /* deblocking filter control */
#define   RKVENC_CFG_NUM_REF0_IDX	GENMASK(9, 5) /* num_ref_idx_l0 - 1 */
#define   RKVENC_CFG_SLICE_TYPE		GENMASK(11, 10)
#define     RKVENC_SLICE_TYPE_P		0
#define     RKVENC_SLICE_TYPE_I		2
#define   RKVENC_CFG_CABAC_INIT_IDC	GENMASK(13, 12)
#define   RKVENC_CFG_FRM_MBS_ONLY	BIT(14)
#define   RKVENC_CFG_DIRECT_8X8_INF	BIT(15)
#define   RKVENC_CFG_PIC_INIT_QP_M26	GENMASK(21, 16) /* pic_init_qp - 26 (signed) */
#define   RKVENC_CFG_CB_QP_OFFSET	GENMASK(27, 22) /* chroma_qp_index_offset (signed) */

/* reg106-108: Deblocking filter parameters, slice header extras */
#define RKVENC_SYNT_DBF			RKVENC_REG(106) /* 0x01A8 */
#define   RKVENC_DBF_DIS_IDC		GENMASK(1, 0)   /* disable_deblocking_filter_idc */
#define   RKVENC_DBF_ALPHA_DIV2		GENMASK(7, 2)   /* slice_alpha_c0_offset / 2 */
#define   RKVENC_DBF_BETA_DIV2		GENMASK(13, 8)  /* slice_beta_offset / 2 */

#define RKVENC_SYNT_SLICE0		RKVENC_REG(107) /* 0x01AC */
#define RKVENC_SYNT_SLICE1		RKVENC_REG(108) /* 0x01B0 */

/*
 * reg109 @ 0x01B4 — Reference management / MMCO
 */
#define RKVENC_SYNT_REF			RKVENC_REG(109) /* 0x01B4 */
#define   RKVENC_REF_LTRF_FLAG		BIT(0)
#define   RKVENC_REF_MMCO_TYPE0		GENMASK(4, 1)
#define   RKVENC_REF_MMCO_PARM0		GENMASK(20, 5)
#define   RKVENC_REF_MMCO_TYPE1		GENMASK(24, 21)
#define   RKVENC_REF_MMCO_TYPE2		GENMASK(28, 25)

/* reg110-123: Additional syntax / reserved */

/*
 * ============================================================
 * Rate control registers (reg124-131) @ 0x01F0-0x020C
 * ============================================================
 */

#define RKVENC_RC_QP			RKVENC_REG(124) /* 0x01F0 */
#define   RKVENC_RC_QP_TARGET		GENMASK(5, 0)
#define   RKVENC_RC_QP_MIN		GENMASK(11, 6)
#define   RKVENC_RC_QP_MAX		GENMASK(17, 12)
#define   RKVENC_RC_QP_I_MIN		GENMASK(23, 18)
#define   RKVENC_RC_QP_I_MAX		GENMASK(29, 24)

#define RKVENC_RC_CTU_TARGET		RKVENC_REG(125) /* 0x01F4 */
#define   RKVENC_RC_CTU_BITS		GENMASK(19, 0)  /* target bits per CTU row */

/* reg126-131: CTU-level QP threshold/delta zones (5 thresholds, 8 zones) */
#define RKVENC_RC_THD0			RKVENC_REG(126) /* 0x01F8 */
#define RKVENC_RC_THD1			RKVENC_REG(127) /* 0x01FC */
#define RKVENC_RC_THD2			RKVENC_REG(128) /* 0x0200 */
#define RKVENC_RC_DELTA0		RKVENC_REG(129) /* 0x0204 */
#define RKVENC_RC_DELTA1		RKVENC_REG(130) /* 0x0208 */
#define RKVENC_RC_DELTA2		RKVENC_REG(131) /* 0x020C */

/*
 * ============================================================
 * Status registers (reg132-208) @ 0x0210-0x0340
 * Read-back only, valid after enc_done interrupt.
 * ============================================================
 */

#define RKVENC_ST_BSL			RKVENC_REG(132) /* 0x0210 */
#define   RKVENC_ST_BS_LGTH		GENMASK(26, 0)  /* output bitstream byte count */

#define RKVENC_ST_SSE0			RKVENC_REG(133) /* 0x0214 */
#define RKVENC_ST_SSE1			RKVENC_REG(134) /* 0x0218 */
#define RKVENC_ST_SSE2			RKVENC_REG(135) /* 0x021C */

/* QP distribution, MADI/MADP complexity stats */
#define RKVENC_ST_QP			RKVENC_REG(136) /* 0x0220 */
#define   RKVENC_ST_QP_SUM		GENMASK(21, 0)
#define   RKVENC_ST_QP_NUM		GENMASK(31, 22)

#define RKVENC_ST_MADI			RKVENC_REG(137) /* 0x0224 */
#define RKVENC_ST_MADP			RKVENC_REG(138) /* 0x0228 */

/* MB partition type distribution */
#define RKVENC_ST_MBTYPE0		RKVENC_REG(139) /* 0x022C */
#define RKVENC_ST_MBTYPE1		RKVENC_REG(140) /* 0x0230 */

/*
 * reg141-208: Additional statistics, reserved
 * Full readback is useful for debug but not required for MVP.
 */

/* Convenience: first and last status register offsets */
#define RKVENC_ST_START			RKVENC_REG(132)
#define RKVENC_ST_END			RKVENC_REG(208)

/*
 * ============================================================
 * Slice splitting registers (reg205-208) @ 0x0334-0x0340
 * ============================================================
 */

#define RKVENC_SLC_CFG			RKVENC_REG(205) /* 0x0334 */
#define   RKVENC_SLC_EN			BIT(0)
#define   RKVENC_SLC_MODE		BIT(1)          /* 0=by MB rows, 1=by bytes */
#define   RKVENC_SLC_MB_ROWS		GENMASK(15, 2)  /* MB rows per slice */

#define RKVENC_SLC_SIZE			RKVENC_REG(206) /* 0x0338 */
#define   RKVENC_SLC_MAX_BYTES		GENMASK(19, 0)  /* max bytes per slice */

/*
 * ============================================================
 * L2 indirect register access
 *
 * L2 registers are accessed through an address/data port pair.
 * Write the target L2 address to the address port, then write
 * the data to the data port. Repeat for each L2 register.
 *
 * L2 contains RDO tuning parameters, lambda tables, and
 * quality control values.
 * ============================================================
 */

#define RKVENC_L2_ADDR_PORT		RKVENC_REG(68)  /* 0x0110 */
#define RKVENC_L2_DATA_PORT		RKVENC_REG(69)  /* 0x0114 */

/*
 * L2 register addresses (written to RKVENC_L2_ADDR_PORT)
 * These are the indirect addresses within the L2 region.
 */
#define RKVENC_L2_RDO_PRED_INTRA	0x10004
#define RKVENC_L2_RDO_PRED_INTER	0x10008
#define RKVENC_L2_RDO_ATFLT_I0		0x10034
#define RKVENC_L2_RDO_ATFLT_I1		0x10038
#define RKVENC_L2_RDO_ATFLT_P0		0x1003C
#define RKVENC_L2_RDO_ATFLT_P1		0x10040
#define RKVENC_L2_RDO_AFLK_I		0x1006C
#define RKVENC_L2_RDO_AFLK_P		0x10070

/*
 * L2 tuning table offsets for klut and lambda tables.
 * Start addresses for bulk programming.
 */
#define RKVENC_L2_KLUT_START		0x10200
#define RKVENC_L2_KLUT_COUNT		30

#define RKVENC_L2_LAMBDA_START		0x10280
#define RKVENC_L2_LAMBDA_COUNT		58

/*
 * ============================================================
 * H.264 NAL unit type constants
 * ============================================================
 */

#define RKVENC_H264_NAL_IDR		5
#define RKVENC_H264_NAL_NON_IDR		1

/*
 * H.264 profile_idc values
 */
#define RKVENC_H264_BASELINE		66
#define RKVENC_H264_MAIN		77
#define RKVENC_H264_HIGH		100

#endif /* _RKVENC_REGS_H_ */
