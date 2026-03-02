// SPDX-License-Identifier: GPL-2.0-only
/*
 * VEPU540 simulation test harness.
 *
 * Constructs a fake rkvenc_dev + rkvenc_ctx, runs the full H.264
 * register programming pipeline against the simulated register
 * file, then validates key register values.
 *
 * Build:  make -C sim/
 * Run:    ./sim/sim_test
 *
 * Exit code 0 = all assertions passed.
 */

#include "kstubs.h"

/*
 * Pull in the real register definitions.  kstubs.h provides the
 * linux/ headers they need.
 */
#include "../driver/rkvenc_regs.h"

/*
 * We can't #include rkvenc.h directly because it pulls in real
 * kernel headers.  Instead, re-declare the structures we need
 * (they're simple POD types) and include the functions we test.
 */

/* Forward-declare what sim_hw.c provides */
extern void     sim_dump_regs(unsigned int start, unsigned int end);
extern u32      sim_read_reg(unsigned int idx);
extern unsigned int sim_enc_start_count;
extern unsigned int sim_int_clear_count;

/* ---- Minimal driver struct re-declarations ---- */

#define RKVENC_NUM_RECON_BUFS  2
#define RKVENC_ALIGN_W         16
#define RKVENC_ALIGN_H         16
#define RKVENC_MB_DIM          16

struct rkvenc_variant {
	bool is_vepu540;
	unsigned int me_ram_size_default;
};

struct rkvenc_aux_buf {
	void *cpu;
	dma_addr_t dma;
	size_t size;
};

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

struct rkvenc_dev {
	struct device *dev;
	void *regs;   /* void* instead of __iomem for sim */
	struct clk_bulk_data clks[3];
	const struct rkvenc_variant *variant;
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct v4l2_m2m_dev *m2m_dev;
	struct mutex lock;
};

struct rkvenc_ctx {
	struct v4l2_fh fh;
	struct rkvenc_dev *dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_pix_format_mplane src_fmt;
	struct v4l2_pix_format_mplane dst_fmt;
	u32 width;
	u32 height;
	u32 mb_width;
	u32 mb_height;
	struct rkvenc_h264_params params;
	u32 gop_size;
	u32 frames_since_idr;
	struct rkvenc_aux_buf recon[RKVENC_NUM_RECON_BUFS];
	unsigned int recon_idx;
	struct rkvenc_aux_buf me_buf;
	struct rkvenc_aux_buf mv_buf[RKVENC_NUM_RECON_BUFS];
	u32 regs[RKVENC_REG_NUM];
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

/* ---- inline helpers from rkvenc.h ---- */

static inline void rkvenc_write(struct rkvenc_dev *rkvenc, u32 reg, u32 val)
{
	writel(val, (u8 *)rkvenc->regs + reg);
}

static inline u32 rkvenc_read(struct rkvenc_dev *rkvenc, u32 reg)
{
	return readl((u8 *)rkvenc->regs + reg);
}

static inline void rkvenc_write_regs(struct rkvenc_dev *rkvenc,
				     const u32 *shadow,
				     unsigned int start_reg,
				     unsigned int count)
{
	unsigned int i;
	for (i = 0; i < count; i++)
		writel(shadow[start_reg + i],
		       (u8 *)rkvenc->regs + RKVENC_REG(start_reg + i));
}

/*
 * Pull in the real H.264 encoding code.
 * This compiles the actual setup_* functions against our stubs.
 * Block rkvenc.h (it would pull real kernel headers) — we already
 * have all the needed structs/helpers declared above.
 */
#define _RKVENC_H_
#include "../driver/rkvenc_h264.c"

/* ================================================================
 * Test infrastructure
 * ================================================================ */

static int tests_run;
static int tests_passed;
static int tests_failed;

#define ASSERT_EQ(desc, actual, expected) do {				\
	tests_run++;							\
	if ((actual) == (expected)) {					\
		tests_passed++;						\
	} else {							\
		tests_failed++;						\
		fprintf(stderr, "  FAIL: %s: got 0x%x, expected 0x%x\n",\
			(desc), (unsigned)(actual),			\
			(unsigned)(expected));				\
	}								\
} while (0)

