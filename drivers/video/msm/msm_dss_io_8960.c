/* Copyright (c) 2008-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/clk.h>
#include "msm_fb.h"
#include "mipi_dsi.h"
#include <mach/msm_iomap.h>

/* multimedia sub system clock control */
char *mmss_cc_base = MSM_MMSS_CLK_CTL_BASE;
/* multimedia sub system sfpb */
char *mmss_sfpb_base;
void  __iomem *periph_base;

int mipi_dsi_clk_on;
static struct dsi_clk_desc dsicore_clk;
static struct dsi_clk_desc dsi_pclk;

static struct clk *dsi_byte_div_clk;
static struct clk *dsi_esc_clk;
static struct clk *dsi_m_pclk;
static struct clk *dsi_s_pclk;

static struct clk *amp_pclk;

void mipi_dsi_clk_init(struct device *dev)
{
	amp_pclk = clk_get(NULL, "amp_pclk");
	if (IS_ERR(amp_pclk)) {
		pr_err("can't find amp_pclk\n");
		goto mipi_dsi_clk_err;
	}

	dsi_m_pclk = clk_get(dev, "dsi_m_pclk");
	if (IS_ERR(dsi_m_pclk)) {
		pr_err("can't find dsi_m_pclk\n");
		goto mipi_dsi_clk_err;
	}

	dsi_s_pclk = clk_get(dev, "dsi_s_pclk");
	if (IS_ERR(dsi_s_pclk)) {
		pr_err("can't find dsi_s_pclk\n");
		goto mipi_dsi_clk_err;
	}

	dsi_byte_div_clk = clk_get(dev, "dsi_byte_div_clk");
	if (IS_ERR(dsi_byte_div_clk)) {
		pr_err("can't find dsi_byte_div_clk\n");
		goto mipi_dsi_clk_err;
	}

	dsi_esc_clk = clk_get(dev, "dsi_esc_clk");
	if (IS_ERR(dsi_esc_clk)) {
		printk(KERN_ERR "can't find dsi_esc_clk\n");
		goto mipi_dsi_clk_err;
	}

	return;

mipi_dsi_clk_err:
	mipi_dsi_clk_deinit(NULL);
}

void mipi_dsi_clk_deinit(struct device *dev)
{
	clk_put(amp_pclk);
	clk_put(dsi_m_pclk);
	clk_put(dsi_s_pclk);
	clk_put(dsi_byte_div_clk);
	clk_put(dsi_esc_clk);
}

static void mipi_dsi_clk_ctrl(struct dsi_clk_desc *clk, int clk_en)
{
	char	*cc, *ns, *md;
	int	pmxo_sel = 0;
	char	mnd_en = 1, root_en = 1;
	uint32	data, val;

	cc = mmss_cc_base + 0x004c;
	md = mmss_cc_base + 0x0050;
	ns = mmss_cc_base + 0x0054;

	if (clk_en) {
		if (clk->mnd_mode == 0) {
			data  = clk->pre_div_func << 14;
			data |= clk->src;
			MIPI_OUTP_SECURE(ns, data);
			MIPI_OUTP_SECURE(cc, ((pmxo_sel << 8)
						| (clk->mnd_mode << 6)
						| (root_en << 2) | clk_en));
		} else {
			val = clk->d * 2;
			data = (~val) & 0x0ff;
			data |= clk->m << 8;
			MIPI_OUTP_SECURE(md, data);

			val = clk->n - clk->m;
			data = (~val) & 0x0ff;
			data <<= 24;
			data |= clk->src;
			MIPI_OUTP_SECURE(ns, data);

			MIPI_OUTP_SECURE(cc, ((pmxo_sel << 8)
					      | (clk->mnd_mode << 6)
					      | (mnd_en << 5)
					      | (root_en << 2) | clk_en));
		}
	} else
		MIPI_OUTP_SECURE(cc, 0);

	wmb();
}

