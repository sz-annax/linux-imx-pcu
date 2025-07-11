/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Connected Cars A/S
 */
#ifndef _FXLS8962AF_H_
#define _FXLS8962AF_H_

struct regmap;
struct device;

enum {
	fxls8962af,
	fxls8964af,
	fxls8967af,
	fxls8974cf,
};

void fxls8962af_core_shutdown(struct device *dev);
int fxls8962af_core_probe(struct device *dev, struct regmap *regmap, int irq);

extern const struct dev_pm_ops fxls8962af_pm_ops;
extern const struct regmap_config fxls8962af_i2c_regmap_conf;
extern const struct regmap_config fxls8962af_spi_regmap_conf;

#endif				/* _FXLS8962AF_H_ */