#define ASSERT_NE(desc, actual, not_expected) do {			\
	tests_run++;							\
	if ((actual) != (not_expected)) {				\
		tests_passed++;						\
	} else {							\
		tests_failed++;						\
		fprintf(stderr, "  FAIL: %s: got 0x%x, should not be 0x%x\n",\
			(desc), (unsigned)(actual),			\
			(unsigned)(not_expected));			\
	}								\
} while (0)

#define ASSERT_TRUE(desc, cond) do {					\
	tests_run++;							\
	if (cond) {							\
		tests_passed++;						\
	} else {							\
		tests_failed++;						\
		fprintf(stderr, "  FAIL: %s\n", (desc));		\
	}								\
} while (0)

/* ================================================================
 * Test setup helpers
 * ================================================================ */

static struct rkvenc_dev test_dev;
static struct rkvenc_ctx test_ctx;
static struct device test_device;
static struct rkvenc_variant test_variant;

/* Fake V4L2 controls */
static struct v4l2_ctrl ctrl_profile_store;
static struct v4l2_ctrl ctrl_level_store;
static struct v4l2_ctrl ctrl_entropy_store;
static struct v4l2_ctrl ctrl_8x8_store;
static struct v4l2_ctrl ctrl_gop_store;
static struct v4l2_ctrl ctrl_qp_i_store;
static struct v4l2_ctrl ctrl_qp_p_store;
static struct v4l2_ctrl ctrl_qp_min_store;
static struct v4l2_ctrl ctrl_qp_max_store;
static struct v4l2_ctrl ctrl_force_key_store;

/* Fake VB2 buffers */
static struct vb2_v4l2_buffer fake_src;
static struct vb2_v4l2_buffer fake_dst;

static void setup_test_env(void)
{
	memset(&test_dev, 0, sizeof(test_dev));
	memset(&test_ctx, 0, sizeof(test_ctx));
	memset(&test_device, 0, sizeof(test_device));

	test_device.name = "sim-rkvenc";
	test_variant.is_vepu540 = true;
	test_variant.me_ram_size_default = 0;

	test_dev.dev = &test_device;
	test_dev.regs = sim_ioremap(4096);
	test_dev.variant = &test_variant;

	test_ctx.dev = &test_dev;
	test_ctx.width = 1920;
	test_ctx.height = 1080;
	test_ctx.mb_width = 1920 / 16;   /* 120 */
	test_ctx.mb_height = 1088 / 16;  /* 68 — aligned to 16 */
	test_ctx.gop_size = 30;
	test_ctx.frames_since_idr = 0;
	test_ctx.recon_idx = 0;

	/* Wire up controls with default values */
	ctrl_profile_store.val = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
	ctrl_level_store.val = 40;  /* level 4.0 */
	ctrl_entropy_store.val = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC;
	ctrl_8x8_store.val = 1;
	ctrl_gop_store.val = 30;
	ctrl_qp_i_store.val = 26;
	ctrl_qp_p_store.val = 28;
	ctrl_qp_min_store.val = 10;
	ctrl_qp_max_store.val = 51;
	ctrl_force_key_store.val = 0;

	test_ctx.ctrl_profile = &ctrl_profile_store;
	test_ctx.ctrl_level = &ctrl_level_store;
	test_ctx.ctrl_entropy = &ctrl_entropy_store;
	test_ctx.ctrl_8x8 = &ctrl_8x8_store;
	test_ctx.ctrl_gop = &ctrl_gop_store;
	test_ctx.ctrl_qp_i = &ctrl_qp_i_store;
	test_ctx.ctrl_qp_p = &ctrl_qp_p_store;
	test_ctx.ctrl_qp_min = &ctrl_qp_min_store;
	test_ctx.ctrl_qp_max = &ctrl_qp_max_store;
	test_ctx.ctrl_force_key = &ctrl_force_key_store;
}

/* ================================================================
 * Test cases
 * ================================================================ */

static void test_version_read(void)
{
	u32 ver;

	fprintf(stderr, "\n--- test_version_read ---\n");
	ver = rkvenc_read(&test_dev, RKVENC_VERSION);
	fprintf(stderr, "  VERSION = 0x%08x\n", ver);

	ASSERT_NE("version not zero", ver, 0);
	ASSERT_TRUE("H.264 capable", ver & RKVENC_VER_H264_ENC);
	ASSERT_TRUE("H.265 capable", ver & RKVENC_VER_H265_ENC);
}

