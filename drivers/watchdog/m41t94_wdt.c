// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/watchdog.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/bcd.h>

/* M41T94 Register Definitions */
#define M41T94_REG_100THS		0x00
#define M41T94_REG_SECONDS		0x01
#define M41T94_REG_MINUTES		0x02
#define M41T94_REG_HOURS		0x03
#define M41T94_REG_DAY			0x04
#define M41T94_REG_DATE			0x05
#define M41T94_REG_MONTH		0x06
#define M41T94_REG_YEAR			0x07
#define M41T94_REG_CONTROL		0x08
#define M41T94_REG_WATCHDOG		0x09
#define M41T94_REG_ALARM_MONTH	0x0A
#define M41T94_REG_ALARM_DATE	0x0B
#define M41T94_REG_ALARM_HOUR	0x0C
#define M41T94_REG_ALARM_MIN	0x0D
#define M41T94_REG_ALARM_SEC	0x0E
#define M41T94_REG_FLAGS		0x0F
#define M41T94_REG_HT			0x0C  /* Halt update register */
#define M41T94_REG_SQW			0x13
#define M41T94_REG_USER_RAM		0x14  /* Start of 44-byte user RAM */

/* Bit Definitions */
#define M41T94_BIT_HALT		0x40  /* Halt update bit */
#define M41T94_BIT_STOP		0x80  /* Stop oscillator bit */
#define M41T94_BIT_CB		0x40  /* Century bit */
#define M41T94_BIT_CEB		0x80  /* Century enable bit */
#define WDS_STEERING_BIT	(1 << 7)
#define WDF_FLAG_BIT		(1 << 7)
#define AF_FLAG_BIT			(1 << 6)
#define BL_FLAG_BIT			(1 << 4)

/* Watchdog Resolution Bits */
#define WD_RES_1_16_SEC		0x00
#define WD_RES_1_4_SEC		0x01
#define WD_RES_1_SEC		0x02
#define WD_RES_4_SEC		0x03

struct m41t94_wdt {
	struct spi_device *spi;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *wdi_gpio;
	struct watchdog_device wdd;
	u8 timeout_reg;
	struct miscdevice nvram_miscdev;
};

static int m41t94_write_register(struct spi_device *spi, u8 reg, u8 value)
{
	u8 buf[2] = { reg | 0x80, value }; /* Write command: set MSB */
	return spi_write(spi, buf, 2);
}

static int m41t94_read_register(struct spi_device *spi, u8 reg, u8 *value)
{
	int ret = spi_w8r8(spi, reg);
	if (ret < 0)
		return ret;
	
	*value = ret;
	return 0;
}

static int m41t94_init_oscillator(struct spi_device *spi)
{
	int ret;
	u8 reg_val;
	
	/* Read current seconds register */
	ret = spi_w8r8(spi, M41T94_REG_SECONDS);
	if (ret < 0)
		return ret;
	reg_val = ret;
	
	/* Set STOP bit to halt oscillator */
	ret = m41t94_write_register(spi, M41T94_REG_SECONDS, reg_val | M41T94_BIT_STOP);
	if (ret)
		return ret;
	
	/* Wait a moment */
	msleep(10);
	
	/* Clear STOP bit to restart oscillator */
	ret = m41t94_write_register(spi, M41T94_REG_SECONDS, reg_val & ~M41T94_BIT_STOP);
	if (ret)
		return ret;
	
	/* Wait for oscillator to stabilize */
	msleep(1000);
	
	return 0;
}

static int m41t94_wdt_start(struct watchdog_device *wdd)
{
	struct m41t94_wdt *wdt = watchdog_get_drvdata(wdd);
	int ret;

	// Enable chip if GPIO is provided
	if (wdt->enable_gpio)
		gpiod_set_value_cansleep(wdt->enable_gpio, 1);

	// Write watchdog register with stored timeout value
	ret = m41t94_write_register(wdt->spi, M41T94_REG_WATCHDOG, wdt->timeout_reg);
	if (ret)
		return ret;

	// Clear WDF flag by reading flags register
	u8 flags;
	ret = m41t94_read_register(wdt->spi, M41T94_REG_FLAGS, &flags);
	if (ret)
		return ret;

	return 0;
}

