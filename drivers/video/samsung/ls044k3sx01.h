/* linux/drivers/video/samsung/s3cfb_ls044k3sx01.h
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 
 *
 */
 
#ifndef S3CFB_LS044K3SX01_H
#define S3CFB_LS044k3SX01_H

#include <video/mipi_display.h>
/*The default value is 200us when esc_clk is 20MHZ, 
  *The value double if esc_clk is 10MHZ
  */
#define BTA_NONE 0
#define BTA_TIMEOUT 500
#define BTA_TIMEOUT_LONG 50000	/* 50ms */

#define lcd_to_master(a)		(a->dsim_dev->master)
#define lcd_to_master_ops(a)	((lcd_to_master(a))->master_ops)

#define write_cmd(lcd, cmd0, cmd1, bta) \
	lcd_to_master_ops(lcd)->cmd_write(lcd_to_master(lcd), \
					MIPI_DSI_DCS_SHORT_WRITE_PARAM, \
					cmd0, cmd1, bta)
#define write_gen_data(lcd, array, size, bta)	\
	lcd_to_master_ops(lcd)->cmd_write(lcd_to_master(lcd),\
					MIPI_DSI_GENERIC_LONG_WRITE, \
					(unsigned int)array, size, bta)
#define write_data(lcd, array, size, bta)	\
	lcd_to_master_ops(lcd)->cmd_write(lcd_to_master(lcd),\
					MIPI_DSI_DCS_LONG_WRITE, \
					(unsigned int)array, size, bta)

#define PP_NARG(...) \
    PP_NARG_(__VA_ARGS__,PP_RSEQ_N())
#define PP_NARG_(...) \
    PP_ARG_N(__VA_ARGS__)
#define PP_ARG_N( \
     _1, _2, _3, _4, _5, _6, _7, _8, _9,_10, \
    _11,_12,_13,_14,_15,_16,_17,_18,_19,_20, \
    _21,_22,_23,_24,_25,_26,_27,_28,_29,_30, \
    _31,_32,  N, ...) N
#define PP_RSEQ_N() \
    32,31,30, \
    29,28,27,26,25,24,23,22,21,20, \
    19,18,17,16,15,14,13,12,11,10, \
     9, 8, 7, 6, 5, 4, 3, 2, 1, 0

#define LCD_PARAM_DCS_CMD(mdelay, ...) {\
	.param = {__VA_ARGS__},\
	.delay = mdelay,}

#define LCD_PARAM_DEF( ...) {\
	.param = {__VA_ARGS__},\
	.size = PP_NARG(__VA_ARGS__),}

#define LCD_PARAM_DEF_END {.size = -1,}

#define LCD_TEST

struct ls044k3sx01_param {
	char param[24];
	int size;
	int delay;	/* delay time ms */
};

enum lcd_state {
	LCD_DISPLAY_SLEEP_IN,
	LCD_DISPLAY_DEEP_STAND_BY,
	LCD_DISPLAY_POWER_OFF,
};

struct ls044k3sx01_info {
	struct device			*dev;
	struct lcd_device		*ld;

	struct mipi_dsim_lcd_device *dsim_dev;
	struct lcd_platform_data	*ddi_pd;
	enum lcd_state state;
};