static void test_alloc_aux_bufs(void)
{
	int ret, i;

	fprintf(stderr, "\n--- test_alloc_aux_bufs ---\n");

	ret = rkvenc_h264_alloc_aux_bufs(&test_ctx);
	ASSERT_EQ("alloc return", ret, 0);

	for (i = 0; i < RKVENC_NUM_RECON_BUFS; i++) {
		ASSERT_NE("recon[i].cpu not NULL",
			  (uintptr_t)test_ctx.recon[i].cpu, 0);
		ASSERT_NE("recon[i].dma not 0",
			  (u32)test_ctx.recon[i].dma, 0);
		ASSERT_NE("mv_buf[i].cpu not NULL",
			  (uintptr_t)test_ctx.mv_buf[i].cpu, 0);
	}
	ASSERT_NE("me_buf.cpu not NULL",
		  (uintptr_t)test_ctx.me_buf.cpu, 0);

	fprintf(stderr, "  recon size = %zu bytes\n",
		test_ctx.recon[0].size);
	fprintf(stderr, "  mv size    = %zu bytes\n",
		test_ctx.mv_buf[0].size);
	fprintf(stderr, "  me size    = %zu bytes\n",
		test_ctx.me_buf.size);
}

static void test_idr_encode(void)
{
	int ret;
	u32 reg;

	fprintf(stderr, "\n--- test_idr_encode (I-frame, 1920x1080, High, CABAC, QP=26) ---\n");

	/* Prepare params as device_run() would */
	test_ctx.params.profile_idc = 100;  /* High */
	test_ctx.params.level_idc = 40;
	test_ctx.params.entropy_mode = 1;   /* CABAC */
	test_ctx.params.transform_8x8 = true;
	test_ctx.params.qp = 26;
	test_ctx.params.qp_min = 10;
	test_ctx.params.qp_max = 51;
	test_ctx.params.is_idr = true;
	test_ctx.params.slice_type = RKVENC_SLICE_TYPE_I;
	test_ctx.params.frame_num = 0;
	test_ctx.params.idr_pic_id = 1;
	test_ctx.params.poc_lsb = 0;
	test_ctx.params.cabac_init_idc = 0;
	test_ctx.params.dbf_dis_idc = 0;
	test_ctx.params.dbf_alpha = 0;
	test_ctx.params.dbf_beta = 0;
	test_ctx.recon_idx = 0;

	ret = rkvenc_h264_encode_frame(&test_ctx, &fake_src, &fake_dst);
	ASSERT_EQ("encode return", ret, 0);

	/* Dump non-zero registers */
	sim_dump_regs(0, 30);
	sim_dump_regs(89, 91);
	sim_dump_regs(92, 109);
	sim_dump_regs(124, 131);

	/* ---- Validate reg001 (ENC_STRT) ---- */
	reg = sim_read_reg(1);
	ASSERT_TRUE("ENC_STRT: single-frame cmd",
		    FIELD_GET(RKVENC_STRT_CMD, reg) == RKVENC_STRT_CMD_SINGLE);
	ASSERT_TRUE("ENC_STRT: clock gating enabled",
		    reg & RKVENC_STRT_CLK_GATE_EN);

	/* ---- Validate reg004 (INT_EN) ---- */
	reg = sim_read_reg(4);
	ASSERT_TRUE("INT_EN: enc_done enabled",
		    reg & RKVENC_INT_ENC_DONE);
	ASSERT_TRUE("INT_EN: error bits enabled",
		    (reg & RKVENC_INT_ERROR_BITS) == RKVENC_INT_ERROR_BITS);

	/* ---- Validate reg012 (ENC_PIC) ---- */
	reg = sim_read_reg(12);
	{
		u32 w8 = FIELD_GET(RKVENC_PIC_WD8_M1, reg) + 1;
		u32 h8 = FIELD_GET(RKVENC_PIC_HD8_M1, reg) + 1;

		fprintf(stderr, "  ENC_PIC: w8=%u (px=%u), h8=%u (px=%u)\n",
			w8, w8 * 8, h8, h8 * 8);
		ASSERT_EQ("width in 8px", w8 * 8, 1920);
		ASSERT_EQ("height in 8px", h8 * 8, 1080);
	}

	/* ---- Validate reg014 (SRC_FMT) ---- */
	reg = sim_read_reg(14);
	ASSERT_EQ("SRC_FMT: NV12",
		  FIELD_GET(RKVENC_SRC_CFMT, reg),
		  (u32)RKVENC_SRC_CFMT_YUV420SP);

	/* ---- Validate reg093 (SPS) ---- */
	reg = sim_read_reg(93);
	ASSERT_EQ("SPS: profile_idc=100 (High)",
		  FIELD_GET(RKVENC_SPS_PROFILE_IDC, reg), 100U);
	ASSERT_EQ("SPS: level_idc=40",
		  FIELD_GET(RKVENC_SPS_LEVEL_IDC, reg), 40U);

	/* ---- Validate reg094 (SPS2) ---- */
	reg = sim_read_reg(94);
	{
		u32 w_mbs = FIELD_GET(RKVENC_SPS_PIC_W_MBS, reg) + 1;
		u32 h_mbs = FIELD_GET(RKVENC_SPS_PIC_H_MBS, reg) + 1;

		fprintf(stderr, "  SPS2: w_mbs=%u, h_mbs=%u\n", w_mbs, h_mbs);
		ASSERT_EQ("pic_width_in_mbs", w_mbs, 120U);
	}

	/* ---- Validate reg105 (codec config) ---- */
	reg = sim_read_reg(105);
	fprintf(stderr, "  reg105 (codec cfg) = 0x%08x\n", reg);
	ASSERT_TRUE("CABAC enabled", reg & RKVENC_CFG_ETPY_MODE);
	ASSERT_TRUE("8x8 transform enabled", reg & RKVENC_CFG_TRNS_8X8);
	ASSERT_EQ("slice type = I",
		  FIELD_GET(RKVENC_CFG_SLICE_TYPE, reg),
		  (u32)RKVENC_SLICE_TYPE_I);
	ASSERT_TRUE("frame_mbs_only set", reg & RKVENC_CFG_FRM_MBS_ONLY);

	/* ---- Validate reg092 (NAL header) ---- */
	reg = sim_read_reg(92);
	ASSERT_EQ("NAL unit type = IDR (5)",
		  FIELD_GET(RKVENC_NAL_UNIT_TYPE, reg), 5U);
	ASSERT_EQ("NAL ref_idc = 3 (IDR)",
		  FIELD_GET(RKVENC_NAL_REF_IDC, reg), 3U);

	/* ---- Validate reg124 (RC QP) ---- */
	reg = sim_read_reg(124);
	ASSERT_EQ("RC target QP = 26",
		  FIELD_GET(RKVENC_RC_QP_TARGET, reg), 26U);
	ASSERT_EQ("RC QP min = 10",
		  FIELD_GET(RKVENC_RC_QP_MIN, reg), 10U);
	ASSERT_EQ("RC QP max = 51",
		  FIELD_GET(RKVENC_RC_QP_MAX, reg), 51U);

	/* ---- Validate DMA addresses are non-zero ---- */
	ASSERT_NE("src Y addr",  sim_read_reg(16), 0);
	ASSERT_NE("src UV addr", sim_read_reg(17), 0);
	ASSERT_NE("BS addr",     sim_read_reg(23), 0);
	ASSERT_NE("BS size",     sim_read_reg(24), 0);
	ASSERT_NE("recon Y",     sim_read_reg(19), 0);
	ASSERT_NE("ref Y",       sim_read_reg(21), 0);
	ASSERT_NE("ME addr",     sim_read_reg(29), 0);

	/* ---- Validate HW was kicked ---- */
	ASSERT_EQ("encode started", sim_enc_start_count, 1);

	/* ---- Simulate IRQ readback ---- */
	{
		u32 status = rkvenc_read(&test_dev, RKVENC_INT_STA);
		u32 bs_len;

		ASSERT_TRUE("ENC_DONE in INT_STA",
			    status & RKVENC_INT_ENC_DONE);

		bs_len = rkvenc_read(&test_dev, RKVENC_ST_BSL) &
			 RKVENC_ST_BS_LGTH;
		fprintf(stderr, "  bitstream length = %u bytes\n", bs_len);
		ASSERT_TRUE("bitstream length > 0", bs_len > 0);

		/* Acknowledge */
		rkvenc_write(&test_dev, RKVENC_INT_CLR, status);
		status = rkvenc_read(&test_dev, RKVENC_INT_STA);
		ASSERT_EQ("INT_STA cleared after ack", status, 0);
	}
}