static void mipi_dsi_sfpb_cfg(void)
{
	char *sfpb;
	int data;

	sfpb = mmss_sfpb_base + 0x058;

	data = MIPI_INP(sfpb);
	data |= 0x01800;
	MIPI_OUTP(sfpb, data);
	wmb();
}

static void mipi_dsi_pclk_ctrl(struct dsi_clk_desc *clk, int clk_en)
{
	char	*cc, *ns, *md;
	char	mnd_en = 1, root_en = 1;
	uint32	data, val;

	cc = mmss_cc_base + 0x0130;
	md = mmss_cc_base + 0x0134;
	ns = mmss_cc_base + 0x0138;

	if (clk_en) {
		if (clk->mnd_mode == 0) {
			data  = clk->pre_div_func << 12;
			data |= clk->src;
			MIPI_OUTP_SECURE(ns, data);
			MIPI_OUTP_SECURE(cc, ((clk->mnd_mode << 6)
					      | (root_en << 2) | clk_en));
		} else {
			val = clk->d * 2;
			data = (~val) & 0x0ff;
			data |= clk->m << 8;
			MIPI_OUTP_SECURE(md, data);

			val = clk->n - clk->m;
			data = (~val) & 0x0ff;
			data <<= 24;
			data |= clk->src;
			MIPI_OUTP_SECURE(ns, data);

			MIPI_OUTP_SECURE(cc, ((clk->mnd_mode << 6)
					      | (mnd_en << 5)
					      | (root_en << 2) | clk_en));
		}
	} else
		MIPI_OUTP_SECURE(cc, 0);

	wmb();
}

static void mipi_dsi_ahb_en(void)
{
	char	*ahb;

	ahb = mmss_cc_base + 0x08;

	pr_debug("%s: ahb=%x %x\n",
		__func__, (int) ahb, MIPI_INP_SECURE(ahb));
}

static void mipi_dsi_calibration(void)
{
	int i = 0;
	uint32 term_cnt = 5000;
	int cal_busy = MIPI_INP(MIPI_DSI_BASE + 0x550);

	/* DSI1_DSIPHY_REGULATOR_CAL_PWR_CFG */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0518, 0x01);

	/* DSI1_DSIPHY_CAL_SW_CFG2 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0534, 0x0);
	/* DSI1_DSIPHY_CAL_HW_CFG1 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x053c, 0x5a);
	/* DSI1_DSIPHY_CAL_HW_CFG3 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0544, 0x10);
	/* DSI1_DSIPHY_CAL_HW_CFG4 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0548, 0x01);
	/* DSI1_DSIPHY_CAL_HW_CFG0 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0538, 0x01);

	/* DSI1_DSIPHY_CAL_HW_TRIGGER */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0528, 0x01);
	usleep_range(5000, 5000);
	/* DSI1_DSIPHY_CAL_HW_TRIGGER */
	MIPI_OUTP(MIPI_DSI_BASE + 0x0528, 0x00);

	cal_busy = MIPI_INP(MIPI_DSI_BASE + 0x550);
	while (cal_busy & 0x10) {
		i++;
		if (i > term_cnt) {
			pr_err("DSI1 PHY REGULATOR NOT READY,"
				"exceeded polling TIMEOUT!\n");
			break;
		}
		cal_busy = MIPI_INP(MIPI_DSI_BASE + 0x550);
	}
}

void mipi_dsi_phy_rdy_poll(void)
{
	uint32 phy_pll_busy;
	uint32 i = 0;
	uint32 term_cnt = 0xFFFFFF;

	phy_pll_busy = MIPI_INP(MIPI_DSI_BASE + 0x280);
	while (!(phy_pll_busy & 0x1)) {
		i++;
		if (i > term_cnt) {
			pr_err("DSI1 PHY NOT READY, exceeded polling TIMEOUT!\n");
			break;
		}
		phy_pll_busy = MIPI_INP(MIPI_DSI_BASE + 0x280);
	}
}

