/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: 2023 Nicholas Chin */

#if defined(__linux__)
#include <sys/io.h>
#endif

#if defined(__OpenBSD__)
#include <machine/sysarch.h>
#include <sys/types.h>
#if defined(__amd64__)
#include <amd64/pio.h>
#elif defined(__i386__)
#include <i386/pio.h>
#endif /* __i386__ */
#endif /* __OpenBSD__ */

#include <errno.h>

#include "accessors.h"

uint32_t
pci_read_32(uint32_t dev, uint8_t reg)
{
	sys_outl(PCI_CFG_ADDR, dev | reg);
	return sys_inl(PCI_CFG_DATA);
}

void
pci_write_32(uint32_t dev, uint8_t reg, uint32_t value)
{
	sys_outl(PCI_CFG_ADDR, dev | reg);
	sys_outl(PCI_CFG_DATA, value);
}

void
sys_outb(unsigned int port, uint8_t data)
{
	#if defined(__linux__)
	outb(data, port);
	#endif
	#if defined(__OpenBSD__)
	outb(port, data);
	#endif
}

void
sys_outl(unsigned int port, uint32_t data)
{
	#if defined(__linux__)
	outl(data, port);
	#endif
	#if defined(__OpenBSD__)
	outl(port, data);
	#endif
}

uint8_t
sys_inb(unsigned int port)
{
	#if defined(__linux__) || defined (__OpenBSD__)
	return inb(port);
	#endif
	return 0;
}

uint32_t
sys_inl(unsigned int port)
{
	#if defined(__linux__) || defined (__OpenBSD__)
	return inl(port);
	#endif
	return 0;
}

int
sys_iopl(int level)
{
#if defined(__linux__)
	return iopl(level);
#endif
#if defined(__OpenBSD__)
#if defined(__i386__)
	return i386_iopl(level);
#elif defined(__amd64__)
	return amd64_iopl(level);
#endif /* __amd64__ */
#endif /* __OpenBSD__ */
	errno = ENOSYS;
	return -1;
}
