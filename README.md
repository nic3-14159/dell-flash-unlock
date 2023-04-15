# Dell Latitude E6400 Internal Flashing

This utility allows you to use flashrom's internal programmer to program the
entire BIOS flash chip from software while still running the original Dell
BIOS, which normally restricts software writes to the flash chip.

## TL;DR
Run `make` to compile the utility, and then run `sudo ./e6400_flash_unlock` and
follow the directions it outputs.

## How it works
There are several ways the firmware can protect itself from being overwritten.
One way is the Intel Flash Descriptor (IFD) permissions. On Intel systems, the
flash image is divided into several regions such as the IFD itself, Gigabit
Ethernet (GBE) non-volative memory, Management Engine (ME) firmware, Platform
Data (PD), and the BIOS. The IFD contains a section which specifies the
read/write permissions for each SPI controller (such as the host system) and
each region of the flash, which are enforced by the chipset.

On the Latitude E6400, the host has read-only access to the IFD, no access to
the ME region, and read-write access to the PD, GBE, and BIOS regions. In order
for flashrom to write to the entire flash internally, the host needs full
permissions to all of these regions. Since the IFD is read only, we cannot
change these permissions unless we directly access the chip using an external
programmer, which defeats the purpose of internal flashing.

However, Intel chipsets have a pin strap that allows the flash descriptor
permissions to be overridden depending on the value of the pin at power on,
granting RW permissions to all regions. On the ICH9M chipset on the E6400, this
pin is HDA\_DOCK\_EN/GPIO33, which will enable the override if it is sampled
low. This pin happens to be connected to a GPIO controlled by the Embedded
Controller (EC), a small microcontroller on the board which handles things like
the keyboard, touchpad, LEDs, and other system level tasks. Software can send a
certain command to the EC, which tells it to pull GPIO33 low on the next boot.

Although we now have full access according to the IFD permissions, we still
cannot flash the whole chip, due to another protection the firmware uses.
Before software can update the BIOS, it must change the BIOS Write Enable
(BIOSWE) bit in the chipset from 0 to 1. However, if the BIOS Lock Enable (BLE)
bit is also set to 1, then changing the BIOSWE bit triggers a System Management
Interrupt (SMI). This causes the processor to enter System Management Mode
(SMM), a highly privileged x86 execution state which operates transparently to
the operating system. The code that SMM runs is provided by the BIOS, which
checks the BIOSWE bit and sets it back to 0 before returning control to the OS.
This feature is intended to only allow SMM code to update the system firmware.
As the switch to SMM suspends the execution of the OS, it appears to the OS
that the BIOSWE bit was never set to 1.  Unfortunately, the BLE bit cannot be
set back to 0 once it is set to 1, so this functionality cannot be disabled
after it is first enabled by the BIOS.

Older versions of the E6400 BIOS did not set the BLE bit, allowing flashrom to
flash the entire flash chip internally after only setting the descriptor
override. However, more recent versions do set it, so we may have hit a dead
end unless we force downgrade to an older version (though there is a more
convenient method, as we are about to see).

What if there was a way to sidestep the BIOS Lock entirely? As it turns out,
there is, and it's called the Global SMI Enable (GBL\_SMI\_EN) bit. If it's set
to 1, then the chipset will generate SMIs, such as when we change BIOSWE with
BLE set. If it's 0, then no SMI will be generated, even with the BLE bit set.
On the E6400, GBL\_SMI\_EN is set to 1, and it can be changed back to 0, unlike
the BLE bit. But there still might be one bit in the way, the SMI\_LOCK bit,
which prevents modifications to GBL\_SMI\_EN when SMI\_LOCK is 1. Like the BLE
bit, it cannot be changed back to 0 once it set to 1. But we are in luck, as
the vendor E6400 BIOS leaves SMI\_LOCK unset at 0, allowing us to clear
GBL\_SMI\_EN and disable SMIs, bypassing the BIOS Lock protections.

There are other possible protection mechanisms that the firmware can utilize,
such as Protected Range Register settings, which apply access permissions to
address ranges of the flash, similar to the IFD. However, the E6400 vendor
firmware does not utilize these, so they will not be discussed.
