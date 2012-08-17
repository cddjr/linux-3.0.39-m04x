#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ctype.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/lcd.h>
#include <linux/backlight.h>

#include <video/mipi_display.h>
#include <plat/mipi_dsim.h>
#include "ls044k3sx01.h"

#define CHECK_PANEL_RET(func) do {\
	int ret = func;\
	if (ret) {\
		pr_err("#LCD WRITE ERROR: line %d\n", __LINE__);\
		return ret;}\
} while(0);

static struct ls044k3sx01_info *g_lcd_info;
static int write_to_lcd(struct ls044k3sx01_info *lcd,
		const struct ls044k3sx01_param *param)
{
	int i = 0, ret = 0;

	do {
		ret = param[i].size ?
			write_data(lcd, param[i].param, param[i].size, BTA_TIMEOUT):
			write_cmd(lcd, param[i].param[0], param[i].param[1], BTA_TIMEOUT);
		if (param[i].delay)
			msleep(param[i].delay);
	} while (!ret && param[++i].size != -1);

	return ret;
}

static int lcd_panel_init_code(struct ls044k3sx01_info *lcd)
{
	return write_to_lcd(lcd, ls044k3sx01_init_seq);
}

static int lcd_panel_sleep_in(struct ls044k3sx01_info *lcd)
{
	return write_to_lcd(lcd, ls044k3sx01_slpin_seq);
}

static int lcd_panel_sleep_out(struct ls044k3sx01_info *lcd)
{
	return write_to_lcd(lcd, ls044k3sx01_slpout_seq);
}

static int lcd_panel_display_on(struct ls044k3sx01_info *lcd)
{
	return write_to_lcd(lcd, ls044k3sx01_dspon_seq); 
}

static int lcd_panel_display_off(struct ls044k3sx01_info *lcd)
{
	return write_to_lcd(lcd, ls044k3sx01_dspoff_seq);
}
#ifdef LCD_TEST
static int lcd_panel_hsync_out(struct ls044k3sx01_info *lcd)
{
	return write_to_lcd(lcd, ls044k3sx01_hsync_out_seq);
}
static int lcd_panel_vsync_out(struct ls044k3sx01_info *lcd)
{
	return write_to_lcd(lcd, ls044k3sx01_vsync_out_seq);
}

static int lcd_panel_set_brightness(struct ls044k3sx01_info *lcd, int brt)
{
	const struct ls044k3sx01_param ls044k3sx01_brightness[] = {
		LCD_PARAM_DCS_CMD(0, 0x51, brt),
		LCD_PARAM_DEF_END,
	};

	return write_to_lcd(lcd, ls044k3sx01_brightness);
}
#endif
static int lcd_panel_cabc_seq(struct ls044k3sx01_info *lcd, int enable)
{
	if (enable)
		return write_to_lcd(lcd, ls044k3sx01_cabc_seq);
	else
		return write_to_lcd(lcd, ls044k3sx01_cabc_seq_off);
}
static int lcd_init(struct mipi_dsim_lcd_device *mipi_dev)
{
	struct ls044k3sx01_info *lcd = dev_get_drvdata(&mipi_dev->dev);
	
	CHECK_PANEL_RET(lcd_panel_sleep_out(lcd));
	CHECK_PANEL_RET(lcd_panel_init_code(lcd));
	CHECK_PANEL_RET(lcd_panel_display_on(lcd));
	CHECK_PANEL_RET(lcd_panel_cabc_seq(g_lcd_info ,true));

	return 0;
}

static int lcd_remove(struct mipi_dsim_lcd_device *mipi_dev)
{
	struct ls044k3sx01_info *lcd = NULL;
	struct lcd_platform_data	*ddi_pd;

	lcd = (struct ls044k3sx01_info *)dev_get_drvdata(&mipi_dev->dev);
	ddi_pd = lcd->ddi_pd;

	if (ddi_pd->power_on)
		ddi_pd->power_on(lcd->ld, false);

	kfree(lcd);

	return dev_set_drvdata(&mipi_dev->dev, NULL);
}
int lcd_cabc_opr(unsigned int brightness, unsigned int enable)
{
	CHECK_PANEL_RET(lcd_panel_cabc_seq(g_lcd_info , enable));
	return 0;
}
EXPORT_SYMBOL_GPL(lcd_cabc_opr);

