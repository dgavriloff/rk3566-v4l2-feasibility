// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip RKVENC (VEPU540/541) V4L2 encoder driver — V4L2 m2m layer
 *
 * Implements format negotiation, VB2 queue ops, V4L2 controls,
 * and the m2m device_run callback.
 *
 * Copyright (c) 2026 rk3566-v4l2-feasibility contributors
 */

#include <linux/pm_runtime.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "rkvenc.h"

/*
 * Supported raw input formats (OUTPUT queue).
 * NV12 is the primary format; others can be added per VEPU540 capability.
 */
static const u32 rkvenc_src_fmts[] = {
	V4L2_PIX_FMT_NV12,
};

/*
 * Supported compressed output formats (CAPTURE queue).
 * H.265 will be added in a later phase.
 */
static const u32 rkvenc_dst_fmts[] = {
	V4L2_PIX_FMT_H264,
};

static const u32 *rkvenc_get_fmts(bool output, unsigned int *count)
{
	if (output) {
		*count = ARRAY_SIZE(rkvenc_src_fmts);
		return rkvenc_src_fmts;
	}
	*count = ARRAY_SIZE(rkvenc_dst_fmts);
	return rkvenc_dst_fmts;
}

static bool rkvenc_is_valid_fmt(const u32 *fmts, unsigned int count, u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		if (fmts[i] == fourcc)
			return true;
	}
	return false;
}

/* ----------------------------------------------------------------
 * Format helpers
 * ---------------------------------------------------------------- */

static void rkvenc_set_default_src_fmt(struct v4l2_pix_format_mplane *fmt)
{
	memset(fmt, 0, sizeof(*fmt));
	fmt->pixelformat = V4L2_PIX_FMT_NV12;
	fmt->width = 1920;
	fmt->height = 1080;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_REC709;
	fmt->num_planes = 1;

	/* NV12: Y + interleaved UV in a single plane */
	fmt->plane_fmt[0].bytesperline = ALIGN(fmt->width, RKVENC_ALIGN_W);
	fmt->plane_fmt[0].sizeimage =
		fmt->plane_fmt[0].bytesperline * ALIGN(fmt->height, RKVENC_ALIGN_H) * 3 / 2;
}

static void rkvenc_set_default_dst_fmt(struct v4l2_pix_format_mplane *fmt)
{
	memset(fmt, 0, sizeof(*fmt));
	fmt->pixelformat = V4L2_PIX_FMT_H264;
	fmt->width = 1920;
	fmt->height = 1080;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_REC709;
	fmt->num_planes = 1;

	/*
	 * Compressed buffer size: conservative estimate.
	 * Worst case H.264 is ~1.5x raw for high QP, but typically
	 * much less. We use raw frame size as the upper bound.
	 */
	fmt->plane_fmt[0].bytesperline = 0;
	fmt->plane_fmt[0].sizeimage = 1920 * 1080 * 3 / 2;
}

static void rkvenc_try_src_fmt(struct v4l2_pix_format_mplane *fmt)
{
	unsigned int count;
	const u32 *fmts = rkvenc_get_fmts(true, &count);
	u32 stride;

	if (!rkvenc_is_valid_fmt(fmts, count, fmt->pixelformat))
		fmt->pixelformat = V4L2_PIX_FMT_NV12;

	fmt->width = clamp(fmt->width, (u32)RKVENC_MIN_WIDTH,
			   (u32)RKVENC_MAX_WIDTH);
	fmt->height = clamp(fmt->height, (u32)RKVENC_MIN_HEIGHT,
			    (u32)RKVENC_MAX_HEIGHT);

	/* Align to macroblock boundaries */
	fmt->width = ALIGN(fmt->width, RKVENC_MB_DIM);
	fmt->height = ALIGN(fmt->height, RKVENC_MB_DIM);

	fmt->field = V4L2_FIELD_NONE;
	fmt->num_planes = 1;

	stride = ALIGN(fmt->width, RKVENC_ALIGN_W);
	fmt->plane_fmt[0].bytesperline = stride;
	fmt->plane_fmt[0].sizeimage =
		stride * ALIGN(fmt->height, RKVENC_ALIGN_H) * 3 / 2;
}

