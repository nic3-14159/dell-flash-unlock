/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: 2023 Nicholas Chin */

#if defined(__linux__)
#include <sys/io.h>
#endif

#if defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/types.h>
#include <machine/sysarch.h>
#endif /* __OpenBSD__ || __NetBSD__ */

#if defined(__OpenBSD__)
#if defined(__amd64__)
#include <amd64/pio.h>
#elif defined(__i386__)
#include <i386/pio.h>
#endif /* __i386__ */
#endif /* __OpenBSD__ */

#if defined(__FreeBSD__)
#include <fcntl.h>
#include <sys/types.h>
#include <machine/cpufunc.h>
#include <unistd.h>
#endif /* __FreeBSD__ */

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
	#if defined(__OpenBSD__) || defined(__FreeBSD__)
	outb(port, data);
	#endif
	#if defined(__NetBSD__)
	__asm__ volatile ("outb %b0, %w1" : : "a"(data), "d"(port));
	#endif
}

void
sys_outl(unsigned int port, uint32_t data)
{
	#if defined(__linux__)
	outl(data, port);
	#endif
	#if defined(__OpenBSD__) || defined(__FreeBSD__)
	outl(port, data);
	#endif
	#if defined(__NetBSD__)
	__asm__ volatile ("outl %0, %w1" : : "a"(data), "d"(port));
	#endif
}

uint8_t
sys_inb(unsigned int port)
{
	#if defined(__linux__) || defined (__OpenBSD__) \
		|| defined(__FreeBSD__)
	return inb(port);
	#endif

	#if defined(__NetBSD__)
	uint8_t retval;
	__asm__ volatile ("inb %w1, %b0" : "=a" (retval) : "d" (port));
	return retval;
	#endif
	return 0;
}

uint32_t
sys_inl(unsigned int port)
{
	#if defined(__linux__) || defined (__OpenBSD__) \
		|| defined(__FreeBSD__)
	return inl(port);
	#endif
	#if defined(__NetBSD__)
	int retval;
	__asm__ volatile ("inl %w1, %0" : "=a" (retval) : "d" (port));
	return retval;
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

#if defined(__NetBSD__)
#if defined(__i386__)
	return i386_iopl(level);
#elif defined(__amd64__)
	return x86_64_iopl(level);
#endif /* __amd64__ */
#endif /* __NetBSD__ */

#if defined(__FreeBSD__)
	/* Refer to io(4) manual page. This assumes the legacy behavior
	 * where opening /dev/io raises the IOPL of the process */
	static int io_fd = -1;

	/* Requesting privileged access */
	if (level > 0) {
		if (io_fd == -1) {
			io_fd = open("/dev/io", O_RDONLY);
			return (io_fd == -1) ? -1 : 0;
		}
	/* Lowering access to lowest level */
	} else if (level == 0 && io_fd != -1) {
		if (close(io_fd) == -1) {
			return -1;
		} else {
			io_fd = -1;
		}
	}
	return 0;
#endif /* __FreeBSD__ */

	errno = ENOSYS;
	return -1;
}
