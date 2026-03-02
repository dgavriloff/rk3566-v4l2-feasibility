// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip RKVENC (VEPU540/541) V4L2 encoder driver — platform driver
 *
 * Handles probe, remove, runtime PM, and interrupt servicing.
 *
 * Copyright (c) 2026 rk3566-v4l2-feasibility contributors
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include "rkvenc.h"

/*
 * Hardware variant data.
 * The VEPU540 (RK3566/3568) and VEPU541 (RV1126) share the same register
 * layout with minor ME RAM sizing differences.
 */
static const struct rkvenc_variant rk3568_vepu540_variant = {
	.is_vepu540 = true,
	.me_ram_size_default = 0,	/* computed at runtime from resolution */
};

static const struct rkvenc_variant rv1126_vepu541_variant = {
	.is_vepu540 = false,
	.me_ram_size_default = 0,
};

/*
 * Interrupt handler.
 *
 * The VEPU540 signals completion (or error) via a single interrupt line.
 * We read the status register, acknowledge it, then delegate completion
 * to the V4L2 m2m framework.
 */
static irqreturn_t rkvenc_irq_handler(int irq, void *priv)
{
	struct rkvenc_dev *rkvenc = priv;
	u32 status;

	status = rkvenc_read(rkvenc, RKVENC_INT_STA);
	if (!status)
		return IRQ_NONE;

	/* Acknowledge all pending interrupts */
	rkvenc_write(rkvenc, RKVENC_INT_CLR, status);

	/*
	 * Find the running context via V4L2 m2m.
	 * The m2m framework guarantees only one job runs at a time.
	 */
	if (!rkvenc->m2m_dev)
		return IRQ_HANDLED;

	/*
	 * Get the current m2m context. If there's no active job,
	 * we may have received a stale interrupt during shutdown.
	 */
	{
		struct vb2_v4l2_buffer *src_buf, *dst_buf;
		struct rkvenc_ctx *ctx;
		enum vb2_buffer_state buf_state;
		u32 bytesused;

		ctx = v4l2_m2m_get_curr_priv(rkvenc->m2m_dev);
		if (!ctx)
			return IRQ_HANDLED;

		src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		if (!src_buf || !dst_buf) {
			dev_err(rkvenc->dev,
				"IRQ with missing buffers (src=%p dst=%p)\n",
				src_buf, dst_buf);
			return IRQ_HANDLED;
		}

		if (status & RKVENC_INT_ERROR_BITS) {
			dev_err(rkvenc->dev,
				"encode error: status=0x%08x%s%s%s%s%s\n",
				status,
				(status & RKVENC_INT_BSF_OVFLW) ? " BSF_OVFLW" : "",
				(status & RKVENC_INT_BRSP_OSTD) ? " BRSP_OSTD" : "",
				(status & RKVENC_INT_WBUS_ERR) ? " WBUS_ERR" : "",
				(status & RKVENC_INT_RBUS_ERR) ? " RBUS_ERR" : "",
				(status & RKVENC_INT_WDG) ? " WDG" : "");

			/*
			 * On bus errors or watchdog, attempt a force reset.
			 * On BSF overflow, the frame simply failed — no reset needed.
			 */
			if (status & (RKVENC_INT_WBUS_ERR | RKVENC_INT_RBUS_ERR |
				      RKVENC_INT_WDG))
				rkvenc_write(rkvenc, RKVENC_ENC_CLR,
					     RKVENC_CLR_FORCE);

			buf_state = VB2_BUF_STATE_ERROR;
			bytesused = 0;
		} else if (status & RKVENC_INT_ENC_DONE) {
			/* Success: read bitstream length from status register */
			bytesused = rkvenc_read(rkvenc, RKVENC_ST_BSL) &
				    RKVENC_ST_BS_LGTH;
			buf_state = VB2_BUF_STATE_DONE;
		} else {
			/* Unexpected status — treat as error */
			dev_warn(rkvenc->dev,
				 "unexpected IRQ status: 0x%08x\n", status);
			buf_state = VB2_BUF_STATE_ERROR;
			bytesused = 0;
		}

		/* Set output bitstream size */
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, bytesused);

		/* Copy timestamp from source to destination */
		dst_buf->vb2_buf.timestamp = src_buf->vb2_buf.timestamp;

		/* Mark key frame in buffer flags */
		if (ctx->params.is_idr)
			dst_buf->flags |= V4L2_BUF_FLAG_KEYFRAME;
		else
			dst_buf->flags |= V4L2_BUF_FLAG_PFRAME;

		v4l2_m2m_buf_done(src_buf, buf_state);
		v4l2_m2m_buf_done(dst_buf, buf_state);
		v4l2_m2m_job_finish(rkvenc->m2m_dev, ctx->fh.m2m_ctx);
	}

	return IRQ_HANDLED;
}

