/* Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/clkdev.h>

#include <mach/msm_iomap.h>
#include <mach/clk.h>
#include <mach/msm_xo.h>
#include <mach/scm-io.h>
#include <mach/rpm.h>
#include <mach/rpm-regulator.h>

#include "clock-local.h"
#include "clock-rpm.h"
#include "clock-voter.h"

#ifdef CONFIG_MSM_SECURE_IO
#undef readl_relaxed
#undef writel_relaxed
#define readl_relaxed secure_readl
#define writel_relaxed secure_writel
#endif

#define REG(off)	(MSM_CLK_CTL_BASE + (off))
#define REG_MM(off)	(MSM_MMSS_CLK_CTL_BASE + (off))
#define REG_LPA(off)	(MSM_LPASS_CLK_CTL_BASE + (off))

/* Peripheral clock registers. */
#define CE2_HCLK_CTL_REG			REG(0x2740)
#define CLK_HALT_CFPB_STATEA_REG		REG(0x2FCC)
#define CLK_HALT_CFPB_STATEB_REG		REG(0x2FD0)
#define CLK_HALT_CFPB_STATEC_REG		REG(0x2FD4)
#define CLK_HALT_DFAB_STATE_REG			REG(0x2FC8)
#define CLK_HALT_MSS_SMPSS_MISC_STATE_REG	REG(0x2FDC)
#define CLK_HALT_SFPB_MISC_STATE_REG		REG(0x2FD8)
#define CLK_TEST_REG				REG(0x2FA0)
#define GSBIn_HCLK_CTL_REG(n)			REG(0x29C0+(0x20*((n)-1)))
#define GSBIn_QUP_APPS_MD_REG(n)		REG(0x29C8+(0x20*((n)-1)))
#define GSBIn_QUP_APPS_NS_REG(n)		REG(0x29CC+(0x20*((n)-1)))
#define GSBIn_RESET_REG(n)			REG(0x29DC+(0x20*((n)-1)))
#define GSBIn_UART_APPS_MD_REG(n)		REG(0x29D0+(0x20*((n)-1)))
#define GSBIn_UART_APPS_NS_REG(n)		REG(0x29D4+(0x20*((n)-1)))
#define PDM_CLK_NS_REG				REG(0x2CC0)
#define BB_PLL_ENA_SC0_REG			REG(0x34C0)
#define BB_PLL0_STATUS_REG			REG(0x30D8)
#define BB_PLL6_STATUS_REG			REG(0x3118)
#define BB_PLL8_L_VAL_REG			REG(0x3144)
#define BB_PLL8_M_VAL_REG			REG(0x3148)
#define BB_PLL8_MODE_REG			REG(0x3140)
#define BB_PLL8_N_VAL_REG			REG(0x314C)
#define BB_PLL8_STATUS_REG			REG(0x3158)
#define PLLTEST_PAD_CFG_REG			REG(0x2FA4)
#define PMEM_ACLK_CTL_REG			REG(0x25A0)
#define PPSS_HCLK_CTL_REG			REG(0x2580)
#define PRNG_CLK_NS_REG				REG(0x2E80)
#define RINGOSC_NS_REG				REG(0x2DC0)
#define RINGOSC_STATUS_REG			REG(0x2DCC)
#define RINGOSC_TCXO_CTL_REG			REG(0x2DC4)
#define SC0_U_CLK_BRANCH_ENA_VOTE_REG		REG(0x3080)
#define SC1_U_CLK_BRANCH_ENA_VOTE_REG		REG(0x30A0)
#define SC0_U_CLK_SLEEP_ENA_VOTE_REG		REG(0x3084)
#define SC1_U_CLK_SLEEP_ENA_VOTE_REG		REG(0x30A4)
#define SDCn_APPS_CLK_MD_REG(n)			REG(0x2828+(0x20*((n)-1)))
#define SDCn_APPS_CLK_NS_REG(n)			REG(0x282C+(0x20*((n)-1)))
#define SDCn_HCLK_CTL_REG(n)			REG(0x2820+(0x20*((n)-1)))
#define SDCn_RESET_REG(n)			REG(0x2830+(0x20*((n)-1)))
#define TSIF_HCLK_CTL_REG			REG(0x2700)
#define TSIF_REF_CLK_MD_REG			REG(0x270C)
#define TSIF_REF_CLK_NS_REG			REG(0x2710)
#define TSSC_CLK_CTL_REG			REG(0x2CA0)
#define USB_FSn_HCLK_CTL_REG(n)			REG(0x2960+(0x20*((n)-1)))
#define USB_FSn_RESET_REG(n)			REG(0x2974+(0x20*((n)-1)))
#define USB_FSn_SYSTEM_CLK_CTL_REG(n)		REG(0x296C+(0x20*((n)-1)))
#define USB_FSn_XCVR_FS_CLK_MD_REG(n)		REG(0x2964+(0x20*((n)-1)))
#define USB_FSn_XCVR_FS_CLK_NS_REG(n)		REG(0x2968+(0x20*((n)-1)))
#define USB_HS1_HCLK_CTL_REG			REG(0x2900)
#define USB_HS1_RESET_REG			REG(0x2910)
#define USB_HS1_XCVR_FS_CLK_MD_REG		REG(0x2908)
#define USB_HS1_XCVR_FS_CLK_NS_REG		REG(0x290C)
#define USB_PHY0_RESET_REG			REG(0x2E20)

/* Multimedia clock registers. */
#define AHB_EN_REG				REG_MM(0x0008)
#define AHB_EN2_REG				REG_MM(0x0038)
#define AHB_NS_REG				REG_MM(0x0004)
#define AXI_NS_REG				REG_MM(0x0014)
#define CAMCLK_CC_REG				REG_MM(0x0140)
#define CAMCLK_MD_REG				REG_MM(0x0144)
#define CAMCLK_NS_REG				REG_MM(0x0148)
#define CSI_CC_REG				REG_MM(0x0040)
#define CSI_NS_REG				REG_MM(0x0048)
#define DBG_BUS_VEC_A_REG			REG_MM(0x01C8)
#define DBG_BUS_VEC_B_REG			REG_MM(0x01CC)
#define DBG_BUS_VEC_C_REG			REG_MM(0x01D0)
#define DBG_BUS_VEC_D_REG			REG_MM(0x01D4)
#define DBG_BUS_VEC_E_REG			REG_MM(0x01D8)
#define DBG_BUS_VEC_F_REG			REG_MM(0x01DC)
#define DBG_BUS_VEC_H_REG			REG_MM(0x01E4)
#define DBG_CFG_REG_HS_REG			REG_MM(0x01B4)
#define DBG_CFG_REG_LS_REG			REG_MM(0x01B8)
#define GFX2D0_CC_REG				REG_MM(0x0060)
#define GFX2D0_MD0_REG				REG_MM(0x0064)
#define GFX2D0_MD1_REG				REG_MM(0x0068)
#define GFX2D0_NS_REG				REG_MM(0x0070)
#define GFX2D1_CC_REG				REG_MM(0x0074)
#define GFX2D1_MD0_REG				REG_MM(0x0078)
#define GFX2D1_MD1_REG				REG_MM(0x006C)
#define GFX2D1_NS_REG				REG_MM(0x007C)
#define GFX3D_CC_REG				REG_MM(0x0080)
#define GFX3D_MD0_REG				REG_MM(0x0084)
#define GFX3D_MD1_REG				REG_MM(0x0088)
#define GFX3D_NS_REG				REG_MM(0x008C)
#define IJPEG_CC_REG				REG_MM(0x0098)
#define IJPEG_MD_REG				REG_MM(0x009C)
#define IJPEG_NS_REG				REG_MM(0x00A0)
#define JPEGD_CC_REG				REG_MM(0x00A4)
#define JPEGD_NS_REG				REG_MM(0x00AC)
#define MAXI_EN_REG				REG_MM(0x0018)
#define MAXI_EN3_REG				REG_MM(0x002C)
#define MDP_CC_REG				REG_MM(0x00C0)
#define MDP_MD0_REG				REG_MM(0x00C4)
#define MDP_MD1_REG				REG_MM(0x00C8)
#define MDP_NS_REG				REG_MM(0x00D0)
#define MISC_CC_REG				REG_MM(0x0058)
#define MISC_CC2_REG				REG_MM(0x005C)
#define PIXEL_CC_REG				REG_MM(0x00D4)
#define PIXEL_CC2_REG				REG_MM(0x0120)
#define PIXEL_MD_REG				REG_MM(0x00D8)
#define PIXEL_NS_REG				REG_MM(0x00DC)
#define MM_PLL0_MODE_REG			REG_MM(0x0300)
#define MM_PLL0_STATUS_REG			REG_MM(0x0318)
#define MM_PLL1_MODE_REG			REG_MM(0x031C)
#define MM_PLL1_STATUS_REG			REG_MM(0x0334)
#define MM_PLL2_CONFIG_REG			REG_MM(0x0348)
#define MM_PLL2_L_VAL_REG			REG_MM(0x033C)
#define MM_PLL2_M_VAL_REG			REG_MM(0x0340)
#define MM_PLL2_MODE_REG			REG_MM(0x0338)
#define MM_PLL2_N_VAL_REG			REG_MM(0x0344)
#define MM_PLL2_STATUS_REG			REG_MM(0x0350)
#define ROT_CC_REG				REG_MM(0x00E0)
#define ROT_NS_REG				REG_MM(0x00E8)
#define SAXI_EN_REG				REG_MM(0x0030)
#define SW_RESET_AHB_REG			REG_MM(0x020C)
#define SW_RESET_ALL_REG			REG_MM(0x0204)
#define SW_RESET_AXI_REG			REG_MM(0x0208)
#define SW_RESET_CORE_REG			REG_MM(0x0210)
#define TV_CC_REG				REG_MM(0x00EC)
#define TV_CC2_REG				REG_MM(0x0124)
#define TV_MD_REG				REG_MM(0x00F0)
#define TV_NS_REG				REG_MM(0x00F4)
#define VCODEC_CC_REG				REG_MM(0x00F8)
#define VCODEC_MD0_REG				REG_MM(0x00FC)
#define VCODEC_MD1_REG				REG_MM(0x0128)
#define VCODEC_NS_REG				REG_MM(0x0100)
#define VFE_CC_REG				REG_MM(0x0104)
#define VFE_MD_REG				REG_MM(0x0108)
#define VFE_NS_REG				REG_MM(0x010C)
#define VPE_CC_REG				REG_MM(0x0110)
#define VPE_NS_REG				REG_MM(0x0118)

/* Low-power Audio clock registers. */
#define LCC_CLK_LS_DEBUG_CFG_REG		REG_LPA(0x00A8)
#define LCC_CODEC_I2S_MIC_MD_REG		REG_LPA(0x0064)
#define LCC_CODEC_I2S_MIC_NS_REG		REG_LPA(0x0060)
#define LCC_CODEC_I2S_MIC_STATUS_REG		REG_LPA(0x0068)
#define LCC_CODEC_I2S_SPKR_MD_REG		REG_LPA(0x0070)
#define LCC_CODEC_I2S_SPKR_NS_REG		REG_LPA(0x006C)
#define LCC_CODEC_I2S_SPKR_STATUS_REG		REG_LPA(0x0074)
#define LCC_MI2S_MD_REG				REG_LPA(0x004C)
#define LCC_MI2S_NS_REG				REG_LPA(0x0048)
#define LCC_MI2S_STATUS_REG			REG_LPA(0x0050)
#define LCC_PCM_MD_REG				REG_LPA(0x0058)
#define LCC_PCM_NS_REG				REG_LPA(0x0054)
#define LCC_PCM_STATUS_REG			REG_LPA(0x005C)
#define LCC_PLL0_CONFIG_REG			REG_LPA(0x0014)
#define LCC_PLL0_L_VAL_REG			REG_LPA(0x0004)
#define LCC_PLL0_M_VAL_REG			REG_LPA(0x0008)
#define LCC_PLL0_MODE_REG			REG_LPA(0x0000)
#define LCC_PLL0_N_VAL_REG			REG_LPA(0x000C)
#define LCC_PLL0_STATUS_REG			REG_LPA(0x0018)
#define LCC_PRI_PLL_CLK_CTL_REG			REG_LPA(0x00C4)
#define LCC_SPARE_I2S_MIC_MD_REG		REG_LPA(0x007C)
#define LCC_SPARE_I2S_MIC_NS_REG		REG_LPA(0x0078)
#define LCC_SPARE_I2S_MIC_STATUS_REG		REG_LPA(0x0080)
#define LCC_SPARE_I2S_SPKR_MD_REG		REG_LPA(0x0088)
#define LCC_SPARE_I2S_SPKR_NS_REG		REG_LPA(0x0084)
#define LCC_SPARE_I2S_SPKR_STATUS_REG		REG_LPA(0x008C)

/* MUX source input identifiers. */
#define pxo_to_bb_mux		0
#define mxo_to_bb_mux		1
#define cxo_to_bb_mux		pxo_to_bb_mux
#define pll0_to_bb_mux		2
#define pll8_to_bb_mux		3
#define pll6_to_bb_mux		4
#define gnd_to_bb_mux		6
#define pxo_to_mm_mux		0
#define pll1_to_mm_mux		1	/* or MMSS_PLL0 */
#define pll2_to_mm_mux		1	/* or MMSS_PLL1 */
#define pll3_to_mm_mux		3	/* or MMSS_PLL2 */
#define pll8_to_mm_mux		2	/* or MMSS_GPERF */
#define pll0_to_mm_mux		3	/* or MMSS_GPLL0 */
#define mxo_to_mm_mux		4
#define gnd_to_mm_mux		6
#define cxo_to_xo_mux		0
#define pxo_to_xo_mux		1
#define mxo_to_xo_mux		2
#define gnd_to_xo_mux		3
#define pxo_to_lpa_mux		0
#define cxo_to_lpa_mux		1
#define pll4_to_lpa_mux		2	/* or LPA_PLL0 */
#define gnd_to_lpa_mux		6

/* Test Vector Macros */
#define TEST_TYPE_PER_LS	1
#define TEST_TYPE_PER_HS	2
#define TEST_TYPE_MM_LS		3
#define TEST_TYPE_MM_HS		4
#define TEST_TYPE_LPA		5
#define TEST_TYPE_SHIFT		24
#define TEST_CLK_SEL_MASK	BM(23, 0)
#define TEST_VECTOR(s, t)	(((t) << TEST_TYPE_SHIFT) | BVAL(23, 0, (s)))
#define TEST_PER_LS(s)		TEST_VECTOR((s), TEST_TYPE_PER_LS)
#define TEST_PER_HS(s)		TEST_VECTOR((s), TEST_TYPE_PER_HS)
#define TEST_MM_LS(s)		TEST_VECTOR((s), TEST_TYPE_MM_LS)
#define TEST_MM_HS(s)		TEST_VECTOR((s), TEST_TYPE_MM_HS)
#define TEST_LPA(s)		TEST_VECTOR((s), TEST_TYPE_LPA)

struct pll_rate {
	const uint32_t	l_val;
	const uint32_t	m_val;
	const uint32_t	n_val;
	const uint32_t	vco;
	const uint32_t	post_div;
	const uint32_t	i_bits;
};
#define PLL_RATE(l, m, n, v, d, i) { l, m, n, v, (d>>1), i }
/*
 * Clock frequency definitions and macros
 */
#define MN_MODE_DUAL_EDGE 0x2

/* MD Registers */
#define MD4(m_lsb, m, n_lsb, n) \
		(BVAL((m_lsb+3), m_lsb, m) | BVAL((n_lsb+3), n_lsb, ~(n)))
#define MD8(m_lsb, m, n_lsb, n) \
		(BVAL((m_lsb+7), m_lsb, m) | BVAL((n_lsb+7), n_lsb, ~(n)))
#define MD16(m, n) (BVAL(31, 16, m) | BVAL(15, 0, ~(n)))

/* NS Registers */
#define NS(n_msb, n_lsb, n, m, mde_lsb, d_msb, d_lsb, d, s_msb, s_lsb, s) \
		(BVAL(n_msb, n_lsb, ~(n-m)) \
		| (BVAL((mde_lsb+1), mde_lsb, MN_MODE_DUAL_EDGE) * !!(n)) \
		| BVAL(d_msb, d_lsb, (d-1)) | BVAL(s_msb, s_lsb, s))

#define NS_MM(n_msb, n_lsb, n, m, d_msb, d_lsb, d, s_msb, s_lsb, s) \
		(BVAL(n_msb, n_lsb, ~(n-m)) | BVAL(d_msb, d_lsb, (d-1)) \
		| BVAL(s_msb, s_lsb, s))

#define NS_DIVSRC(d_msb , d_lsb, d, s_msb, s_lsb, s) \
		(BVAL(d_msb, d_lsb, (d-1)) | BVAL(s_msb, s_lsb, s))

#define NS_DIV(d_msb , d_lsb, d) \
		BVAL(d_msb, d_lsb, (d-1))

#define NS_SRC_SEL(s_msb, s_lsb, s) \
		BVAL(s_msb, s_lsb, s)

#define NS_MND_BANKED4(n0_lsb, n1_lsb, n, m, s0_lsb, s1_lsb, s) \
		 (BVAL((n0_lsb+3), n0_lsb, ~(n-m)) \
		| BVAL((n1_lsb+3), n1_lsb, ~(n-m)) \
		| BVAL((s0_lsb+2), s0_lsb, s) \
		| BVAL((s1_lsb+2), s1_lsb, s))

#define NS_MND_BANKED8(n0_lsb, n1_lsb, n, m, s0_lsb, s1_lsb, s) \
		 (BVAL((n0_lsb+7), n0_lsb, ~(n-m)) \
		| BVAL((n1_lsb+7), n1_lsb, ~(n-m)) \
		| BVAL((s0_lsb+2), s0_lsb, s) \
		| BVAL((s1_lsb+2), s1_lsb, s))

#define NS_DIVSRC_BANKED(d0_msb, d0_lsb, d1_msb, d1_lsb, d, \
	s0_msb, s0_lsb, s1_msb, s1_lsb, s) \
		 (BVAL(d0_msb, d0_lsb, (d-1)) | BVAL(d1_msb, d1_lsb, (d-1)) \
		| BVAL(s0_msb, s0_lsb, s) \
		| BVAL(s1_msb, s1_lsb, s))

/* CC Registers */
#define CC(mde_lsb, n) (BVAL((mde_lsb+1), mde_lsb, MN_MODE_DUAL_EDGE) * !!(n))
#define CC_BANKED(mde0_lsb, mde1_lsb, n) \
		((BVAL((mde0_lsb+1), mde0_lsb, MN_MODE_DUAL_EDGE) \
		| BVAL((mde1_lsb+1), mde1_lsb, MN_MODE_DUAL_EDGE)) \
		* !!(n))

