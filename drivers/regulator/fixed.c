// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * fixed.c
 *
 * Copyright 2008 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * Copyright (c) 2009 Nokia Corporation
 * Roger Quadros <ext-roger.quadros@nokia.com>
 *
 * This is useful for systems with mixed controllable and
 * non-controllable regulators, as well as for allowing testing on
 * systems with no controllable regulators.
 */

#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/reboot.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/fixed.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/clk.h>
#include <linux/pinctrl/consumer.h>

/* Default time in millisecond to wait for emergency shutdown */
#define FV_DEF_EMERG_SHUTDWN_TMO	10

struct fixed_voltage_data {
	struct regulator_desc desc;
	struct regulator_dev *dev;

	struct clk *enable_clock;
	unsigned int enable_counter;
	int performance_state;
};

struct fixed_dev_type {
	bool has_enable_clock;
	bool has_performance_state;
};

static int reg_clock_enable(struct regulator_dev *rdev)
{
	struct fixed_voltage_data *priv = rdev_get_drvdata(rdev);
	int ret = 0;

	ret = clk_prepare_enable(priv->enable_clock);
	if (ret)
		return ret;

	priv->enable_counter++;

	return ret;
}

static int reg_clock_disable(struct regulator_dev *rdev)
{
	struct fixed_voltage_data *priv = rdev_get_drvdata(rdev);

	clk_disable_unprepare(priv->enable_clock);
	priv->enable_counter--;

	return 0;
}

static int reg_domain_enable(struct regulator_dev *rdev)
{
	struct fixed_voltage_data *priv = rdev_get_drvdata(rdev);
	struct device *dev = rdev->dev.parent;
	int ret;

	ret = dev_pm_genpd_set_performance_state(dev, priv->performance_state);
	if (ret)
		return ret;

	priv->enable_counter++;

	return ret;
}

static int reg_domain_disable(struct regulator_dev *rdev)
{
	struct fixed_voltage_data *priv = rdev_get_drvdata(rdev);
	struct device *dev = rdev->dev.parent;
	int ret;

	ret = dev_pm_genpd_set_performance_state(dev, 0);
	if (ret)
		return ret;

	priv->enable_counter--;

	return 0;
}

static int reg_is_enabled(struct regulator_dev *rdev)
{
	struct fixed_voltage_data *priv = rdev_get_drvdata(rdev);

	return priv->enable_counter > 0;
}

static irqreturn_t reg_fixed_under_voltage_irq_handler(int irq, void *data)
{
	struct fixed_voltage_data *priv = data;
	struct regulator_dev *rdev = priv->dev;

	regulator_notifier_call_chain(rdev, REGULATOR_EVENT_UNDER_VOLTAGE,
				      NULL);

	return IRQ_HANDLED;
}

/**
 * reg_fixed_get_irqs - Get and register the optional IRQ for fixed voltage
 *                      regulator.
 * @dev: Pointer to the device structure.
 * @priv: Pointer to fixed_voltage_data structure containing private data.
 *
 * This function tries to get the IRQ from the device firmware node.
 * If it's an optional IRQ and not found, it returns 0.
 * Otherwise, it attempts to request the threaded IRQ.
 *
 * Return: 0 on success, or a negative error number on failure.
 */
static int reg_fixed_get_irqs(struct device *dev,
			      struct fixed_voltage_data *priv)
{
	int ret;

	ret = fwnode_irq_get(dev_fwnode(dev), 0);
	/* This is optional IRQ. If not found we will get -EINVAL */
	if (ret == -EINVAL)
		return 0;
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get IRQ\n");

	ret = devm_request_threaded_irq(dev, ret, NULL,
					reg_fixed_under_voltage_irq_handler,
					IRQF_ONESHOT, "under-voltage", priv);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request IRQ\n");

	return 0;
}

/**
 * of_get_fixed_voltage_config - extract fixed_voltage_config structure info
 * @dev: device requesting for fixed_voltage_config
 * @desc: regulator description
 *
 * Populates fixed_voltage_config structure by extracting data from device
 * tree node.
 *
 * Return: Pointer to a populated &struct fixed_voltage_config or %NULL if
 *	   memory allocation fails.
 */