static int rkvenc_probe(struct platform_device *pdev)
{
	struct rkvenc_dev *rkvenc;
	struct resource *res;
	int irq, ret;

	rkvenc = devm_kzalloc(&pdev->dev, sizeof(*rkvenc), GFP_KERNEL);
	if (!rkvenc)
		return -ENOMEM;

	rkvenc->dev = &pdev->dev;
	mutex_init(&rkvenc->lock);
	platform_set_drvdata(pdev, rkvenc);

	/* Get variant data */
	rkvenc->variant = of_device_get_match_data(&pdev->dev);
	if (!rkvenc->variant)
		return -EINVAL;

	/* Map registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rkvenc->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rkvenc->regs))
		return PTR_ERR(rkvenc->regs);

	/* Get clocks */
	rkvenc->clks[RKVENC_CLK_ACLK].id = "aclk";
	rkvenc->clks[RKVENC_CLK_HCLK].id = "hclk";
	rkvenc->clks[RKVENC_CLK_CORE].id = "core";
	ret = devm_clk_bulk_get(&pdev->dev, RKVENC_NUM_CLKS, rkvenc->clks);
	if (ret) {
		dev_err(&pdev->dev, "failed to get clocks: %d\n", ret);
		return ret;
	}

	/* Get and request IRQ */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, rkvenc_irq_handler,
			       0, RKVENC_NAME, rkvenc);
	if (ret) {
		dev_err(&pdev->dev, "failed to request IRQ %d: %d\n", irq, ret);
		return ret;
	}

	/* Enable runtime PM */
	pm_runtime_set_autosuspend_delay(&pdev->dev, 100);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	/* Verify HW is accessible: read version register */
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to power on: %d\n", ret);
		goto err_pm_disable;
	}

	{
		u32 version = rkvenc_read(rkvenc, RKVENC_VERSION);

		dev_info(&pdev->dev,
			 "RKVENC version=0x%08x h264=%d h265=%d vepu=%s\n",
			 version,
			 !!(version & RKVENC_VER_H264_ENC),
			 !!(version & RKVENC_VER_H265_ENC),
			 rkvenc->variant->is_vepu540 ? "540" : "541");

		if (!(version & RKVENC_VER_H264_ENC)) {
			dev_err(&pdev->dev, "H.264 encoder not present\n");
			ret = -ENODEV;
			pm_runtime_put(&pdev->dev);
			goto err_pm_disable;
		}
	}

	pm_runtime_put(&pdev->dev);

	/* Register V4L2 device and video node */
	ret = rkvenc_v4l2_register(rkvenc);
	if (ret) {
		dev_err(&pdev->dev, "failed to register V4L2: %d\n", ret);
		goto err_pm_disable;
	}

	dev_info(&pdev->dev, "RKVENC encoder driver loaded\n");
	return 0;

err_pm_disable:
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static void rkvenc_remove(struct platform_device *pdev)
{
	struct rkvenc_dev *rkvenc = platform_get_drvdata(pdev);

	rkvenc_v4l2_unregister(rkvenc);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

static int rkvenc_runtime_suspend(struct device *dev)
{
	struct rkvenc_dev *rkvenc = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(RKVENC_NUM_CLKS, rkvenc->clks);
	return 0;
}

static int rkvenc_runtime_resume(struct device *dev)
{
	struct rkvenc_dev *rkvenc = dev_get_drvdata(dev);

	return clk_bulk_prepare_enable(RKVENC_NUM_CLKS, rkvenc->clks);
}

static const struct dev_pm_ops rkvenc_pm_ops = {
	SET_RUNTIME_PM_OPS(rkvenc_runtime_suspend, rkvenc_runtime_resume, NULL)
};

static const struct of_device_id rkvenc_dt_match[] = {
	{
		.compatible = "rockchip,rk3568-rkvenc",
		.data = &rk3568_vepu540_variant,
	},
	{
		.compatible = "rockchip,rv1126-rkvenc",
		.data = &rv1126_vepu541_variant,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rkvenc_dt_match);

static struct platform_driver rkvenc_driver = {
	.probe = rkvenc_probe,
	.remove = rkvenc_remove,
	.driver = {
		.name = RKVENC_NAME,
		.of_match_table = rkvenc_dt_match,
		.pm = &rkvenc_pm_ops,
	},
};
module_platform_driver(rkvenc_driver);

MODULE_DESCRIPTION("Rockchip RKVENC (VEPU540/541) V4L2 encoder driver");
MODULE_LICENSE("GPL");