#define PREF_DIV_RATIO 27
struct dsiphy_pll_divider_config pll_divider_config;

int mipi_dsi_phy_pll_config(u32 clk_rate)
{
	struct dsiphy_pll_divider_config *dividers;
	u32 fb_divider, tmp;
	dividers = &pll_divider_config;

	/* DSIPHY_PLL_CTRL_x:    1     2     3     8     9     10 */
	/* masks               0xff  0x07  0x3f  0x0f  0xff  0xff */

	/* DSIPHY_PLL_CTRL_1 */
	fb_divider = ((dividers->fb_divider) / 2) - 1;
	MIPI_OUTP(MIPI_DSI_BASE + 0x204, fb_divider & 0xff);

	/* DSIPHY_PLL_CTRL_2 */
	tmp = MIPI_INP(MIPI_DSI_BASE + 0x208);
	tmp &= ~0x07;
	tmp |= (fb_divider >> 8) & 0x07;
	MIPI_OUTP(MIPI_DSI_BASE + 0x208, tmp);

	/* DSIPHY_PLL_CTRL_3 */
	tmp = MIPI_INP(MIPI_DSI_BASE + 0x20c);
	tmp &= ~0x3f;
	tmp |= (dividers->ref_divider_ratio - 1) & 0x3f;
	MIPI_OUTP(MIPI_DSI_BASE + 0x20c, tmp);

	/* DSIPHY_PLL_CTRL_8 */
	tmp = MIPI_INP(MIPI_DSI_BASE + 0x220);
	tmp &= ~0x0f;
	tmp |= (dividers->bit_clk_divider - 1) & 0x0f;
	MIPI_OUTP(MIPI_DSI_BASE + 0x220, tmp);

	/* DSIPHY_PLL_CTRL_9 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x224, (dividers->byte_clk_divider - 1));

	/* DSIPHY_PLL_CTRL_10 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x228, (dividers->dsi_clk_divider - 1));

	return 0;
}

int mipi_dsi_clk_div_config(uint8 bpp, uint8 lanes,
			    uint32 *expected_dsi_pclk)
{
	u32 fb_divider, rate, vco;
	u32 div_ratio = 0;
	struct dsi_clk_mnd_table const *mnd_entry = mnd_table;
	if (pll_divider_config.clk_rate == 0)
		pll_divider_config.clk_rate = 454000000;

	rate = pll_divider_config.clk_rate / 1000000; /* In Mhz */

	if (rate < 125) {
		vco = rate * 8;
		div_ratio = 8;
	} else if (rate < 250) {
		vco = rate * 4;
		div_ratio = 4;
	} else if (rate < 500) {
		vco = rate * 2;
		div_ratio = 2;
	} else {
		vco = rate * 1;
		div_ratio = 1;
	}

	/* find the mnd settings from mnd_table entry */
	for (; mnd_entry != mnd_table + ARRAY_SIZE(mnd_table); ++mnd_entry) {
		if (((mnd_entry->lanes) == lanes) &&
			((mnd_entry->bpp) == bpp))
			break;
	}

	if (mnd_entry == mnd_table + ARRAY_SIZE(mnd_table)) {
		pr_err("%s: requested Lanes, %u & BPP, %u, not supported\n",
			__func__, lanes, bpp);
		return -EINVAL;
	}
	fb_divider = ((vco * PREF_DIV_RATIO) / 27);
	pll_divider_config.fb_divider = fb_divider;
	pll_divider_config.ref_divider_ratio = PREF_DIV_RATIO;
	pll_divider_config.bit_clk_divider = div_ratio;
	pll_divider_config.byte_clk_divider =
			pll_divider_config.bit_clk_divider * 8;
	pll_divider_config.dsi_clk_divider =
			(mnd_entry->dsiclk_div) * div_ratio;

	if (mnd_entry->dsiclk_d == 0) {
		dsicore_clk.mnd_mode = 0;
		dsicore_clk.src = 0x3;
		dsicore_clk.pre_div_func = (mnd_entry->dsiclk_n - 1);
	} else {
		dsicore_clk.mnd_mode = 2;
		dsicore_clk.src = 0x3;
		dsicore_clk.m = mnd_entry->dsiclk_m;
		dsicore_clk.n = mnd_entry->dsiclk_n;
		dsicore_clk.d = mnd_entry->dsiclk_d;
	}

	if ((mnd_entry->pclk_d == 0)
		|| (mnd_entry->pclk_m == 1)) {
		dsi_pclk.mnd_mode = 0;
		dsi_pclk.src = 0x3;
		dsi_pclk.pre_div_func = (mnd_entry->pclk_n - 1);
		*expected_dsi_pclk = ((vco * 1000000) /
					((pll_divider_config.dsi_clk_divider)
					* (mnd_entry->pclk_n)));
	} else {
		dsi_pclk.mnd_mode = 2;
		dsi_pclk.src = 0x3;
		dsi_pclk.m = mnd_entry->pclk_m;
		dsi_pclk.n = mnd_entry->pclk_n;
		dsi_pclk.d = mnd_entry->pclk_d;
		*expected_dsi_pclk = ((vco * 1000000 * dsi_pclk.m) /
					((pll_divider_config.dsi_clk_divider)
					* (mnd_entry->pclk_n)));
	}
	return 0;
}