static struct msm_xo_voter *xo_pxo, *xo_cxo;

static bool xo_clk_is_local(struct clk *clk)
{
	return false;
}

static int pxo_clk_enable(struct clk *clk)
{
	return msm_xo_mode_vote(xo_pxo, MSM_XO_MODE_ON);
}

static void pxo_clk_disable(struct clk *clk)
{
	msm_xo_mode_vote(xo_pxo, MSM_XO_MODE_OFF);
}

static struct clk_ops clk_ops_pxo = {
	.enable = pxo_clk_enable,
	.disable = pxo_clk_disable,
	.get_rate = fixed_clk_get_rate,
	.is_local = xo_clk_is_local,
};

static struct fixed_clk pxo_clk = {
	.rate = 27000000,
	.c = {
		.dbg_name = "pxo_clk",
		.ops = &clk_ops_pxo,
		CLK_INIT(pxo_clk.c),
	},
};

static int cxo_clk_enable(struct clk *clk)
{
	return msm_xo_mode_vote(xo_cxo, MSM_XO_MODE_ON);
}

static void cxo_clk_disable(struct clk *clk)
{
	msm_xo_mode_vote(xo_cxo, MSM_XO_MODE_OFF);
}

static struct clk_ops clk_ops_cxo = {
	.enable = cxo_clk_enable,
	.disable = cxo_clk_disable,
	.get_rate = fixed_clk_get_rate,
	.is_local = xo_clk_is_local,
};

static struct fixed_clk cxo_clk = {
	.rate = 19200000,
	.c = {
		.dbg_name = "cxo_clk",
		.ops = &clk_ops_cxo,
		CLK_INIT(cxo_clk.c),
	},
};

static struct pll_vote_clk pll8_clk = {
	.rate = 384000000,
	.en_reg = BB_PLL_ENA_SC0_REG,
	.en_mask = BIT(8),
	.status_reg = BB_PLL8_STATUS_REG,
	.status_mask = BIT(16),
	.parent = &pxo_clk.c,
	.c = {
		.dbg_name = "pll8_clk",
		.ops = &clk_ops_pll_vote,
		CLK_INIT(pll8_clk.c),
	},
};

static struct pll_clk pll2_clk = {
	.rate = 800000000,
	.mode_reg = MM_PLL1_MODE_REG,
	.status_reg = MM_PLL1_STATUS_REG,
	.parent = &pxo_clk.c,
	.c = {
		.dbg_name = "pll2_clk",
		.ops = &clk_ops_pll,
		CLK_INIT(pll2_clk.c),
	},
};

static struct pll_clk pll3_clk = {
	.rate = 0, /* TODO: Detect rate dynamically */
	.mode_reg = MM_PLL2_MODE_REG,
	.status_reg = MM_PLL2_STATUS_REG,
	.parent = &pxo_clk.c,
	.c = {
		.dbg_name = "pll3_clk",
		.ops = &clk_ops_pll,
		CLK_INIT(pll3_clk.c),
	},
};

static int pll4_clk_enable(struct clk *clk)
{
	struct msm_rpm_iv_pair iv = { MSM_RPM_ID_PLL_4, 1 };
	return msm_rpm_set_noirq(MSM_RPM_CTX_SET_0, &iv, 1);
}

static void pll4_clk_disable(struct clk *clk)
{
	struct msm_rpm_iv_pair iv = { MSM_RPM_ID_PLL_4, 0 };
	msm_rpm_set_noirq(MSM_RPM_CTX_SET_0, &iv, 1);
}

static struct clk *pll4_clk_get_parent(struct clk *clk)
{
	return &pxo_clk.c;
}

static bool pll4_clk_is_local(struct clk *clk)
{
	return false;
}

static struct clk_ops clk_ops_pll4 = {
	.enable = pll4_clk_enable,
	.disable = pll4_clk_disable,
	.get_rate = fixed_clk_get_rate,
	.get_parent = pll4_clk_get_parent,
	.is_local = pll4_clk_is_local,
};

static struct fixed_clk pll4_clk = {
	.rate = 540672000,
	.c = {
		.dbg_name = "pll4_clk",
		.ops = &clk_ops_pll4,
		CLK_INIT(pll4_clk.c),
	},
};

/*
 * SoC-specific Set-Rate Functions
 */

/* Unlike other clocks, the TV rate is adjusted through PLL
 * re-programming. It is also routed through an MND divider. */
static void set_rate_tv(struct clk_local *clk, struct clk_freq_tbl *nf)
{
	struct pll_rate *rate = nf->extra_freq_data;
	uint32_t pll_mode, pll_config, misc_cc2;

	/* Disable PLL output. */
	pll_mode = readl_relaxed(MM_PLL2_MODE_REG);
	pll_mode &= ~BIT(0);
	writel_relaxed(pll_mode, MM_PLL2_MODE_REG);

	/* Assert active-low PLL reset. */
	pll_mode &= ~BIT(2);
	writel_relaxed(pll_mode, MM_PLL2_MODE_REG);

	/* Program L, M and N values. */
	writel_relaxed(rate->l_val, MM_PLL2_L_VAL_REG);
	writel_relaxed(rate->m_val, MM_PLL2_M_VAL_REG);
	writel_relaxed(rate->n_val, MM_PLL2_N_VAL_REG);

	/* Configure MN counter, post-divide, VCO, and i-bits. */
	pll_config = readl_relaxed(MM_PLL2_CONFIG_REG);
	pll_config &= ~(BM(22, 20) | BM(18, 0));
	pll_config |= rate->n_val ? BIT(22) : 0;
	pll_config |= BVAL(21, 20, rate->post_div);
	pll_config |= BVAL(17, 16, rate->vco);
	pll_config |= rate->i_bits;
	writel_relaxed(pll_config, MM_PLL2_CONFIG_REG);

	/* Configure MND. */
	set_rate_mnd(clk, nf);

	/* Configure hdmi_ref_clk to be equal to the TV clock rate. */
	misc_cc2 = readl_relaxed(MISC_CC2_REG);
	misc_cc2 &= ~(BIT(28)|BM(21, 18));
	misc_cc2 |= (BIT(28)|BVAL(21, 18, (nf->ns_val >> 14) & 0x3));
	writel_relaxed(misc_cc2, MISC_CC2_REG);

	/* De-assert active-low PLL reset. */
	pll_mode |= BIT(2);
	writel_relaxed(pll_mode, MM_PLL2_MODE_REG);

	/* Enable PLL output. */
	pll_mode |= BIT(0);
	writel_relaxed(pll_mode, MM_PLL2_MODE_REG);
}

/*
 * SoC-specific functions required by clock-local driver
 */

/* Update the sys_vdd voltage given a level. */
int soc_update_sys_vdd(enum sys_vdd_level level)
{
	static const int vdd_uv[] = {
		[NONE]    =  500000,
		[LOW]     = 1000000,
		[NOMINAL] = 1100000,
		[HIGH]    = 1200000,
	};

	return rpm_vreg_set_voltage(RPM_VREG_ID_PM8058_S1, RPM_VREG_VOTER3,
				    vdd_uv[level], vdd_uv[HIGH], 1);
}

/* Enable/disable a power rail associated with a clock. */
int soc_set_pwr_rail(struct clk *clk, int enable)
{
	/* Nothing to do */
	return 0;
}

/* Sample clock for 'ticks' reference clock ticks. */
static uint32_t run_measurement(unsigned ticks)
{
	/* Stop counters and set the XO4 counter start value. */
	writel_relaxed(0x0, RINGOSC_TCXO_CTL_REG);
	writel_relaxed(ticks, RINGOSC_TCXO_CTL_REG);

	/* Wait for timer to become ready. */
	while ((readl_relaxed(RINGOSC_STATUS_REG) & BIT(25)) != 0)
		cpu_relax();

	/* Run measurement and wait for completion. */
	writel_relaxed(BIT(20)|ticks, RINGOSC_TCXO_CTL_REG);
	while ((readl_relaxed(RINGOSC_STATUS_REG) & BIT(25)) == 0)
		cpu_relax();

	/* Stop counters. */
	writel_relaxed(0x0, RINGOSC_TCXO_CTL_REG);

	/* Return measured ticks. */
	return readl_relaxed(RINGOSC_STATUS_REG) & BM(24, 0);
}

/* Perform a hardware rate measurement for a given clock.
   FOR DEBUG USE ONLY: Measurements take ~15 ms! */
static int __soc_clk_measure_rate(u32 test_vector)
{
	unsigned long flags;
	uint32_t clk_sel, pdm_reg_backup, ringosc_reg_backup;
	uint64_t raw_count_short, raw_count_full;
	int ret;

	spin_lock_irqsave(&local_clock_reg_lock, flags);

	/* Program the test vector. */
	clk_sel = test_vector & TEST_CLK_SEL_MASK;
	switch (test_vector >> TEST_TYPE_SHIFT) {
	case TEST_TYPE_PER_LS:
		writel_relaxed(0x4030D00|BVAL(7, 0, clk_sel), CLK_TEST_REG);
		break;
	case TEST_TYPE_PER_HS:
		writel_relaxed(0x4020000|BVAL(16, 10, clk_sel), CLK_TEST_REG);
		break;
	case TEST_TYPE_MM_LS:
		writel_relaxed(0x4030D97, CLK_TEST_REG);
		writel_relaxed(BVAL(6, 1, clk_sel)|BIT(0), DBG_CFG_REG_LS_REG);
		break;
	case TEST_TYPE_MM_HS:
		writel_relaxed(0x402B800, CLK_TEST_REG);
		writel_relaxed(BVAL(6, 1, clk_sel)|BIT(0), DBG_CFG_REG_HS_REG);
		break;
	case TEST_TYPE_LPA:
		writel_relaxed(0x4030D98, CLK_TEST_REG);
		writel_relaxed(BVAL(6, 1, clk_sel)|BIT(0),
			       LCC_CLK_LS_DEBUG_CFG_REG);
		break;
	default:
		ret = -EPERM;
		goto err;
	}
	/* Make sure test vector is set before starting measurements. */
	dsb();

	/* Enable CXO/4 and RINGOSC branch and root. */
	pdm_reg_backup = readl_relaxed(PDM_CLK_NS_REG);
	ringosc_reg_backup = readl_relaxed(RINGOSC_NS_REG);
	writel_relaxed(0x2898, PDM_CLK_NS_REG);
	writel_relaxed(0xA00, RINGOSC_NS_REG);

	/*
	 * The ring oscillator counter will not reset if the measured clock
	 * is not running.  To detect this, run a short measurement before
	 * the full measurement.  If the raw results of the two are the same
	 * then the clock must be off.
	 */

	/* Run a short measurement. (~1 ms) */
	raw_count_short = run_measurement(0x1000);
	/* Run a full measurement. (~14 ms) */
	raw_count_full = run_measurement(0x10000);

	writel_relaxed(ringosc_reg_backup, RINGOSC_NS_REG);
	writel_relaxed(pdm_reg_backup, PDM_CLK_NS_REG);

	/* Return 0 if the clock is off. */
	if (raw_count_full == raw_count_short)
		ret = 0;
	else {
		/* Compute rate in Hz. */
		raw_count_full = ((raw_count_full * 10) + 15) * 4800000;
		do_div(raw_count_full, ((0x10000 * 10) + 35));
		ret = (int)raw_count_full;
	}

	/* Route dbg_hs_clk to PLLTEST.  300mV single-ended amplitude. */
	writel_relaxed(0x3CF8, PLLTEST_PAD_CFG_REG);
err:
	spin_unlock_irqrestore(&local_clock_reg_lock, flags);

	return ret;
}

static int soc_clk_measure_rate(struct clk *clk)
{
	return __soc_clk_measure_rate(to_local(clk)->b.test_vector);
}

static int branch_clk_measure_rate(struct clk *clk)
{
	return __soc_clk_measure_rate(to_branch_clk(clk)->b.test_vector);
}

/* Implementation for clk_set_flags(). */
int soc_clk_set_flags(struct clk *clk, unsigned flags)
{
	return -EPERM;
}

static int soc_clk_reset(struct clk *clk, enum clk_reset_action action)
{
	return branch_reset(&to_local(clk)->b, action);
}

static struct clk_ops soc_clk_ops_8x60 = {
	.enable = local_clk_enable,
	.disable = local_clk_disable,
	.auto_off = local_clk_auto_off,
	.set_rate = local_clk_set_rate,
	.set_min_rate = local_clk_set_min_rate,
	.set_max_rate = local_clk_set_max_rate,
	.get_rate = local_clk_get_rate,
	.list_rate = local_clk_list_rate,
	.is_enabled = local_clk_is_enabled,
	.round_rate = local_clk_round_rate,
	.reset = soc_clk_reset,
	.set_flags = soc_clk_set_flags,
	.measure_rate = soc_clk_measure_rate,
	.is_local = local_clk_is_local,
	.get_parent = local_clk_get_parent,
};

static struct clk_ops clk_ops_branch = {
	.enable = branch_clk_enable,
	.disable = branch_clk_disable,
	.auto_off = branch_clk_auto_off,
	.is_enabled = branch_clk_is_enabled,
	.reset = branch_clk_reset,
	.set_flags = soc_clk_set_flags,
	.is_local = local_clk_is_local,
	.measure_rate = branch_clk_measure_rate,
	.get_parent = branch_clk_get_parent,
	.set_parent = branch_clk_set_parent,
};

static struct clk_ops clk_ops_reset = {
	.reset = branch_clk_reset,
	.is_local = local_clk_is_local,
};

/*
 * Clock Descriptions
 */

/* AXI Interfaces */
static struct branch_clk gmem_axi_clk = {
	.b = {
		.en_reg = MAXI_EN_REG,
		.en_mask = BIT(24),
		.halt_reg = DBG_BUS_VEC_E_REG,
		.halt_check = HALT,
		.halt_bit = 6,
		.test_vector = TEST_MM_HS(0x11),
	},
	.c = {
		.dbg_name = "gmem_axi_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gmem_axi_clk.c),
	},
};

static struct branch_clk ijpeg_axi_clk = {
	.b = {
		.en_reg = MAXI_EN_REG,
		.en_mask = BIT(21),
		.reset_reg = SW_RESET_AXI_REG,
		.reset_mask = BIT(14),
		.halt_reg = DBG_BUS_VEC_E_REG,
		.halt_check = HALT,
		.halt_bit = 4,
		.test_vector = TEST_MM_HS(0x12),
	},
	.c = {
		.dbg_name = "ijpeg_axi_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(ijpeg_axi_clk.c),
	},
};

static struct branch_clk imem_axi_clk = {
	.b = {
		.en_reg = MAXI_EN_REG,
		.en_mask = BIT(22),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(10),
		.halt_reg = DBG_BUS_VEC_E_REG,
		.halt_check = HALT,
		.halt_bit = 7,
		.test_vector = TEST_MM_HS(0x13),
	},
	.c = {
		.dbg_name = "imem_axi_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(imem_axi_clk.c),
	},
};

static struct branch_clk jpegd_axi_clk = {
	.b = {
		.en_reg = MAXI_EN_REG,
		.en_mask = BIT(25),
		.halt_reg = DBG_BUS_VEC_E_REG,
		.halt_check = HALT,
		.halt_bit = 5,
		.test_vector = TEST_MM_HS(0x14),
	},
	.c = {
		.dbg_name = "jpegd_axi_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(jpegd_axi_clk.c),
	},
};

static struct branch_clk mdp_axi_clk = {
	.b = {
		.en_reg = MAXI_EN_REG,
		.en_mask = BIT(23),
		.reset_reg = SW_RESET_AXI_REG,
		.reset_mask = BIT(13),
		.halt_reg = DBG_BUS_VEC_E_REG,
		.halt_check = HALT,
		.halt_bit = 8,
		.test_vector = TEST_MM_HS(0x15),
	},
	.c = {
		.dbg_name = "mdp_axi_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(mdp_axi_clk.c),
	},
};

static struct branch_clk vcodec_axi_clk = {
	.b = {
		.en_reg = MAXI_EN_REG,
		.en_mask = BIT(19),
		.reset_reg = SW_RESET_AXI_REG,
		.reset_mask = BIT(4)|BIT(5),
		.halt_reg = DBG_BUS_VEC_E_REG,
		.halt_check = HALT,
		.halt_bit = 3,
		.test_vector = TEST_MM_HS(0x17),
	},
	.c = {
		.dbg_name = "vcodec_axi_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(vcodec_axi_clk.c),
	},
};

static struct branch_clk vfe_axi_clk = {
	.b = {
		.en_reg = MAXI_EN_REG,
		.en_mask = BIT(18),
		.reset_reg = SW_RESET_AXI_REG,
		.reset_mask = BIT(9),
		.halt_reg = DBG_BUS_VEC_E_REG,
		.halt_check = HALT,
		.halt_bit = 0,
		.test_vector = TEST_MM_HS(0x18),
	},
	.c = {
		.dbg_name = "vfe_axi_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(vfe_axi_clk.c),
	},
};

static struct branch_clk rot_axi_clk = {
	.b = {
		.reset_reg = SW_RESET_AXI_REG,
		.reset_mask = BIT(6),
	},
	.c = {
		.dbg_name = "rot_axi_clk",
		.ops = &clk_ops_reset,
		CLK_INIT(rot_axi_clk.c),
	},
};

static struct branch_clk vpe_axi_clk = {
	.b = {
		.reset_reg = SW_RESET_AXI_REG,
		.reset_mask = BIT(15),
	},
	.c = {
		.dbg_name = "vpe_axi_clk",
		.ops = &clk_ops_reset,
		CLK_INIT(vpe_axi_clk.c),
	},
};

/* AHB Interfaces */
static struct branch_clk amp_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(24),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 18,
		.test_vector = TEST_MM_LS(0x06),
	},
	.c = {
		.dbg_name = "amp_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(amp_p_clk.c),
	},
};

static struct branch_clk csi0_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(7),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(17),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 16,
		.test_vector = TEST_MM_LS(0x07),
	},
	.c = {
		.dbg_name = "csi0_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(csi0_p_clk.c),
	},
};

static struct branch_clk csi1_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(20),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(16),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 17,
		.test_vector = TEST_MM_LS(0x08),
	},
	.c = {
		.dbg_name = "csi1_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(csi1_p_clk.c),
	},
};

static struct branch_clk dsi_m_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(9),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(6),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 19,
		.test_vector = TEST_MM_LS(0x09),
	},
	.c = {
		.dbg_name = "dsi_m_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(dsi_m_p_clk.c),
	},
};