static struct fixed_voltage_config *
of_get_fixed_voltage_config(struct device *dev,
			    const struct regulator_desc *desc)
{
	struct fixed_voltage_config *config;
	struct device_node *np = dev->of_node;
	struct regulator_init_data *init_data;

	config = devm_kzalloc(dev, sizeof(struct fixed_voltage_config),
								 GFP_KERNEL);
	if (!config)
		return ERR_PTR(-ENOMEM);

	config->init_data = of_get_regulator_init_data(dev, dev->of_node, desc);
	if (!config->init_data)
		return ERR_PTR(-EINVAL);

	init_data = config->init_data;
	init_data->constraints.apply_uV = 0;

	config->supply_name = init_data->constraints.name;
	if (init_data->constraints.min_uV == init_data->constraints.max_uV) {
		config->microvolts = init_data->constraints.min_uV;
	} else {
		dev_err(dev,
			 "Fixed regulator specified with variable voltages\n");
		return ERR_PTR(-EINVAL);
	}

	if (init_data->constraints.boot_on)
		config->enabled_at_boot = true;

	of_property_read_u32(np, "startup-delay-us", &config->startup_delay);
	of_property_read_u32(np, "off-on-delay-us", &config->off_on_delay);

	if (of_property_present(np, "vin-supply"))
		config->input_supply = "vin";

	return config;
}

static const struct regulator_ops fixed_voltage_ops = {
};

static const struct regulator_ops fixed_voltage_clkenabled_ops = {
	.enable = reg_clock_enable,
	.disable = reg_clock_disable,
	.is_enabled = reg_is_enabled,
};

static const struct regulator_ops fixed_voltage_domain_ops = {
	.enable = reg_domain_enable,
	.disable = reg_domain_disable,
	.is_enabled = reg_is_enabled,
};

static int reg_fixed_voltage_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fixed_voltage_config *config;
	struct fixed_voltage_data *drvdata;
	const struct fixed_dev_type *drvtype = of_device_get_match_data(dev);
	struct regulator_config cfg = { };
	enum gpiod_flags gflags;
	int ret;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct fixed_voltage_data),
			       GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	if (pdev->dev.of_node) {
		config = of_get_fixed_voltage_config(&pdev->dev,
						     &drvdata->desc);
		if (IS_ERR(config))
			return PTR_ERR(config);
	} else {
		config = dev_get_platdata(&pdev->dev);
	}

	if (!config)
		return -ENOMEM;

	drvdata->desc.name = devm_kstrdup(&pdev->dev,
					  config->supply_name,
					  GFP_KERNEL);
	if (drvdata->desc.name == NULL) {
		dev_err(&pdev->dev, "Failed to allocate supply name\n");
		return -ENOMEM;
	}
	drvdata->desc.type = REGULATOR_VOLTAGE;
	drvdata->desc.owner = THIS_MODULE;

	if (drvtype && drvtype->has_enable_clock) {
		drvdata->desc.ops = &fixed_voltage_clkenabled_ops;

		drvdata->enable_clock = devm_clk_get(dev, NULL);
		if (IS_ERR(drvdata->enable_clock)) {
			dev_err(dev, "Can't get enable-clock from devicetree\n");
			return PTR_ERR(drvdata->enable_clock);
		}
	} else if (drvtype && drvtype->has_performance_state) {
		drvdata->desc.ops = &fixed_voltage_domain_ops;

		drvdata->performance_state = of_get_required_opp_performance_state(dev->of_node, 0);
		if (drvdata->performance_state < 0) {
			dev_err(dev, "Can't get performance state from devicetree\n");
			return drvdata->performance_state;
		}
	} else {
		drvdata->desc.ops = &fixed_voltage_ops;
	}

	drvdata->desc.enable_time = config->startup_delay;
	drvdata->desc.off_on_delay = config->off_on_delay;

	if (config->input_supply) {
		drvdata->desc.supply_name = devm_kstrdup(&pdev->dev,
					    config->input_supply,
					    GFP_KERNEL);
		if (!drvdata->desc.supply_name)
			return -ENOMEM;
	}

	if (config->microvolts)
		drvdata->desc.n_voltages = 1;

	drvdata->desc.fixed_uV = config->microvolts;

	/*
	 * The signal will be inverted by the GPIO core if flagged so in the
	 * descriptor.
	 */
	if (config->enabled_at_boot)
		gflags = GPIOD_OUT_HIGH;
	else
		gflags = GPIOD_OUT_LOW;

	/*
	 * Some fixed regulators share the enable line between two
	 * regulators which makes it necessary to get a handle on the
	 * same descriptor for two different consumers. This will get
	 * the GPIO descriptor, but only the first call will initialize
	 * it so any flags such as inversion or open drain will only
	 * be set up by the first caller and assumed identical on the
	 * next caller.
	 *
	 * FIXME: find a better way to deal with this.
	 */
	gflags |= GPIOD_FLAGS_BIT_NONEXCLUSIVE;

	/*
	 * Do not use devm* here: the regulator core takes over the
	 * lifecycle management of the GPIO descriptor.
	 */
	cfg.ena_gpiod = gpiod_get_optional(&pdev->dev, NULL, gflags);
	if (IS_ERR(cfg.ena_gpiod))
		return dev_err_probe(&pdev->dev, PTR_ERR(cfg.ena_gpiod),
				     "can't get GPIO\n");

	cfg.dev = &pdev->dev;
	cfg.init_data = config->init_data;
	cfg.driver_data = drvdata;
	cfg.of_node = pdev->dev.of_node;

	drvdata->dev = devm_regulator_register(&pdev->dev, &drvdata->desc,
					       &cfg);
	if (IS_ERR(drvdata->dev)) {
		ret = dev_err_probe(&pdev->dev, PTR_ERR(drvdata->dev),
				    "Failed to register regulator: %ld\n",
				    PTR_ERR(drvdata->dev));
		return ret;
	}

	platform_set_drvdata(pdev, drvdata);

	dev_dbg(&pdev->dev, "%s supplying %duV\n", drvdata->desc.name,
		drvdata->desc.fixed_uV);

	ret = reg_fixed_get_irqs(dev, drvdata);
	if (ret)
		return ret;

	return 0;
}