static void rkvenc_try_dst_fmt(struct v4l2_pix_format_mplane *fmt,
			       const struct v4l2_pix_format_mplane *src_fmt)
{
	unsigned int count;
	const u32 *fmts = rkvenc_get_fmts(false, &count);

	if (!rkvenc_is_valid_fmt(fmts, count, fmt->pixelformat))
		fmt->pixelformat = V4L2_PIX_FMT_H264;

	/* Destination dimensions must match source */
	fmt->width = src_fmt->width;
	fmt->height = src_fmt->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->num_planes = 1;
	fmt->plane_fmt[0].bytesperline = 0;

	/* Compressed size: at least raw frame size */
	if (fmt->plane_fmt[0].sizeimage <
	    src_fmt->plane_fmt[0].sizeimage)
		fmt->plane_fmt[0].sizeimage =
			src_fmt->plane_fmt[0].sizeimage;
}

/* ----------------------------------------------------------------
 * V4L2 ioctl ops
 * ---------------------------------------------------------------- */

static int rkvenc_querycap(struct file *file, void *fh,
			   struct v4l2_capability *cap)
{
	strscpy(cap->driver, RKVENC_NAME, sizeof(cap->driver));
	strscpy(cap->card, "Rockchip RKVENC encoder", sizeof(cap->card));
	return 0;
}

static int rkvenc_enum_fmt(struct file *file, void *fh,
			   struct v4l2_fmtdesc *f, bool output)
{
	unsigned int count;
	const u32 *fmts = rkvenc_get_fmts(output, &count);

	if (f->index >= count)
		return -EINVAL;

	f->pixelformat = fmts[f->index];
	return 0;
}

static int rkvenc_enum_fmt_vid_out(struct file *file, void *fh,
				   struct v4l2_fmtdesc *f)
{
	return rkvenc_enum_fmt(file, fh, f, true);
}

static int rkvenc_enum_fmt_vid_cap(struct file *file, void *fh,
				   struct v4l2_fmtdesc *f)
{
	return rkvenc_enum_fmt(file, fh, f, false);
}

static int rkvenc_g_fmt_vid_out(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct rkvenc_ctx *ctx = container_of(fh, struct rkvenc_ctx, fh);

	f->fmt.pix_mp = ctx->src_fmt;
	return 0;
}

static int rkvenc_g_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct rkvenc_ctx *ctx = container_of(fh, struct rkvenc_ctx, fh);

	f->fmt.pix_mp = ctx->dst_fmt;
	return 0;
}

static int rkvenc_try_fmt_vid_out(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	rkvenc_try_src_fmt(&f->fmt.pix_mp);
	return 0;
}

static int rkvenc_try_fmt_vid_cap(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	struct rkvenc_ctx *ctx = container_of(fh, struct rkvenc_ctx, fh);

	rkvenc_try_dst_fmt(&f->fmt.pix_mp, &ctx->src_fmt);
	return 0;
}

static int rkvenc_s_fmt_vid_out(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct rkvenc_ctx *ctx = container_of(fh, struct rkvenc_ctx, fh);

	rkvenc_try_src_fmt(&f->fmt.pix_mp);
	ctx->src_fmt = f->fmt.pix_mp;

	ctx->width = ctx->src_fmt.width;
	ctx->height = ctx->src_fmt.height;
	ctx->mb_width = ctx->width / RKVENC_MB_DIM;
	ctx->mb_height = ctx->height / RKVENC_MB_DIM;

	/* Propagate to destination format */
	rkvenc_try_dst_fmt(&ctx->dst_fmt, &ctx->src_fmt);

	return 0;
}

static int rkvenc_s_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct rkvenc_ctx *ctx = container_of(fh, struct rkvenc_ctx, fh);

	rkvenc_try_dst_fmt(&f->fmt.pix_mp, &ctx->src_fmt);
	ctx->dst_fmt = f->fmt.pix_mp;
	return 0;
}

static int rkvenc_enum_framesizes(struct file *file, void *fh,
				  struct v4l2_frmsizeenum *fsize)
{
	unsigned int src_count, dst_count;
	const u32 *src_fmts = rkvenc_get_fmts(true, &src_count);
	const u32 *dst_fmts = rkvenc_get_fmts(false, &dst_count);