static struct branch_clk dsi_s_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(18),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(5),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 20,
		.test_vector = TEST_MM_LS(0x0A),
	},
	.c = {
		.dbg_name = "dsi_s_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(dsi_s_p_clk.c),
	},
};

static struct branch_clk gfx2d0_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(19),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(12),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 2,
		.test_vector = TEST_MM_LS(0x0C),
	},
	.c = {
		.dbg_name = "gfx2d0_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gfx2d0_p_clk.c),
	},
};

static struct branch_clk gfx2d1_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(2),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(11),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 3,
		.test_vector = TEST_MM_LS(0x0D),
	},
	.c = {
		.dbg_name = "gfx2d1_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gfx2d1_p_clk.c),
	},
};

static struct branch_clk gfx3d_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(3),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(10),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 4,
		.test_vector = TEST_MM_LS(0x0E),
	},
	.c = {
		.dbg_name = "gfx3d_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gfx3d_p_clk.c),
	},
};

static struct branch_clk hdmi_m_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(14),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(9),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 5,
		.test_vector = TEST_MM_LS(0x0F),
	},
	.c = {
		.dbg_name = "hdmi_m_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(hdmi_m_p_clk.c),
	},
};

static struct branch_clk hdmi_s_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(4),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(9),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 6,
		.test_vector = TEST_MM_LS(0x10),
	},
	.c = {
		.dbg_name = "hdmi_s_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(hdmi_s_p_clk.c),
	},
};

static struct branch_clk ijpeg_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(5),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(7),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 9,
		.test_vector = TEST_MM_LS(0x11),
	},
	.c = {
		.dbg_name = "ijepg_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(ijpeg_p_clk.c),
	},
};

static struct branch_clk imem_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(6),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(8),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 10,
		.test_vector = TEST_MM_LS(0x12),
	},
	.c = {
		.dbg_name = "imem_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(imem_p_clk.c),
	},
};

static struct branch_clk jpegd_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(21),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(4),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 7,
		.test_vector = TEST_MM_LS(0x13),
	},
	.c = {
		.dbg_name = "jpegd_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(jpegd_p_clk.c),
	},
};

static struct branch_clk mdp_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(10),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(3),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 11,
		.test_vector = TEST_MM_LS(0x14),
	},
	.c = {
		.dbg_name = "mdp_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(mdp_p_clk.c),
	},
};

static struct branch_clk rot_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(12),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(2),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 13,
		.test_vector = TEST_MM_LS(0x16),
	},
	.c = {
		.dbg_name = "rot_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(rot_p_clk.c),
	},
};

static struct branch_clk smmu_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(15),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 22,
		.test_vector = TEST_MM_LS(0x18),
	},
	.c = {
		.dbg_name = "smmu_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(smmu_p_clk.c),
	},
};

static struct branch_clk tv_enc_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(25),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(15),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 23,
		.test_vector = TEST_MM_LS(0x19),
	},
	.c = {
		.dbg_name = "tv_enc_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(tv_enc_p_clk.c),
	},
};

static struct branch_clk vcodec_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(11),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(1),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 12,
		.test_vector = TEST_MM_LS(0x1A),
	},
	.c = {
		.dbg_name = "vcodec_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(vcodec_p_clk.c),
	},
};

static struct branch_clk vfe_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(13),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(0),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 14,
		.test_vector = TEST_MM_LS(0x1B),
	},
	.c = {
		.dbg_name = "vfe_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(vfe_p_clk.c),
	},
};

static struct branch_clk vpe_p_clk = {
	.b = {
		.en_reg = AHB_EN_REG,
		.en_mask = BIT(16),
		.reset_reg = SW_RESET_AHB_REG,
		.reset_mask = BIT(14),
		.halt_reg = DBG_BUS_VEC_F_REG,
		.halt_check = HALT,
		.halt_bit = 15,
		.test_vector = TEST_MM_LS(0x1C),
	},
	.c = {
		.dbg_name = "vpe_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(vpe_p_clk.c),
	},
};

/*
 * Peripheral Clocks
 */
#define CLK_GSBI_UART(i, n, h_r, h_c, h_b, tv) \
	struct clk_local i##_clk = { \
		.b = { \
			.en_reg = GSBIn_UART_APPS_NS_REG(n), \
			.en_mask = BIT(9), \
			.reset_reg = GSBIn_RESET_REG(n), \
			.reset_mask = BIT(0), \
			.halt_reg = h_r, \
			.halt_check = h_c, \
			.halt_bit = h_b, \
			.test_vector = tv, \
		}, \
		.ns_reg = GSBIn_UART_APPS_NS_REG(n), \
		.md_reg = GSBIn_UART_APPS_MD_REG(n), \
		.root_en_mask = BIT(11), \
		.ns_mask = (BM(31, 16) | BM(6, 0)), \
		.set_rate = set_rate_mnd, \
		.freq_tbl = clk_tbl_gsbi_uart, \
		.current_freq = &local_dummy_freq, \
		.c = { \
			.dbg_name = #i "_clk", \
			.ops = &soc_clk_ops_8x60, \
			.flags = CLKFLAG_AUTO_OFF, \
			CLK_INIT(i##_clk.c), \
		}, \
	}
#define F_GSBI_UART(f, s, d, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD16(m, n), \
		.ns_val = NS(31, 16, n, m, 5, 4, 3, d, 2, 0, s##_to_bb_mux), \
		.mnd_en_mask = BIT(8) * !!(n), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_gsbi_uart[] = {
	F_GSBI_UART(       0, gnd,  1,  0,   0, NONE),
	F_GSBI_UART( 1843200, pll8, 1,  3, 625, LOW),
	F_GSBI_UART( 3686400, pll8, 1,  6, 625, LOW),
	F_GSBI_UART( 7372800, pll8, 1, 12, 625, LOW),
	F_GSBI_UART(14745600, pll8, 1, 24, 625, LOW),
	F_GSBI_UART(16000000, pll8, 4,  1,   6, LOW),
	F_GSBI_UART(24000000, pll8, 4,  1,   4, LOW),
	F_GSBI_UART(32000000, pll8, 4,  1,   3, LOW),
	F_GSBI_UART(40000000, pll8, 1,  5,  48, NOMINAL),
	F_GSBI_UART(46400000, pll8, 1, 29, 240, NOMINAL),
	F_GSBI_UART(48000000, pll8, 4,  1,   2, NOMINAL),
	F_GSBI_UART(51200000, pll8, 1,  2,  15, NOMINAL),
	F_GSBI_UART(56000000, pll8, 1,  7,  48, NOMINAL),
	F_GSBI_UART(58982400, pll8, 1, 96, 625, NOMINAL),
	F_GSBI_UART(64000000, pll8, 2,  1,   3, NOMINAL),
	F_END
};

static CLK_GSBI_UART(gsbi1_uart,   1, CLK_HALT_CFPB_STATEA_REG, HALT, 10,
			TEST_PER_LS(0x3E));
static CLK_GSBI_UART(gsbi2_uart,   2, CLK_HALT_CFPB_STATEA_REG, HALT,  6,
			TEST_PER_LS(0x42));
static CLK_GSBI_UART(gsbi3_uart,   3, CLK_HALT_CFPB_STATEA_REG, HALT,  2,
			TEST_PER_LS(0x46));
static CLK_GSBI_UART(gsbi4_uart,   4, CLK_HALT_CFPB_STATEB_REG, HALT, 26,
			TEST_PER_LS(0x4A));
static CLK_GSBI_UART(gsbi5_uart,   5, CLK_HALT_CFPB_STATEB_REG, HALT, 22,
			TEST_PER_LS(0x4E));
static CLK_GSBI_UART(gsbi6_uart,   6, CLK_HALT_CFPB_STATEB_REG, HALT, 18,
			TEST_PER_LS(0x52));
static CLK_GSBI_UART(gsbi7_uart,   7, CLK_HALT_CFPB_STATEB_REG, HALT, 14,
			TEST_PER_LS(0x56));
static CLK_GSBI_UART(gsbi8_uart,   8, CLK_HALT_CFPB_STATEB_REG, HALT, 10,
			TEST_PER_LS(0x5A));
static CLK_GSBI_UART(gsbi9_uart,   9, CLK_HALT_CFPB_STATEB_REG, HALT,  6,
			TEST_PER_LS(0x5E));
static CLK_GSBI_UART(gsbi10_uart, 10, CLK_HALT_CFPB_STATEB_REG, HALT,  2,
			TEST_PER_LS(0x62));
static CLK_GSBI_UART(gsbi11_uart, 11, CLK_HALT_CFPB_STATEC_REG, HALT, 17,
			TEST_PER_LS(0x66));
static CLK_GSBI_UART(gsbi12_uart, 12, CLK_HALT_CFPB_STATEC_REG, HALT, 13,
			TEST_PER_LS(0x6A));

#define CLK_GSBI_QUP(i, n, h_r, h_c, h_b, tv) \
	struct clk_local i##_clk = { \
		.b = { \
			.en_reg = GSBIn_QUP_APPS_NS_REG(n), \
			.en_mask = BIT(9), \
			.reset_reg = GSBIn_RESET_REG(n), \
			.reset_mask = BIT(0), \
			.halt_reg = h_r, \
			.halt_check = h_c, \
			.halt_bit = h_b, \
			.test_vector = tv, \
		}, \
		.ns_reg = GSBIn_QUP_APPS_NS_REG(n), \
		.md_reg = GSBIn_QUP_APPS_MD_REG(n), \
		.root_en_mask = BIT(11), \
		.ns_mask = (BM(23, 16) | BM(6, 0)), \
		.set_rate = set_rate_mnd, \
		.freq_tbl = clk_tbl_gsbi_qup, \
		.current_freq = &local_dummy_freq, \
		.c = { \
			.dbg_name = #i "_clk", \
			.ops = &soc_clk_ops_8x60, \
			.flags = CLKFLAG_AUTO_OFF, \
			CLK_INIT(i##_clk.c), \
		}, \
	}
#define F_GSBI_QUP(f, s, d, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD8(16, m, 0, n), \
		.ns_val = NS(23, 16, n, m, 5, 4, 3, d, 2, 0, s##_to_bb_mux), \
		.mnd_en_mask = BIT(8) * !!(n), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_gsbi_qup[] = {
	F_GSBI_QUP(       0, gnd,   1, 0,  0, NONE),
	F_GSBI_QUP( 1100000, pxo,  1, 2, 49, LOW),
	F_GSBI_QUP( 5400000, pxo,  1, 1,  5, LOW),
	F_GSBI_QUP(10800000, pxo,  1, 2,  5, LOW),
	F_GSBI_QUP(15060000, pll8, 1, 2, 51, LOW),
	F_GSBI_QUP(24000000, pll8, 4, 1,  4, LOW),
	F_GSBI_QUP(25600000, pll8, 1, 1, 15, NOMINAL),
	F_GSBI_QUP(27000000, pxo,  1, 0,  0, NOMINAL),
	F_GSBI_QUP(48000000, pll8, 4, 1,  2, NOMINAL),
	F_GSBI_QUP(51200000, pll8, 1, 2, 15, NOMINAL),
	F_END
};

static CLK_GSBI_QUP(gsbi1_qup,   1, CLK_HALT_CFPB_STATEA_REG, HALT,  9,
			TEST_PER_LS(0x3F));
static CLK_GSBI_QUP(gsbi2_qup,   2, CLK_HALT_CFPB_STATEA_REG, HALT,  4,
			TEST_PER_LS(0x44));
static CLK_GSBI_QUP(gsbi3_qup,   3, CLK_HALT_CFPB_STATEA_REG, HALT,  0,
			TEST_PER_LS(0x48));
static CLK_GSBI_QUP(gsbi4_qup,   4, CLK_HALT_CFPB_STATEB_REG, HALT, 24,
			TEST_PER_LS(0x4C));
static CLK_GSBI_QUP(gsbi5_qup,   5, CLK_HALT_CFPB_STATEB_REG, HALT, 20,
			TEST_PER_LS(0x50));
static CLK_GSBI_QUP(gsbi6_qup,   6, CLK_HALT_CFPB_STATEB_REG, HALT, 16,
			TEST_PER_LS(0x54));
static CLK_GSBI_QUP(gsbi7_qup,   7, CLK_HALT_CFPB_STATEB_REG, HALT, 12,
			TEST_PER_LS(0x58));
static CLK_GSBI_QUP(gsbi8_qup,   8, CLK_HALT_CFPB_STATEB_REG, HALT,  8,
			TEST_PER_LS(0x5C));
static CLK_GSBI_QUP(gsbi9_qup,   9, CLK_HALT_CFPB_STATEB_REG, HALT,  4,
			TEST_PER_LS(0x60));
static CLK_GSBI_QUP(gsbi10_qup, 10, CLK_HALT_CFPB_STATEB_REG, HALT,  0,
			TEST_PER_LS(0x64));
static CLK_GSBI_QUP(gsbi11_qup, 11, CLK_HALT_CFPB_STATEC_REG, HALT, 15,
			TEST_PER_LS(0x68));
static CLK_GSBI_QUP(gsbi12_qup, 12, CLK_HALT_CFPB_STATEC_REG, HALT, 11,
			TEST_PER_LS(0x6C));

#define F_PDM(f, s, d, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.ns_val = NS_SRC_SEL(1, 0, s##_to_xo_mux), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_pdm[] = {
	F_PDM(       0, gnd, 1, NONE),
	F_PDM(27000000, pxo, 1, LOW),
	F_END
};

struct clk_local pdm_clk = {
	.b = {
		.en_reg = PDM_CLK_NS_REG,
		.en_mask = BIT(9),
		.reset_reg = PDM_CLK_NS_REG,
		.reset_mask = BIT(12),
		.halt_reg = CLK_HALT_CFPB_STATEC_REG,
		.halt_check = HALT,
		.halt_bit = 3,
	},
	.ns_reg = PDM_CLK_NS_REG,
	.root_en_mask = BIT(11),
	.ns_mask = BM(1, 0),
	.set_rate = set_rate_nop,
	.freq_tbl = clk_tbl_pdm,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "pdm_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(pdm_clk.c),
	},
};

static struct branch_clk pmem_clk = {
	.b = {
		.en_reg = PMEM_ACLK_CTL_REG,
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_DFAB_STATE_REG,
		.halt_check = HALT,
		.halt_bit = 20,
		.test_vector = TEST_PER_LS(0x26),
	},
	.c = {
		.dbg_name = "pmem_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(pmem_clk.c),
	},
};

#define F_PRNG(f, s, d, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.ns_val = NS_DIVSRC(6, 3, d, 2, 0, s##_to_bb_mux), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_prng[] = {
	F_PRNG(64000000, pll8,  6, NOMINAL),
	F_END
};

struct clk_local prng_clk = {
	.b = {
		.en_reg = SC0_U_CLK_BRANCH_ENA_VOTE_REG,
		.en_mask = BIT(10),
		.reset_reg = PRNG_CLK_NS_REG,
		.reset_mask = BIT(12),
		.halt_reg = CLK_HALT_SFPB_MISC_STATE_REG,
		.halt_check = HALT_VOTED,
		.halt_bit = 10,
		.test_vector = TEST_PER_LS(0x7D),
	},
	.ns_reg = PRNG_CLK_NS_REG,
	.ns_mask = (BM(6, 3) | BM(2, 0)),
	.set_rate = set_rate_nop,
	.freq_tbl = clk_tbl_prng,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "prng_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(prng_clk.c),
	},
};

#define CLK_SDC(i, n, h_r, h_c, h_b, tv) \
	struct clk_local i##_clk = { \
		.b = { \
			.en_reg = SDCn_APPS_CLK_NS_REG(n), \
			.en_mask = BIT(9), \
			.reset_reg = SDCn_RESET_REG(n), \
			.reset_mask = BIT(0), \
			.halt_reg = h_r, \
			.halt_check = h_c, \
			.halt_bit = h_b, \
			.test_vector = tv, \
		}, \
		.ns_reg = SDCn_APPS_CLK_NS_REG(n), \
		.md_reg = SDCn_APPS_CLK_MD_REG(n), \
		.root_en_mask = BIT(11), \
		.ns_mask = (BM(23, 16) | BM(6, 0)), \
		.set_rate = set_rate_mnd, \
		.freq_tbl = clk_tbl_sdc, \
		.current_freq = &local_dummy_freq, \
		.c = { \
			.dbg_name = #i "_clk", \
			.ops = &soc_clk_ops_8x60, \
			.flags = CLKFLAG_AUTO_OFF, \
			CLK_INIT(i##_clk.c), \
		}, \
	}
#define F_SDC(f, s, d, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD8(16, m, 0, n), \
		.ns_val = NS(23, 16, n, m, 5, 4, 3, d, 2, 0, s##_to_bb_mux), \
		.mnd_en_mask = BIT(8) * !!(n), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_sdc[] = {
	F_SDC(       0, gnd,   1, 0,   0, NONE),
	F_SDC(  144000, pxo,   3, 2, 125, LOW),
	F_SDC(  400000, pll8,  4, 1, 240, LOW),
	F_SDC(16000000, pll8,  4, 1,   6, LOW),
	F_SDC(17070000, pll8,  1, 2,  45, LOW),
	F_SDC(20210000, pll8,  1, 1,  19, LOW),
	F_SDC(24000000, pll8,  4, 1,   4, LOW),
	F_SDC(48000000, pll8,  4, 1,   2, NOMINAL),
	F_END
};

static CLK_SDC(sdc1, 1, CLK_HALT_DFAB_STATE_REG, HALT, 6, TEST_PER_LS(0x13));
static CLK_SDC(sdc2, 2, CLK_HALT_DFAB_STATE_REG, HALT, 5, TEST_PER_LS(0x15));
static CLK_SDC(sdc3, 3, CLK_HALT_DFAB_STATE_REG, HALT, 4, TEST_PER_LS(0x17));
static CLK_SDC(sdc4, 4, CLK_HALT_DFAB_STATE_REG, HALT, 3, TEST_PER_LS(0x19));
static CLK_SDC(sdc5, 5, CLK_HALT_DFAB_STATE_REG, HALT, 2, TEST_PER_LS(0x1B));

#define F_TSIF_REF(f, s, d, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD16(m, n), \
		.ns_val = NS(31, 16, n, m, 5, 4, 3, d, 2, 0, s##_to_bb_mux), \
		.mnd_en_mask = BIT(8) * !!(n), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_tsif_ref[] = {
	F_TSIF_REF(     0, gnd,  1, 0,   0, NONE),
	F_TSIF_REF(105000, pxo,  1, 1, 256, LOW),
	F_END
};

struct clk_local tsif_ref_clk = {
	.b = {
		.en_reg = TSIF_REF_CLK_NS_REG,
		.en_mask = BIT(9),
		.halt_reg = CLK_HALT_CFPB_STATEC_REG,
		.halt_check = HALT,
		.halt_bit = 5,
		.test_vector = TEST_PER_LS(0x91),
	},
	.ns_reg = TSIF_REF_CLK_NS_REG,
	.md_reg = TSIF_REF_CLK_MD_REG,
	.root_en_mask = BIT(11),
	.ns_mask = (BM(31, 16) | BM(6, 0)),
	.set_rate = set_rate_mnd,
	.freq_tbl = clk_tbl_tsif_ref,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "tsif_ref_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(tsif_ref_clk.c),
	},
};

#define F_TSSC(f, s, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.ns_val = NS_SRC_SEL(1, 0, s##_to_xo_mux), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_tssc[] = {
	F_TSSC(       0, gnd, NONE),
	F_TSSC(27000000, pxo, LOW),
	F_END
};