static void test_p_frame_encode(void)
{
	int ret;
	u32 reg;

	fprintf(stderr, "\n--- test_p_frame_encode ---\n");

	/* After IDR, set up for P-frame */
	test_ctx.params.is_idr = false;
	test_ctx.params.slice_type = RKVENC_SLICE_TYPE_P;
	test_ctx.params.frame_num = 1;
	test_ctx.params.poc_lsb = 2;
	test_ctx.params.qp = 28;
	test_ctx.params.cabac_init_idc = 1;
	/* recon_idx was toggled by the IDR encode, now = 1 */

	ret = rkvenc_h264_encode_frame(&test_ctx, &fake_src, &fake_dst);
	ASSERT_EQ("P encode return", ret, 0);

	/* NAL should be non-IDR */
	reg = sim_read_reg(92);
	ASSERT_EQ("NAL unit type = non-IDR (1)",
		  FIELD_GET(RKVENC_NAL_UNIT_TYPE, reg), 1U);

	/* Slice type = P */
	reg = sim_read_reg(105);
	ASSERT_EQ("slice type = P",
		  FIELD_GET(RKVENC_CFG_SLICE_TYPE, reg),
		  (u32)RKVENC_SLICE_TYPE_P);

	/* Reference should be different from reconstruction */
	{
		u32 recon_y = sim_read_reg(19);
		u32 ref_y = sim_read_reg(21);

		fprintf(stderr, "  recon_Y=0x%08x, ref_Y=0x%08x\n",
			recon_y, ref_y);
		ASSERT_NE("recon != ref (ping-pong)", recon_y, ref_y);
	}

	/* QP should be 28 */
	reg = sim_read_reg(124);
	ASSERT_EQ("P-frame QP = 28",
		  FIELD_GET(RKVENC_RC_QP_TARGET, reg), 28U);

	ASSERT_EQ("two encodes total", sim_enc_start_count, 2);
}

