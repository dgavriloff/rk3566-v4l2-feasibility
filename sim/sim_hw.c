/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * VEPU540 hardware behaviour model for simulation.
 *
 * A flat 4 KiB memory region acts as the register file.  writel/readl
 * go straight to the buffer, except:
 *
 *   VERSION (0x000):  always returns a canned value reporting
 *                     H.264 + H.265 capability.
 *   INT_STA (0x01C):  set to ENC_DONE when ENC_STRT is written.
 *   INT_CLR (0x018):  writing clears bits in INT_STA.
 *   ST_BSL  (0x210):  filled with a fake bitstream length.
 *   ENC_CLR (0x008):  writing force-clear resets INT_STA.
 *
 * This is enough to exercise the full driver pipeline (register
 * programming → kick → IRQ readback) in userspace.
 */

#include "kstubs.h"

/* ---- simulated register file ---- */
#define SIM_REG_FILE_SIZE  (4 * 1024)

static u8  sim_regfile[SIM_REG_FILE_SIZE];
static u8 *sim_base;   /* set by sim_ioremap */

/* Event counters for verification */
unsigned int sim_enc_start_count;
unsigned int sim_int_clear_count;
unsigned int sim_force_clear_count;

/*
 * Canned VERSION register:  0x54_01_03_00
 *   [31:24] = 0x54  ("T" — Rockchip RKVENC signature)
 *   [9]     = 1     (H.265 capable)
 *   [8]     = 1     (H.264 capable)
 *   [7:0]   = 0x00  (sub-version)
 */
#define SIM_VERSION_VALUE   0x54000300

/* Fake bitstream output length (bytes) for successful encode */
#define SIM_BS_LENGTH       42000

/* Register offsets we intercept (must match rkvenc_regs.h) */
#define OFF_VERSION   0x0000
#define OFF_ENC_STRT  0x0004
#define OFF_ENC_CLR   0x0008
#define OFF_INT_EN    0x0010
#define OFF_INT_CLR   0x0018
#define OFF_INT_STA   0x001C
#define OFF_ST_BSL    0x0210

/* INT bits (must match rkvenc_regs.h) */
#define INT_ENC_DONE  BIT(0)

/* ---- MMIO helpers ---- */

void *sim_ioremap(size_t size)
{
	memset(sim_regfile, 0, sizeof(sim_regfile));
	sim_base = sim_regfile;
	sim_enc_start_count = 0;
	sim_int_clear_count = 0;
	sim_force_clear_count = 0;
	return sim_base;
}

void sim_iounmap(void *base)
{
	sim_base = NULL;
}

void sim_writel(u32 val, volatile void *addr)
{
	size_t off = (u8 *)addr - sim_base;

	if (off >= SIM_REG_FILE_SIZE) {
		fprintf(stderr, "[SIM] writel out of bounds: off=0x%zx\n", off);
		return;
	}

	/* Store the value */
	*(volatile u32 *)addr = val;

	/* ---- HW behaviour hooks ---- */
	switch (off) {
	case OFF_ENC_STRT:
		/*
		 * Hardware starts encoding.  In real HW this takes
		 * milliseconds; in sim we instantly set the result.
		 */
		sim_enc_start_count++;

		/* Set ENC_DONE in INT_STA */
		*(u32 *)(sim_base + OFF_INT_STA) |= INT_ENC_DONE;

		/* Fill fake bitstream length */
		*(u32 *)(sim_base + OFF_ST_BSL) = SIM_BS_LENGTH;

		fprintf(stderr, "[SIM] ENC_STRT written (val=0x%08x) → "
			"encode #%u complete, bs_len=%u\n",
			val, sim_enc_start_count, SIM_BS_LENGTH);
		break;

	case OFF_INT_CLR:
		/* Clear acknowledged interrupt bits */
		*(u32 *)(sim_base + OFF_INT_STA) &= ~val;
		sim_int_clear_count++;
		break;

	case OFF_ENC_CLR:
		if (val & BIT(1)) {   /* force clear */
			*(u32 *)(sim_base + OFF_INT_STA) = 0;
			sim_force_clear_count++;
			fprintf(stderr, "[SIM] force clear\n");
		}
		break;

	default:
		break;
	}
}

u32 sim_readl(const volatile void *addr)
{
	size_t off = (const u8 *)addr - sim_base;

	if (off >= SIM_REG_FILE_SIZE) {
		fprintf(stderr, "[SIM] readl out of bounds: off=0x%zx\n", off);
		return 0xDEAD;
	}

	/* Intercept VERSION */
	if (off == OFF_VERSION)
		return SIM_VERSION_VALUE;

	return *(const volatile u32 *)addr;
}

/* ---- Register dump helper ---- */
void sim_dump_regs(unsigned int start, unsigned int end)
{
	unsigned int i;

	fprintf(stderr, "\n=== Register dump (reg%03u–reg%03u) ===\n",
		start, end);
	for (i = start; i <= end; i++) {
		u32 val = *(u32 *)(sim_base + i * 4);
		if (val != 0)
			fprintf(stderr, "  reg%03u [0x%03x] = 0x%08x\n",
				i, i * 4, val);
	}
	fprintf(stderr, "=== end dump ===\n\n");
}

/* Read a register by index (for test assertions) */
u32 sim_read_reg(unsigned int idx)
{
	return *(u32 *)(sim_base + idx * 4);
}
