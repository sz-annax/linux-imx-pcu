// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * COPYRIGHT 2015-2023 ARM Limited. All rights reserved.
 * COPYRIGHT 2023 NXP
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <linux/version.h>
#include <linux/of_address.h>
#include <mali_kbase.h>
#include <mali_kbase_config.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

#include "mali_kbase_config_platform.h"

#ifndef IMX_GPU_BLK_CTRL
#if KERNEL_VERSION(6, 12, 0) >= LINUX_VERSION_CODE
#define IMX_GPU_BLK_CTRL 1
#endif
#endif

static struct kbase_platform_config dummy_platform_config;

struct kbase_platform_config *kbase_get_platform_config(void)
{
	return &dummy_platform_config;
}

#ifndef CONFIG_OF
int kbase_platform_register(void)
{
	return 0;
}

void kbase_platform_unregister(void)
{
}
#endif

static int platform_init_func(struct kbase_device *kbdev)
{
	struct imx_platform_ctx *ictx;
	struct platform_device *pdev;

	pdev = to_platform_device(kbdev->dev);

	ictx = devm_kzalloc(kbdev->dev, sizeof(struct imx_platform_ctx), GFP_KERNEL);
	if (pdev->num_resources > 1) {
#ifdef IMX_GPU_BLK_CTRL
		ictx->reg_blk_ctrl = devm_platform_ioremap_resource_byname(pdev, "gpumix_blk_ctrl");
		if (IS_ERR_OR_NULL(ictx->reg_blk_ctrl))
			ictx->reg_blk_ctrl = devm_platform_ioremap_resource(pdev, 1);

		dev_dbg(kbdev->dev, "blk ctrl reg = %pK\n", ictx->reg_blk_ctrl);
	}

	if (pdev->num_resources > 2) {
#endif
		ictx->reg_tcm = devm_platform_ioremap_resource_byname(pdev, "tcm");
		if (!IS_ERR_OR_NULL(ictx->reg_tcm))
			dev_dbg(kbdev->dev, "wave dump reg = %pK\n", ictx->reg_tcm);
	}

	ictx->kbdev = kbdev;
	kbdev->platform_context = ictx;
	imx_waveform_start(kbdev);

	return 0;
}
static void platform_term_func(struct kbase_device *kbdev)
{
	struct imx_platform_ctx *ictx = kbdev->platform_context;

	devm_kfree(kbdev->dev, ictx);
}
struct kbase_platform_funcs_conf platform_funcs = {
	.platform_init_func = &platform_init_func,
	.platform_term_func = &platform_term_func,
};

int imx_waveform_start(struct kbase_device *kbdev)
{
	struct imx_platform_ctx *ictx = kbdev->platform_context;

	if (IS_ERR_OR_NULL(ictx->reg_tcm))
		return -EINVAL;

	if (ictx->dumpStarted == 1) {
		dev_info(kbdev->dev, "waveform dump already started\n");
		return 0;
	}
	ictx->dumpStarted = 1;

	dev_info(kbdev->dev, "start wave dump\n");
	writel(0xc1, ictx->reg_tcm);
	return 0;
}

int imx_waveform_stop(struct kbase_device *kbdev)
{
	struct imx_platform_ctx *ictx = kbdev->platform_context;

	if (IS_ERR_OR_NULL(ictx->reg_tcm))
		return -EINVAL;

	if (ictx->dumpStarted == 0) {
		dev_info(kbdev->dev, "waveform dump not start\n");
		return 0;
	}
	writel(0xc2, ictx->reg_tcm);
	dev_info(kbdev->dev, "stop wave dump\n");
	return 0;
}