static int m41t94_wdt_stop(struct watchdog_device *wdd)
{
	struct m41t94_wdt *wdt = watchdog_get_drvdata(wdd);

	// Disable watchdog by writing 0 to watchdog register
	int ret = m41t94_write_register(wdt->spi, M41T94_REG_WATCHDOG, 0);
	if (ret)
		return ret;

	// Disable chip if GPIO is provided
	if (wdt->enable_gpio)
		gpiod_set_value_cansleep(wdt->enable_gpio, 0);

	return 0;
}

static int m41t94_wdt_ping(struct watchdog_device *wdd)
{
	struct m41t94_wdt *wdt = watchdog_get_drvdata(wdd);

	// Toggle WDI pin to feed the watchdog
	if (wdt->wdi_gpio) {
		gpiod_set_value_cansleep(wdt->wdi_gpio, 1);
		udelay(10);
		gpiod_set_value_cansleep(wdt->wdi_gpio, 0);
	} else {
		// Alternatively, write to watchdog register to reset it
		return m41t94_write_register(wdt->spi, M41T94_REG_WATCHDOG, wdt->timeout_reg);
	}

	return 0;
}

static int m41t94_wdt_set_timeout(struct watchdog_device *wdd, unsigned int timeout)
{
	struct m41t94_wdt *wdt = watchdog_get_drvdata(wdd);
	u8 rb, bmb;
	unsigned int resolution_ms;
	
	// 确定最佳分辨率和乘数组合
	if (timeout <= 31) {
		// 使用 1 秒分辨率
		rb = WD_RES_1_SEC;
		bmb = timeout;
	} else if (timeout <= 124) {
		// 使用 4 秒分辨率
		rb = WD_RES_4_SEC;
		bmb = (timeout + 3) / 4; // 向上取整到最近的 4 秒倍数
		if (bmb > 31) bmb = 31; // 确保不超过最大值
	} else {
		// 超过最大超时时间，设置为最大值
		rb = WD_RES_4_SEC;
		bmb = 31;
		timeout = 124; // 31 * 4
	}
	
	// Format watchdog register value
	// Bit 7: WDS (watchdog steering bit)
	// Bits 6-2: BMB4-BMB0 (multiplier bits)
	// Bits 1-0: RB1-RB0 (resolution bits)
	wdt->timeout_reg = WDS_STEERING_BIT | (bmb << 2) | rb;
	// 计算实际超时时间
	unsigned int actual_timeout = (rb == WD_RES_4_SEC) ? (bmb * 4) : bmb;
	wdd->timeout = actual_timeout;
	
	dev_info(&wdt->spi->dev, "Requested timeout: %u sec, Actual timeout: %u sec (reg: 0x%02x)\n",
             timeout, actual_timeout, wdt->timeout_reg);
	
	return 0;
}

/* NVRAM access functions */
static ssize_t m41t94_nvram_read(struct file *file, char __user *buf, 
				size_t count, loff_t *ppos)
{
	struct m41t94_wdt *wdt = container_of(file->private_data, 
					     struct m41t94_wdt, nvram_miscdev);
	u8 cmd;
	u8 *data;
	int ret;
	
	if (*ppos >= 44)
		return 0;
	
	if (*ppos + count > 44)
		count = 44 - *ppos;
	
	data = kmalloc(count, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	
	cmd = M41T94_REG_USER_RAM + *ppos;
	
	// Send read command and read data
	ret = spi_write_then_read(wdt->spi, &cmd, 1, data, count);
	if (ret)
		goto out;
	
	if (copy_to_user(buf, data, count)) {
		ret = -EFAULT;
		goto out;
	}
	
	*ppos += count;
	ret = count;
	
out:
	kfree(data);
	return ret;
}

static ssize_t m41t94_nvram_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct m41t94_wdt *wdt = container_of(file->private_data, 
					     struct m41t94_wdt, nvram_miscdev);
	u8 *tx_buf;
	int ret;
	
	if (*ppos >= 44)
		return -EFBIG;
	
	if (*ppos + count > 44)
		count = 44 - *ppos;
	
	tx_buf = kmalloc(count + 1, GFP_KERNEL);
	if (!tx_buf)
		return -ENOMEM;
	
	if (copy_from_user(&tx_buf[1], buf, count)) {
		ret = -EFAULT;
		goto out;
	}
	
	// Set write command with MSB set
	tx_buf[0] = (M41T94_REG_USER_RAM + *ppos) | 0x80;
	
	// Write data
	ret = spi_write(wdt->spi, tx_buf, count + 1);
	if (ret)
		goto out;
	
	*ppos += count;
	ret = count;
	
out:
	kfree(tx_buf);
	return ret;
}