#ifdef LCD_TEST
int lcd_cabc_set_brightness(unsigned int brightness)
{
	CHECK_PANEL_RET(lcd_panel_set_brightness(g_lcd_info , brightness));
	return 0;
}
EXPORT_SYMBOL_GPL(lcd_cabc_set_brightness);

static ssize_t lcd_sync_enable(struct device *dev, struct device_attribute
					*attr, const char *buf, size_t size)
{
	struct ls044k3sx01_info *lcd = dev_get_drvdata(dev);
	int num;

	sscanf(buf, "%d", &num);
	pr_info("cabc %s\n", num == 0 ? "vsync out" : "hsync out");

	if (num == 0) {
		CHECK_PANEL_RET(lcd_panel_vsync_out(lcd));
	} else {
		CHECK_PANEL_RET(lcd_panel_hsync_out(lcd));
	}

	return sizeof(num);
}
static DEVICE_ATTR(sync, 0644, NULL, lcd_sync_enable);
#endif

static int lcd_probe(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct ls044k3sx01_info *lcd = NULL;
	int err = 0;

	lcd = kzalloc(sizeof(struct ls044k3sx01_info), GFP_KERNEL);
	if (!lcd) {
		dev_err(&dsim_dev->dev, "failed to allocate ls044k3sx01 structure.\n");
		return -ENOMEM;
	}

	lcd->dsim_dev = dsim_dev;
	lcd->ddi_pd = (struct lcd_platform_data *)dsim_dev->platform_data;

	if (IS_ERR_OR_NULL(lcd->ddi_pd))
		pr_err("%s: ddi_pd is NULL\n", __func__);

	lcd->dev = &dsim_dev->dev;

	dev_set_drvdata(&dsim_dev->dev, lcd);

	lcd->ld = lcd_device_register("ls044k3sx01", lcd->dev, lcd,
				NULL);
	if (IS_ERR(lcd->ld)) {
		dev_err(lcd->dev, "failed to register lcd ops.\n");
		goto err_dev_register;
	}

	lcd->state = LCD_DISPLAY_POWER_OFF;

#ifdef LCD_TEST
	err = device_create_file(lcd->dev, &dev_attr_sync);
	if (err < 0) {
		dev_err(lcd->dev, "Failed to create attr file cabc %d!\n", err);
	}
#endif

	g_lcd_info = lcd;

	pr_info("ls044k3sx01_probe finish\n");
	return err;

err_dev_register:
	kfree(lcd);
	return -1;
}
static void lcd_shutdown(struct mipi_dsim_lcd_device *mipi_dev)
{
	struct ls044k3sx01_info *lcd = dev_get_drvdata(&mipi_dev->dev);

	lcd->state = LCD_DISPLAY_POWER_OFF;

	/* lcd power off */
	if (lcd->ddi_pd->power_on)
		lcd->ddi_pd->power_on(lcd->ld, false);
}

static int lcd_suspend(struct mipi_dsim_lcd_device *mipi_dev)
{
	struct ls044k3sx01_info *lcd = dev_get_drvdata(&mipi_dev->dev);

	CHECK_PANEL_RET(lcd_panel_display_off(lcd));
	CHECK_PANEL_RET(lcd_panel_sleep_in(lcd));

	lcd->state = LCD_DISPLAY_SLEEP_IN;

	return 0;
}

static int lcd_resume(struct mipi_dsim_lcd_device *mipi_dev)
{
	struct ls044k3sx01_info *lcd = dev_get_drvdata(&mipi_dev->dev);

	/* lcd power on */
	if (lcd->ddi_pd->power_on)
		lcd->ddi_pd->power_on(lcd->ld, true);

	pr_debug("%s: lcd->state = %d\n", __func__, lcd->state);

	return 0;
}

static struct mipi_dsim_lcd_driver ls044k3sx01_mipi_driver = {
	.name	= "ls044k3sx01",
	.id		= -1,
	.probe	= lcd_probe,
	.init_lcd	= lcd_init,
	.suspend	= lcd_suspend,
	.resume	= lcd_resume,
	.shutdown = lcd_shutdown,
	.remove	= lcd_remove,
};

static int __init ls044k3sx01_init(void)
{
	return s5p_mipi_dsi_register_lcd_driver(&ls044k3sx01_mipi_driver);
}

subsys_initcall(ls044k3sx01_init);
