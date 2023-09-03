/*
 * Copyright (c) 2023 Nicholas Chin
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <sys/io.h>
#include <sys/mman.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

enum
EC_FDO_CMD {
	QUERY = 0,
	SET_OVERRIDE = 2,
	UNSET_OVERRIDE = 3
};

uint32_t pci_read_32(uint32_t dev, uint8_t reg);
int get_fdo_status(void);
uint8_t ec_fdo_command(enum EC_FDO_CMD arg);
void write_ec_reg(uint8_t index, uint8_t data);
void send_ec_cmd(uint8_t cmd);
int wait_ec(void);
int get_gbl_smi_en(void);
int set_gbl_smi_en(int enable);
/* uint8_t read_ec_reg(uint8_t index); */

#define EC_INDEX 0x910
#define EC_DATA 0x911

#define PCI_CFG_ADDR 0xcf8
#define PCI_CFG_DATA 0xcfc
#define PCI_DEV(bus, dev, func) (1u << 31 | bus << 16 | dev << 11 | func << 8)
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

	if (ioperm(EC_INDEX, 2, 1) == -1)
		err(errno, "Could not access EC IO ports");
	if ((devmemfd = open("/dev/mem", O_RDONLY)) == -1)
		err(errno, "/dev/mem");
	if (ioperm(PCI_CFG_ADDR, 8, 1) == -1)
		err(errno, "Could not access PCI config IO ports");

	/* Read RCBA and PMBASE from the LPC config registers */
	long int rcba = pci_read_32(LPC_DEV, 0xf0) & 0xffffc000;
	pmbase = pci_read_32(LPC_DEV, 0x40) & 0xff80;
	if (ioperm(pmbase + SMI_EN_REG, 4, 1) == -1)
		err(errno, "Could not access SMI_EN register IO ports");

	/* FDO pin-strap status bit is in RCBA mmio space */
	rcba_mmio = mmap(0, RCBA_MMIO_LEN, PROT_READ, MAP_SHARED, devmemfd,
			rcba);
	if (rcba_mmio == MAP_FAILED)
		err(errno, "Could not map RCBA");

	if (get_fdo_status() == 1) {
		printf("Sending FDO override command to EC:\n");
		ec_fdo_command(SET_OVERRIDE);
		printf("Flash Descriptor Override enabled. Shut down now. The "
			"EC will auto-boot the system and set the override.\n"
			"Upon boot, re-run this utility to unlock flash.\n");
	} else if (get_gbl_smi_en()){
		set_gbl_smi_en(0);
		printf("SMIs disabled. Internal flashing should work now.\n"
			"After flashing, re-run this utility to enable SMIs.\n"
			"(shutdown is buggy when SMIs are disabled)\n");
	} else {
		set_gbl_smi_en(1);
		printf("SMIs enabled, you can now shutdown the system.\n");
	}
	return errno;
}

uint32_t
pci_read_32(uint32_t dev, uint8_t reg)
{
	outl(dev | reg, PCI_CFG_ADDR);
	return inl(PCI_CFG_DATA);
}
int
get_fdo_status(void)
{
	return (*(uint16_t*)(rcba_mmio + SPIBAR + HSFS_REG) >> 13) & 1;
}

/*
 * arg:
 * 0 = Query EC FDO status - TODO
 * 2 = Enable FDO for next boot
 * 3 = Disable FDO for next boot - TODO
 */
uint8_t
ec_fdo_command(enum EC_FDO_CMD arg)
{
	write_ec_reg(0x12, arg);
	send_ec_cmd(0xb8);
	return 1;
}

void
write_ec_reg(uint8_t index, uint8_t data)
{
	outb(index, EC_INDEX);
	outb(data, EC_DATA);
}

void
send_ec_cmd(uint8_t cmd)
{
	outb(0, EC_INDEX);
	outb(cmd, EC_DATA);
	if (wait_ec() == -1)
		errx(EXIT_FAILURE, "Timeout while waiting for EC!");
}

int
wait_ec(void)
{
	uint8_t busy;
	int timeout = 1000;
	do {
		outb(0, EC_INDEX);
		busy = inb(EC_DATA);
		timeout--;
		usleep(1000);
	} while (busy && timeout > 0);
	return timeout > 0 ? 0 : -1;
}

int
get_gbl_smi_en(void)
{
	return inl(pmbase + SMI_EN_REG) & 1;
}

int
set_gbl_smi_en(int enable)
{
	uint32_t smi_en = inl(pmbase + SMI_EN_REG);
	if (enable) {
		smi_en |= 1;
	} else {
		smi_en &= ~1;
	}
	outl(smi_en, pmbase + SMI_EN_REG);
	return (get_gbl_smi_en() == enable);
}

/*
uint8_t
read_ec_reg(uint8_t index)
{
	outb(index, EC_INDEX);
	return inb(EC_DATA);
}
*/