	if (fsize->index != 0)
		return -EINVAL;

	if (!rkvenc_is_valid_fmt(src_fmts, src_count, fsize->pixel_format) &&
	    !rkvenc_is_valid_fmt(dst_fmts, dst_count, fsize->pixel_format))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = RKVENC_MIN_WIDTH;
	fsize->stepwise.max_width = RKVENC_MAX_WIDTH;
	fsize->stepwise.step_width = RKVENC_MB_DIM;
	fsize->stepwise.min_height = RKVENC_MIN_HEIGHT;
	fsize->stepwise.max_height = RKVENC_MAX_HEIGHT;
	fsize->stepwise.step_height = RKVENC_MB_DIM;
	return 0;
}

static const struct v4l2_ioctl_ops rkvenc_ioctl_ops = {
	.vidioc_querycap		= rkvenc_querycap,

	.vidioc_enum_fmt_vid_out_mplane	= rkvenc_enum_fmt_vid_out,
	.vidioc_enum_fmt_vid_cap_mplane	= rkvenc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_out_mplane	= rkvenc_g_fmt_vid_out,
	.vidioc_g_fmt_vid_cap_mplane	= rkvenc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_out_mplane	= rkvenc_try_fmt_vid_out,
	.vidioc_try_fmt_vid_cap_mplane	= rkvenc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_out_mplane	= rkvenc_s_fmt_vid_out,
	.vidioc_s_fmt_vid_cap_mplane	= rkvenc_s_fmt_vid_cap,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,

	.vidioc_enum_framesizes		= rkvenc_enum_framesizes,

	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/* ----------------------------------------------------------------
 * VB2 queue ops
 * ---------------------------------------------------------------- */

static int rkvenc_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
			      unsigned int *nplanes, unsigned int sizes[],
			      struct device *alloc_devs[])
{
	struct rkvenc_ctx *ctx = vb2_get_drv_priv(vq);
	const struct v4l2_pix_format_mplane *fmt;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		fmt = &ctx->src_fmt;
	else
		fmt = &ctx->dst_fmt;

	if (*nplanes) {
		/* REQBUFS with existing allocation — validate sizes */
		if (*nplanes != fmt->num_planes)
			return -EINVAL;
		if (sizes[0] < fmt->plane_fmt[0].sizeimage)
			return -EINVAL;
		return 0;
	}

	*nplanes = fmt->num_planes;
	sizes[0] = fmt->plane_fmt[0].sizeimage;
	return 0;
}

static int rkvenc_buf_prepare(struct vb2_buffer *vb)
{
	struct rkvenc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	const struct v4l2_pix_format_mplane *fmt;

	if (V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type))
		fmt = &ctx->src_fmt;
	else
		fmt = &ctx->dst_fmt;

	if (vb2_plane_size(vb, 0) < fmt->plane_fmt[0].sizeimage)
		return -EINVAL;

	/* For OUTPUT (raw) buffers, set bytesused to expected frame size */
	if (V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type))
		vb2_set_plane_payload(vb, 0, fmt->plane_fmt[0].sizeimage);

	return 0;
}

static void rkvenc_buf_queue(struct vb2_buffer *vb)
{
	struct rkvenc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, to_vb2_v4l2_buffer(vb));
}

static int rkvenc_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct rkvenc_ctx *ctx = vb2_get_drv_priv(vq);
	int ret;

	/*
	 * Only allocate internal buffers when the OUTPUT (source) queue
	 * starts streaming. The CAPTURE queue doesn't need internal bufs.
	 */
	if (!V4L2_TYPE_IS_OUTPUT(vq->type))
		return 0;

	ret = pm_runtime_resume_and_get(ctx->dev->dev);
	if (ret)
		return ret;

	/* Allocate reconstruction, ME, and MV buffers */
	ret = rkvenc_h264_alloc_aux_bufs(ctx);
	if (ret) {
		pm_runtime_put(ctx->dev->dev);
		return ret;
	}

	/* Reset encoding state */
	ctx->frames_since_idr = 0;
	ctx->params.frame_num = 0;
	ctx->params.idr_pic_id = 0;
	ctx->params.poc_lsb = 0;
	ctx->recon_idx = 0;

	return 0;
}