struct clk_local tssc_clk = {
	.b = {
		.en_reg = TSSC_CLK_CTL_REG,
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEC_REG,
		.halt_check = HALT,
		.halt_bit = 4,
		.test_vector = TEST_PER_LS(0x94),
	},
	.ns_reg = TSSC_CLK_CTL_REG,
	.ns_mask = BM(1, 0),
	.set_rate = set_rate_nop,
	.freq_tbl = clk_tbl_tssc,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "tssc_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(tssc_clk.c),
	},
};

#define F_USB(f, s, d, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD8(16, m, 0, n), \
		.ns_val = NS(23, 16, n, m, 5, 4, 3, d, 2, 0, s##_to_bb_mux), \
		.mnd_en_mask = BIT(8) * !!(n), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_usb[] = {
	F_USB(       0, gnd,  1, 0,  0, NONE),
	F_USB(60000000, pll8, 1, 5, 32, NOMINAL),
	F_END
};

struct clk_local usb_hs1_xcvr_clk = {
	.b = {
		.en_reg = USB_HS1_XCVR_FS_CLK_NS_REG,
		.en_mask = BIT(9),
		.reset_reg = USB_HS1_RESET_REG,
		.reset_mask = BIT(0),
		.halt_reg = CLK_HALT_DFAB_STATE_REG,
		.halt_check = HALT,
		.halt_bit = 0,
		.test_vector = TEST_PER_LS(0x85),
	},
	.ns_reg = USB_HS1_XCVR_FS_CLK_NS_REG,
	.md_reg = USB_HS1_XCVR_FS_CLK_MD_REG,
	.root_en_mask = BIT(11),
	.ns_mask = (BM(23, 16) | BM(6, 0)),
	.set_rate = set_rate_mnd,
	.freq_tbl = clk_tbl_usb,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "usb_hs1_xcvr_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(usb_hs1_xcvr_clk.c),
	},
};

static struct branch_clk usb_phy0_clk = {
	.b = {
		.reset_reg = USB_PHY0_RESET_REG,
		.reset_mask = BIT(0),
	},
	.c = {
		.dbg_name = "usb_phy0_clk",
		.ops = &clk_ops_reset,
		CLK_INIT(usb_phy0_clk.c),
	},
};

#define CLK_USB_FS(i, n) \
	struct clk_local i##_clk = { \
		.ns_reg = USB_FSn_XCVR_FS_CLK_NS_REG(n), \
		.b = { \
			.en_reg = USB_FSn_XCVR_FS_CLK_NS_REG(n), \
		}, \
		.md_reg = USB_FSn_XCVR_FS_CLK_MD_REG(n), \
		.root_en_mask = BIT(11), \
		.ns_mask = (BM(23, 16) | BM(6, 0)), \
		.set_rate = set_rate_mnd, \
		.freq_tbl = clk_tbl_usb, \
		.current_freq = &local_dummy_freq, \
		.c = { \
			.dbg_name = #i "_clk", \
			.ops = &soc_clk_ops_8x60, \
			.flags = CLKFLAG_AUTO_OFF, \
			CLK_INIT(i##_clk.c), \
		}, \
	}

static CLK_USB_FS(usb_fs1_src, 1);
static struct branch_clk usb_fs1_xcvr_clk = {
	.b = {
		.en_reg = USB_FSn_XCVR_FS_CLK_NS_REG(1),
		.en_mask = BIT(9),
		.reset_reg = USB_FSn_RESET_REG(1),
		.reset_mask = BIT(1),
		.halt_reg = CLK_HALT_CFPB_STATEA_REG,
		.halt_check = HALT,
		.halt_bit = 15,
		.test_vector = TEST_PER_LS(0x8B),
	},
	.parent = &usb_fs1_src_clk.c,
	.c = {
		.dbg_name = "usb_fs1_xcvr_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(usb_fs1_xcvr_clk.c),
	},
};

static struct branch_clk usb_fs1_sys_clk = {
	.b = {
		.en_reg = USB_FSn_SYSTEM_CLK_CTL_REG(1),
		.en_mask = BIT(4),
		.reset_reg = USB_FSn_RESET_REG(1),
		.reset_mask = BIT(0),
		.halt_reg = CLK_HALT_CFPB_STATEA_REG,
		.halt_check = HALT,
		.halt_bit = 16,
		.test_vector = TEST_PER_LS(0x8A),
	},
	.parent = &usb_fs1_src_clk.c,
	.c = {
		.dbg_name = "usb_fs1_sys_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(usb_fs1_sys_clk.c),
	},
};

static CLK_USB_FS(usb_fs2_src, 2);
static struct branch_clk usb_fs2_xcvr_clk = {
	.b = {
		.en_reg = USB_FSn_XCVR_FS_CLK_NS_REG(2),
		.en_mask = BIT(9),
		.reset_reg = USB_FSn_RESET_REG(2),
		.reset_mask = BIT(1),
		.halt_reg = CLK_HALT_CFPB_STATEA_REG,
		.halt_check = HALT,
		.halt_bit = 12,
		.test_vector = TEST_PER_LS(0x8E),
	},
	.parent = &usb_fs2_src_clk.c,
	.c = {
		.dbg_name = "usb_fs2_xcvr_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(usb_fs2_xcvr_clk.c),
	},
};

static struct branch_clk usb_fs2_sys_clk = {
	.b = {
		.en_reg = USB_FSn_SYSTEM_CLK_CTL_REG(2),
		.en_mask = BIT(4),
		.reset_reg = USB_FSn_RESET_REG(2),
		.reset_mask = BIT(0),
		.halt_reg = CLK_HALT_CFPB_STATEA_REG,
		.halt_check = HALT,
		.halt_bit = 13,
		.test_vector = TEST_PER_LS(0x8D),
	},
	.parent = &usb_fs2_src_clk.c,
	.c = {
		.dbg_name = "usb_fs2_sys_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(usb_fs2_sys_clk.c),
	},
};

/* Fast Peripheral Bus Clocks */
static struct branch_clk ce2_p_clk = {
	.b = {
		.en_reg = CE2_HCLK_CTL_REG,
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEC_REG,
		.halt_check = HALT,
		.halt_bit = 0,
		.test_vector = TEST_PER_LS(0x93),
	},
	.parent = &pxo_clk.c,
	.c = {
		.dbg_name = "ce2_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(ce2_p_clk.c),
	},
};

static struct branch_clk gsbi1_p_clk = {
	.b = {
		.en_reg = GSBIn_HCLK_CTL_REG(1),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEA_REG,
		.halt_check = HALT,
		.halt_bit = 11,
		.test_vector = TEST_PER_LS(0x3D),
	},
	.c = {
		.dbg_name = "gsbi1_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gsbi1_p_clk.c),
	},
};

static struct branch_clk gsbi2_p_clk = {
	.b = {
		.en_reg = GSBIn_HCLK_CTL_REG(2),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEA_REG,
		.halt_check = HALT,
		.halt_bit = 7,
		.test_vector = TEST_PER_LS(0x41),
	},
	.c = {
		.dbg_name = "gsbi2_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gsbi2_p_clk.c),
	},
};

static struct branch_clk gsbi3_p_clk = {
	.b = {
		.en_reg = GSBIn_HCLK_CTL_REG(3),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEA_REG,
		.halt_check = HALT,
		.halt_bit = 3,
		.test_vector = TEST_PER_LS(0x45),
	},
	.c = {
		.dbg_name = "gsbi3_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gsbi3_p_clk.c),
	},
};

static struct branch_clk gsbi4_p_clk = {
	.b = {
		.en_reg = GSBIn_HCLK_CTL_REG(4),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEB_REG,
		.halt_check = HALT,
		.halt_bit = 27,
		.test_vector = TEST_PER_LS(0x49),
	},
	.c = {
		.dbg_name = "gsbi4_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gsbi4_p_clk.c),
	},
};

static struct branch_clk gsbi5_p_clk = {
	.b = {
		.en_reg = GSBIn_HCLK_CTL_REG(5),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEB_REG,
		.halt_check = HALT,
		.halt_bit = 23,
		.test_vector = TEST_PER_LS(0x4D),
	},
	.c = {
		.dbg_name = "gsbi5_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gsbi5_p_clk.c),
	},
};

static struct branch_clk gsbi6_p_clk = {
	.b = {
		.en_reg = GSBIn_HCLK_CTL_REG(6),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEB_REG,
		.halt_check = HALT,
		.halt_bit = 19,
		.test_vector = TEST_PER_LS(0x51),
	},
	.c = {
		.dbg_name = "gsbi6_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gsbi6_p_clk.c),
	},
};

static struct branch_clk gsbi7_p_clk = {
	.b = {
		.en_reg = GSBIn_HCLK_CTL_REG(7),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEB_REG,
		.halt_check = HALT,
		.halt_bit = 15,
		.test_vector = TEST_PER_LS(0x55),
	},
	.c = {
		.dbg_name = "gsbi7_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gsbi7_p_clk.c),
	},
};

static struct branch_clk gsbi8_p_clk = {
	.b = {
		.en_reg = GSBIn_HCLK_CTL_REG(8),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEB_REG,
		.halt_check = HALT,
		.halt_bit = 11,
		.test_vector = TEST_PER_LS(0x59),
	},
	.c = {
		.dbg_name = "gsbi8_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gsbi8_p_clk.c),
	},
};

static struct branch_clk gsbi9_p_clk = {
	.b = {
		.en_reg = GSBIn_HCLK_CTL_REG(9),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEB_REG,
		.halt_check = HALT,
		.halt_bit = 7,
		.test_vector = TEST_PER_LS(0x5D),
	},
	.c = {
		.dbg_name = "gsbi9_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gsbi9_p_clk.c),
	},
};

static struct branch_clk gsbi10_p_clk = {
	.b = {
		.en_reg = GSBIn_HCLK_CTL_REG(10),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEB_REG,
		.halt_check = HALT,
		.halt_bit = 3,
		.test_vector = TEST_PER_LS(0x61),
	},
	.c = {
		.dbg_name = "gsbi10_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gsbi10_p_clk.c),
	},
};

static struct branch_clk gsbi11_p_clk = {
	.b = {
		.en_reg = GSBIn_HCLK_CTL_REG(11),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEC_REG,
		.halt_check = HALT,
		.halt_bit = 18,
		.test_vector = TEST_PER_LS(0x65),
	},
	.c = {
		.dbg_name = "gsbi11_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gsbi11_p_clk.c),
	},
};

static struct branch_clk gsbi12_p_clk = {
	.b = {
		.en_reg = GSBIn_HCLK_CTL_REG(12),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEC_REG,
		.halt_check = HALT,
		.halt_bit = 14,
		.test_vector = TEST_PER_LS(0x69),
	},
	.c = {
		.dbg_name = "gsbi12_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gsbi12_p_clk.c),
	},
};

static struct branch_clk ppss_p_clk = {
	.b = {
		.en_reg = PPSS_HCLK_CTL_REG,
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_DFAB_STATE_REG,
		.halt_check = HALT,
		.halt_bit = 19,
		.test_vector = TEST_PER_LS(0x2B),
	},
	.c = {
		.dbg_name = "ppss_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(ppss_p_clk.c),
	},
};

static struct branch_clk tsif_p_clk = {
	.b = {
		.en_reg = TSIF_HCLK_CTL_REG,
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEC_REG,
		.halt_check = HALT,
		.halt_bit = 7,
		.test_vector = TEST_PER_LS(0x8F),
	},
	.c = {
		.dbg_name = "tsif_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(tsif_p_clk.c),
	},
};

static struct branch_clk usb_fs1_p_clk = {
	.b = {
		.en_reg = USB_FSn_HCLK_CTL_REG(1),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEA_REG,
		.halt_check = HALT,
		.halt_bit = 17,
		.test_vector = TEST_PER_LS(0x89),
	},
	.c = {
		.dbg_name = "usb_fs1_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(usb_fs1_p_clk.c),
	},
};

static struct branch_clk usb_fs2_p_clk = {
	.b = {
		.en_reg = USB_FSn_HCLK_CTL_REG(2),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_CFPB_STATEA_REG,
		.halt_check = HALT,
		.halt_bit = 14,
		.test_vector = TEST_PER_LS(0x8C),
	},
	.c = {
		.dbg_name = "usb_fs2_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(usb_fs2_p_clk.c),
	},
};

static struct branch_clk usb_hs1_p_clk = {
	.b = {
		.en_reg = USB_HS1_HCLK_CTL_REG,
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_DFAB_STATE_REG,
		.halt_check = HALT,
		.halt_bit = 1,
		.test_vector = TEST_PER_LS(0x84),
	},
	.c = {
		.dbg_name = "usb_hs1_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(usb_hs1_p_clk.c),
	},
};

static struct branch_clk sdc1_p_clk = {
	.b = {
		.en_reg = SDCn_HCLK_CTL_REG(1),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_DFAB_STATE_REG,
		.halt_check = HALT,
		.halt_bit = 11,
		.test_vector = TEST_PER_LS(0x12),
	},
	.c = {
		.dbg_name = "sdc1_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(sdc1_p_clk.c),
	},
};

static struct branch_clk sdc2_p_clk = {
	.b = {
		.en_reg = SDCn_HCLK_CTL_REG(2),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_DFAB_STATE_REG,
		.halt_check = HALT,
		.halt_bit = 10,
		.test_vector = TEST_PER_LS(0x14),
	},
	.c = {
		.dbg_name = "sdc2_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(sdc2_p_clk.c),
	},
};

static struct branch_clk sdc3_p_clk = {
	.b = {
		.en_reg = SDCn_HCLK_CTL_REG(3),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_DFAB_STATE_REG,
		.halt_check = HALT,
		.halt_bit = 9,
		.test_vector = TEST_PER_LS(0x16),
	},
	.c = {
		.dbg_name = "sdc3_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(sdc3_p_clk.c),
	},
};

static struct branch_clk sdc4_p_clk = {
	.b = {
		.en_reg = SDCn_HCLK_CTL_REG(4),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_DFAB_STATE_REG,
		.halt_check = HALT,
		.halt_bit = 8,
		.test_vector = TEST_PER_LS(0x18),
	},
	.c = {
		.dbg_name = "sdc4_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(sdc4_p_clk.c),
	},
};

static struct branch_clk sdc5_p_clk = {
	.b = {
		.en_reg = SDCn_HCLK_CTL_REG(5),
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_DFAB_STATE_REG,
		.halt_check = HALT,
		.halt_bit = 7,
		.test_vector = TEST_PER_LS(0x1A),
	},
	.c = {
		.dbg_name = "sdc5_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(sdc5_p_clk.c),
	},
};

/* HW-Voteable Clocks */
static struct branch_clk adm0_clk = {
	.b = {
		.en_reg = SC0_U_CLK_BRANCH_ENA_VOTE_REG,
		.en_mask = BIT(2),
		.halt_reg = CLK_HALT_MSS_SMPSS_MISC_STATE_REG,
		.halt_check = HALT_VOTED,
		.halt_bit = 14,
		.test_vector = TEST_PER_HS(0x2A),
	},
	.parent = &pxo_clk.c,
	.c = {
		.dbg_name = "adm0_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(adm0_clk.c),
	},
};

static struct branch_clk adm0_p_clk = {
	.b = {
		.en_reg = SC0_U_CLK_BRANCH_ENA_VOTE_REG,
		.en_mask = BIT(3),
		.halt_reg = CLK_HALT_MSS_SMPSS_MISC_STATE_REG,
		.halt_check = HALT_VOTED,
		.halt_bit = 13,
		.test_vector = TEST_PER_LS(0x80),
	},
	.c = {
		.dbg_name = "adm0_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(adm0_p_clk.c),
	},
};

static struct branch_clk adm1_clk = {
	.b = {
		.en_reg = SC0_U_CLK_BRANCH_ENA_VOTE_REG,
		.en_mask = BIT(4),
		.halt_reg = CLK_HALT_MSS_SMPSS_MISC_STATE_REG,
		.halt_check = HALT_VOTED,
		.halt_bit = 12,
		.test_vector = TEST_PER_HS(0x2B),
	},
	.parent = &pxo_clk.c,
	.c = {
		.dbg_name = "adm1_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(adm1_clk.c),
	},
};

static struct branch_clk adm1_p_clk = {
	.b = {
		.en_reg = SC0_U_CLK_BRANCH_ENA_VOTE_REG,
		.en_mask = BIT(5),
		.halt_reg = CLK_HALT_MSS_SMPSS_MISC_STATE_REG,
		.halt_check = HALT_VOTED,
		.halt_bit = 11,
		.test_vector = TEST_PER_LS(0x81),
	},
	.c = {
		.dbg_name = "adm1_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(adm1_p_clk.c),
	},
};

static struct branch_clk modem_ahb1_p_clk = {
	.b = {
		.en_reg = SC0_U_CLK_BRANCH_ENA_VOTE_REG,
		.en_mask = BIT(0),
		.halt_reg = CLK_HALT_MSS_SMPSS_MISC_STATE_REG,
		.halt_check = HALT_VOTED,
		.halt_bit = 8,
		.test_vector = TEST_PER_LS(0x08),
	},
	.c = {
		.dbg_name = "modem_ahb1_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(modem_ahb1_p_clk.c),
	},
};

static struct branch_clk modem_ahb2_p_clk = {
	.b = {
		.en_reg = SC0_U_CLK_BRANCH_ENA_VOTE_REG,
		.en_mask = BIT(1),
		.halt_reg = CLK_HALT_MSS_SMPSS_MISC_STATE_REG,
		.halt_check = HALT_VOTED,
		.halt_bit = 7,
		.test_vector = TEST_PER_LS(0x09),
	},
	.c = {
		.dbg_name = "modem_ahb2_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(modem_ahb2_p_clk.c),
	},
};

