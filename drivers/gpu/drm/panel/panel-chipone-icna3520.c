// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 Gianni Spadoni <me@gio.blue>
// Generated with linux-mdss-dsi-panel-driver-generator from the Ayn Odin 3 device tree

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct icna3520 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	enum drm_panel_orientation orientation;
};

static inline struct icna3520 *to_icna3520(struct drm_panel *panel)
{
	return container_of(panel, struct icna3520, panel);
}

static void icna3520_reset(struct icna3520 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(20);
}

static int icna3520_on(struct icna3520 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9c, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfd, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x48, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9f, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3,
				     0x00, 0xd8, 0x00, 0x1c, 0x00, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0x0d, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0x1a, 0x71);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc1,
				     0x11, 0x00, 0x00, 0x89, 0x30, 0x80, 0x07,
				     0x80, 0x04, 0x38, 0x00, 0x0c, 0x02, 0x1c,
				     0x02, 0x1c, 0x02, 0x00, 0x02, 0x0e, 0x00,
				     0x20, 0x01, 0x1f, 0x00, 0x07, 0x00, 0x0c,
				     0x08, 0xbb, 0x08, 0x7a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2,
				     0x18, 0x00, 0x10, 0xf0, 0x03, 0x0c, 0x20,
				     0x00, 0x06, 0x0b, 0x0b, 0x33, 0x0e, 0x1c,
				     0x2a, 0x38, 0x46, 0x54, 0x62, 0x69, 0x70,
				     0x77, 0x79, 0x7b, 0x7d, 0x7e, 0x01, 0x02,
				     0x01, 0x00, 0x09, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3,
				     0x09, 0xbe, 0x19, 0xfc, 0x19, 0xfa, 0x19,
				     0xf8, 0x1a, 0x38, 0x1a, 0x78, 0x1a, 0xb6,
				     0x2a, 0xf6, 0x2b, 0x34, 0x2b, 0x74, 0x3b,
				     0x74, 0x6b, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9f, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x24);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x00);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	return dsi_ctx.accum_err;
}

static int icna3520_off(struct icna3520 *ctx)
{
	// FIXME: this gets called during shutdown and freezes causing a panic
	// skip this shutdown for now

	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	struct device *dev = &ctx->dsi->dev;
	dev_err(dev, "Not turning off display (fixme)\n");
	return 0;

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int icna3520_prepare(struct drm_panel *panel)
{
	struct icna3520 *ctx = to_icna3520(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	icna3520_reset(ctx);

	ret = icna3520_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		return ret;
	}

	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);

	ret = mipi_dsi_picture_parameter_set(ctx->dsi, &pps);
	if (ret < 0) {
		dev_err(panel->dev, "failed to transmit PPS: %d\n", ret);
		return ret;
	}
	
	ret = mipi_dsi_compression_mode(ctx->dsi, true);
	if (ret < 0) {
		dev_err(dev, "failed to enable compression mode: %d\n", ret);
		return ret;
	}

	msleep(28); 
	return 0;
}

static int icna3520_unprepare(struct drm_panel *panel)
{
	struct icna3520 *ctx = to_icna3520(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = icna3520_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);

	return 0;
}

static const struct drm_display_mode icna3520_mode = {
	.clock = (1080 + 24 + 1 + 24) * (1920 + 28 + 1 + 28) * 120 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 24,
	.hsync_end = 1080 + 24 + 1,
	.htotal = 1080 + 24 + 1 + 24,
	.vdisplay = 1920,
	.vsync_start = 1920 + 28,
	.vsync_end = 1920 + 28 + 1,
	.vtotal = 1920 + 28 + 1 + 28,
	.width_mm = 71,
	.height_mm = 130,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int icna3520_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	int ret = drm_connector_helper_get_modes_fixed(connector, &icna3520_mode);
	if (ret < 0)
		return ret;

	struct icna3520 *ctx = to_icna3520(panel);
	ret = drm_connector_set_panel_orientation(connector, ctx->orientation);
	if (ret < 0) {
		struct device *dev = &ctx->dsi->dev;
		dev_err(dev, "Failed to set panel orientation: %d\n", ret);
	}

	return 0;
}

static enum drm_panel_orientation icna3520_get_orientation(struct drm_panel *panel)
{
	struct icna3520 *ctx = to_icna3520(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs icna3520_panel_funcs = {
	.prepare = icna3520_prepare,
	.unprepare = icna3520_unprepare,
	.get_modes = icna3520_get_modes,
	.get_orientation = icna3520_get_orientation,
};

static int icna3520_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int icna3520_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops icna3520_bl_ops = {
	.update_status = icna3520_bl_update_status,
	.get_brightness = icna3520_bl_get_brightness,
};

static struct backlight_device *
icna3520_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 2600,
		.max_brightness = 3300, // 0xd55, so actually 3413
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &icna3520_bl_ops, &props);
}

static int icna3520_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct icna3520 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	drm_panel_init(&ctx->panel, dev, &icna3520_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	ret = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret < 0) {
		dev_err(dev, "%pFailed to get orientation %d\n", dev->of_node, ret);
		return ret;
	}

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET;
	dsi->mode_flags |= MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = icna3520_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &ctx->dsc;

	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 1;
	
	ctx->dsc.block_pred_enable = true;
	
	ctx->dsc.slice_height = 12;
	ctx->dsc.slice_width = 540;
	ctx->dsc.slice_count = 2;
	WARN_ON(1080 % ctx->dsc.slice_width);

	// TODO: this panel should be RGB101010
	ctx->dsc.bits_per_component = 8;
	// ctx->dsc.convert_rgb = true;
	ctx->dsc.bits_per_pixel = 8 << 4; /* 4 fractional bits */
	
	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	dev_dbg(dev, "Successfully probed panel\n");

	return 0;
}

static void icna3520_remove(struct mipi_dsi_device *dsi)
{
	struct icna3520 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id icna3520_of_match[] = {
	{ .compatible = "chipone,icna3520" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, icna3520_of_match);

static struct mipi_dsi_driver icna3520_driver = {
	.probe = icna3520_probe,
	.remove = icna3520_remove,
	.driver = {
		.name = "panel-icna3520",
		.of_match_table = icna3520_of_match,
	},
};
module_mipi_dsi_driver(icna3520_driver);

MODULE_AUTHOR("Gianni Spadoni <me@gio.blue>"); 
MODULE_DESCRIPTION("DRM driver for icna3520 amoled panel with DSC");
MODULE_LICENSE("GPL");
