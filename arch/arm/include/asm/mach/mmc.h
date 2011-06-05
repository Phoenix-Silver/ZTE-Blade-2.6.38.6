/*
 *  arch/arm/include/asm/mach/mmc.h
 */
#ifndef ASMARM_MACH_MMC_H
#define ASMARM_MACH_MMC_H

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>

#define SDC_DAT1_DISABLE 0
#define SDC_DAT1_ENABLE  1
#define SDC_DAT1_ENWAKE  2
#define SDC_DAT1_DISWAKE 3

struct embedded_sdio_data {
        struct sdio_cis cis;
        struct sdio_cccr cccr;
        struct sdio_embedded_func *funcs;
        int num_funcs;
};

/* This structure keeps information per regulator */
struct msm_mmc_reg_data {
	/* voltage regulator handle */
	struct regulator *reg;
	/* regulator name */
	const char *name;
	/* voltage level to be set */
	unsigned int level;
	/* Load values for low power and high power mode */
	unsigned int lpm_uA;
	unsigned int hpm_uA;
	/*
	 * is set voltage supported for this regulator?
	 * false => set voltage is not supported
	 * true  => set voltage is supported
	 */
	bool set_voltage_sup;
	/* is this regulator enabled? */
	bool is_enabled;
	/* is this regulator needs to be always on? */
	bool always_on;
	/* is low power mode setting required for this regulator? */
	bool lpm_sup;
};

/*
 * This structure keeps information for all the
 * regulators required for a SDCC slot.
 */
struct msm_mmc_slot_reg_data {
	struct msm_mmc_reg_data *vdd_data; /* keeps VDD/VCC regulator info */
	struct msm_mmc_reg_data *vccq_data; /* keeps VCCQ regulator info */
	struct msm_mmc_reg_data *vddp_data; /* keeps VDD Pad regulator info */
};

struct mmc_platform_data {
	unsigned int ocr_mask;			/* available voltages */
	u32 (*translate_vdd)(struct device *, unsigned int);
	void (*sdio_lpm_gpio_setup)(struct device *, unsigned int);
	unsigned int (*status)(struct device *);
        unsigned int status_irq;
	unsigned int status_gpio;
        struct embedded_sdio_data *embedded_sdio;
        unsigned int sdiowakeup_irq;
	int (*register_status_notify)(void (*callback)(int card_present, void *dev_id), void *dev_id);
        unsigned long irq_flags;
        unsigned long mmc_bus_width;
        int (*wpswitch) (struct device *);
	int dummy52_required;
	unsigned int msmsdcc_fmin;
	unsigned int msmsdcc_fmid;
	unsigned int msmsdcc_fmax;
	bool nonremovable;
	bool pclk_src_dfab;
	int (*cfg_mpm_sdiowakeup)(struct device *, unsigned);
	bool sdcc_v4_sup;
	unsigned int wpswitch_gpio;
	unsigned char wpswitch_polarity;
	struct msm_mmc_slot_reg_data *vreg_data;
	int is_sdio_al_client;
};

#endif
