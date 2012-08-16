/*
 * mx2_camera.c - camera driver helper for m040 board
 *
 * Copyright (C) 2012 Meizu Technology Co.Ltd, Zhuhai, China
 * Author:  Jerry Mo   <jerrymo@meizu.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/lcd.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/videodev2.h>

#include <asm/mach-types.h>

#include <mach/map.h>
#include <mach/gpio-m040.h>
#include <mach/i2c-m040.h>

#include <plat/fimc.h>
#include <plat/devs.h>
#include <plat/pd.h>
#include <plat/csis.h>
#include <plat/iic.h>

#ifdef CONFIG_VIDEO_M6MO
#include <media/m6mo.h>
#endif

#ifdef CONFIG_VIDEO_FIMC
#ifdef CONFIG_VIDEO_M6MO

/* 
  * because front and back sensor are bundled
  * to one ISP, so we set their both i2c info to m6mo
  * actually it's useless
*/
static struct i2c_board_info ov9724_i2c_info = {
	I2C_BOARD_INFO("m6mo", 0x1f),
};

/* front camera sensor */
static struct s3c_platform_camera ov9724 = {
	.id = CAMERA_CSI_C,
	.type = CAM_TYPE_MIPI,
	.fmt	= MIPI_CSI_YCBCR422_8BIT,
	.order422	= CAM_ORDER422_8BIT_CBYCRY,
	.pixelformat = V4L2_PIX_FMT_UYVY,

	.i2c_busnum = 5,
	.info	= &ov9724_i2c_info,

	.clk_name = "sclk_cam0",
	.srclk_name = "xusbxti",
	.clk_rate = 24000000,
	
	.line_length = 1920,
	.width = 640,
	.height = 480,
	.window = {
		.left	= 0,
		.top	= 0,
		.width = 640,
		.height = 480,
	},

	.mipi_lanes = 2,
	.mipi_settle = 12,
	.mipi_align = 32,

	.inv_pclk	= 0,
	.inv_vsync = 1,
	.inv_href	= 0,
	.inv_hsync = 0,

	.initialized = 0,
};

static struct i2c_board_info imx175_i2c_info = {
	I2C_BOARD_INFO("m6mo", 0x1f),
};

/* back camera sensor */
static struct s3c_platform_camera imx175 = {
	.id = CAMERA_CSI_C,
	.type = CAM_TYPE_MIPI,
	.fmt	= MIPI_CSI_YCBCR422_8BIT,
	.order422	= CAM_ORDER422_8BIT_CBYCRY,
	.pixelformat = V4L2_PIX_FMT_UYVY,

	.i2c_busnum = 5,
	.info	= &imx175_i2c_info,

	.clk_name = "sclk_cam0",
	.srclk_name = "xusbxti",
	.clk_rate = 24000000,
	
	.line_length = 1920,
	.width = 960,
	.height = 720,
	.window = {
		.left	= 0,
		.top	= 0,
		.width = 960,
		.height = 720,
	},

	.mipi_lanes = 2,
	.mipi_settle = 12,
	.mipi_align = 32,

	.inv_pclk	= 0,
	.inv_vsync = 1,
	.inv_href	= 0,
	.inv_hsync = 0,

	.initialized = 0,
};
#endif

/* fimc platform data setting */
static struct s3c_platform_fimc fimc_plat = {
	.camera = {
#ifdef CONFIG_VIDEO_M6MO
		&imx175,
		&ov9724,
#endif
	},
	.hw_ver = 0x51,
};
#endif  /* CONFIG_VIDEO_FIMC */

#ifdef CONFIG_VIDEO_M6MO
static int m6mo_init_gpio(struct device *dev)
{
	int ret = 0;

	/*m6mo reset pin shoud be initialized to low*/
	ret = gpio_request_one(M040_ISP_RST, GPIOF_OUT_INIT_LOW, "M6MO_RESET");
	if (ret) {
		pr_err("%s():gpio_request failed\n", __func__);
		return -EINVAL;
	}
	
	/* 
	  * YCVZ pin is used to configure YUV bus, if it's not used, it's better to set high when power on, 
	  * and set low when power off
	*/
	ret = gpio_request_one(M040_ISP_YCVZ, GPIOF_OUT_INIT_LOW, "M6MO_YCVZ");
	if (ret) {
		pr_err("%s():gpio_request failed\n", __func__);
		gpio_free(M040_ISP_RST);
	}

	return ret;
}

static int m6mo_init_clock(struct device *dev)
{
	struct clk *srclk, *clk;
	int ret = 0;
	
	/* source clk for MCLK*/
	srclk = clk_get(dev, "xusbxti");
	if (IS_ERR(srclk)) {
		dev_err(dev, "failed to get srclk source\n");
		return -EINVAL;
	}

	/* mclk */
	clk = clk_get(dev, "sclk_cam0");
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get mclk source\n");
		ret = -EINVAL;
		goto exit_clkget_cam;
	}

	if (clk_set_parent(clk, srclk)) {
		dev_err(dev, "unable to set parent.\n");
		ret = -EINVAL;
		goto exit_clkset_parent;
	}

	clk_set_rate(clk, 24000000);
	