static void rkvenc_stop_streaming(struct vb2_queue *vq)
{
	struct rkvenc_ctx *ctx = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *buf;

	/* Return all queued buffers to userspace */
	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (!buf)
			break;
		v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
	}

	if (!V4L2_TYPE_IS_OUTPUT(vq->type))
		return;

	/* Free internal buffers */
	rkvenc_h264_free_aux_bufs(ctx);
	pm_runtime_put(ctx->dev->dev);
}

static const struct vb2_ops rkvenc_vb2_ops = {
	.queue_setup		= rkvenc_queue_setup,
	.buf_prepare		= rkvenc_buf_prepare,
	.buf_queue		= rkvenc_buf_queue,
	.start_streaming	= rkvenc_start_streaming,
	.stop_streaming		= rkvenc_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

/* ----------------------------------------------------------------
 * V4L2 m2m ops — job scheduling
 * ---------------------------------------------------------------- */

/*
 * device_run: called by the m2m framework when both source and destination
 * buffers are available. This is where we program the hardware and kick
 * the encode. The IRQ handler (rkvenc_drv.c) completes the job.
 */
static void rkvenc_device_run(void *priv)
{
	struct rkvenc_ctx *ctx = priv;
	struct rkvenc_dev *rkvenc = ctx->dev;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	int ret;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	if (!src_buf || !dst_buf) {
		dev_err(rkvenc->dev, "device_run: missing buffers\n");
		return;
	}

	/* Read controls into params struct */
	ctx->params.profile_idc = v4l2_ctrl_g_ctrl(ctx->ctrl_profile);
	ctx->params.level_idc = v4l2_ctrl_g_ctrl(ctx->ctrl_level);
	ctx->params.entropy_mode = v4l2_ctrl_g_ctrl(ctx->ctrl_entropy);
	ctx->params.transform_8x8 = v4l2_ctrl_g_ctrl(ctx->ctrl_8x8);
	ctx->params.qp_min = v4l2_ctrl_g_ctrl(ctx->ctrl_qp_min);
	ctx->params.qp_max = v4l2_ctrl_g_ctrl(ctx->ctrl_qp_max);
	ctx->gop_size = v4l2_ctrl_g_ctrl(ctx->ctrl_gop);

	/* Map profile control enum values to profile_idc */
	switch (ctx->params.profile_idc) {
	case V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE:
		ctx->params.profile_idc = RKVENC_H264_BASELINE;
		break;
	case V4L2_MPEG_VIDEO_H264_PROFILE_MAIN:
		ctx->params.profile_idc = RKVENC_H264_MAIN;
		break;
	case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH:
	default:
		ctx->params.profile_idc = RKVENC_H264_HIGH;
		break;
	}

	/* Determine frame type: IDR at start and every GOP frames */
	if (ctx->frames_since_idr == 0 ||
	    ctx->frames_since_idr >= ctx->gop_size ||
	    v4l2_ctrl_g_ctrl(ctx->ctrl_force_key)) {
		ctx->params.is_idr = true;
		ctx->params.slice_type = RKVENC_SLICE_TYPE_I;
		ctx->params.qp = v4l2_ctrl_g_ctrl(ctx->ctrl_qp_i);
		ctx->params.frame_num = 0;
		ctx->params.poc_lsb = 0;
		ctx->frames_since_idr = 0;
		ctx->params.idr_pic_id++;
	} else {
		ctx->params.is_idr = false;
		ctx->params.slice_type = RKVENC_SLICE_TYPE_P;
		ctx->params.qp = v4l2_ctrl_g_ctrl(ctx->ctrl_qp_p);
	}

	/* Clamp QP to configured range */
	ctx->params.qp = clamp_t(u8, ctx->params.qp,
				  ctx->params.qp_min, ctx->params.qp_max);

	/* Deblocking: enable by default */
	ctx->params.dbf_dis_idc = 0;
	ctx->params.dbf_alpha = 0;
	ctx->params.dbf_beta = 0;

	/* CABAC init IDC: 0 for I, 1 for P */
	ctx->params.cabac_init_idc = ctx->params.is_idr ? 0 : 1;

	/* Restrict features based on profile */
	if (ctx->params.profile_idc == RKVENC_H264_BASELINE) {
		ctx->params.entropy_mode = 0;	/* CAVLC only */
		ctx->params.transform_8x8 = false;
	}
	if (ctx->params.profile_idc != RKVENC_H264_HIGH)
		ctx->params.transform_8x8 = false;

	/* Program hardware and kick encode */
	ret = rkvenc_h264_encode_frame(ctx, src_buf, dst_buf);
	if (ret) {
		dev_err(rkvenc->dev, "failed to program encode: %d\n", ret);
		src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (src_buf)
			v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
		if (dst_buf)
			v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
		v4l2_m2m_job_finish(rkvenc->m2m_dev, ctx->fh.m2m_ctx);
		return;
	}

	/* Advance frame counters (IRQ handler will complete the job) */
	ctx->frames_since_idr++;
	ctx->params.frame_num++;
	ctx->params.poc_lsb += 2;
}

static const struct v4l2_m2m_ops rkvenc_m2m_ops = {
	.device_run = rkvenc_device_run,
};

/* ----------------------------------------------------------------
 * V4L2 controls
 * ---------------------------------------------------------------- */

static int rkvenc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	/* Controls are read in device_run, nothing to do here */
	return 0;
}

