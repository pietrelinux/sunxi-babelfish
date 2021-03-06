#include <fixup.h>
#include <print.h>
#include <soc.h>
#include <types.h>
#include <version.h>

#include <libfdt.h>

#include <uapi/asm/setup.h>

#include <lib/libfex/script_bin.h>

#define SCRIPT_BASE_ADDRESS	(void*)0x43000000
#define FDT_BASE_ADDRESS	(void*)0x45000000

typedef void *(*kernel_entry)(u32 dummy, u32 machid, void *dtb);

extern u32 zImage_start;
extern u32 exec_base;

static inline void *abs_addr(void* rel_addr)
{
	return rel_addr + exec_base;
}

static void cortex_a7_init_quirks()
{
	/* Enable SMP mode for CPU0, by setting bit 6 of Auxiliary Ctl reg */
	asm volatile(
		"mrc p15, 0, r0, c1, c0, 1\n"
		"orr r0, r0, #0x40\n"
		"mcr p15, 0, r0, c1, c0, 1\n");

	/* Set arch timers frequency */
	asm volatile("mcr p15, 0, %0, c14, c0, 0"
		     :
		     : "r"(24000000));
}


void main(u32 dummy, u32 machid, const struct tag *tags)
	__attribute__((section(".main")));

void main(u32 dummy, u32 machid, const struct tag *tags)
{
	kernel_entry start_kernel;
	struct script *script;
	struct soc *soc;
	u32 cpuid, midr;
	void *abs_fdt;
	int ret;

	putstr("++ Allwinner Babelfish ");
	putstr(VERSION);
	putstr(" (FEX to DT translator) ++\n\n");

	soc = match_soc();
	if (!soc)
		return;

	putstr("Detected SoC: ");
	if (soc->compatible) {
		putstr(soc->compatible);
		putstr("\n");
	} else {
		putstr("Unknown.\n");
		return;
	}

	abs_fdt = abs_addr(soc->fdt);
	ret = fdt_open_into(abs_fdt, FDT_BASE_ADDRESS,
			    2 * fdt_totalsize(abs_fdt));
	if (ret) {
		putstr("Error opening the device tree\n");
		return;
	}

	ret = fdt_fixup_from_atags(FDT_BASE_ADDRESS, tags);
	if (ret) {
		putstr("Couldn't fixup the device tree with ATAGS\n");
		return;
	}

#ifndef CUSTOM_DTB
	script = script_new();
	ret = script_decompile_bin(SCRIPT_BASE_ADDRESS, script);
	if (!ret)
		return;

	ret = fdt_fixup_from_fex(soc, FDT_BASE_ADDRESS, script);
	if (ret) {
		putstr("Error in fdt_fixup ");
		printhex(ret);
		putstr("\n");
		return;
	}
#endif

	/* Read Main ID Register to identify CPU core */
	asm volatile (
		"mrc p15, 0, %0, c0, c0, 0\n"
		: "=r" (midr));

	cpuid = (midr & 0xffff) >> 4;
	if (cpuid == 0xc07) {
		putstr("Cortex-A7 based SoCs.... Applying appropriate init quirks\n");
		cortex_a7_init_quirks();
	}

	start_kernel = (kernel_entry)abs_addr(&zImage_start);
	putstr("Booting Linux...\n");
	start_kernel(0, 0xffffffff, FDT_BASE_ADDRESS);
}
