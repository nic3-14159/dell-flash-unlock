/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: 2023 Nicholas Chin */

#include <sys/mman.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "accessors.h"

int get_fdo_status(void);
int check_lpc_decode(void);
void ec_set_fdo(void);
void write_ec_reg(uint8_t index, uint8_t data);
void send_ec_cmd(uint8_t cmd);
int wait_ec(void);
int check_bios_write_en(void);
int set_gbl_smi_en(int enable);
int get_gbl_smi_en(void);

#define EC_INDEX 0x910
#define EC_DATA 0x911
#define EC_ENABLE_FDO 2

#define LPC_DEV PCI_DEV(0, 0x1f, 0)

#define RCBA_MMIO_LEN 0x4000

/* Register offsets */
#define SPIBAR 0x3800
#define HSFS_REG  0x04
#define SMI_EN_REG 0x30

volatile uint8_t *rcba_mmio;
uint16_t pmbase;

int
main(int argc, char *argv[])
{
	int devmemfd;
	(void)argc;
	(void)argv;

	if (sys_iopl(3) == -1)
		err(errno, "Could not access IO ports");
	if ((devmemfd = open("/dev/mem", O_RDONLY)) == -1)
		err(errno, "/dev/mem");

	/* Read RCBA and PMBASE from the LPC config registers */
	long int rcba = pci_read_32(LPC_DEV, 0xf0) & 0xffffc000;
	pmbase = pci_read_32(LPC_DEV, 0x40) & 0xff80;

	/* FDO pin-strap status bit is in RCBA mmio space */
	rcba_mmio = mmap(0, RCBA_MMIO_LEN, PROT_READ, MAP_SHARED, devmemfd,
			rcba);
	if (rcba_mmio == MAP_FAILED)
		err(errno, "Could not map RCBA");

	if (get_fdo_status() == 1) { /* Descriptor not overridden */
		if (check_lpc_decode() == -1)
			err(errno = ECANCELED, "Can't forward I/O to LPC");

		printf("Sending FDO override command to EC:\n");
		ec_set_fdo();
		printf("Flash Descriptor Override enabled.\n"
			"Shut down (don't reboot) now.\n\n"
			"The EC may auto-boot on some systems; if not then "
			"manually power on.\n When the system boots rerun "
			"this utility to finish unlocking.\n");
	} else if (check_bios_write_en() == 0) {
		/* SMI locks in place, try disabling SMIs to bypass them */
		if (set_gbl_smi_en(0)) {
			printf("SMIs disabled. Internal flashing should work "
				"now.\n After flashing, re-run this utility "
				"to enable SMIs.\n (shutdown is buggy when "
				"SMIs are disabled)\n");
		} else {
			err(errno = ECANCELED, "Could not disable SMIs!");
		}
	} else { /* SMI locks not in place or bypassed */
		if (get_gbl_smi_en()) {
			/* SMIs are still enabled, assume this is an Exx10
			 * or newer which don't need the SMM bypass */
			printf("Flash is unlocked.\n"
				"Internal flashing should work.\n");
		} else {
			/* SMIs disabled, assume this is an Exx00 after
			 * unlocking and flashing */
			set_gbl_smi_en(1);
			printf("SMIs enabled.\n"
				"You can now shutdown the system.\n");
		}
	}
	sys_iopl(0);
	return errno;
}

int
get_fdo_status(void)
{
	return (*(uint16_t*)(rcba_mmio + SPIBAR + HSFS_REG) >> 13) & 1;
}

int
check_lpc_decode(void)
{
	/* Check that at a Generic Decode Range Register is set up to
	 * forward I/O ports 0x910 and 0x911 over LPC for the EC */
	int i = 0;
	int gen_dec_free = -1;
	for (; i < 4; i++) {
		uint32_t reg_val = pci_read_32(LPC_DEV, 0x84 + 4*i);
		uint16_t base_addr = reg_val & 0xfffc;
		uint16_t mask = ((reg_val >> 16) & 0xfffc) | 0x3;

		/* Bit 0 is the enable for each decode range. If disabled, note
		 * this register as available to add our own range decode */
		if ((reg_val & 1) == 0)
			gen_dec_free = i;

		/* Check if the current range register matches port 0x910.
		 * 0x911 doesn't need to be checked as the LPC bridge only
		 * decodes at the dword level, and thus a check is redundant */
		if ((0x910 & ~mask) == base_addr) {
			return 0;
		}
	}

	/* No matching range found, try setting a range in a free register */
	if (gen_dec_free != -1) {
		/* Set up an I/O decode range from 0x910-0x913 */
		pci_write_32(LPC_DEV, 0x84 + 4 * gen_dec_free, 0x911);
		return 0;
	} else {
		return -1;
	}
}

void
ec_set_fdo(void)
{
	/* EC FDO command arguments for reference:
	 * 0 = Query EC FDO status
	 * 2 = Enable FDO for next boot
	 * 3 = Disable FDO for next boot */
	write_ec_reg(0x12, EC_ENABLE_FDO);
	send_ec_cmd(0xb8);
}

void
write_ec_reg(uint8_t index, uint8_t data)
{
	sys_outb(EC_INDEX, index);
	sys_outb(EC_DATA, data);
}

void
send_ec_cmd(uint8_t cmd)
{
	sys_outb(EC_INDEX, 0);
	sys_outb(EC_DATA, cmd);
	if (wait_ec() == -1)
		err(errno = ECANCELED, "Timeout while waiting for EC!");
}

int
wait_ec(void)
{
	uint8_t busy;
	int timeout = 1000;
	do {
		sys_outb(EC_INDEX, 0);
		busy = sys_inb(EC_DATA);
		timeout--;
		usleep(1000);
	} while (busy && timeout > 0);
	return timeout > 0 ? 0 : -1;
}

int
check_bios_write_en(void)
{
	uint8_t bios_cntl = pci_read_32(LPC_DEV, 0xdc) & 0xff;
	/* Bit 5 = SMM BIOS Write Protect Disable (SMM_BWP)
	 * Bit 1 = BIOS Lock Enable (BLE)
	 * If both are 0, then there's no write protection */
	if ((bios_cntl & 0x22) == 0)
		return 1;

	/* SMM protection is enabled, but try enabling writes
	 * anyway in case the vendor SMM code doesn't reset it */
	pci_write_32(LPC_DEV, 0xdc, bios_cntl | 0x1);
	return pci_read_32(LPC_DEV, 0xdc) & 0x1;
}

int
set_gbl_smi_en(int enable)
{
	uint32_t smi_en = sys_inl(pmbase + SMI_EN_REG);
	if (enable) {
		smi_en |= 1;
	} else {
		smi_en &= ~1;
	}
	sys_outl(pmbase + SMI_EN_REG, smi_en);
	return (get_gbl_smi_en() == enable);
}

int
get_gbl_smi_en(void)
{
	return sys_inl(pmbase + SMI_EN_REG) & 1;
}