static void mipi_dsi_configure_serdes(void)
{
	void __iomem *cc;

	/* PHY registers programemd thru S2P interface */
	if (periph_base) {
		MIPI_OUTP(periph_base + 0x2c, 0x000000b6);
		MIPI_OUTP(periph_base + 0x2c, 0x000001b5);
		MIPI_OUTP(periph_base + 0x2c, 0x000001b4);
		MIPI_OUTP(periph_base + 0x2c, 0x000003b3);
		MIPI_OUTP(periph_base + 0x2c, 0x000003a2);
		MIPI_OUTP(periph_base + 0x2c, 0x000002a1);
		MIPI_OUTP(periph_base + 0x2c, 0x000008a0);
		MIPI_OUTP(periph_base + 0x2c, 0x00000d9f);
		MIPI_OUTP(periph_base + 0x2c, 0x0000109e);
		MIPI_OUTP(periph_base + 0x2c, 0x0000209d);
		MIPI_OUTP(periph_base + 0x2c, 0x0000109c);
		MIPI_OUTP(periph_base + 0x2c, 0x0000079a);
		MIPI_OUTP(periph_base + 0x2c, 0x00000c99);
		MIPI_OUTP(periph_base + 0x2c, 0x00002298);
		MIPI_OUTP(periph_base + 0x2c, 0x000000a7);
		MIPI_OUTP(periph_base + 0x2c, 0x000000a6);
		MIPI_OUTP(periph_base + 0x2c, 0x000000a5);
		MIPI_OUTP(periph_base + 0x2c, 0x00007fa4);
		MIPI_OUTP(periph_base + 0x2c, 0x0000eea8);
		MIPI_OUTP(periph_base + 0x2c, 0x000006aa);
		MIPI_OUTP(periph_base + 0x2c, 0x00002095);
		MIPI_OUTP(periph_base + 0x2c, 0x00000493);
		MIPI_OUTP(periph_base + 0x2c, 0x00001092);
		MIPI_OUTP(periph_base + 0x2c, 0x00000691);
		MIPI_OUTP(periph_base + 0x2c, 0x00005490);
		MIPI_OUTP(periph_base + 0x2c, 0x0000038d);
		MIPI_OUTP(periph_base + 0x2c, 0x0000148c);
		MIPI_OUTP(periph_base + 0x2c, 0x0000058b);
		MIPI_OUTP(periph_base + 0x2c, 0x0000078a);
		MIPI_OUTP(periph_base + 0x2c, 0x00001f89);
		MIPI_OUTP(periph_base + 0x2c, 0x00003388);
		MIPI_OUTP(periph_base + 0x2c, 0x00006387);
		MIPI_OUTP(periph_base + 0x2c, 0x00004886);
		MIPI_OUTP(periph_base + 0x2c, 0x00005085);
		MIPI_OUTP(periph_base + 0x2c, 0x00000084);
		MIPI_OUTP(periph_base + 0x2c, 0x0000da83);
		MIPI_OUTP(periph_base + 0x2c, 0x0000b182);
		MIPI_OUTP(periph_base + 0x2c, 0x00002f81);
		MIPI_OUTP(periph_base + 0x2c, 0x00004080);
		MIPI_OUTP(periph_base + 0x2c, 0x00004180);
		MIPI_OUTP(periph_base + 0x2c, 0x000006aa);
	}

	cc = MIPI_DSI_BASE + 0x0130;
	MIPI_OUTP(cc, 0x806c11c8);
	MIPI_OUTP(cc, 0x804c11c8);
	MIPI_OUTP(cc, 0x806d0080);
	MIPI_OUTP(cc, 0x804d0080);
	MIPI_OUTP(cc, 0x00000000);
	MIPI_OUTP(cc, 0x807b1597);
	MIPI_OUTP(cc, 0x805b1597);
	MIPI_OUTP(cc, 0x807c0080);
	MIPI_OUTP(cc, 0x805c0080);
	MIPI_OUTP(cc, 0x00000000);
	MIPI_OUTP(cc, 0x807911c8);
	MIPI_OUTP(cc, 0x805911c8);
	MIPI_OUTP(cc, 0x807a0080);
	MIPI_OUTP(cc, 0x805a0080);
	MIPI_OUTP(cc, 0x00000000);
	MIPI_OUTP(cc, 0x80721555);
	MIPI_OUTP(cc, 0x80521555);
	MIPI_OUTP(cc, 0x80730000);
	MIPI_OUTP(cc, 0x80530000);
	MIPI_OUTP(cc, 0x00000000);
}