static void test_baseline_restrictions(void)
{
	int ret;
	u32 reg;

	fprintf(stderr, "\n--- test_baseline_restrictions ---\n");

	/* Switch to Baseline profile */
	test_ctx.params.profile_idc = 66;  /* Baseline */
	test_ctx.params.entropy_mode = 0;  /* CAVLC forced */
	test_ctx.params.transform_8x8 = false;
	test_ctx.params.is_idr = true;
	test_ctx.params.slice_type = RKVENC_SLICE_TYPE_I;
	test_ctx.params.frame_num = 0;
	test_ctx.params.qp = 26;

	ret = rkvenc_h264_encode_frame(&test_ctx, &fake_src, &fake_dst);
	ASSERT_EQ("baseline encode return", ret, 0);

	reg = sim_read_reg(93);
	ASSERT_EQ("profile_idc = 66 (Baseline)",
		  FIELD_GET(RKVENC_SPS_PROFILE_IDC, reg), 66U);

	reg = sim_read_reg(105);
	ASSERT_TRUE("CAVLC (entropy_mode=0)", !(reg & RKVENC_CFG_ETPY_MODE));
	ASSERT_TRUE("no 8x8 transform", !(reg & RKVENC_CFG_TRNS_8X8));
}

static void test_free_aux_bufs(void)
{
	fprintf(stderr, "\n--- test_free_aux_bufs ---\n");

	rkvenc_h264_free_aux_bufs(&test_ctx);

	ASSERT_EQ("recon[0] freed",
		  (uintptr_t)test_ctx.recon[0].cpu, 0);
	ASSERT_EQ("me_buf freed",
		  (uintptr_t)test_ctx.me_buf.cpu, 0);
}

/* ================================================================
 * H.264 NAL unit generation tests
 * ================================================================ */

#include "h264_nalu.h"