#if defined(CONFIG_OF)
static const struct fixed_dev_type fixed_voltage_data = {
	.has_enable_clock = false,
};

static const struct fixed_dev_type fixed_clkenable_data = {
	.has_enable_clock = true,
};

static const struct fixed_dev_type fixed_domain_data = {
	.has_performance_state = true,
};

static const struct of_device_id fixed_of_match[] = {
	{
		.compatible = "regulator-fixed",
		.data = &fixed_voltage_data,
	},
	{
		.compatible = "regulator-fixed-clock",
		.data = &fixed_clkenable_data,
	},
	{
		.compatible = "regulator-fixed-domain",
		.data = &fixed_domain_data,
	},
	{
	},
};
MODULE_DEVICE_TABLE(of, fixed_of_match);
#endif

#ifdef CONFIG_PM_SLEEP
static int reg_fixed_voltage_suspend(struct device *dev)
{
	pinctrl_pm_select_sleep_state(dev);

	return 0;
}
static int reg_fixed_voltage_resume(struct device *dev)
{
	pinctrl_pm_select_default_state(dev);

	return 0;
}
#endif

static const struct dev_pm_ops reg_fixed_voltage_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(reg_fixed_voltage_suspend,
		reg_fixed_voltage_resume)
};

static struct platform_driver regulator_fixed_voltage_driver = {
	.probe		= reg_fixed_voltage_probe,
	.driver		= {
		.name		= "reg-fixed-voltage",
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(fixed_of_match),
		.pm = &reg_fixed_voltage_pm_ops,
	},
};

static int __init regulator_fixed_voltage_init(void)
{
	return platform_driver_register(&regulator_fixed_voltage_driver);
}
subsys_initcall(regulator_fixed_voltage_init);

static void __exit regulator_fixed_voltage_exit(void)
{
	platform_driver_unregister(&regulator_fixed_voltage_driver);
}
module_exit(regulator_fixed_voltage_exit);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("Fixed voltage regulator");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:reg-fixed-voltage");