exit_clkset_parent:
	clk_put(clk);
exit_clkget_cam:
	clk_put(srclk);

	return ret;
}

static int m6mo_set_power(int cam_index, bool enable)
{
	struct regulator_bulk_data supplies[5];
	int num_consumers, ret;

	pr_info("%s():camera index = %d, enable = %d\n", __FUNCTION__, cam_index, enable);

	supplies[0].supply = "cam_isp_1.8v";
	supplies[1].supply = "cam_isp_1.2v";
	if (cam_index == 0) {  /* IMX175 */
		supplies[2].supply = "cam_back_1.2v";
		supplies[3].supply = "cam_back_2.7v";
		supplies[4].supply = "cam_back_af_2.7v";
		num_consumers = 5;
	} else if (cam_index == 1) {  /* OV9724 */
		supplies[2].supply = "cam_front_2.8v";
		supplies[3].supply = "cam_front_1.5v";
		num_consumers = 4;
	} else {
		pr_err("%s:wrong camera index\n", __func__);
		return -EINVAL;
	}
	
	ret = regulator_bulk_get(NULL, num_consumers, supplies);
	if (ret) {
		pr_err("%s():regulator_bulk_get failed\n", __func__);
		return ret;
	}

	if (enable) {
		ret = regulator_bulk_enable(num_consumers, supplies);
		gpio_set_value(M040_ISP_RST, 1);
		gpio_set_value(M040_ISP_YCVZ, 1);
	} else { 
		gpio_set_value(M040_ISP_RST, 0);
		ret = regulator_bulk_disable(num_consumers, supplies);
		gpio_set_value(M040_ISP_YCVZ, 0);
	}
	if (ret) {
		pr_err("%s():regulator_bulk_%sable failed\n", __func__, enable?"en":"dis");
		goto exit_regulator;
	}

	msleep(5);

exit_regulator:
	regulator_bulk_free(num_consumers, supplies);
	
	return 0;
}


static void m6mo_reset(void)
{
	gpio_set_value(M040_ISP_RST, 0);
	msleep(5);
	gpio_set_value(M040_ISP_RST, 1);
	msleep(5);
}

static int m6mo_clock_enable(struct device *dev, bool enable)
{
	struct clk *fimc_clk, *clk;
	int ret = 0;

	/* be able to handle clock on/off only with this clock */
	fimc_clk = clk_get(&s3c_device_fimc0.dev, "fimc");
	if (IS_ERR(fimc_clk)) {
		dev_err(dev, "failed to get interface clock\n");
		return -EINVAL;
	}

	/* mclk */
	clk = clk_get(dev, "sclk_cam0");
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get mclk source\n");
		ret = -EINVAL;
		goto exit_clkget_cam;
	}

	if (enable) {
		clk_enable(fimc_clk);
		clk_enable(clk);
	} else {
		clk_disable(clk);
		clk_disable(fimc_clk);
	}

	clk_put(clk);

exit_clkget_cam:
	clk_put(fimc_clk);
	
	return ret;
}

static struct m6mo_platform_data m6mo_plat = {
	.default_width = 640,
	.default_height = 480,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000,
	.is_mipi = 1,

	.init_gpio = m6mo_init_gpio,
	.init_clock = m6mo_init_clock,
	.set_power = m6mo_set_power,
	.reset = m6mo_reset,
	.clock_enable = m6mo_clock_enable,
};

static struct i2c_board_info __initdata i2c_devs5[] = {
	{
		I2C_BOARD_INFO("m6mo", 0x1f),
		.platform_data = &m6mo_plat,
		.irq = M040_CAMERA_ISP_IRQ,
	},
};
#else
static struct i2c_board_info __initdata i2c_devs5[] = {
};
#endif

static int __init mx2_init_camera(void)
{
	s3c_i2c5_set_platdata(&m040_default_i2c5_data);
	i2c_register_board_info(5, i2c_devs5, ARRAY_SIZE(i2c_devs5));
	
#ifdef CONFIG_VIDEO_FIMC
	s3c_fimc0_set_platdata(&fimc_plat);
	s3c_fimc1_set_platdata(NULL);
	s3c_fimc2_set_platdata(NULL);
	s3c_fimc3_set_platdata(NULL);
#ifndef CONFIG_PM_GENERIC_DOMAINS
	s3c_device_fimc0.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s3c_device_fimc1.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s3c_device_fimc2.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s3c_device_fimc3.dev.parent = &exynos4_device_pd[PD_CAM].dev;
#endif
#endif	

#ifdef CONFIG_VIDEO_FIMC_MIPI
	s3c_csis0_set_platdata(NULL);
#ifndef CONFIG_PM_GENERIC_DOMAINS
	s3c_device_csis0.dev.parent = &exynos4_device_pd[PD_CAM].dev;
#endif
#endif

	return 0;
}

arch_initcall(mx2_init_camera);

MODULE_DESCRIPTION("m040 fimc and camera driver helper");
MODULE_AUTHOR("Jerry Mo <jerrymo@meizu.com>");
MODULE_LICENSE("GPLV2");