/* Scratch buffer for NAL output */
static uint8_t nalu_buf[4096];

static void test_bitstream_writer(void)
{
	struct bs_writer bs;
	uint8_t tmp[32];

	fprintf(stderr, "\n--- test_bitstream_writer ---\n");

	/* ue(0) = 1 (1 bit) */
	bs_init(&bs, tmp, sizeof(tmp));
	bs_put_ue(&bs, 0);
	bs_trailing_bits(&bs);
	ASSERT_EQ("ue(0) first byte", tmp[0], 0xC0);  /* 1 1000000 */

	/* ue(1) = 010 (3 bits) */
	bs_init(&bs, tmp, sizeof(tmp));
	bs_put_ue(&bs, 1);
	bs_trailing_bits(&bs);
	ASSERT_EQ("ue(1) first byte", tmp[0], 0x50);  /* 010 10000 */

	/* ue(5) = 00110 (5 bits), + trailing 100 = 00110100 = 0x34 */
	bs_init(&bs, tmp, sizeof(tmp));
	bs_put_ue(&bs, 5);
	bs_trailing_bits(&bs);
	ASSERT_EQ("ue(5) first byte", tmp[0], 0x34);

	/* se(-1) → mapped=2 → ue(2) = 011 (3 bits) */
	bs_init(&bs, tmp, sizeof(tmp));
	bs_put_se(&bs, -1);
	bs_trailing_bits(&bs);
	ASSERT_EQ("se(-1) first byte", tmp[0], 0x70);  /* 011 10000 */

	/* se(1) → mapped=1 → ue(1) = 010 (3 bits) */
	bs_init(&bs, tmp, sizeof(tmp));
	bs_put_se(&bs, 1);
	bs_trailing_bits(&bs);
	ASSERT_EQ("se(1) first byte", tmp[0], 0x50);  /* 010 10000 */

	/* Multi-bit packing: 8 bits then ue */
	bs_init(&bs, tmp, sizeof(tmp));
	bs_put_bits(&bs, 8, 0xAB);
	bs_put_ue(&bs, 0);
	bs_trailing_bits(&bs);
	ASSERT_EQ("8-bit + ue(0) byte 0", tmp[0], 0xAB);
	ASSERT_EQ("8-bit + ue(0) byte 1", tmp[1], 0xC0);
}

static void test_sps_generation(void)
{
	size_t len;
	struct h264_sps_params sps = {
		.profile_idc = 100,      /* High */
		.level_idc = 40,         /* 4.0 */
		.log2_max_frame_num = 4,
		.log2_max_poc_lsb = 4,
		.mb_width = 120,         /* 1920/16 */
		.mb_height = 68,         /* 1088/16 */
		.transform_8x8 = true,
		.crop_bottom = 8,        /* 1088-1080 = 8 */
	};

	fprintf(stderr, "\n--- test_sps_generation ---\n");

	len = h264_write_sps(nalu_buf, sizeof(nalu_buf), &sps);

	fprintf(stderr, "  SPS NAL: %zu bytes\n", len);
	ASSERT_TRUE("SPS length > 4 (start code)", len > 4);

	/* Verify Annex B start code */
	ASSERT_EQ("start code [0]", nalu_buf[0], 0x00);
	ASSERT_EQ("start code [1]", nalu_buf[1], 0x00);
	ASSERT_EQ("start code [2]", nalu_buf[2], 0x00);
	ASSERT_EQ("start code [3]", nalu_buf[3], 0x01);

	/* NAL header: forbidden=0, ref_idc=3, type=7 → 0x67 */
	ASSERT_EQ("NAL header = 0x67 (SPS)", nalu_buf[4], 0x67);

	/* profile_idc = 100 = 0x64 */
	ASSERT_EQ("profile_idc = 0x64 (High)", nalu_buf[5], 0x64);

	/* constraint_set flags = 0x00 for High */
	ASSERT_EQ("constraints = 0x00", nalu_buf[6], 0x00);

	/* level_idc = 40 = 0x28 */
	ASSERT_EQ("level_idc = 0x28", nalu_buf[7], 0x28);

	/* Test Baseline profile too */
	sps.profile_idc = 66;
	sps.transform_8x8 = false;
	sps.crop_bottom = 0;
	sps.mb_height = 67;  /* exact: no cropping needed for e.g. 1072 */

	len = h264_write_sps(nalu_buf, sizeof(nalu_buf), &sps);
	ASSERT_EQ("Baseline NAL header", nalu_buf[4], 0x67);
	ASSERT_EQ("Baseline profile_idc", nalu_buf[5], 0x42);
	ASSERT_EQ("Baseline constraints", nalu_buf[6], 0xC0);

	fprintf(stderr, "  Baseline SPS: %zu bytes\n", len);
}

