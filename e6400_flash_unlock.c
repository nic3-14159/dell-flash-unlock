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

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/io.h>
#include <sys/mman.h>
#include <errno.h>
#include <err.h>

#define EC_INDEX 0x910
#define EC_DATA 0x911
#define PMBASE 0x1000
#define SMI_EN_REG (PMBASE + 0x30)

// Hardcoded to what E6400 vendor BIOS sets it to
// Assuming this doesn't change between versions
#define RCBA 0xfed18000

#define RCBA_MMIO_LEN 0x4000
#define SPIBAR 0x3800
#define HSFS_REG  0x04

volatile uint8_t *rcba_mmio;

enum
EC_FDO_CMD {
	QUERY = 0,
	SET_OVERRIDE = 2,
	UNSET_OVERRIDE = 3
};

void wait_ec(void);
uint8_t read_ec_reg(uint8_t index);
void write_ec_reg(uint8_t index, uint8_t data);
void send_ec_cmd(uint8_t cmd);
uint8_t ec_fdo_command(enum EC_FDO_CMD arg);
int get_fdo_status(void);
int get_gbl_smi_en(void);
int set_gbl_smi_en(int enable);

void
wait_ec(void) {
	uint8_t busy;
	do {
		outb(0, EC_INDEX);
		busy = inb(EC_DATA);
	} while (busy); 
}

uint8_t
read_ec_reg(uint8_t index) {
	outb(index, EC_INDEX);
	return inb(EC_DATA);
}

void
write_ec_reg(uint8_t index, uint8_t data) {
	outb(index, EC_INDEX);
	outb(data, EC_DATA);
}

void
send_ec_cmd(uint8_t cmd) {
	outb(0, EC_INDEX);
	outb(cmd, EC_DATA);
	wait_ec();
}

/**
 * arg:
 * 0 = Query EC FDO status - TODO
 * 2 = Enable FDO for next boot
 * 3 = Disable FDO for next boot - TODO
 */
uint8_t
ec_fdo_command(enum EC_FDO_CMD arg) {
	write_ec_reg(0x12, arg);
	send_ec_cmd(0xb8);
	return 1;
}

int
get_fdo_status(void) {
	return (*(uint16_t*)(rcba_mmio + SPIBAR + HSFS_REG) >> 13) & 1;
}

int
get_gbl_smi_en(void) {
	return inl(SMI_EN_REG) & 1;
}

int
set_gbl_smi_en(int enable) {
	uint32_t smi_en = inl(SMI_EN_REG);	
	if (enable) {
		smi_en |= 1;
	} else {
		smi_en &= ~1;
	}
	outl(smi_en, SMI_EN_REG);
	return (get_gbl_smi_en() == enable);
}

int
main(int argc, char *argv[]) {
	int devmemfd;

	(void)argc;
	(void)argv;

	if ((ioperm(EC_INDEX, 2, 1) == -1) || (ioperm(SMI_EN_REG, 4, 1) == -1))
		err(errno, "Could not access IO ports");

	if ((devmemfd = open("/dev/mem", O_RDONLY)) == -1)
		err(errno, "/dev/mem");

	// Flash Descriptor Override Pin-Strap Status bit is in RCBA mmio space
	rcba_mmio = mmap(0, RCBA_MMIO_LEN, PROT_READ, MAP_SHARED, devmemfd,
			RCBA);
	if (rcba_mmio == MAP_FAILED)
		err(errno, "Could not map RCBA");

	if (get_fdo_status() == 1) {
		// FDO strap not set, so tell EC to set it on next boot
		ec_fdo_command(2);
		printf("Flash Descriptor override enabled. Please *shutdown* "
			"(don't reboot) the system.\nThe EC will automatically"
			"boot the system and set the descriptor override.\n"
			"Then run this utility again to complete the unlock"
			"process\n");
	} else if (get_gbl_smi_en()){
		/*
		 * SMIs enabled, so disable them to bypass
		 * the BIOS Lock function
		 */
		set_gbl_smi_en(0);
		printf("SMIs disabled, you should now be able to run flashrom "
			"-p internal on the entire flash!\n\nAfter you are "
			"done flashing, run this utility again to re-enable "
			"SMIs,\n as the system will not power off properly if "
			"SMIs are disabled.\n");
	} else {
		/*
		 * SMIs disabled, so re-enable them so that
		 * shutdown works properly
		 */
		set_gbl_smi_en(1);
		printf("SMIs enabled, you can now shutdown the system.\n");
	}
	return errno;
}