static const struct file_operations m41t94_nvram_fops = {
	.owner = THIS_MODULE,
	.read = m41t94_nvram_read,
	.write = m41t94_nvram_write,
};

static const struct watchdog_info m41t94_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "M41T94 Watchdog",
};

static const struct watchdog_ops m41t94_wdt_ops = {
	.owner = THIS_MODULE,
	.start = m41t94_wdt_start,
	.stop = m41t94_wdt_stop,
	.ping = m41t94_wdt_ping,
	.set_timeout = m41t94_wdt_set_timeout,
};

static int m41t94_wdt_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct m41t94_wdt *wdt;
	u32 timeout = 60; // default 60 seconds
	int ret;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->spi = spi;

	// Parse device tree properties
	wdt->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(wdt->enable_gpio))
		return PTR_ERR(wdt->enable_gpio);

	wdt->wdi_gpio = devm_gpiod_get_optional(dev, "wdi", GPIOD_OUT_LOW);
	if (IS_ERR(wdt->wdi_gpio))
		return PTR_ERR(wdt->wdi_gpio);

	of_property_read_u32(dev->of_node, "timeout-sec", &timeout);

	// Initialize oscillator
	ret = m41t94_init_oscillator(spi);
	if (ret) {
		dev_err(dev, "Failed to initialize oscillator\n");
		return ret;
	}

	// Initialize watchdog device
	wdt->wdd.info = &m41t94_wdt_info;
	wdt->wdd.ops = &m41t94_wdt_ops;
	wdt->wdd.min_timeout = 1;
	wdt->wdd.max_timeout = 124; // 31 * 4 seconds
	wdt->wdd.parent = dev;

	watchdog_set_drvdata(&wdt->wdd, wdt);
	watchdog_init_timeout(&wdt->wdd, timeout, dev);

	ret = m41t94_wdt_set_timeout(&wdt->wdd, wdt->wdd.timeout);
	if (ret)
		return ret;

	// Register NVRAM misc device
	wdt->nvram_miscdev.minor = MISC_DYNAMIC_MINOR;
	wdt->nvram_miscdev.name = "m41t94-nvram";
	wdt->nvram_miscdev.fops = &m41t94_nvram_fops;
	wdt->nvram_miscdev.parent = dev;
	
	ret = misc_register(&wdt->nvram_miscdev);
	if (ret) {
		dev_err(dev, "Failed to register NVRAM misc device\n");
		return ret;
	}

	ret = devm_watchdog_register_device(dev, &wdt->wdd);
	if (ret) {
		misc_deregister(&wdt->nvram_miscdev);
		return ret;
	}

	dev_info(dev, "M41T94 watchdog and NVRAM driver initialized\n");
	return 0;
}

static void m41t94_wdt_remove(struct spi_device *spi)
{
	struct m41t94_wdt *wdt = spi_get_drvdata(spi);
	
	misc_deregister(&wdt->nvram_miscdev);
	
	return;
}

static const struct of_device_id m41t94_wdt_of_match[] = {
	{ .compatible = "st,m41t94-wdt" },
	{},
};
MODULE_DEVICE_TABLE(of, m41t94_wdt_of_match);

static struct spi_driver m41t94_wdt_driver = {
	.driver = {
		.name = "m41t94-watchdog",
		.of_match_table = m41t94_wdt_of_match,
	},
	.probe = m41t94_wdt_probe,
	.remove = m41t94_wdt_remove,
};
module_spi_driver(m41t94_wdt_driver);

MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("M41T94 Watchdog and NVRAM Driver");
MODULE_LICENSE("GPL");