static struct branch_clk pmic_arb0_p_clk = {
	.b = {
		.en_reg = SC0_U_CLK_BRANCH_ENA_VOTE_REG,
		.en_mask = BIT(8),
		.halt_reg = CLK_HALT_SFPB_MISC_STATE_REG,
		.halt_check = HALT_VOTED,
		.halt_bit = 22,
		.test_vector = TEST_PER_LS(0x7B),
	},
	.c = {
		.dbg_name = "pmic_arb0_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(pmic_arb0_p_clk.c),
	},
};

static struct branch_clk pmic_arb1_p_clk = {
	.b = {
		.en_reg = SC0_U_CLK_BRANCH_ENA_VOTE_REG,
		.en_mask = BIT(9),
		.halt_reg = CLK_HALT_SFPB_MISC_STATE_REG,
		.halt_check = HALT_VOTED,
		.halt_bit = 21,
		.test_vector = TEST_PER_LS(0x7C),
	},
	.c = {
		.dbg_name = "pmic_arb1_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(pmic_arb1_p_clk.c),
	},
};

static struct branch_clk pmic_ssbi2_clk = {
	.b = {
		.en_reg = SC0_U_CLK_BRANCH_ENA_VOTE_REG,
		.en_mask = BIT(7),
		.halt_reg = CLK_HALT_SFPB_MISC_STATE_REG,
		.halt_check = HALT_VOTED,
		.halt_bit = 23,
		.test_vector = TEST_PER_LS(0x7A),
	},
	.c = {
		.dbg_name = "pmic_ssbi2_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(pmic_ssbi2_clk.c),
	},
};

static struct branch_clk rpm_msg_ram_p_clk = {
	.b = {
		.en_reg = SC0_U_CLK_BRANCH_ENA_VOTE_REG,
		.en_mask = BIT(6),
		.halt_reg = CLK_HALT_SFPB_MISC_STATE_REG,
		.halt_check = HALT_VOTED,
		.halt_bit = 12,
		.test_vector = TEST_PER_LS(0x7F),
	},
	.c = {
		.dbg_name = "rpm_msg_ram_p_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(rpm_msg_ram_p_clk.c),
	},
};

/*
 * Multimedia Clocks
 */

static struct branch_clk amp_clk = {
	.b = {
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(20),
	},
	.c = {
		.dbg_name = "amp_clk",
		.ops = &clk_ops_reset,
		CLK_INIT(amp_clk.c),
	},
};

#define F_CAM(f, s, d, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD8(8, m, 0, n), \
		.ns_val = NS_MM(31, 24, n, m, 15, 14, d, 2, 0, s##_to_mm_mux), \
		.cc_val = CC(6, n), \
		.mnd_en_mask = BIT(5) * !!(n), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_cam[] = {
	F_CAM(        0, gnd,  1, 0,  0, NONE),
	F_CAM(  6000000, pll8, 4, 1, 16, LOW),
	F_CAM(  8000000, pll8, 4, 1, 12, LOW),
	F_CAM( 12000000, pll8, 4, 1,  8, LOW),
	F_CAM( 16000000, pll8, 4, 1,  6, LOW),
	F_CAM( 19200000, pll8, 4, 1,  5, LOW),
	F_CAM( 24000000, pll8, 4, 1,  4, LOW),
	F_CAM( 32000000, pll8, 4, 1,  3, LOW),
	F_CAM( 48000000, pll8, 4, 1,  2, LOW),
	F_CAM( 64000000, pll8, 3, 1,  2, LOW),
	F_CAM( 96000000, pll8, 4, 0,  0, NOMINAL),
	F_CAM(128000000, pll8, 3, 0,  0, NOMINAL),
	F_END
};

struct clk_local cam_clk = {
	.b = {
		.en_reg = CAMCLK_CC_REG,
		.en_mask = BIT(0),
		.halt_check = DELAY,
		.test_vector = TEST_MM_LS(0x1D),
	},
	.ns_reg = CAMCLK_NS_REG,
	.md_reg = CAMCLK_MD_REG,
	.root_en_mask = BIT(2),
	.ns_mask = (BM(31, 24) | BM(15, 14) | BM(2, 0)),
	.cc_mask = BM(7, 6),
	.set_rate = set_rate_mnd_8,
	.freq_tbl = clk_tbl_cam,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "cam_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(cam_clk.c),
	},
};

#define F_CSI(f, s, d, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.ns_val = NS_DIVSRC(15, 12, d, 2, 0, s##_to_mm_mux),  \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_csi[] = {
	F_CSI(        0, gnd, 1, NONE),
	F_CSI(192000000, pll8, 2, LOW),
	F_CSI(384000000, pll8, 1, NOMINAL),
	F_END
};

struct clk_local csi_src_clk = {
	.ns_reg = CSI_NS_REG,
	.b = {
		.en_reg = CSI_CC_REG,
	},
	.root_en_mask = BIT(2),
	.ns_mask = (BM(15, 12) | BM(2, 0)),
	.set_rate = set_rate_nop,
	.freq_tbl = clk_tbl_csi,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "csi_src_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(csi_src_clk.c),
	},
};

static struct branch_clk csi0_clk = {
	.b = {
		.en_reg = CSI_CC_REG,
		.en_mask = BIT(0),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(8),
		.halt_reg = DBG_BUS_VEC_B_REG,
		.halt_check = HALT,
		.halt_bit = 13,
		.test_vector = TEST_MM_HS(0x00),
	},
	.parent = &csi_src_clk.c,
	.c = {
		.dbg_name = "csi0_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(csi0_clk.c),
	},
};

static struct branch_clk csi1_clk = {
	.b = {
		.en_reg = CSI_CC_REG,
		.en_mask = BIT(7),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(18),
		.halt_reg = DBG_BUS_VEC_B_REG,
		.halt_check = HALT,
		.halt_bit = 14,
		.test_vector = TEST_MM_HS(0x01),
	},
	.parent = &csi_src_clk.c,
	.c = {
		.dbg_name = "csi1_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(csi1_clk.c),
	},
};

#define F_DSI(d) \
	{ \
		.freq_hz = d, \
		.ns_val = BVAL(27, 24, (d-1)), \
	}
/* The DSI_BYTE clock is sourced from the DSI PHY PLL, which may change rate
 * without this clock driver knowing.  So, overload the clk_set_rate() to set
 * the divider (1 to 16) of the clock with respect to the PLL rate. */
static struct clk_freq_tbl clk_tbl_dsi_byte[] = {
	F_DSI(1),  F_DSI(2),  F_DSI(3),  F_DSI(4),
	F_DSI(5),  F_DSI(6),  F_DSI(7),  F_DSI(8),
	F_DSI(9),  F_DSI(10), F_DSI(11), F_DSI(12),
	F_DSI(13), F_DSI(14), F_DSI(15), F_DSI(16),
	F_END
};


struct clk_local dsi_byte_clk = {
	.b = {
		.en_reg = MISC_CC_REG,
		.halt_check = DELAY,
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(7),
		.test_vector = TEST_MM_LS(0x00),
	},
	.ns_reg = MISC_CC2_REG,
	.root_en_mask = BIT(2),
	.ns_mask = BM(27, 24),
	.set_rate = set_rate_nop,
	.freq_tbl = clk_tbl_dsi_byte,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "dsi_byte_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(dsi_byte_clk.c),
	},
};

static struct branch_clk dsi_esc_clk = {
	.b = {
		.en_reg = MISC_CC_REG,
		.en_mask = BIT(0),
		.halt_reg = DBG_BUS_VEC_B_REG,
		.halt_check = HALT,
		.halt_bit = 24,
		.test_vector = TEST_MM_LS(0x23),
	},
	.c = {
		.dbg_name = "dsi_esc_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(dsi_esc_clk.c),
	},
};

#define F_GFX2D(f, s, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD4(4, m, 0, n), \
		.ns_val = NS_MND_BANKED4(20, 16, n, m, 3, 0, s##_to_mm_mux), \
		.cc_val = CC_BANKED(9, 6, n), \
		.mnd_en_mask = (BIT(8) | BIT(5)) * !!(n), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_gfx2d[] = {
	F_GFX2D(        0, gnd,  0,  0, NONE),
	F_GFX2D( 27000000, pxo,  0,  0, LOW),
	F_GFX2D( 48000000, pll8, 1,  8, LOW),
	F_GFX2D( 54857000, pll8, 1,  7, LOW),
	F_GFX2D( 64000000, pll8, 1,  6, LOW),
	F_GFX2D( 76800000, pll8, 1,  5, LOW),
	F_GFX2D( 96000000, pll8, 1,  4, LOW),
	F_GFX2D(128000000, pll8, 1,  3, NOMINAL),
	F_GFX2D(145455000, pll2, 2, 11, NOMINAL),
	F_GFX2D(160000000, pll2, 1,  5, NOMINAL),
	F_GFX2D(177778000, pll2, 2,  9, NOMINAL),
	F_GFX2D(200000000, pll2, 1,  4, NOMINAL),
	F_GFX2D(228571000, pll2, 2,  7, HIGH),
	F_END
};

static struct bank_masks bmnd_info_gfx2d0 = {
	.bank_sel_mask =			BIT(11),
	.bank0_mask = {
			.md_reg =		GFX2D0_MD0_REG,
			.ns_mask =		BM(23, 20) | BM(5, 3),
			.rst_mask =		BIT(25),
			.mnd_en_mask =		BIT(8),
			.mode_mask =		BM(10, 9),
	},
	.bank1_mask = {
			.md_reg =		GFX2D0_MD1_REG,
			.ns_mask =		BM(19, 16) | BM(2, 0),
			.rst_mask =		BIT(24),
			.mnd_en_mask =		BIT(5),
			.mode_mask =		BM(7, 6),
	},
};

struct clk_local gfx2d0_clk = {
	.b = {
		.en_reg = GFX2D0_CC_REG,
		.en_mask = BIT(0),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(14),
		.halt_reg = DBG_BUS_VEC_A_REG,
		.halt_check = HALT,
		.halt_bit = 9,
		.test_vector = TEST_MM_HS(0x07),
	},
	.ns_reg = GFX2D0_NS_REG,
	.root_en_mask = BIT(2),
	.set_rate = set_rate_mnd_banked,
	.freq_tbl = clk_tbl_gfx2d,
	.bank_masks = &bmnd_info_gfx2d0,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "gfx2d0_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gfx2d0_clk.c),
	},
};

static struct bank_masks bmnd_info_gfx2d1 = {
	.bank_sel_mask =		BIT(11),
	.bank0_mask = {
			.md_reg =		GFX2D1_MD0_REG,
			.ns_mask =		BM(23, 20) | BM(5, 3),
			.rst_mask =		BIT(25),
			.mnd_en_mask =		BIT(8),
			.mode_mask =		BM(10, 9),
	},
	.bank1_mask = {
			.md_reg =		GFX2D1_MD1_REG,
			.ns_mask =		BM(19, 16) | BM(2, 0),
			.rst_mask =		BIT(24),
			.mnd_en_mask =		BIT(5),
			.mode_mask =		BM(7, 6),
	},
};

struct clk_local gfx2d1_clk = {
	.b = {
		.en_reg = GFX2D1_CC_REG,
		.en_mask = BIT(0),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(13),
		.halt_reg = DBG_BUS_VEC_A_REG,
		.halt_check = HALT,
		.halt_bit = 14,
		.test_vector = TEST_MM_HS(0x08),
	},
	.ns_reg = GFX2D1_NS_REG,
	.root_en_mask = BIT(2),
	.set_rate = set_rate_mnd_banked,
	.freq_tbl = clk_tbl_gfx2d,
	.bank_masks = &bmnd_info_gfx2d1,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "gfx2d1_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gfx2d1_clk.c),
	},
};

#define F_GFX3D(f, s, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD4(4, m, 0, n), \
		.ns_val = NS_MND_BANKED4(18, 14, n, m, 3, 0, s##_to_mm_mux), \
		.cc_val = CC_BANKED(9, 6, n), \
		.mnd_en_mask = (BIT(8) | BIT(5)) * !!(n), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_gfx3d[] = {
	F_GFX3D(        0, gnd,  0,  0, NONE),
	F_GFX3D( 27000000, pxo,  0,  0, LOW),
	F_GFX3D( 48000000, pll8, 1,  8, LOW),
	F_GFX3D( 54857000, pll8, 1,  7, LOW),
	F_GFX3D( 64000000, pll8, 1,  6, LOW),
	F_GFX3D( 76800000, pll8, 1,  5, LOW),
	F_GFX3D( 96000000, pll8, 1,  4, LOW),
	F_GFX3D(128000000, pll8, 1,  3, NOMINAL),
	F_GFX3D(145455000, pll2, 2, 11, NOMINAL),
	F_GFX3D(160000000, pll2, 1,  5, NOMINAL),
	F_GFX3D(177778000, pll2, 2,  9, NOMINAL),
	F_GFX3D(200000000, pll2, 1,  4, NOMINAL),
	F_GFX3D(228571000, pll2, 2,  7, HIGH),
	F_GFX3D(266667000, pll2, 1,  3, HIGH),
	F_GFX3D(320000000, pll2, 2,  5, HIGH),
	F_END
};

static struct bank_masks bmnd_info_gfx3d = {
	.bank_sel_mask =		BIT(11),
	.bank0_mask = {
			.md_reg =		GFX3D_MD0_REG,
			.ns_mask =		BM(21, 18) | BM(5, 3),
			.rst_mask =		BIT(23),
			.mnd_en_mask =		BIT(8),
			.mode_mask =		BM(10, 9),
	},
	.bank1_mask = {
			.md_reg =		GFX3D_MD1_REG,
			.ns_mask =		BM(17, 14) | BM(2, 0),
			.rst_mask =		BIT(22),
			.mnd_en_mask =		BIT(5),
			.mode_mask =		BM(7, 6),
	},
};

struct clk_local gfx3d_clk = {
	.b = {
		.en_reg = GFX3D_CC_REG,
		.en_mask = BIT(0),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(12),
		.halt_reg = DBG_BUS_VEC_A_REG,
		.halt_check = HALT,
		.halt_bit = 4,
		.test_vector = TEST_MM_HS(0x09),
	},
	.ns_reg = GFX3D_NS_REG,
	.root_en_mask = BIT(2),
	.set_rate = set_rate_mnd_banked,
	.freq_tbl = clk_tbl_gfx3d,
	.bank_masks = &bmnd_info_gfx3d,
	.depends = &gmem_axi_clk.c,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "gfx3d_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(gfx3d_clk.c),
	},
};

#define F_IJPEG(f, s, d, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD8(8, m, 0, n), \
		.ns_val = NS_MM(23, 16, n, m, 15, 12, d, 2, 0, s##_to_mm_mux), \
		.cc_val = CC(6, n), \
		.mnd_en_mask = BIT(5) * !!n, \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_ijpeg[] = {
	F_IJPEG(        0, gnd,  1, 0,  0, NONE),
	F_IJPEG( 27000000, pxo,  1, 0,  0, LOW),
	F_IJPEG( 36570000, pll8, 1, 2, 21, LOW),
	F_IJPEG( 54860000, pll8, 7, 0,  0, LOW),
	F_IJPEG( 96000000, pll8, 4, 0,  0, LOW),
	F_IJPEG(109710000, pll8, 1, 2,  7, LOW),
	F_IJPEG(128000000, pll8, 3, 0,  0, NOMINAL),
	F_IJPEG(153600000, pll8, 1, 2,  5, NOMINAL),
	F_IJPEG(200000000, pll2, 4, 0,  0, NOMINAL),
	F_IJPEG(228571000, pll2, 1, 2,  7, NOMINAL),
	F_END
};

struct clk_local ijpeg_clk = {
	.b = {
		.en_reg = IJPEG_CC_REG,
		.en_mask = BIT(0),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(9),
		.halt_reg = DBG_BUS_VEC_A_REG,
		.halt_check = HALT,
		.halt_bit = 24,
		.test_vector = TEST_MM_HS(0x05),
	},
	.ns_reg = IJPEG_NS_REG,
	.md_reg = IJPEG_MD_REG,
	.root_en_mask = BIT(2),
	.ns_mask = (BM(23, 16) | BM(15, 12) | BM(2, 0)),
	.cc_mask = BM(7, 6),
	.set_rate = set_rate_mnd,
	.freq_tbl = clk_tbl_ijpeg,
	.depends = &ijpeg_axi_clk.c,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "ijpeg_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(ijpeg_clk.c),
	},
};

#define F_JPEGD(f, s, d, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.ns_val = NS_DIVSRC(15, 12, d, 2, 0, s##_to_mm_mux), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_jpegd[] = {
	F_JPEGD(        0, gnd,  1, NONE),
	F_JPEGD( 64000000, pll8, 6, LOW),
	F_JPEGD( 76800000, pll8, 5, LOW),
	F_JPEGD( 96000000, pll8, 4, LOW),
	F_JPEGD(160000000, pll2, 5, NOMINAL),
	F_JPEGD(200000000, pll2, 4, NOMINAL),
	F_END
};

struct clk_local jpegd_clk = {
	.b = {
		.en_reg = JPEGD_CC_REG,
		.en_mask = BIT(0),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(19),
		.halt_reg = DBG_BUS_VEC_A_REG,
		.halt_check = HALT,
		.halt_bit = 19,
		.test_vector = TEST_MM_HS(0x0A),
	},
	.ns_reg = JPEGD_NS_REG,
	.root_en_mask = BIT(2),
	.ns_mask =  (BM(15, 12) | BM(2, 0)),
	.set_rate = set_rate_nop,
	.freq_tbl = clk_tbl_jpegd,
	.depends = &jpegd_axi_clk.c,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "jpegd_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(jpegd_clk.c),
	},
};