static void test_pps_generation(void)
{
	size_t len;
	struct h264_pps_params pps = {
		.profile_idc = 100,
		.entropy_cabac = true,
		.transform_8x8 = true,
		.pic_init_qp_minus26 = 0,
		.chroma_qp_index_offset = 0,
	};

	fprintf(stderr, "\n--- test_pps_generation ---\n");

	len = h264_write_pps(nalu_buf, sizeof(nalu_buf), &pps);

	fprintf(stderr, "  PPS NAL: %zu bytes\n", len);
	ASSERT_TRUE("PPS length > 4", len > 4);

	/* Start code */
	ASSERT_EQ("start code [3]", nalu_buf[3], 0x01);

	/* NAL header: forbidden=0, ref_idc=3, type=8 → 0x68 */
	ASSERT_EQ("NAL header = 0x68 (PPS)", nalu_buf[4], 0x68);
}

static void test_slice_header(void)
{
	size_t len;
	struct h264_slice_params sl = {
		.is_idr = true,
		.slice_type = 2,        /* I */
		.frame_num = 0,
		.idr_pic_id = 0,
		.poc_lsb = 0,
		.log2_max_frame_num = 4,
		.log2_max_poc_lsb = 4,
		.entropy_cabac = true,
		.cabac_init_idc = 0,
		.slice_qp_delta = 0,
		.nal_ref_idc = 3,
		.dbf_dis_idc = 0,
		.dbf_alpha = 0,
		.dbf_beta = 0,
	};

	fprintf(stderr, "\n--- test_slice_header ---\n");

	len = h264_write_slice_header(nalu_buf, sizeof(nalu_buf), &sl);
	fprintf(stderr, "  IDR slice header: %zu bytes\n", len);

	/* NAL header: ref_idc=3, type=5 → 0x65 */
	ASSERT_EQ("IDR NAL header = 0x65", nalu_buf[4], 0x65);

	/* P-frame slice */
	sl.is_idr = false;
	sl.slice_type = 0;   /* P */
	sl.frame_num = 1;
	sl.poc_lsb = 2;
	sl.nal_ref_idc = 2;
	sl.cabac_init_idc = 1;

	len = h264_write_slice_header(nalu_buf, sizeof(nalu_buf), &sl);
	fprintf(stderr, "  P slice header: %zu bytes\n", len);

	/* NAL header: ref_idc=2, type=1 → 0x41 */
	ASSERT_EQ("P NAL header = 0x41", nalu_buf[4], 0x41);
}

/*
 * End-to-end test: generate a multi-frame .h264 file.
 *
 * Structure: [SPS][PPS][IDR][P][P][P]
 *
 * The slice data is dummy (just the header + trailing), but the
 * parameter sets and NAL structure are spec-compliant.  This file
 * should parse with ffprobe showing the correct codec parameters.
 */
#define H264_OUT_FILE  "sim_output.h264"
#define GOP_FRAMES     4  /* 1 IDR + 3 P */