void mipi_dsi_phy_init(int panel_ndx, struct msm_panel_info const *panel_info,
	int target_type)
{
	struct mipi_dsi_phy_ctrl *pd;
	int i, off;

	MIPI_OUTP(MIPI_DSI_BASE + 0x128, 0x0001);/* start phy sw reset */
	msleep(100);
	MIPI_OUTP(MIPI_DSI_BASE + 0x128, 0x0000);/* end phy w reset */
	MIPI_OUTP(MIPI_DSI_BASE + 0x500, 0x0003);/* regulator_ctrl_0 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x504, 0x0001);/* regulator_ctrl_1 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x508, 0x0001);/* regulator_ctrl_2 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x50c, 0x0000);/* regulator_ctrl_3 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x510, 0x0100);/* regulator_ctrl_4 */

	pd = (panel_info->mipi).dsi_phy_db;

	off = 0x0480;	/* strength 0 - 2 */
	for (i = 0; i < 3; i++) {
		MIPI_OUTP(MIPI_DSI_BASE + off, pd->strength[i]);
		wmb();
		off += 4;
	}

	off = 0x0470;	/* ctrl 0 - 3 */
	for (i = 0; i < 4; i++) {
		MIPI_OUTP(MIPI_DSI_BASE + off, pd->ctrl[i]);
		wmb();
		off += 4;
	}

	off = 0x0500;	/* regulator ctrl 0 - 4 */
	for (i = 0; i < 5; i++) {
		MIPI_OUTP(MIPI_DSI_BASE + off, pd->regulator[i]);
		wmb();
		off += 4;
	}
	mipi_dsi_calibration();

	off = 0x0204;	/* pll ctrl 1 - 19, skip 0 */
	for (i = 1; i < 20; i++) {
		MIPI_OUTP(MIPI_DSI_BASE + off, pd->pll[i]);
		wmb();
		off += 4;
	}

	if (panel_info)
		mipi_dsi_phy_pll_config(panel_info->clk_rate);

	/* pll ctrl 0 */
	MIPI_OUTP(MIPI_DSI_BASE + 0x200, pd->pll[0]);
	wmb();
	MIPI_OUTP(MIPI_DSI_BASE + 0x200, (pd->pll[0] | 0x01));

	mipi_dsi_phy_rdy_poll();

	off = 0x0440;	/* phy timig ctrl 0 - 11 */
	for (i = 0; i < 12; i++) {
		MIPI_OUTP(MIPI_DSI_BASE + off, pd->timing[i]);
		wmb();
		off += 4;
	}
	clk_enable(dsi_byte_div_clk);
	clk_enable(dsi_esc_clk);

	if (target_type == 1)
		mipi_dsi_configure_serdes();
}

