/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * H.264 NAL unit generation for VEPU540 simulation.
 *
 * The VEPU540 hardware produces raw coded slice data.  SPS and PPS
 * NAL units must be generated in software and prepended to the
 * bitstream.  This module implements the Exp-Golomb bitstream writer
 * and SPS/PPS/slice-header generators needed for that.
 *
 * These functions are also required by the real kernel driver —
 * they're tested here in userspace first.
 */

#ifndef _H264_NALU_H_
#define _H264_NALU_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- Bitstream writer ---- */

struct bs_writer {
	uint8_t *buf;
	size_t   capacity;
	size_t   byte_pos;
	int      bit_pos;   /* bits remaining in current byte (8..1) */
};

void bs_init(struct bs_writer *bs, uint8_t *buf, size_t capacity);
void bs_put_bits(struct bs_writer *bs, unsigned int n, uint32_t val);
void bs_put_ue(struct bs_writer *bs, uint32_t val);    /* Exp-Golomb unsigned */
void bs_put_se(struct bs_writer *bs, int32_t val);     /* Exp-Golomb signed */
void bs_trailing_bits(struct bs_writer *bs);
size_t bs_bytes_written(const struct bs_writer *bs);

/* ---- SPS parameters ---- */

struct h264_sps_params {
	uint8_t  profile_idc;        /* 66=Baseline, 77=Main, 100=High */
	uint8_t  level_idc;          /* e.g. 40 = Level 4.0 */
	uint8_t  log2_max_frame_num; /* 4..16 */
	uint8_t  log2_max_poc_lsb;   /* 4..16 */
	uint16_t mb_width;           /* pic_width_in_mbs */
	uint16_t mb_height;          /* pic_height_in_map_units */
	bool     transform_8x8;
	uint16_t crop_bottom;        /* bottom crop in luma samples, 0=none */
};

/* ---- PPS parameters ---- */

struct h264_pps_params {
	uint8_t  profile_idc;
	bool     entropy_cabac;
	bool     transform_8x8;
	int8_t   pic_init_qp_minus26;
	int8_t   chroma_qp_index_offset;
};

/* ---- Slice header parameters ---- */

struct h264_slice_params {
	bool     is_idr;
	uint8_t  slice_type;         /* 0=P, 2=I */
	uint16_t frame_num;
	uint16_t idr_pic_id;
	uint16_t poc_lsb;
	uint8_t  log2_max_frame_num;
	uint8_t  log2_max_poc_lsb;
	bool     entropy_cabac;
	uint8_t  cabac_init_idc;
	int8_t   slice_qp_delta;
	uint8_t  nal_ref_idc;
	uint8_t  dbf_dis_idc;
	int8_t   dbf_alpha;
	int8_t   dbf_beta;
};

/*
 * Write NAL units to buf.  Each includes the 4-byte Annex B start
 * code (00 00 00 01).  Returns total bytes written.
 *
 * Note: RBSP emulation prevention (0x03 stuffing) is not implemented.
 * For the small parameter sets we generate, forbidden byte sequences
 * (00 00 00/01/02/03) are statistically unlikely and irrelevant for
 * simulation.  A production driver would add it.
 */
size_t h264_write_sps(uint8_t *buf, size_t cap,
		      const struct h264_sps_params *p);
size_t h264_write_pps(uint8_t *buf, size_t cap,
		      const struct h264_pps_params *p);
size_t h264_write_slice_header(uint8_t *buf, size_t cap,
			       const struct h264_slice_params *p);

#endif /* _H264_NALU_H_ */