#define F_MDP(f, s, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD8(8, m, 0, n), \
		.ns_val = NS_MND_BANKED8(22, 14, n, m, 3, 0, s##_to_mm_mux), \
		.cc_val = CC_BANKED(9, 6, n), \
		.mnd_en_mask = (BIT(8) | BIT(5)) * !!(n), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_mdp[] = {
	F_MDP(        0, gnd,  0,  0, NONE),
	F_MDP(  9600000, pll8, 1, 40, LOW),
	F_MDP( 13710000, pll8, 1, 28, LOW),
	F_MDP( 27000000, pxo,  0,  0, LOW),
	F_MDP( 29540000, pll8, 1, 13, LOW),
	F_MDP( 34910000, pll8, 1, 11, LOW),
	F_MDP( 38400000, pll8, 1, 10, LOW),
	F_MDP( 59080000, pll8, 2, 13, LOW),
	F_MDP( 76800000, pll8, 1,  5, LOW),
	F_MDP( 85330000, pll8, 2,  9, LOW),
	F_MDP( 96000000, pll8, 1,  4, NOMINAL),
	F_MDP(128000000, pll8, 1,  3, NOMINAL),
	F_MDP(160000000, pll2, 1,  5, NOMINAL),
	F_MDP(177780000, pll2, 2,  9, NOMINAL),
	F_MDP(200000000, pll2, 1,  4, NOMINAL),
	F_END
};

static struct bank_masks bmnd_info_mdp = {
	.bank_sel_mask =		BIT(11),
	.bank0_mask = {
			.md_reg =		MDP_MD0_REG,
			.ns_mask =		BM(29, 22) | BM(5, 3),
			.rst_mask =		BIT(31),
			.mnd_en_mask =		BIT(8),
			.mode_mask =		BM(10, 9),
	},
	.bank1_mask = {
			.md_reg =		MDP_MD1_REG,
			.ns_mask =		BM(21, 14) | BM(2, 0),
			.rst_mask =		BIT(30),
			.mnd_en_mask =		BIT(5),
			.mode_mask =		BM(7, 6),
	},
};

struct clk_local mdp_clk = {
	.b = {
		.en_reg = MDP_CC_REG,
		.en_mask = BIT(0),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(21),
		.halt_reg = DBG_BUS_VEC_C_REG,
		.halt_check = HALT,
		.halt_bit = 10,
		.test_vector = TEST_MM_HS(0x1A),
	},
	.ns_reg = MDP_NS_REG,
	.root_en_mask = BIT(2),
	.set_rate = set_rate_mnd_banked,
	.freq_tbl = clk_tbl_mdp,
	.bank_masks = &bmnd_info_mdp,
	.depends = &mdp_axi_clk.c,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "mdp_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(mdp_clk.c),
	},
};

#define F_MDP_VSYNC(f, s, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.ns_val = NS_SRC_SEL(13, 13, s##_to_bb_mux), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_mdp_vsync[] = {
	F_MDP_VSYNC(27000000, pxo, LOW),
	F_END
};

struct clk_local mdp_vsync_clk = {
	.b = {
		.en_reg = MISC_CC_REG,
		.en_mask = BIT(6),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(3),
		.halt_reg = DBG_BUS_VEC_B_REG,
		.halt_check = HALT,
		.halt_bit = 22,
		.test_vector = TEST_MM_LS(0x20),
	},
	.ns_reg = MISC_CC2_REG,
	.ns_mask = BIT(13),
	.set_rate = set_rate_nop,
	.freq_tbl = clk_tbl_mdp_vsync,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "mdp_vsync_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(mdp_vsync_clk.c),
	},
};

#define F_PIXEL_MDP(f, s, d, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD16(m, n), \
		.ns_val = NS_MM(31, 16, n, m, 15, 14, d, 2, 0, s##_to_mm_mux), \
		.cc_val = CC(6, n), \
		.mnd_en_mask = BIT(5) * !!(n), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_pixel_mdp[] = {
	F_PIXEL_MDP(        0, gnd, 1,   0,    0, NONE),
	F_PIXEL_MDP( 25600000, pll8, 3,   1,    5, LOW),
	F_PIXEL_MDP( 42667000, pll8, 1,   1,    9, LOW),
	F_PIXEL_MDP( 43192000, pll8, 1,  64,  569, LOW),
	F_PIXEL_MDP( 48000000, pll8, 4,   1,    2, LOW),
	F_PIXEL_MDP( 53990000, pll8, 2, 169,  601, LOW),
	F_PIXEL_MDP( 64000000, pll8, 2,   1,    3, LOW),
	F_PIXEL_MDP( 69300000, pll8, 1, 231, 1280, LOW),
	F_PIXEL_MDP( 76800000, pll8, 1,   1,    5, LOW),
	F_PIXEL_MDP( 85333000, pll8, 1,   2,    9, LOW),
	F_PIXEL_MDP(106500000, pll8, 1,  71,  256, NOMINAL),
	F_PIXEL_MDP(109714000, pll8, 1,   2,    7, NOMINAL),
	F_END
};

struct clk_local pixel_mdp_clk = {
	.ns_reg = PIXEL_NS_REG,
	.md_reg = PIXEL_MD_REG,
	.b = {
		.en_reg = PIXEL_CC_REG,
		.en_mask = BIT(0),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(5),
		.halt_reg = DBG_BUS_VEC_C_REG,
		.halt_check = HALT,
		.halt_bit = 23,
		.test_vector = TEST_MM_LS(0x04),
	},
	.root_en_mask = BIT(2),
	.ns_mask = (BM(31, 16) | BM(15, 14) | BM(2, 0)),
	.cc_mask = BM(7, 6),
	.set_rate = set_rate_mnd,
	.freq_tbl = clk_tbl_pixel_mdp,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "pixel_mdp_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(pixel_mdp_clk.c),
	},
};

static struct branch_clk pixel_lcdc_clk = {
	.b = {
		.en_reg = PIXEL_CC_REG,
		.en_mask = BIT(8),
		.halt_reg = DBG_BUS_VEC_C_REG,
		.halt_check = HALT,
		.halt_bit = 21,
		.test_vector = TEST_MM_LS(0x01),
	},
	.parent = &pixel_mdp_clk.c,
	.c = {
		.dbg_name = "pixel_lcdc_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(pixel_lcdc_clk.c),
	},
};

#define F_ROT(f, s, d, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.ns_val = NS_DIVSRC_BANKED(29, 26, 25, 22, d, \
				21, 19, 18, 16, s##_to_mm_mux), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_rot[] = {
	F_ROT(        0, gnd,   1, NONE),
	F_ROT( 27000000, pxo,   1, LOW),
	F_ROT( 29540000, pll8, 13, LOW),
	F_ROT( 32000000, pll8, 12, LOW),
	F_ROT( 38400000, pll8, 10, LOW),
	F_ROT( 48000000, pll8,  8, LOW),
	F_ROT( 54860000, pll8,  7, LOW),
	F_ROT( 64000000, pll8,  6, LOW),
	F_ROT( 76800000, pll8,  5, LOW),
	F_ROT( 96000000, pll8,  4, NOMINAL),
	F_ROT(100000000, pll2,  8, NOMINAL),
	F_ROT(114290000, pll2,  7, NOMINAL),
	F_ROT(133330000, pll2,  6, NOMINAL),
	F_ROT(160000000, pll2,  5, NOMINAL),
	F_END
};

static struct bank_masks bdiv_info_rot = {
	.bank_sel_mask = BIT(30),
	.bank0_mask = {
		.ns_mask =	BM(25, 22) | BM(18, 16),
	},
	.bank1_mask = {
		.ns_mask =	BM(29, 26) | BM(21, 19),
	},
};

struct clk_local rot_clk = {
	.b = {
		.en_reg = ROT_CC_REG,
		.en_mask = BIT(0),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(2),
		.halt_reg = DBG_BUS_VEC_C_REG,
		.halt_check = HALT,
		.halt_bit = 15,
		.test_vector = TEST_MM_HS(0x1B),
	},
	.ns_reg = ROT_NS_REG,
	.root_en_mask = BIT(2),
	.set_rate = set_rate_div_banked,
	.freq_tbl = clk_tbl_rot,
	.bank_masks = &bdiv_info_rot,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "rot_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(rot_clk.c),
	},
};

#define F_TV(f, s, p_r, d, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD8(8, m, 0, n), \
		.ns_val = NS_MM(23, 16, n, m, 15, 14, d, 2, 0, s##_to_mm_mux), \
		.cc_val = CC(6, n), \
		.mnd_en_mask = BIT(5) * !!(n), \
		.sys_vdd = v, \
		.extra_freq_data = p_r, \
	}
/* Switching TV freqs requires PLL reconfiguration. */
static struct pll_rate mm_pll2_rate[] = {
	[0] = PLL_RATE( 7, 6301, 13500, 0, 4, 0x4248B), /*  50400500 Hz */
	[1] = PLL_RATE( 8,    0,     0, 0, 4, 0x4248B), /*  54000000 Hz */
	[2] = PLL_RATE(16,    2,   125, 0, 4, 0x5248F), /* 108108000 Hz */
	[3] = PLL_RATE(22,    0,     0, 2, 4, 0x6248B), /* 148500000 Hz */
	[4] = PLL_RATE(44,    0,     0, 2, 4, 0x6248F), /* 297000000 Hz */
};
static struct clk_freq_tbl clk_tbl_tv[] = {
	F_TV(        0, gnd,  &mm_pll2_rate[0], 1, 0, 0, NONE),
	F_TV( 25200000, pll3, &mm_pll2_rate[0], 2, 0, 0, LOW),
	F_TV( 27000000, pll3, &mm_pll2_rate[1], 2, 0, 0, LOW),
	F_TV( 27030000, pll3, &mm_pll2_rate[2], 4, 0, 0, LOW),
	F_TV( 74250000, pll3, &mm_pll2_rate[3], 2, 0, 0, NOMINAL),
	F_TV(148500000, pll3, &mm_pll2_rate[4], 2, 0, 0, NOMINAL),
	F_END
};

struct clk_local tv_src_clk = {
	.ns_reg = TV_NS_REG,
	.b = {
		.en_reg = TV_CC_REG,
	},
	.md_reg = TV_MD_REG,
	.root_en_mask = BIT(2),
	.ns_mask = (BM(23, 16) | BM(15, 14) | BM(2, 0)),
	.cc_mask = BM(7, 6),
	.set_rate = set_rate_tv,
	.freq_tbl = clk_tbl_tv,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "tv_src_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(tv_src_clk.c),
	},
};

static struct branch_clk tv_enc_clk = {
	.b = {
		.en_reg = TV_CC_REG,
		.en_mask = BIT(8),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(0),
		.halt_reg = DBG_BUS_VEC_D_REG,
		.halt_check = HALT,
		.halt_bit = 8,
		.test_vector = TEST_MM_LS(0x22),
	},
	.parent = &tv_src_clk.c,
	.c = {
		.dbg_name = "tv_enc_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(tv_enc_clk.c),
	},
};

static struct branch_clk tv_dac_clk = {
	.b = {
		.en_reg = TV_CC_REG,
		.en_mask = BIT(10),
		.halt_reg = DBG_BUS_VEC_D_REG,
		.halt_check = HALT,
		.halt_bit = 9,
		.test_vector = TEST_MM_LS(0x21),
	},
	.parent = &tv_src_clk.c,
	.c = {
		.dbg_name = "tv_dac_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(tv_dac_clk.c),
	},
};

static struct branch_clk mdp_tv_clk = {
	.b = {
		.en_reg = TV_CC_REG,
		.en_mask = BIT(0),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(4),
		.halt_reg = DBG_BUS_VEC_D_REG,
		.halt_check = HALT,
		.halt_bit = 11,
		.test_vector = TEST_MM_HS(0x1F),
	},
	.parent = &tv_src_clk.c,
	.c = {
		.dbg_name = "mdp_tv_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(mdp_tv_clk.c),
	},
};

static struct branch_clk hdmi_tv_clk = {
	.b = {
		.en_reg = TV_CC_REG,
		.en_mask = BIT(12),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(1),
		.halt_reg = DBG_BUS_VEC_D_REG,
		.halt_check = HALT,
		.halt_bit = 10,
		.test_vector = TEST_MM_HS(0x1E),
	},
	.parent = &tv_src_clk.c,
	.c = {
		.dbg_name = "hdmi_tv_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(hdmi_tv_clk.c),
	},
};

static struct branch_clk hdmi_app_clk = {
	.b = {
		.en_reg = MISC_CC2_REG,
		.en_mask = BIT(11),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(11),
		.halt_reg = DBG_BUS_VEC_B_REG,
		.halt_check = HALT,
		.halt_bit = 25,
		.test_vector = TEST_MM_LS(0x1F),
	},
	.c = {
		.dbg_name = "hdmi_app_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(hdmi_app_clk.c),
	},
};

#define F_VCODEC(f, s, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD8(8, m, 0, n), \
		.ns_val = NS_MM(18, 11, n, m, 0, 0, 1, 2, 0, s##_to_mm_mux), \
		.cc_val = CC(6, n), \
		.mnd_en_mask = BIT(5) * !!(n), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_vcodec[] = {
	F_VCODEC(        0, gnd,  0,  0, NONE),
	F_VCODEC( 27000000, pxo,  0,  0, LOW),
	F_VCODEC( 32000000, pll8, 1, 12, LOW),
	F_VCODEC( 48000000, pll8, 1,  8, LOW),
	F_VCODEC( 54860000, pll8, 1,  7, LOW),
	F_VCODEC( 96000000, pll8, 1,  4, LOW),
	F_VCODEC(133330000, pll2, 1,  6, NOMINAL),
	F_VCODEC(200000000, pll2, 1,  4, NOMINAL),
	F_VCODEC(228570000, pll2, 2,  7, HIGH),
	F_END
};

struct clk_local vcodec_clk = {
	.b = {
		.en_reg = VCODEC_CC_REG,
		.en_mask = BIT(0),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(6),
		.halt_reg = DBG_BUS_VEC_C_REG,
		.halt_check = HALT,
		.halt_bit = 29,
		.test_vector = TEST_MM_HS(0x0B),
	},
	.ns_reg = VCODEC_NS_REG,
	.md_reg = VCODEC_MD0_REG,
	.root_en_mask = BIT(2),
	.ns_mask = (BM(18, 11) | BM(2, 0)),
	.cc_mask = BM(7, 6),
	.set_rate = set_rate_mnd,
	.freq_tbl = clk_tbl_vcodec,
	.depends = &vcodec_axi_clk.c,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "vcodec_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(vcodec_clk.c),
	},
};

#define F_VPE(f, s, d, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.ns_val = NS_DIVSRC(15, 12, d, 2, 0, s##_to_mm_mux), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_vpe[] = {
	F_VPE(        0, gnd,   1, NONE),
	F_VPE( 27000000, pxo,   1, LOW),
	F_VPE( 34909000, pll8, 11, LOW),
	F_VPE( 38400000, pll8, 10, LOW),
	F_VPE( 64000000, pll8,  6, LOW),
	F_VPE( 76800000, pll8,  5, LOW),
	F_VPE( 96000000, pll8,  4, NOMINAL),
	F_VPE(100000000, pll2,  8, NOMINAL),
	F_VPE(160000000, pll2,  5, NOMINAL),
	F_VPE(200000000, pll2,  4, HIGH),
	F_END
};

struct clk_local vpe_clk = {
	.b = {
		.en_reg = VPE_CC_REG,
		.en_mask = BIT(0),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(17),
		.halt_reg = DBG_BUS_VEC_A_REG,
		.halt_check = HALT,
		.halt_bit = 28,
		.test_vector = TEST_MM_HS(0x1C),
	},
	.ns_reg = VPE_NS_REG,
	.root_en_mask = BIT(2),
	.ns_mask = (BM(15, 12) | BM(2, 0)),
	.set_rate = set_rate_nop,
	.freq_tbl = clk_tbl_vpe,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "vpe_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(vpe_clk.c),
	},
};

#define F_VFE(f, s, d, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD8(8, m, 0, n), \
		.ns_val = NS_MM(23, 16, n, m, 11, 10, d, 2, 0, s##_to_mm_mux), \
		.cc_val = CC(6, n), \
		.mnd_en_mask = BIT(5) * !!(n), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_vfe[] = {
	F_VFE(        0, gnd,   1, 0,  0, NONE),
	F_VFE( 13960000, pll8,  1, 2, 55, LOW),
	F_VFE( 27000000, pxo,   1, 0,  0, LOW),
	F_VFE( 36570000, pll8,  1, 2, 21, LOW),
	F_VFE( 38400000, pll8,  2, 1,  5, LOW),
	F_VFE( 45180000, pll8,  1, 2, 17, LOW),
	F_VFE( 48000000, pll8,  2, 1,  4, LOW),
	F_VFE( 54860000, pll8,  1, 1,  7, LOW),
	F_VFE( 64000000, pll8,  2, 1,  3, LOW),
	F_VFE( 76800000, pll8,  1, 1,  5, LOW),
	F_VFE( 96000000, pll8,  2, 1,  2, LOW),
	F_VFE(109710000, pll8,  1, 2,  7, LOW),
	F_VFE(128000000, pll8,  1, 1,  3, NOMINAL),
	F_VFE(153600000, pll8,  1, 2,  5, NOMINAL),
	F_VFE(200000000, pll2,  2, 1,  2, NOMINAL),
	F_VFE(228570000, pll2,  1, 2,  7, NOMINAL),
	F_VFE(266667000, pll2,  1, 1,  3, HIGH),
	F_END
};

struct clk_local vfe_clk = {
	.b = {
		.en_reg = VFE_CC_REG,
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(15),
		.halt_reg = DBG_BUS_VEC_B_REG,
		.halt_check = HALT,
		.halt_bit = 6,
		.en_mask = BIT(0),
		.test_vector = TEST_MM_HS(0x06),
	},
	.ns_reg = VFE_NS_REG,
	.md_reg = VFE_MD_REG,
	.root_en_mask = BIT(2),
	.ns_mask = (BM(23, 16) | BM(11, 10) | BM(2, 0)),
	.cc_mask = BM(7, 6),
	.set_rate = set_rate_mnd,
	.freq_tbl = clk_tbl_vfe,
	.depends = &vfe_axi_clk.c,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "vfe_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(vfe_clk.c),
	},
};

static struct branch_clk csi0_vfe_clk = {
	.b = {
		.en_reg = VFE_CC_REG,
		.en_mask = BIT(12),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(24),
		.halt_reg = DBG_BUS_VEC_B_REG,
		.halt_check = HALT,
		.halt_bit = 7,
		.test_vector = TEST_MM_HS(0x03),
	},
	.parent = &vfe_clk.c,
	.c = {
		.dbg_name = "csi0_vfe_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(csi0_vfe_clk.c),
	},
};

static struct branch_clk csi1_vfe_clk = {
	.b = {
		.en_reg = VFE_CC_REG,
		.en_mask = BIT(10),
		.reset_reg = SW_RESET_CORE_REG,
		.reset_mask = BIT(23),
		.halt_reg = DBG_BUS_VEC_B_REG,
		.halt_check = HALT,
		.halt_bit = 8,
		.test_vector = TEST_MM_HS(0x04),
	},
	.parent = &vfe_clk.c,
	.c = {
		.dbg_name = "csi1_vfe_clk",
		.ops = &clk_ops_branch,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(csi1_vfe_clk.c),
	},
};

/*
 * Low Power Audio Clocks
 */
