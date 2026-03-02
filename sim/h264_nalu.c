// SPDX-License-Identifier: GPL-2.0-only
/*
 * H.264 NAL unit generation — bitstream writer, SPS, PPS, slice header.
 *
 * Implements Exp-Golomb coding and the H.264 parameter set syntax
 * per ITU-T H.264 (04/2017) sections 7.3.2.1, 7.3.2.2, 7.3.3.
 */

#include "h264_nalu.h"
#include <string.h>

/* ================================================================
 * Bitstream writer
 * ================================================================ */

void bs_init(struct bs_writer *bs, uint8_t *buf, size_t capacity)
{
	bs->buf = buf;
	bs->capacity = capacity;
	bs->byte_pos = 0;
	bs->bit_pos = 8;
	if (capacity > 0)
		buf[0] = 0;
}

void bs_put_bits(struct bs_writer *bs, unsigned int n, uint32_t val)
{
	while (n > 0) {
		if (bs->byte_pos >= bs->capacity)
			return;

		unsigned int avail = (unsigned int)bs->bit_pos;
		unsigned int write = n < avail ? n : avail;
		unsigned int shift = avail - write;
		uint8_t mask = (uint8_t)(((1U << write) - 1) << shift);
		uint8_t bits = (uint8_t)(((val >> (n - write)) &
				((1U << write) - 1)) << shift);

		bs->buf[bs->byte_pos] = (bs->buf[bs->byte_pos] & ~mask) | bits;
		bs->bit_pos -= (int)write;
		n -= write;

		if (bs->bit_pos == 0) {
			bs->byte_pos++;
			bs->bit_pos = 8;
			if (bs->byte_pos < bs->capacity)
				bs->buf[bs->byte_pos] = 0;
		}
	}
}

void bs_put_ue(struct bs_writer *bs, uint32_t val)
{
	uint32_t code = val + 1;
	int bits = 0;
	uint32_t tmp = code;

	while (tmp > 0) {
		bits++;
		tmp >>= 1;
	}

	/* (bits-1) leading zeros */
	for (int i = 0; i < bits - 1; i++)
		bs_put_bits(bs, 1, 0);

	/* The code word */
	bs_put_bits(bs, (unsigned int)bits, code);
}

void bs_put_se(struct bs_writer *bs, int32_t val)
{
	uint32_t mapped;

	if (val > 0)
		mapped = (uint32_t)(2 * val - 1);
	else
		mapped = (uint32_t)(-2 * val);

	bs_put_ue(bs, mapped);
}

void bs_trailing_bits(struct bs_writer *bs)
{
	bs_put_bits(bs, 1, 1);  /* rbsp_stop_one_bit */
	if (bs->bit_pos != 8)
		bs_put_bits(bs, (unsigned int)bs->bit_pos, 0);
}

size_t bs_bytes_written(const struct bs_writer *bs)
{
	return bs->byte_pos + (bs->bit_pos < 8 ? 1 : 0);
}

/* ================================================================
 * Annex B start code
 * ================================================================ */

static size_t write_start_code(uint8_t *buf)
{
	buf[0] = 0x00;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x01;
	return 4;
}

/* ================================================================
 * SPS — Sequence Parameter Set (nal_unit_type = 7)
 *
 * ITU-T H.264 section 7.3.2.1.1
 * ================================================================ */

