/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: 2023 Nicholas Chin */

#include <stdint.h>

#define PCI_CFG_ADDR 0xcf8
#define PCI_CFG_DATA 0xcfc
#define PCI_DEV(bus, dev, func) (1u << 31 | bus << 16 | dev << 11 | func << 8)

uint32_t pci_read_32(uint32_t dev, uint8_t reg);
void pci_write_32(uint32_t dev, uint8_t reg, uint32_t value);

int sys_iopl(int level);
void sys_outb(unsigned int port, uint8_t data);
void sys_outl(unsigned int port, uint32_t data);
uint8_t sys_inb(unsigned int port);
uint32_t sys_inl(unsigned int port);