void mipi_dsi_clk_enable(void)
{
	if (mipi_dsi_clk_on) {
		pr_err("%s: mipi_dsi_clk already ON\n", __func__);
		return;
	}

	mipi_dsi_clk_on = 1;

	clk_enable(amp_pclk); /* clock for AHB-master to AXI */
	clk_enable(dsi_m_pclk);
	clk_enable(dsi_s_pclk);
	if (clk_set_rate(dsi_byte_div_clk, 1) < 0)	/* divided by 1 */
		pr_err("%s: dsi_byte_div_clk - "
			"clk_set_rate failed\n", __func__);
	if (clk_set_rate(dsi_esc_clk, 2) < 0) /* divided by 2 */
		pr_err("%s: dsi_esc_clk - "
			"clk_set_rate failed\n", __func__);
	mipi_dsi_pclk_ctrl(&dsi_pclk, 1);
	mipi_dsi_clk_ctrl(&dsicore_clk, 1);
	mipi_dsi_ahb_en();
	mipi_dsi_sfpb_cfg();
}

void mipi_dsi_clk_disable(void)
{
	if (mipi_dsi_clk_on == 0) {
		pr_err("%s: mipi_dsi_clk already OFF\n", __func__);
		return;
	}

	mipi_dsi_clk_on = 0;

	MIPI_OUTP(MIPI_DSI_BASE + 0x0118, 0);

	mipi_dsi_pclk_ctrl(&dsi_pclk, 0);
	mipi_dsi_clk_ctrl(&dsicore_clk, 0);
	clk_disable(dsi_esc_clk);
	clk_disable(dsi_byte_div_clk);
	clk_disable(dsi_m_pclk);
	clk_disable(dsi_s_pclk);
	clk_disable(amp_pclk); /* clock for AHB-master to AXI */
}

void mipi_dsi_phy_ctrl(int on)
{
	if (on) {
		/* DSIPHY_PLL_CTRL_5 */
		MIPI_OUTP(MIPI_DSI_BASE + 0x0214, 0x050);
	} else {
		/* DSIPHY_PLL_CTRL_5 */
		MIPI_OUTP(MIPI_DSI_BASE + 0x0214, 0x05f);

		/* DSIPHY_REGULATOR_CTRL_0 */
		MIPI_OUTP(MIPI_DSI_BASE + 0x0500, 0x02);

		/* DSIPHY_CTRL_0 */
		MIPI_OUTP(MIPI_DSI_BASE + 0x0470, 0x00);

		/* DSIPHY_CTRL_1 */
		MIPI_OUTP(MIPI_DSI_BASE + 0x0474, 0x7f);

		/* DSIPHY_PLL_CTRL_0, disbale dsi pll */
		MIPI_OUTP(MIPI_DSI_BASE + 0x0200, 0x40);

		/* disbale dsi clk */
		MIPI_OUTP(MIPI_DSI_BASE + 0x0118, 0);
	}
}