size_t h264_write_sps(uint8_t *buf, size_t cap,
		      const struct h264_sps_params *p)
{
	struct bs_writer bs;
	size_t off = write_start_code(buf);

	bs_init(&bs, buf + off, cap - off);

	/* NAL header */
	bs_put_bits(&bs, 1, 0);     /* forbidden_zero_bit */
	bs_put_bits(&bs, 2, 3);     /* nal_ref_idc = 3 */
	bs_put_bits(&bs, 5, 7);     /* nal_unit_type = SPS */

	/* profile_idc */
	bs_put_bits(&bs, 8, p->profile_idc);

	/* constraint_set0..5_flags + reserved_zero_2bits */
	{
		uint8_t cs = 0;

		if (p->profile_idc == 66)       /* Baseline */
			cs = 0xC0;             /* set0=1, set1=1 */
		else if (p->profile_idc == 77)  /* Main */
			cs = 0x40;             /* set1=1 */
		bs_put_bits(&bs, 8, cs);
	}

	/* level_idc */
	bs_put_bits(&bs, 8, p->level_idc);

	/* seq_parameter_set_id = 0 */
	bs_put_ue(&bs, 0);

	/* High profile: chroma/bit-depth/scaling */
	if (p->profile_idc >= 100) {
		bs_put_ue(&bs, 1);       /* chroma_format_idc = 1 (4:2:0) */
		bs_put_ue(&bs, 0);       /* bit_depth_luma_minus8 */
		bs_put_ue(&bs, 0);       /* bit_depth_chroma_minus8 */
		bs_put_bits(&bs, 1, 0);  /* qpprime_y_zero_transform_bypass */
		bs_put_bits(&bs, 1, 0);  /* seq_scaling_matrix_present */
	}

	/* log2_max_frame_num_minus4 */
	bs_put_ue(&bs, p->log2_max_frame_num - 4);

	/* pic_order_cnt_type = 0 */
	bs_put_ue(&bs, 0);
	bs_put_ue(&bs, p->log2_max_poc_lsb - 4);

	/* max_num_ref_frames = 1 */
	bs_put_ue(&bs, 1);

	/* gaps_in_frame_num_value_allowed = 0 */
	bs_put_bits(&bs, 1, 0);

	/* pic_width_in_mbs_minus1 */
	bs_put_ue(&bs, p->mb_width - 1);

	/* pic_height_in_map_units_minus1 */
	bs_put_ue(&bs, p->mb_height - 1);

	/* frame_mbs_only_flag = 1 (always progressive) */
	bs_put_bits(&bs, 1, 1);

	/* mb_adaptive_frame_field_flag: not present when frame_mbs_only=1 */

	/* direct_8x8_inference_flag */
	bs_put_bits(&bs, 1, p->transform_8x8 ? 1 : 0);

	/* frame_cropping */
	if (p->crop_bottom > 0) {
		bs_put_bits(&bs, 1, 1);  /* frame_cropping_flag */
		bs_put_ue(&bs, 0);      /* crop_left */
		bs_put_ue(&bs, 0);      /* crop_right */
		bs_put_ue(&bs, 0);      /* crop_top */
		/* 4:2:0 frame_mbs_only: units = 2 luma samples */
		bs_put_ue(&bs, p->crop_bottom / 2);
	} else {
		bs_put_bits(&bs, 1, 0);
	}

	/* vui_parameters_present = 0 */
	bs_put_bits(&bs, 1, 0);

	bs_trailing_bits(&bs);

	return off + bs_bytes_written(&bs);
}

/* ================================================================
 * PPS — Picture Parameter Set (nal_unit_type = 8)
 *
 * ITU-T H.264 section 7.3.2.2
 * ================================================================ */

