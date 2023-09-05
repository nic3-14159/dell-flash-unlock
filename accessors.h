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

#define PCI_CFG_ADDR 0xcf8
#define PCI_CFG_DATA 0xcfc
#define PCI_DEV(bus, dev, func) (1u << 31 | bus << 16 | dev << 11 | func << 8)

uint32_t
pci_read_32(uint32_t dev, uint8_t reg)
{
	outl(dev | reg, PCI_CFG_ADDR);
	return inl(PCI_CFG_DATA);
}

void
pci_write_32(uint32_t dev, uint8_t reg, uint32_t value)
{
	outl(dev | reg, PCI_CFG_ADDR);
	outl(value, PCI_CFG_DATA);
}