static const struct ls044k3sx01_param ls044k3sx01_slpout_seq[] = {
	LCD_PARAM_DCS_CMD(10, MIPI_DCS_EXIT_SLEEP_MODE, 0x0),
	LCD_PARAM_DEF_END,
};
static const struct ls044k3sx01_param ls044k3sx01_slpin_seq[] = {
	LCD_PARAM_DCS_CMD(120, MIPI_DCS_ENTER_SLEEP_MODE, 0x0),
	LCD_PARAM_DEF_END,
};
static const struct ls044k3sx01_param ls044k3sx01_dspon_seq[] = {
	LCD_PARAM_DCS_CMD(10, MIPI_DCS_SET_DISPLAY_ON, 0x0),
	LCD_PARAM_DCS_CMD(0, MIPI_DCS_BACKLIGHT_ON, 0x4),
	LCD_PARAM_DEF_END,
};
static const struct ls044k3sx01_param ls044k3sx01_dspoff_seq[] = {
	LCD_PARAM_DCS_CMD(0, MIPI_DCS_BACKLIGHT_ON, 0x0),
	LCD_PARAM_DCS_CMD(120, MIPI_DCS_SET_DISPLAY_OFF, 0x0),
	LCD_PARAM_DEF_END,
};
static const struct ls044k3sx01_param ls044k3sx01_cabc_seq[] = {
	LCD_PARAM_DEF(0xDF,0x55,0xAA,0x52,0x08),
	LCD_PARAM_DEF(0xB5,0x01,0xff,0x02,0x00,0x00,0x08,0x1c,0x00),
	LCD_PARAM_DCS_CMD(0, 0x5E, 0x78),
	LCD_PARAM_DCS_CMD(0, 0x51, 0xff),
	LCD_PARAM_DCS_CMD(0, MIPI_DCS_BACKLIGHT_ON, 0x2C),
	LCD_PARAM_DCS_CMD(0, 0x55, 0x03),
	LCD_PARAM_DEF_END,
};
static const struct ls044k3sx01_param ls044k3sx01_cabc_seq_off[] = {
	LCD_PARAM_DCS_CMD(0, MIPI_DCS_BACKLIGHT_ON, 0x04),
	LCD_PARAM_DEF_END,
};

#ifdef LCD_TEST
static const struct ls044k3sx01_param ls044k3sx01_hsync_out_seq[] = {
	LCD_PARAM_DEF(0xDF,0x55,0xAA,0x52,0x08),
	LCD_PARAM_DEF(0x9B,0x02),
	LCD_PARAM_DEF(0xCD,0x00,0x00,0x55,0x00,0x55,0x00),
	LCD_PARAM_DEF_END,
};
static const struct ls044k3sx01_param ls044k3sx01_vsync_out_seq[] = {
	LCD_PARAM_DEF(0xDF,0x55,0xAA,0x52,0x08),
	LCD_PARAM_DEF(0x9B,0x04),
	LCD_PARAM_DEF(0xCD,0x00,0x00,0x55,0x00,0x55,0x00),
	LCD_PARAM_DEF_END,
};
#endif
static const struct ls044k3sx01_param ls044k3sx01_init_seq[] = {
	LCD_PARAM_DEF(0xDF,0x55,0xAA,0x52,0x08),
	LCD_PARAM_DEF(0xB5,0x01,0xff,0x02,0x00,0x00,0x08,0x1c,0x00),
	LCD_PARAM_DEF(0xB8,0x33,0x00,0x27,0x00,0x00,0x06,0xB5,0x0A),
	LCD_PARAM_DEF(0xC0,0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,0xFF),
	LCD_PARAM_DEF(0xC1,0x1E,0x3D,0x5D,0x8C,0xC8,0xCF,0x9C,0xD1,0x8C,0xB3,0x6F,0x9E,0xB3,0xC6,0xDF,0xFB),
	LCD_PARAM_DEF(0xC2,0x1E,0x3D,0x5D,0x8C,0xC8,0xCF,0x9C,0xD1,0x8C,0xB3,0x6F,0x9E,0xB3,0xC6,0xDF,0xFB),
	LCD_PARAM_DEF(0xC3,0x1E,0x3D,0x5D,0x8C,0xC8,0xCF,0x9C,0xD1,0x8C,0xB3,0x6F,0x9E,0xB3,0xC6,0xDF,0xFB),
	LCD_PARAM_DEF(0xC4,0x1E,0x3D,0x5B,0x86,0xBA,0xBB,0x77,0xAE,0x72,0xA1,0x6B,0x9A,0xB1,0xC4,0xDE,0xFB),
	LCD_PARAM_DEF(0xC5,0x1E,0x3D,0x5B,0x86,0xBA,0xBB,0x77,0xAE,0x72,0xA1,0x6B,0x9A,0xB1,0xC4,0xDE,0xFB),
	LCD_PARAM_DEF(0xC6,0x1E,0x3D,0x5B,0x86,0xBA,0xBB,0x77,0xAE,0x72,0xA1,0x6B,0x9A,0xB1,0xC4,0xDE,0xFB),
	LCD_PARAM_DEF(0xC8,0x11,0x19,0x06,0x0E,0x13,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00),
	LCD_PARAM_DEF_END,
};
#endif /* S3CFB_LS044K3SX01_H */