static const struct v4l2_ctrl_ops rkvenc_ctrl_ops = {
	.s_ctrl = rkvenc_s_ctrl,
};

static int rkvenc_init_controls(struct rkvenc_ctx *ctx)
{
	struct v4l2_ctrl_handler *hdl = &ctx->ctrl_handler;

	v4l2_ctrl_handler_init(hdl, 10);

	ctx->ctrl_profile = v4l2_ctrl_new_std_menu(hdl, &rkvenc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
		~((1 << V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
		  (1 << V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
		  (1 << V4L2_MPEG_VIDEO_H264_PROFILE_HIGH)),
		V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);

	ctx->ctrl_level = v4l2_ctrl_new_std_menu(hdl, &rkvenc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_LEVEL,
		V4L2_MPEG_VIDEO_H264_LEVEL_5_1,
		0,
		V4L2_MPEG_VIDEO_H264_LEVEL_4_0);

	ctx->ctrl_entropy = v4l2_ctrl_new_std_menu(hdl, &rkvenc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE,
		V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC,
		0,
		V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC);

	ctx->ctrl_8x8 = v4l2_ctrl_new_std(hdl, &rkvenc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM,
		0, 1, 1, 1);

	ctx->ctrl_gop = v4l2_ctrl_new_std(hdl, &rkvenc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_GOP_SIZE,
		1, RKVENC_GOP_MAX, 1, RKVENC_GOP_DEFAULT);

	ctx->ctrl_qp_i = v4l2_ctrl_new_std(hdl, &rkvenc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP,
		0, RKVENC_H264_QP_MAX, 1, RKVENC_H264_QP_DEFAULT);

	ctx->ctrl_qp_p = v4l2_ctrl_new_std(hdl, &rkvenc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP,
		0, RKVENC_H264_QP_MAX, 1, RKVENC_H264_QP_DEFAULT + 2);

	ctx->ctrl_qp_min = v4l2_ctrl_new_std(hdl, &rkvenc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_MIN_QP,
		0, RKVENC_H264_QP_MAX, 1, 10);

	ctx->ctrl_qp_max = v4l2_ctrl_new_std(hdl, &rkvenc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_MAX_QP,
		0, RKVENC_H264_QP_MAX, 1, RKVENC_H264_QP_MAX);

	ctx->ctrl_force_key = v4l2_ctrl_new_std(hdl, &rkvenc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME,
		0, 0, 0, 0);

	if (hdl->error)
		return hdl->error;

	ctx->fh.ctrl_handler = hdl;
	return 0;
}

/* ----------------------------------------------------------------
 * File operations
 * ---------------------------------------------------------------- */

static int rkvenc_open(struct file *file)
{
	struct rkvenc_dev *rkvenc = video_drvdata(file);
	struct rkvenc_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = rkvenc;

	v4l2_fh_init(&ctx->fh, &rkvenc->vdev);
	file->private_data = &ctx->fh;

	/* Set default formats */
	rkvenc_set_default_src_fmt(&ctx->src_fmt);
	rkvenc_set_default_dst_fmt(&ctx->dst_fmt);
	ctx->width = ctx->src_fmt.width;
	ctx->height = ctx->src_fmt.height;
	ctx->mb_width = ctx->width / RKVENC_MB_DIM;
	ctx->mb_height = ctx->height / RKVENC_MB_DIM;

	ctx->gop_size = RKVENC_GOP_DEFAULT;

	/* Initialize controls */
	ret = rkvenc_init_controls(ctx);
	if (ret)
		goto err_fh;

	/* Initialize m2m context */
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(rkvenc->m2m_dev, ctx,
					     &rkvenc_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto err_ctrl;
	}

	v4l2_fh_add(&ctx->fh);
	return 0;

err_ctrl:
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
err_fh:
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	return ret;
}

static int rkvenc_release(struct file *file)
{
	struct rkvenc_ctx *ctx = container_of(file->private_data,
					      struct rkvenc_ctx, fh);

	v4l2_fh_del(&ctx->fh);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	return 0;
}

static const struct v4l2_file_operations rkvenc_fops = {
	.owner		= THIS_MODULE,
	.open		= rkvenc_open,
	.release	= rkvenc_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

/* ----------------------------------------------------------------
 * Queue init callback for v4l2_m2m_ctx_init
 * ---------------------------------------------------------------- */

int rkvenc_queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct rkvenc_ctx *ctx = priv;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->ops = &rkvenc_vb2_ops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->buf_struct_size = sizeof(struct vb2_v4l2_buffer);
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->dev->lock;
	src_vq->dev = ctx->dev->dev;
	src_vq->min_queued_buffers = 1;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &rkvenc_vb2_ops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->buf_struct_size = sizeof(struct vb2_v4l2_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->dev->lock;
	dst_vq->dev = ctx->dev->dev;
	dst_vq->min_queued_buffers = 1;

	return vb2_queue_init(src_vq) ?: vb2_queue_init(dst_vq);
}

/* ----------------------------------------------------------------
 * V4L2 device registration
 * ---------------------------------------------------------------- */

int rkvenc_v4l2_register(struct rkvenc_dev *rkvenc)
{
	struct video_device *vdev = &rkvenc->vdev;
	int ret;

	ret = v4l2_device_register(rkvenc->dev, &rkvenc->v4l2_dev);
	if (ret)
		return ret;

	rkvenc->m2m_dev = v4l2_m2m_init(&rkvenc_m2m_ops);
	if (IS_ERR(rkvenc->m2m_dev)) {
		ret = PTR_ERR(rkvenc->m2m_dev);
		goto err_v4l2;
	}

	strscpy(vdev->name, RKVENC_NAME, sizeof(vdev->name));
	vdev->fops = &rkvenc_fops;
	vdev->ioctl_ops = &rkvenc_ioctl_ops;
	vdev->v4l2_dev = &rkvenc->v4l2_dev;
	vdev->release = video_device_release_empty;
	vdev->lock = &rkvenc->lock;
	vdev->vfl_dir = VFL_DIR_M2M;
	vdev->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;

	video_set_drvdata(vdev, rkvenc);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(rkvenc->dev, "failed to register video device: %d\n",
			ret);
		goto err_m2m;
	}

	dev_info(rkvenc->dev, "registered as /dev/video%d\n", vdev->num);
	return 0;

err_m2m:
	v4l2_m2m_release(rkvenc->m2m_dev);
err_v4l2:
	v4l2_device_unregister(&rkvenc->v4l2_dev);
	return ret;
}

void rkvenc_v4l2_unregister(struct rkvenc_dev *rkvenc)
{
	video_unregister_device(&rkvenc->vdev);
	v4l2_m2m_release(rkvenc->m2m_dev);
	v4l2_device_unregister(&rkvenc->v4l2_dev);
}