static void test_h264_file_output(void)
{
	FILE *fp;
	uint8_t out[8192];
	size_t pos = 0;
	size_t len;
	int i;

	fprintf(stderr, "\n--- test_h264_file_output ---\n");

	/* SPS: 1920x1080 High profile, Level 4.0 */
	struct h264_sps_params sps = {
		.profile_idc = 100,
		.level_idc = 40,
		.log2_max_frame_num = 4,
		.log2_max_poc_lsb = 4,
		.mb_width = 120,
		.mb_height = 68,
		.transform_8x8 = true,
		.crop_bottom = 8,
	};

	struct h264_pps_params pps = {
		.profile_idc = 100,
		.entropy_cabac = true,
		.transform_8x8 = true,
		.pic_init_qp_minus26 = 0,
		.chroma_qp_index_offset = 0,
	};

	/* Write SPS */
	len = h264_write_sps(out + pos, sizeof(out) - pos, &sps);
	fprintf(stderr, "  SPS: %zu bytes @ offset %zu\n", len, pos);
	pos += len;

	/* Write PPS */
	len = h264_write_pps(out + pos, sizeof(out) - pos, &pps);
	fprintf(stderr, "  PPS: %zu bytes @ offset %zu\n", len, pos);
	pos += len;

	/* Write GOP: IDR + P frames */
	for (i = 0; i < GOP_FRAMES; i++) {
		struct h264_slice_params sl = {
			.is_idr = (i == 0),
			.slice_type = (i == 0) ? 2 : 0,
			.frame_num = (uint16_t)(i == 0 ? 0 : i),
			.idr_pic_id = 0,
			.poc_lsb = (uint16_t)(i * 2),
			.log2_max_frame_num = 4,
			.log2_max_poc_lsb = 4,
			.entropy_cabac = true,
			.cabac_init_idc = 0,
			.slice_qp_delta = 0,
			.nal_ref_idc = (uint8_t)(i == 0 ? 3 : 2),
			.dbf_dis_idc = 0,
			.dbf_alpha = 0,
			.dbf_beta = 0,
		};

		len = h264_write_slice_header(out + pos,
					      sizeof(out) - pos, &sl);
		fprintf(stderr, "  %s: %zu bytes @ offset %zu\n",
			i == 0 ? "IDR" : "P", len, pos);
		pos += len;
	}

	fprintf(stderr, "  Total: %zu bytes, %d NALUs\n",
		pos, 2 + GOP_FRAMES);

	/* Validate structure: scan for start codes and check NAL types */
	{
		size_t sc_pos[16];
		uint8_t nal_types[16];
		int nalu_count = 0;
		size_t j;

		for (j = 0; j + 3 < pos; j++) {
			if (out[j] == 0 && out[j+1] == 0 &&
			    out[j+2] == 0 && out[j+3] == 1) {
				if (nalu_count < 16) {
					sc_pos[nalu_count] = j;
					nal_types[nalu_count] =
						out[j+4] & 0x1F;
					nalu_count++;
				}
				j += 3;
			}
		}

		ASSERT_EQ("NALU count", nalu_count, 2 + GOP_FRAMES);
		ASSERT_EQ("NALU[0] = SPS (7)", nal_types[0], 7);
		ASSERT_EQ("NALU[1] = PPS (8)", nal_types[1], 8);
		ASSERT_EQ("NALU[2] = IDR (5)", nal_types[2], 5);

		for (i = 3; i < nalu_count; i++)
			ASSERT_EQ("NALU[n] = non-IDR (1)",
				  nal_types[i], 1);

		(void)sc_pos;  /* used for debug if needed */
	}

	/* Write file */
	fp = fopen(H264_OUT_FILE, "wb");
	if (fp) {
		fwrite(out, 1, pos, fp);
		fclose(fp);
		fprintf(stderr, "  Wrote %s (%zu bytes)\n",
			H264_OUT_FILE, pos);
	} else {
		fprintf(stderr, "  Warning: could not write %s\n",
			H264_OUT_FILE);
	}
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
	fprintf(stderr, "VEPU540 simulation test harness\n");
	fprintf(stderr, "================================\n");

	setup_test_env();

	/* Register programming tests */
	test_version_read();
	test_alloc_aux_bufs();
	test_idr_encode();
	test_p_frame_encode();
	test_baseline_restrictions();
	test_free_aux_bufs();

	/* H.264 NAL generation tests */
	test_bitstream_writer();
	test_sps_generation();
	test_pps_generation();
	test_slice_header();
	test_h264_file_output();

	fprintf(stderr, "\n================================\n");
	fprintf(stderr, "Results: %d/%d passed, %d failed\n",
		tests_passed, tests_run, tests_failed);

	if (tests_failed > 0) {
		fprintf(stderr, "SOME TESTS FAILED\n");
		return 1;
	}

	fprintf(stderr, "ALL TESTS PASSED\n");
	return 0;
}