#define F_AIF_OSR(f, s, d, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD8(8, m, 0, n), \
		.ns_val = NS(31, 24, n, m, 5, 4, 3, d, 2, 0, s##_to_lpa_mux), \
		.mnd_en_mask = BIT(8) * !!(n), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_aif_osr[] = {
	F_AIF_OSR(       0, gnd,  1, 0,   0, NONE),
	F_AIF_OSR(  768000, pll4, 4, 1, 176, LOW),
	F_AIF_OSR( 1024000, pll4, 4, 1, 132, LOW),
	F_AIF_OSR( 1536000, pll4, 4, 1,  88, LOW),
	F_AIF_OSR( 2048000, pll4, 4, 1,  66, LOW),
	F_AIF_OSR( 3072000, pll4, 4, 1,  44, LOW),
	F_AIF_OSR( 4096000, pll4, 4, 1,  33, LOW),
	F_AIF_OSR( 6144000, pll4, 4, 1,  22, LOW),
	F_AIF_OSR( 8192000, pll4, 2, 1,  33, LOW),
	F_AIF_OSR(12288000, pll4, 4, 1,  11, LOW),
	F_AIF_OSR(24576000, pll4, 2, 1,  11, LOW),
	F_END
};

#define CLK_AIF_OSR(i, ns, md, h_r, tv) \
	struct clk_local i##_clk = { \
		.b = { \
			.en_reg = ns, \
			.en_mask = BIT(17), \
			.reset_reg = ns, \
			.reset_mask = BIT(19), \
			.halt_reg = h_r, \
			.halt_check = ENABLE, \
			.halt_bit = 1, \
			.test_vector = tv, \
		}, \
		.ns_reg = ns, \
		.md_reg = md, \
		.root_en_mask = BIT(9), \
		.ns_mask = (BM(31, 24) | BM(6, 0)), \
		.set_rate = set_rate_mnd, \
		.freq_tbl = clk_tbl_aif_osr, \
		.current_freq = &local_dummy_freq, \
		.c = { \
			.dbg_name = #i "_clk", \
			.ops = &soc_clk_ops_8x60, \
			.flags = CLKFLAG_AUTO_OFF, \
			CLK_INIT(i##_clk.c), \
		}, \
	}

#define F_AIF_BIT(d, s) \
	{ \
		.freq_hz = d, \
		.ns_val = (BVAL(14, 14, s) | BVAL(13, 10, (d-1))) \
	}
static struct clk_freq_tbl clk_tbl_aif_bit[] = {
	F_AIF_BIT(0, 1),  /* Use external clock. */
	F_AIF_BIT(1, 0),  F_AIF_BIT(2, 0),  F_AIF_BIT(3, 0),  F_AIF_BIT(4, 0),
	F_AIF_BIT(5, 0),  F_AIF_BIT(6, 0),  F_AIF_BIT(7, 0),  F_AIF_BIT(8, 0),
	F_AIF_BIT(9, 0),  F_AIF_BIT(10, 0), F_AIF_BIT(11, 0), F_AIF_BIT(12, 0),
	F_AIF_BIT(13, 0), F_AIF_BIT(14, 0), F_AIF_BIT(15, 0), F_AIF_BIT(16, 0),
	F_END
};

#define CLK_AIF_BIT(i, ns, h_r, tv) \
	struct clk_local i##_clk = { \
		.b = { \
			.en_reg = ns, \
			.en_mask = BIT(15), \
			.halt_reg = h_r, \
			.halt_check = DELAY, \
			.test_vector = tv, \
		}, \
		.ns_reg = ns, \
		.ns_mask = BM(14, 10), \
		.set_rate = set_rate_nop, \
		.freq_tbl = clk_tbl_aif_bit, \
		.current_freq = &local_dummy_freq, \
		.c = { \
			.dbg_name = #i "_clk", \
			.ops = &soc_clk_ops_8x60, \
			.flags = CLKFLAG_AUTO_OFF, \
			CLK_INIT(i##_clk.c), \
		}, \
	}

static CLK_AIF_OSR(mi2s_osr, LCC_MI2S_NS_REG, LCC_MI2S_MD_REG,
		LCC_MI2S_STATUS_REG, TEST_LPA(0x0A));
static CLK_AIF_BIT(mi2s_bit, LCC_MI2S_NS_REG, LCC_MI2S_STATUS_REG,
		TEST_LPA(0x0B));

static CLK_AIF_OSR(codec_i2s_mic_osr, LCC_CODEC_I2S_MIC_NS_REG,
		LCC_CODEC_I2S_MIC_MD_REG, LCC_CODEC_I2S_MIC_STATUS_REG,
		TEST_LPA(0x0C));
static CLK_AIF_BIT(codec_i2s_mic_bit, LCC_CODEC_I2S_MIC_NS_REG,
		LCC_CODEC_I2S_MIC_STATUS_REG, TEST_LPA(0x0D));

static CLK_AIF_OSR(spare_i2s_mic_osr, LCC_SPARE_I2S_MIC_NS_REG,
		LCC_SPARE_I2S_MIC_MD_REG, LCC_SPARE_I2S_MIC_STATUS_REG,
		TEST_LPA(0x10));
static CLK_AIF_BIT(spare_i2s_mic_bit, LCC_SPARE_I2S_MIC_NS_REG,
		LCC_SPARE_I2S_MIC_STATUS_REG, TEST_LPA(0x11));

static CLK_AIF_OSR(codec_i2s_spkr_osr, LCC_CODEC_I2S_SPKR_NS_REG,
		LCC_CODEC_I2S_SPKR_MD_REG, LCC_CODEC_I2S_SPKR_STATUS_REG,
		TEST_LPA(0x0E));
static CLK_AIF_BIT(codec_i2s_spkr_bit, LCC_CODEC_I2S_SPKR_NS_REG,
		LCC_CODEC_I2S_SPKR_STATUS_REG, TEST_LPA(0x0F));

static CLK_AIF_OSR(spare_i2s_spkr_osr, LCC_SPARE_I2S_SPKR_NS_REG,
		LCC_SPARE_I2S_SPKR_MD_REG, LCC_SPARE_I2S_SPKR_STATUS_REG,
		TEST_LPA(0x12));
static CLK_AIF_BIT(spare_i2s_spkr_bit, LCC_SPARE_I2S_SPKR_NS_REG,
		LCC_SPARE_I2S_SPKR_STATUS_REG, TEST_LPA(0x13));

#define F_PCM(f, s, d, m, n, v) \
	{ \
		.freq_hz = f, \
		.src_clk = &s##_clk.c, \
		.md_val = MD16(m, n), \
		.ns_val = NS(31, 16, n, m, 5, 4, 3, d, 2, 0, s##_to_lpa_mux), \
		.mnd_en_mask = BIT(8) * !!(n), \
		.sys_vdd = v, \
	}
static struct clk_freq_tbl clk_tbl_pcm[] = {
	F_PCM(       0, gnd,  1, 0,   0, NONE),
	F_PCM(  512000, pll4, 4, 1, 264, LOW),
	F_PCM(  768000, pll4, 4, 1, 176, LOW),
	F_PCM( 1024000, pll4, 4, 1, 132, LOW),
	F_PCM( 1536000, pll4, 4, 1,  88, LOW),
	F_PCM( 2048000, pll4, 4, 1,  66, LOW),
	F_PCM( 3072000, pll4, 4, 1,  44, LOW),
	F_PCM( 4096000, pll4, 4, 1,  33, LOW),
	F_PCM( 6144000, pll4, 4, 1,  22, LOW),
	F_PCM( 8192000, pll4, 2, 1,  33, LOW),
	F_PCM(12288000, pll4, 4, 1,  11, LOW),
	F_PCM(24580000, pll4, 2, 1,  11, LOW),
	F_END
};

struct clk_local pcm_clk = {
	.b = {
		.en_reg = LCC_PCM_NS_REG,
		.en_mask = BIT(11),
		.reset_reg = LCC_PCM_NS_REG,
		.reset_mask = BIT(13),
		.halt_reg = LCC_PCM_STATUS_REG,
		.halt_check = ENABLE,
		.halt_bit = 0,
		.test_vector = TEST_LPA(0x14),
	},
	.ns_reg = LCC_PCM_NS_REG,
	.md_reg = LCC_PCM_MD_REG,
	.root_en_mask = BIT(9),
	.ns_mask = (BM(31, 16) | BM(6, 0)),
	.set_rate = set_rate_mnd,
	.freq_tbl = clk_tbl_pcm,
	.current_freq = &local_dummy_freq,
	.c = {
		.dbg_name = "pcm_clk",
		.ops = &soc_clk_ops_8x60,
		.flags = CLKFLAG_AUTO_OFF,
		CLK_INIT(pcm_clk.c),
	},
};

DEFINE_CLK_RPM(afab_clk, afab_a_clk, APPS_FABRIC);
DEFINE_CLK_RPM(cfpb_clk, cfpb_a_clk, CFPB);
DEFINE_CLK_RPM(dfab_clk, dfab_a_clk, DAYTONA_FABRIC);
DEFINE_CLK_RPM(ebi1_clk, ebi1_a_clk, EBI1);
DEFINE_CLK_RPM(mmfab_clk, mmfab_a_clk, MM_FABRIC);
DEFINE_CLK_RPM(mmfpb_clk, mmfpb_a_clk, MMFPB);
DEFINE_CLK_RPM(sfab_clk, sfab_a_clk, SYSTEM_FABRIC);
DEFINE_CLK_RPM(sfpb_clk, sfpb_a_clk, SFPB);
DEFINE_CLK_RPM(smi_clk, smi_a_clk, SMI);

static DEFINE_CLK_VOTER(dfab_dsps_clk, &dfab_clk.c);
static DEFINE_CLK_VOTER(dfab_usb_hs_clk, &dfab_clk.c);
static DEFINE_CLK_VOTER(dfab_sdc1_clk, &dfab_clk.c);
static DEFINE_CLK_VOTER(dfab_sdc2_clk, &dfab_clk.c);
static DEFINE_CLK_VOTER(dfab_sdc3_clk, &dfab_clk.c);
static DEFINE_CLK_VOTER(dfab_sdc4_clk, &dfab_clk.c);
static DEFINE_CLK_VOTER(dfab_sdc5_clk, &dfab_clk.c);

struct clk_lookup msm_clocks_8x60[] = {
	CLK_LOOKUP("cxo",		cxo_clk.c,	NULL),
	CLK_LOOKUP("pll4",		pll4_clk.c,	NULL),
	CLK_LOOKUP("pll4",		pll4_clk.c,	"peripheral-reset"),

	CLK_LOOKUP("afab_clk",		afab_clk.c,	NULL),
	CLK_LOOKUP("afab_a_clk",	afab_a_clk.c,	NULL),
	CLK_LOOKUP("cfpb_clk",		cfpb_clk.c,	NULL),
	CLK_LOOKUP("cfpb_a_clk",	cfpb_a_clk.c,	NULL),
	CLK_LOOKUP("dfab_clk",		dfab_clk.c,	NULL),
	CLK_LOOKUP("dfab_a_clk",	dfab_a_clk.c,	NULL),
	CLK_LOOKUP("ebi1_clk",		ebi1_clk.c,	NULL),
	CLK_LOOKUP("ebi1_a_clk",	ebi1_a_clk.c,	NULL),
	CLK_LOOKUP("mmfab_clk",		mmfab_clk.c,	NULL),
	CLK_LOOKUP("mmfab_a_clk",	mmfab_a_clk.c,	NULL),
	CLK_LOOKUP("mmfpb_clk",		mmfpb_clk.c,	NULL),
	CLK_LOOKUP("mmfpb_a_clk",	mmfpb_a_clk.c,	NULL),
	CLK_LOOKUP("sfab_clk",		sfab_clk.c,	NULL),
	CLK_LOOKUP("sfab_a_clk",	sfab_a_clk.c,	NULL),
	CLK_LOOKUP("sfpb_clk",		sfpb_clk.c,	NULL),
	CLK_LOOKUP("sfpb_a_clk",	sfpb_a_clk.c,	NULL),
	CLK_LOOKUP("smi_clk",		smi_clk.c,	NULL),
	CLK_LOOKUP("smi_a_clk",		smi_a_clk.c,	NULL),

	CLK_LOOKUP("gsbi_uart_clk",	gsbi1_uart_clk.c,		NULL),
	CLK_LOOKUP("gsbi_uart_clk",	gsbi2_uart_clk.c,		NULL),
	CLK_LOOKUP("gsbi_uart_clk",	gsbi3_uart_clk.c, "msm_serial_hsl.2"),
	CLK_LOOKUP("gsbi_uart_clk",	gsbi4_uart_clk.c,		NULL),
	CLK_LOOKUP("gsbi_uart_clk",	gsbi5_uart_clk.c,		NULL),
	CLK_LOOKUP("uartdm_clk",	gsbi6_uart_clk.c, "msm_serial_hs.0"),
	CLK_LOOKUP("gsbi_uart_clk",	gsbi7_uart_clk.c,		NULL),
	CLK_LOOKUP("gsbi_uart_clk",	gsbi8_uart_clk.c,		NULL),
	CLK_LOOKUP("gsbi_uart_clk",	gsbi9_uart_clk.c, "msm_serial_hsl.1"),
	CLK_LOOKUP("gsbi_uart_clk",	gsbi10_uart_clk.c,	NULL),
	CLK_LOOKUP("gsbi_uart_clk",	gsbi11_uart_clk.c,	NULL),
	CLK_LOOKUP("gsbi_uart_clk",	gsbi12_uart_clk.c, "msm_serial_hsl.0"),
	CLK_LOOKUP("spi_clk",		gsbi1_qup_clk.c, "spi_qsd.0"),
	CLK_LOOKUP("gsbi_qup_clk",	gsbi2_qup_clk.c,		NULL),
	CLK_LOOKUP("gsbi_qup_clk",	gsbi3_qup_clk.c, "qup_i2c.0"),
	CLK_LOOKUP("gsbi_qup_clk",	gsbi4_qup_clk.c, "qup_i2c.1"),
	CLK_LOOKUP("gsbi_qup_clk",	gsbi5_qup_clk.c,		NULL),
	CLK_LOOKUP("gsbi_qup_clk",	gsbi6_qup_clk.c,		NULL),
	CLK_LOOKUP("gsbi_qup_clk",	gsbi7_qup_clk.c, "qup_i2c.4"),
	CLK_LOOKUP("gsbi_qup_clk",	gsbi8_qup_clk.c, "qup_i2c.3"),
	CLK_LOOKUP("gsbi_qup_clk",	gsbi9_qup_clk.c, "qup_i2c.2"),
	CLK_LOOKUP("spi_clk",		gsbi10_qup_clk.c,	"spi_qsd.1"),
	CLK_LOOKUP("gsbi_qup_clk",	gsbi11_qup_clk.c,		NULL),
	CLK_LOOKUP("gsbi_qup_clk",	gsbi12_qup_clk.c,	"msm_dsps.0"),
	CLK_LOOKUP("gsbi_qup_clk",	gsbi12_qup_clk.c, "qup_i2c.5"),
	CLK_LOOKUP("pdm_clk",		pdm_clk.c,		NULL),
	CLK_LOOKUP("pmem_clk",		pmem_clk.c,		NULL),
	CLK_LOOKUP("prng_clk",		prng_clk.c,		NULL),
	CLK_LOOKUP("sdc_clk",		sdc1_clk.c, "msm_sdcc.1"),
	CLK_LOOKUP("sdc_clk",		sdc2_clk.c, "msm_sdcc.2"),
	CLK_LOOKUP("sdc_clk",		sdc3_clk.c, "msm_sdcc.3"),
	CLK_LOOKUP("sdc_clk",		sdc4_clk.c, "msm_sdcc.4"),
	CLK_LOOKUP("sdc_clk",		sdc5_clk.c, "msm_sdcc.5"),
	CLK_LOOKUP("tsif_ref_clk",	tsif_ref_clk.c,		NULL),
	CLK_LOOKUP("tssc_clk",		tssc_clk.c,		NULL),
	CLK_LOOKUP("usb_hs_clk",	usb_hs1_xcvr_clk.c,	NULL),
	CLK_LOOKUP("usb_phy_clk",	usb_phy0_clk.c,		NULL),
	CLK_LOOKUP("usb_fs_clk",	usb_fs1_xcvr_clk.c,	NULL),
	CLK_LOOKUP("usb_fs_sys_clk",	usb_fs1_sys_clk.c,	NULL),
	CLK_LOOKUP("usb_fs_src_clk",	usb_fs1_src_clk.c,	NULL),
	CLK_LOOKUP("usb_fs_clk",	usb_fs2_xcvr_clk.c,	NULL),
	CLK_LOOKUP("usb_fs_sys_clk",	usb_fs2_sys_clk.c,	NULL),
	CLK_LOOKUP("usb_fs_src_clk",	usb_fs2_src_clk.c,	NULL),
	CLK_LOOKUP("ce_clk",		ce2_p_clk.c,		NULL),
	CLK_LOOKUP("spi_pclk",		gsbi1_p_clk.c, "spi_qsd.0"),
	CLK_LOOKUP("gsbi_pclk",		gsbi2_p_clk.c,		NULL),
	CLK_LOOKUP("gsbi_pclk",		gsbi3_p_clk.c, "msm_serial_hsl.2"),
	CLK_LOOKUP("gsbi_pclk",		gsbi3_p_clk.c, "qup_i2c.0"),
	CLK_LOOKUP("gsbi_pclk",		gsbi4_p_clk.c, "qup_i2c.1"),
	CLK_LOOKUP("gsbi_pclk",		gsbi5_p_clk.c,		NULL),
	CLK_LOOKUP("uartdm_pclk",	gsbi6_p_clk.c, "msm_serial_hs.0"),
	CLK_LOOKUP("gsbi_pclk",		gsbi7_p_clk.c, "qup_i2c.4"),
	CLK_LOOKUP("gsbi_pclk",		gsbi8_p_clk.c, "qup_i2c.3"),
	CLK_LOOKUP("gsbi_pclk",		gsbi9_p_clk.c, "msm_serial_hsl.1"),
	CLK_LOOKUP("gsbi_pclk",		gsbi9_p_clk.c, "qup_i2c.2"),
	CLK_LOOKUP("spi_pclk",		gsbi10_p_clk.c, "spi_qsd.1"),
	CLK_LOOKUP("gsbi_pclk",		gsbi11_p_clk.c,		NULL),
	CLK_LOOKUP("gsbi_pclk",		gsbi12_p_clk.c, "msm_dsps.0"),
	CLK_LOOKUP("gsbi_pclk",		gsbi12_p_clk.c, "msm_serial_hsl.0"),
	CLK_LOOKUP("gsbi_pclk",		gsbi12_p_clk.c, "qup_i2c.5"),
	CLK_LOOKUP("ppss_pclk",		ppss_p_clk.c,		NULL),
	CLK_LOOKUP("tsif_pclk",		tsif_p_clk.c,		NULL),
	CLK_LOOKUP("usb_fs_pclk",	usb_fs1_p_clk.c,		NULL),
	CLK_LOOKUP("usb_fs_pclk",	usb_fs2_p_clk.c,		NULL),
	CLK_LOOKUP("usb_hs_pclk",	usb_hs1_p_clk.c,		NULL),
	CLK_LOOKUP("sdc_pclk",		sdc1_p_clk.c, "msm_sdcc.1"),
	CLK_LOOKUP("sdc_pclk",		sdc2_p_clk.c, "msm_sdcc.2"),
	CLK_LOOKUP("sdc_pclk",		sdc3_p_clk.c, "msm_sdcc.3"),
	CLK_LOOKUP("sdc_pclk",		sdc4_p_clk.c, "msm_sdcc.4"),
	CLK_LOOKUP("sdc_pclk",		sdc5_p_clk.c, "msm_sdcc.5"),
	CLK_LOOKUP("adm_clk",		adm0_clk.c, "msm_dmov.0"),
	CLK_LOOKUP("adm_pclk",		adm0_p_clk.c, "msm_dmov.0"),
	CLK_LOOKUP("adm_clk",		adm1_clk.c, "msm_dmov.1"),
	CLK_LOOKUP("adm_pclk",		adm1_p_clk.c, "msm_dmov.1"),
	CLK_LOOKUP("modem_ahb1_pclk",	modem_ahb1_p_clk.c,	NULL),
	CLK_LOOKUP("modem_ahb2_pclk",	modem_ahb2_p_clk.c,	NULL),
	CLK_LOOKUP("pmic_arb_pclk",	pmic_arb0_p_clk.c,	NULL),
	CLK_LOOKUP("pmic_arb_pclk",	pmic_arb1_p_clk.c,	NULL),
	CLK_LOOKUP("pmic_ssbi2",	pmic_ssbi2_clk.c,		NULL),
	CLK_LOOKUP("rpm_msg_ram_pclk",	rpm_msg_ram_p_clk.c,	NULL),
	CLK_LOOKUP("amp_clk",		amp_clk.c,		NULL),
	CLK_LOOKUP("cam_clk",		cam_clk.c,		NULL),
	CLK_LOOKUP("csi_clk",		csi0_clk.c,		NULL),
	CLK_LOOKUP("csi_clk",		csi1_clk.c, "msm_camera_ov7692.0"),
	CLK_LOOKUP("csi_clk",		csi1_clk.c, "msm_camera_ov9726.0"),
	CLK_LOOKUP("csi_src_clk",	csi_src_clk.c,		NULL),
	CLK_LOOKUP("dsi_byte_div_clk",	dsi_byte_clk.c,		NULL),
	CLK_LOOKUP("dsi_esc_clk",	dsi_esc_clk.c,		NULL),
	CLK_LOOKUP("gfx2d0_clk",	gfx2d0_clk.c,		NULL),
	CLK_LOOKUP("gfx2d1_clk",	gfx2d1_clk.c,		NULL),
	CLK_LOOKUP("gfx3d_clk",		gfx3d_clk.c,		NULL),
	CLK_LOOKUP("ijpeg_clk",		ijpeg_clk.c,		NULL),
	CLK_LOOKUP("jpegd_clk",		jpegd_clk.c,		NULL),
	CLK_LOOKUP("mdp_clk",		mdp_clk.c,		NULL),
	CLK_LOOKUP("mdp_vsync_clk",	mdp_vsync_clk.c,		NULL),
	CLK_LOOKUP("pixel_lcdc_clk",	pixel_lcdc_clk.c,		NULL),
	CLK_LOOKUP("pixel_mdp_clk",	pixel_mdp_clk.c,		NULL),
	CLK_LOOKUP("rot_clk",		rot_clk.c,		NULL),
	CLK_LOOKUP("tv_enc_clk",	tv_enc_clk.c,		NULL),
	CLK_LOOKUP("tv_dac_clk",	tv_dac_clk.c,		NULL),
	CLK_LOOKUP("vcodec_clk",	vcodec_clk.c,		NULL),
	CLK_LOOKUP("mdp_tv_clk",	mdp_tv_clk.c,		NULL),
	CLK_LOOKUP("hdmi_clk",		hdmi_tv_clk.c,		NULL),
	CLK_LOOKUP("tv_src_clk",	tv_src_clk.c,		NULL),
	CLK_LOOKUP("hdmi_app_clk",	hdmi_app_clk.c,		NULL),
	CLK_LOOKUP("vpe_clk",		vpe_clk.c,		NULL),
	CLK_LOOKUP("csi_vfe_clk",	csi0_vfe_clk.c,		NULL),
	CLK_LOOKUP("csi_vfe_clk",	csi1_vfe_clk.c, "msm_camera_ov7692.0"),
	CLK_LOOKUP("csi_vfe_clk",	csi1_vfe_clk.c, "msm_camera_ov9726.0"),
	CLK_LOOKUP("vfe_clk",		vfe_clk.c,		NULL),
	CLK_LOOKUP("smmu_jpegd_clk",	jpegd_axi_clk.c,		NULL),
	CLK_LOOKUP("smmu_vfe_clk",	vfe_axi_clk.c,		NULL),
	CLK_LOOKUP("vfe_axi_clk",	vfe_axi_clk.c,		NULL),
	CLK_LOOKUP("ijpeg_axi_clk",	ijpeg_axi_clk.c,		NULL),
	CLK_LOOKUP("imem_axi_clk",	imem_axi_clk.c,		NULL),
	CLK_LOOKUP("mdp_axi_clk",	mdp_axi_clk.c,		NULL),
	CLK_LOOKUP("rot_axi_clk",	rot_axi_clk.c,		NULL),
	CLK_LOOKUP("vcodec_axi_clk",	vcodec_axi_clk.c,		NULL),
	CLK_LOOKUP("vpe_axi_clk",	vpe_axi_clk.c,		NULL),
	CLK_LOOKUP("amp_pclk",		amp_p_clk.c,		NULL),
	CLK_LOOKUP("csi_pclk",		csi0_p_clk.c,		NULL),
	CLK_LOOKUP("csi_pclk",		csi1_p_clk.c, "msm_camera_ov7692.0"),
	CLK_LOOKUP("csi_pclk",		csi1_p_clk.c, "msm_camera_ov9726.0"),
	CLK_LOOKUP("dsi_m_pclk",	dsi_m_p_clk.c,		NULL),
	CLK_LOOKUP("dsi_s_pclk",	dsi_s_p_clk.c,		NULL),
	CLK_LOOKUP("gfx2d0_pclk",	gfx2d0_p_clk.c,		NULL),
	CLK_LOOKUP("gfx2d1_pclk",	gfx2d1_p_clk.c,		NULL),
	CLK_LOOKUP("gfx3d_pclk",	gfx3d_p_clk.c,		NULL),
	CLK_LOOKUP("hdmi_m_pclk",	hdmi_m_p_clk.c,		NULL),
	CLK_LOOKUP("hdmi_s_pclk",	hdmi_s_p_clk.c,		NULL),
	CLK_LOOKUP("ijpeg_pclk",	ijpeg_p_clk.c,		NULL),
	CLK_LOOKUP("jpegd_pclk",	jpegd_p_clk.c,		NULL),
	CLK_LOOKUP("imem_pclk",		imem_p_clk.c,		NULL),
	CLK_LOOKUP("mdp_pclk",		mdp_p_clk.c,		NULL),
	CLK_LOOKUP("smmu_pclk",		smmu_p_clk.c,		NULL),
	CLK_LOOKUP("rotator_pclk",	rot_p_clk.c,		NULL),
	CLK_LOOKUP("tv_enc_pclk",	tv_enc_p_clk.c,		NULL),
	CLK_LOOKUP("vcodec_pclk",	vcodec_p_clk.c,		NULL),
	CLK_LOOKUP("vfe_pclk",		vfe_p_clk.c,		NULL),
	CLK_LOOKUP("vpe_pclk",		vpe_p_clk.c,		NULL),
	CLK_LOOKUP("mi2s_osr_clk",	mi2s_osr_clk.c,		NULL),
	CLK_LOOKUP("mi2s_bit_clk",	mi2s_bit_clk.c,		NULL),
	CLK_LOOKUP("i2s_mic_osr_clk",	codec_i2s_mic_osr_clk.c,	NULL),
	CLK_LOOKUP("i2s_mic_bit_clk",	codec_i2s_mic_bit_clk.c,	NULL),
	CLK_LOOKUP("i2s_mic_osr_clk",	spare_i2s_mic_osr_clk.c,	NULL),
	CLK_LOOKUP("i2s_mic_bit_clk",	spare_i2s_mic_bit_clk.c,	NULL),
	CLK_LOOKUP("i2s_spkr_osr_clk",	codec_i2s_spkr_osr_clk.c,	NULL),
	CLK_LOOKUP("i2s_spkr_bit_clk",	codec_i2s_spkr_bit_clk.c,	NULL),
	CLK_LOOKUP("i2s_spkr_osr_clk",	spare_i2s_spkr_osr_clk.c,	NULL),
	CLK_LOOKUP("i2s_spkr_bit_clk",	spare_i2s_spkr_bit_clk.c,	NULL),
	CLK_LOOKUP("pcm_clk",		pcm_clk.c,		NULL),
	CLK_LOOKUP("iommu_clk",		jpegd_axi_clk.c, "msm_iommu.0"),
	CLK_LOOKUP("iommu_clk",		mdp_axi_clk.c, "msm_iommu.2"),
	CLK_LOOKUP("iommu_clk",		mdp_axi_clk.c, "msm_iommu.3"),
	CLK_LOOKUP("iommu_clk",		ijpeg_axi_clk.c, "msm_iommu.5"),
	CLK_LOOKUP("iommu_clk",		vfe_axi_clk.c, "msm_iommu.6"),
	CLK_LOOKUP("iommu_clk",		vcodec_axi_clk.c, "msm_iommu.7"),
	CLK_LOOKUP("iommu_clk",		vcodec_axi_clk.c, "msm_iommu.8"),
	CLK_LOOKUP("iommu_clk",		gfx3d_clk.c, "msm_iommu.9"),
	CLK_LOOKUP("iommu_clk",		gfx2d0_clk.c, "msm_iommu.10"),
	CLK_LOOKUP("iommu_clk",		gfx2d1_clk.c, "msm_iommu.11"),

	CLK_LOOKUP("dfab_dsps_clk",	dfab_dsps_clk.c, NULL),
	CLK_LOOKUP("dfab_usb_hs_clk",	dfab_usb_hs_clk.c, NULL),
	CLK_LOOKUP("dfab_sdc_clk",	dfab_sdc1_clk.c, "msm_sdcc.1"),
	CLK_LOOKUP("dfab_sdc_clk",	dfab_sdc2_clk.c, "msm_sdcc.2"),
	CLK_LOOKUP("dfab_sdc_clk",	dfab_sdc3_clk.c, "msm_sdcc.3"),
	CLK_LOOKUP("dfab_sdc_clk",	dfab_sdc4_clk.c, "msm_sdcc.4"),
	CLK_LOOKUP("dfab_sdc_clk",	dfab_sdc5_clk.c, "msm_sdcc.5"),
};
unsigned msm_num_clocks_8x60 = ARRAY_SIZE(msm_clocks_8x60);

/*
 * Miscellaneous clock register initializations
 */

/* Read, modify, then write-back a register. */
static void rmwreg(uint32_t val, void *reg, uint32_t mask)
{
	uint32_t regval = readl_relaxed(reg);
	regval &= ~mask;
	regval |= val;
	writel_relaxed(regval, reg);
}

static void reg_init(void)
{
	/* Setup MM_PLL2 (PLL3), but turn it off. Rate set by set_rate_tv(). */
	rmwreg(0, MM_PLL2_MODE_REG, BIT(0)); /* Disable output */
	/* Set ref, bypass, assert reset, disable output, disable test mode */
	writel_relaxed(0, MM_PLL2_MODE_REG); /* PXO */
	writel_relaxed(0x00800000, MM_PLL2_CONFIG_REG); /* Enable main out. */

	/* TODO:
	 * The ADM clock votes below should removed once all users of the ADMs
	 * begin voting for the clocks appropriately.
	 */
	/* The clock driver doesn't use SC1's voting register to control
	 * HW-voteable clocks.  Clear its bits so that disabling bits in the
	 * SC0 register will cause the corresponding clocks to be disabled. */
	rmwreg(BIT(12)|BIT(11), SC0_U_CLK_BRANCH_ENA_VOTE_REG, BM(12, 11));
	writel_relaxed(BIT(12)|BIT(11)|BM(5, 2), SC1_U_CLK_BRANCH_ENA_VOTE_REG);
	/* Let sc_aclk and sc_clk halt when both Scorpions are collapsed. */
	writel_relaxed(BIT(12)|BIT(11), SC0_U_CLK_SLEEP_ENA_VOTE_REG);
	writel_relaxed(BIT(12)|BIT(11), SC1_U_CLK_SLEEP_ENA_VOTE_REG);

	/* Deassert MM SW_RESET_ALL signal. */
	writel_relaxed(0, SW_RESET_ALL_REG);

	/* Initialize MM AHB registers: Enable the FPB clock and disable HW
	 * gating for all clocks. Also set VFE_AHB's FORCE_CORE_ON bit to
	 * prevent its memory from being collapsed when the clock is halted.
	 * The sleep and wake-up delays are set to safe values. */
	rmwreg(0x00000003, AHB_EN_REG,  0x0F7FFFFF);
	rmwreg(0x000007F9, AHB_EN2_REG, 0x7FFFBFFF);

	/* Deassert all locally-owned MM AHB resets. */
	rmwreg(0, SW_RESET_AHB_REG, 0xFFF7DFFF);

	/* Initialize MM AXI registers: Enable HW gating for all clocks that
	 * support it. Also set FORCE_CORE_ON bits, and any sleep and wake-up
	 * delays to safe values. */
	rmwreg(0x000207F9, MAXI_EN_REG,  0x0FFFFFFF);
	/* MAXI_EN2_REG is owned by the RPM.  Don't touch it. */
	writel_relaxed(0x3FE7FCFF, MAXI_EN3_REG);
	writel_relaxed(0x000001D8, SAXI_EN_REG);

	/* Initialize MM CC registers: Set MM FORCE_CORE_ON bits so that core
	 * memories retain state even when not clocked. Also, set sleep and
	 * wake-up delays to safe values. */
	writel_relaxed(0x00000000, CSI_CC_REG);
	rmwreg(0x00000000, MISC_CC_REG,  0xFEFFF3FF);
	rmwreg(0x000007FD, MISC_CC2_REG, 0xFFFF7FFF);
	writel_relaxed(0x80FF0000, GFX2D0_CC_REG);
	writel_relaxed(0x80FF0000, GFX2D1_CC_REG);
	writel_relaxed(0x80FF0000, GFX3D_CC_REG);
	writel_relaxed(0x80FF0000, IJPEG_CC_REG);
	writel_relaxed(0x80FF0000, JPEGD_CC_REG);
	/* MDP and PIXEL clocks may be running at boot, don't turn them off. */
	rmwreg(0x80FF0000, MDP_CC_REG,   BM(31, 29) | BM(23, 16));
	rmwreg(0x80FF0000, PIXEL_CC_REG, BM(31, 29) | BM(23, 16));
	writel_relaxed(0x000004FF, PIXEL_CC2_REG);
	writel_relaxed(0x80FF0000, ROT_CC_REG);
	writel_relaxed(0x80FF0000, TV_CC_REG);
	writel_relaxed(0x000004FF, TV_CC2_REG);
	writel_relaxed(0xC0FF0000, VCODEC_CC_REG);
	writel_relaxed(0x80FF0000, VFE_CC_REG);
	writel_relaxed(0x80FF0000, VPE_CC_REG);

	/* De-assert MM AXI resets to all hardware blocks. */
	writel_relaxed(0, SW_RESET_AXI_REG);

	/* Deassert all MM core resets. */
	writel_relaxed(0, SW_RESET_CORE_REG);

	/* Reset 3D core once more, with its clock enabled. This can
	 * eventually be done as part of the GDFS footswitch driver. */
	clk_set_rate(&gfx3d_clk.c, 27000000);
	clk_enable(&gfx3d_clk.c);
	writel_relaxed(BIT(12), SW_RESET_CORE_REG);
	dsb();
	udelay(5);
	writel_relaxed(0, SW_RESET_CORE_REG);
	/* Make sure reset is de-asserted before clock is disabled. */
	mb();
	clk_disable(&gfx3d_clk.c);

	/* Enable TSSC and PDM PXO sources. */
	writel_relaxed(BIT(11), TSSC_CLK_CTL_REG);
	writel_relaxed(BIT(15), PDM_CLK_NS_REG);
	/* Set the dsi_byte_clk src to the DSI PHY PLL,
	 * dsi_esc_clk to PXO/2, and the hdmi_app_clk src to PXO */
	rmwreg(0x400001, MISC_CC2_REG, 0x424003);
}

/* Local clock driver initialization. */
void __init msm_clk_soc_init(void)
{
	xo_pxo = msm_xo_get(MSM_XO_PXO, "clock-8x60");
	if (IS_ERR(xo_pxo)) {
		pr_err("%s: msm_xo_get(PXO) failed.\n", __func__);
		BUG();
	}
	xo_cxo = msm_xo_get(MSM_XO_TCXO_D1, "clock-8x60");
	if (IS_ERR(xo_cxo)) {
		pr_err("%s: msm_xo_get(CXO) failed.\n", __func__);
		BUG();
	}

	local_vote_sys_vdd(HIGH);
	/* Initialize clock registers. */
	reg_init();

	/* Initialize rates for clocks that only support one. */
	clk_set_rate(&pdm_clk.c, 27000000);
	clk_set_rate(&prng_clk.c, 64000000);
	clk_set_rate(&mdp_vsync_clk.c, 27000000);
	clk_set_rate(&tsif_ref_clk.c, 105000);
	clk_set_rate(&tssc_clk.c, 27000000);
	clk_set_rate(&usb_hs1_xcvr_clk.c, 60000000);
	clk_set_rate(&usb_fs1_src_clk.c, 60000000);
	clk_set_rate(&usb_fs2_src_clk.c, 60000000);

	/* The halt status bits for PDM and TSSC may be incorrect at boot.
	 * Toggle these clocks on and off to refresh them. */
	local_clk_enable(&pdm_clk.c);
	local_clk_disable(&pdm_clk.c);
	local_clk_enable(&tssc_clk.c);
	local_clk_disable(&tssc_clk.c);
}

static int msm_clk_soc_late_init(void)
{
	return local_unvote_sys_vdd(HIGH);
}
late_initcall(msm_clk_soc_late_init);
