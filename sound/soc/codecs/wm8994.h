/*
 * wm8994.h  --  WM8994 Soc Audio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _WM8994_H
#define _WM8994_H

#include <sound/soc.h>
#include <linux/firmware.h>

#include "wm_hubs.h"
#include <sound/soc-dapm.h>

/* Sources for AIF1/2 SYSCLK - use with set_dai_sysclk() */
#define WM8994_SYSCLK_MCLK1 1
#define WM8994_SYSCLK_MCLK2 2
#define WM8994_SYSCLK_FLL1  3
#define WM8994_SYSCLK_FLL2  4

/* OPCLK is also configured with set_dai_sysclk, specify division*10 as rate. */
#define WM8994_SYSCLK_OPCLK 5

#define WM8994_FLL1 1
#define WM8994_FLL2 2

#define WM8994_FLL_SRC_MCLK1  1
#define WM8994_FLL_SRC_MCLK2  2
#define WM8994_FLL_SRC_LRCLK  3
#define WM8994_FLL_SRC_BCLK   4

typedef void (*wm8958_micdet_cb)(u16 status, void *data);

int wm8994_mic_detect(struct snd_soc_codec *codec, struct snd_soc_jack *jack,
		      int micbias, int det, int shrt);
int wm8958_mic_detect(struct snd_soc_codec *codec, struct snd_soc_jack *jack,
		      wm8958_micdet_cb cb, void *cb_data);

#define WM8994_CACHE_SIZE 1570

struct wm8994_access_mask {
	unsigned short readable;   /* Mask of readable bits */
	unsigned short writable;   /* Mask of writable bits */
};

extern const struct wm8994_access_mask wm8994_access_masks[WM8994_CACHE_SIZE];
extern const u16 wm8994_reg_defaults[WM8994_CACHE_SIZE];

int wm8958_aif_ev(struct snd_soc_dapm_widget *w,
		  struct snd_kcontrol *kcontrol, int event);

void wm8958_dsp2_init(struct snd_soc_codec *codec);

struct wm8994_micdet {
	struct snd_soc_jack *jack;
	int det;
	int shrt;
};

/* codec private data */
struct wm8994_fll_config {
	int src;
	int in;
	int out;
};

#define WM8994_NUM_DRC 3
#define WM8994_NUM_EQ  3

struct wm8994_priv {
	struct wm_hubs_data hubs;
	enum snd_soc_control_type control_type;
	void *control_data;
	struct snd_soc_codec *codec;
	int sysclk[2];
	int sysclk_rate[2];
	int mclk[2];
	int aifclk[2];
	struct wm8994_fll_config fll[2], fll_suspend[2];

	int dac_rates[2];
	int lrclk_shared[2];

	int mbc_ena[3];
	int hpf1_ena[3];
	int hpf2_ena[3];
	int vss_ena[3];
	int enh_eq_ena[3];

	/* Platform dependant DRC configuration */
	const char **drc_texts;
	int drc_cfg[WM8994_NUM_DRC];
	struct soc_enum drc_enum;

	/* Platform dependant ReTune mobile configuration */
	int num_retune_mobile_texts;
	const char **retune_mobile_texts;
	int retune_mobile_cfg[WM8994_NUM_EQ];
	struct soc_enum retune_mobile_enum;

	/* Platform dependant MBC configuration */
	int mbc_cfg;
	const char **mbc_texts;
	struct soc_enum mbc_enum;

	/* Platform dependant VSS configuration */
	int vss_cfg;
	const char **vss_texts;
	struct soc_enum vss_enum;

	/* Platform dependant VSS HPF configuration */
	int vss_hpf_cfg;
	const char **vss_hpf_texts;
	struct soc_enum vss_hpf_enum;

	/* Platform dependant enhanced EQ configuration */
	int enh_eq_cfg;
	const char **enh_eq_texts;
	struct soc_enum enh_eq_enum;

	struct wm8994_micdet micdet[2];

	wm8958_micdet_cb jack_cb;
	void *jack_cb_data;
	int micdet_irq;

	int revision;
	struct wm8994_pdata *pdata;

	unsigned int aif1clk_enable:1;
	unsigned int aif2clk_enable:1;

	unsigned int aif1clk_disable:1;
	unsigned int aif2clk_disable:1;

	int dsp_active;
	const struct firmware *cur_fw;
	const struct firmware *mbc;
	const struct firmware *mbc_vss;
	const struct firmware *enh_eq;
	
	u8 playback_path;
	u8 capture_path;
	u8 music_headset_nrec_switch;
};

//"Speaker","Headphone","Receiver",
//"Speaker incall","Headphone incall","Receiver incall",
//"Speaker ringtone","Headphone ringtone","Receiver ringtone",
//"None"
/*  the path of Playback & Capture */

enum _playback_path {
		PLAYBACK_NONE = 0,//default
		PLAYBACK_SPK_NORMAL,
		PLAYBACK_HP_NORMAL,
		PLAYBACK_REC_NORMAL,
		PLAYBACK_SPK_HP_NORMAL,// 4
		PLAYBACK_SPK_INCALL,
		PLAYBACK_HS_INCALL,
		PLAYBACK_REC_INCALL,
		PLAYBACK_SPK_HS_INCALL,
		PLAYBACK_HP_INCALL,
		PLAYBACK_SPK_HP_INCALL,
		PLAYBACK_SPK_RING,// 11
		PLAYBACK_HP_RING,
		PLAYBACK_REC_RING,
		PLAYBACK_SPK_HP_RING,
		PLAYBACK_BT_NORMAL,//15
		PLAYBACK_BT_INCALL,
		PLAYBACK_BT_RING,
		PLAYBACK_BT_VOIP,//18
		PLAYBACK_SPK_VOIP,//19
		PLAYBACK_HS_VOIP,
		PLAYBACK_REC_VOIP, // 21
		PLAYBACK_SPK_HS_VOIP,
		PLAYBACK_HP_VOIP, // 23
		PLAYBACK_SPK_HP_VOIP,
		PLAYBACK_MIC_TEST, // 25
		PLAYBACK_REC_TEST,
		PLAYBACK_SPK_TEST, // 27
		PLAYBACK_REC_ECHO_TEST,
		PLAYBACK_SPK_ECHO_TEST, // 29
		PLAYBACK_ANSWER_INCALL,
};
enum _capture_path {
		CAPTURE_NONE = 0,//default
		CAPTURE_MAIN_MIC_NORMAL,
		CAPTURE_SECOND_MIC_NORMAL,
		CAPTURE_HAND_MIC_NORMAL,
		CAPTURE_MAIN_MIC_INCALL,
		CAPTURE_SECOND_MIC_INCALL,
		CAPTURE_HAND_MIC_INCALL,
		CAPTURE_MAIN_MIC_VOIP,
		CAPTURE_SECOND_MIC_VOIP,
		CAPTURE_HAND_MIC_VOIP,
		CAPTURE_TEST, // 10
};

#endif