size_t h264_write_pps(uint8_t *buf, size_t cap,
		      const struct h264_pps_params *p)
{
	struct bs_writer bs;
	size_t off = write_start_code(buf);

	bs_init(&bs, buf + off, cap - off);

	/* NAL header */
	bs_put_bits(&bs, 1, 0);
	bs_put_bits(&bs, 2, 3);     /* nal_ref_idc = 3 */
	bs_put_bits(&bs, 5, 8);     /* nal_unit_type = PPS */

	bs_put_ue(&bs, 0);  /* pic_parameter_set_id */
	bs_put_ue(&bs, 0);  /* seq_parameter_set_id */

	bs_put_bits(&bs, 1, p->entropy_cabac ? 1 : 0);
	bs_put_bits(&bs, 1, 0);  /* bottom_field_pic_order_in_frame_present */

	bs_put_ue(&bs, 0);  /* num_slice_groups_minus1 */
	bs_put_ue(&bs, 0);  /* num_ref_idx_l0_default_active_minus1 */
	bs_put_ue(&bs, 0);  /* num_ref_idx_l1_default_active_minus1 */

	bs_put_bits(&bs, 1, 0);  /* weighted_pred_flag */
	bs_put_bits(&bs, 2, 0);  /* weighted_bipred_idc */

	bs_put_se(&bs, p->pic_init_qp_minus26);
	bs_put_se(&bs, 0);  /* pic_init_qs_minus26 */
	bs_put_se(&bs, p->chroma_qp_index_offset);

	bs_put_bits(&bs, 1, 1);  /* deblocking_filter_control_present */
	bs_put_bits(&bs, 1, 0);  /* constrained_intra_pred */
	bs_put_bits(&bs, 1, 0);  /* redundant_pic_cnt_present */

	/* High profile: extra PPS fields */
	if (p->profile_idc >= 100) {
		bs_put_bits(&bs, 1, p->transform_8x8 ? 1 : 0);
		bs_put_bits(&bs, 1, 0);  /* pic_scaling_matrix_present */
		bs_put_se(&bs, p->chroma_qp_index_offset);
	}

	bs_trailing_bits(&bs);

	return off + bs_bytes_written(&bs);
}

/* ================================================================
 * Slice header (nal_unit_type = 5 for IDR, 1 for non-IDR)
 *
 * ITU-T H.264 section 7.3.3
 *
 * Only the header is written — the caller appends coded slice data
 * (from hardware, or dummy bytes for simulation).
 * ================================================================ */

size_t h264_write_slice_header(uint8_t *buf, size_t cap,
			       const struct h264_slice_params *p)
{
	struct bs_writer bs;
	size_t off = write_start_code(buf);

	bs_init(&bs, buf + off, cap - off);

	/* NAL header */
	bs_put_bits(&bs, 1, 0);
	bs_put_bits(&bs, 2, p->nal_ref_idc);
	bs_put_bits(&bs, 5, p->is_idr ? 5 : 1);

	/* first_mb_in_slice = 0 */
	bs_put_ue(&bs, 0);

	/* slice_type */
	bs_put_ue(&bs, p->slice_type);

	/* pic_parameter_set_id = 0 */
	bs_put_ue(&bs, 0);

	/* frame_num — fixed-width field */
	bs_put_bits(&bs, p->log2_max_frame_num, p->frame_num);

	/* IDR: idr_pic_id */
	if (p->is_idr)
		bs_put_ue(&bs, p->idr_pic_id);

	/* pic_order_cnt_type=0: poc_lsb — fixed-width field */
	bs_put_bits(&bs, p->log2_max_poc_lsb, p->poc_lsb);

	/* P-frame: ref pic list modification */
	if (p->slice_type == 0)
		bs_put_bits(&bs, 1, 0);  /* ref_pic_list_modification = 0 */

	/* dec_ref_pic_marking */
	if (p->is_idr) {
		bs_put_bits(&bs, 1, 0);  /* no_output_of_prior_pics */
		bs_put_bits(&bs, 1, 0);  /* long_term_reference */
	} else {
		bs_put_bits(&bs, 1, 0);  /* adaptive_ref_pic_marking = 0 */
	}

	/* CABAC: cabac_init_idc */
	if (p->entropy_cabac)
		bs_put_ue(&bs, p->cabac_init_idc);

	/* slice_qp_delta */
	bs_put_se(&bs, p->slice_qp_delta);

	/* deblocking filter params */
	bs_put_ue(&bs, p->dbf_dis_idc);
	if (p->dbf_dis_idc != 1) {
		bs_put_se(&bs, p->dbf_alpha);
		bs_put_se(&bs, p->dbf_beta);
	}

	bs_trailing_bits(&bs);

	return off + bs_bytes_written(&bs);
}
