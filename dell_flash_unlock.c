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
#include "chipset_ids.h"

enum Platform get_platform(uint16_t pci_device_id);
int get_fdo_status(uintptr_t spibar);
int check_lpc_decode(void);
void ec_set_fdo(void);
void write_ec_reg(uint8_t index, uint8_t data);
void send_ec_cmd(uint8_t cmd);
int wait_ec(void);
int check_bios_write_en(void);
int set_gbl_smi_en(uint16_t pmbase, int enable);
int get_gbl_smi_en(uint16_t pmbase);

#define EC_INDEX 0x910
#define EC_DATA 0x911
#define EC_ENABLE_FDO 2

#define LPC_DEV PCI_DEV(0, 0x1f, 0)

/* Skylake and newer */
#define PMC_DEV PCI_DEV(0, 0x1f, 2)
#define SPI_DEV PCI_DEV(0, 0x1f, 5)

#define RCBA_MMIO_LEN 0x4000
#define SPI_MEMBAR_LEN 4096

/* Register offsets */
#define SPIBAR 0x3800
#define HSFS_REG  0x04
#define SMI_EN_REG 0x30


int
main(int argc, char *argv[])
{
	int devmemfd;
	(void)argc;
	(void)argv;
	void *mmio;
	uint32_t base_addr;
	uintptr_t spi_mmio_base = 0;
	enum Platform platform;
	uint16_t pmbase = 0;

	if (sys_iopl(3) == -1)
		err(errno, "Could not access IO ports");
	if ((devmemfd = open("/dev/mem", O_RDONLY)) == -1)
		err(errno, "/dev/mem");

	uint16_t lpc_device_id = (uint16_t)((pci_read_32(LPC_DEV, 0x0) >> 16) & 0xffff);
	platform = get_platform(lpc_device_id);

	switch (platform) {
	/* Assume unknown is an older chipset for now */
	case UNKNOWN:
	case GM45:
	case SANDYBRIDGE:
	case IVYBRIDGE:
	case HASWELL:
		/* SPIBAR in RCBA space */
		base_addr = pci_read_32(LPC_DEV, 0xf0) & 0xffffc000;
		mmio = mmap(0, RCBA_MMIO_LEN, PROT_READ, MAP_SHARED, devmemfd,
			base_addr);
		if (mmio == MAP_FAILED)
			err(errno, "Could not map RCBA");

		spi_mmio_base = ((uintptr_t)mmio) + SPIBAR;
		pmbase = pci_read_32(LPC_DEV, 0x40) & 0xff80;
		break;
	case SKYLAKE:
	case KABYLAKE:
		/* SPIBAR in dedicated MMIO BAR space of SPI PCI Device */
		base_addr = pci_read_32(SPI_DEV, 0x10) & 0xfffff000;
		mmio = mmap(0, SPI_MEMBAR_LEN, PROT_READ, MAP_SHARED, devmemfd,
				base_addr);
		if (mmio == MAP_FAILED)
			err(errno, "Could not map SPI MEMBAR");

		spi_mmio_base = (uintptr_t)mmio;
		pmbase = pci_read_32(PMC_DEV, 0x40) & 0xff00;
		break;
	}

	if (get_fdo_status(spi_mmio_base) == 1) { /* Descriptor not overridden */
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
		if (set_gbl_smi_en(pmbase, 0)) {
			printf("SMIs disabled. Internal flashing should work "
				"now.\n After flashing, re-run this utility "
				"to enable SMIs.\n (shutdown is buggy when "
				"SMIs are disabled)\n");
		} else {
			err(errno = ECANCELED, "Could not disable SMIs!");
		}
	} else { /* SMI locks not in place or bypassed */
		if (get_gbl_smi_en(pmbase)) {
			/* SMIs are still enabled, assume this is an Exx10
			 * or newer which don't need the SMM bypass */
			printf("Flash is unlocked.\n"
				"Internal flashing should work.\n");
		} else {
			/* SMIs disabled, assume this is an Exx00 after
			 * unlocking and flashing */
			set_gbl_smi_en(pmbase, 1);
			printf("SMIs enabled.\n"
				"You can now shutdown the system.\n");
		}
	}
	sys_iopl(0);
	return errno;
}

/* TODO: Add other chipsets */
enum Platform get_platform(uint16_t pci_device_id) {
	switch (pci_device_id) {
		case PCI_DID_INTEL_QM170:
		case PCI_DID_INTEL_HM170:
		case PCI_DID_INTEL_CM236:
		case PCI_DID_INTEL_HM175:
		case PCI_DID_INTEL_QM175:
		case PCI_DID_INTEL_CM238:
		case PCI_DID_INTEL_SPT_PCH_U_BASE:
		case PCI_DID_INTEL_SPT_PCH_Y_PREMIUM:
		case PCI_DID_INTEL_SPT_PCH_U_PREMIUM:
			 return SKYLAKE;
			 break;
		case PCI_DID_INTEL_KBL_PCH_Y_PREMIUM_HDCP:
		case PCI_DID_INTEL_KBL_PCH_U_PREMIUM_HDCP:
		case PCI_DID_INTEL_KBL_PCH_U_BASE_HDCP:
		case PCI_DID_INTEL_KBL_PCH_U_BASE:
		case PCI_DID_INTEL_KBL_PCH_Y_PREMIUM:
		case PCI_DID_INTEL_KBL_PCH_U_PREMIUM:
			 return KABYLAKE;
			 break;
		default:
			 return UNKNOWN;
	}

}

int
get_fdo_status(uintptr_t spibar)
{
	return (*(uint16_t*)(spibar + HSFS_REG) >> 13) & 1;
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
set_gbl_smi_en(uint16_t pmbase, int enable)
{
	uint32_t smi_en = sys_inl(pmbase + SMI_EN_REG);
	if (enable) {
		smi_en |= 1;
	} else {
		smi_en &= ~1;
	}
	sys_outl(pmbase + SMI_EN_REG, smi_en);
	return (get_gbl_smi_en(pmbase) == enable);
}

int
get_gbl_smi_en(uint16_t pmbase)
{
	return sys_inl(pmbase + SMI_EN_REG) & 1;
}
