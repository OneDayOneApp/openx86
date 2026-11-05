/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 tongxiaojun
 */
/* PCI mechanism #1 configuration space stub (L4).
 *
 * CONFIG_ADDRESS is a 32-bit register at 0xCF8..0xCFB:
 *   bit 31    enable
 *   bits 23:16 bus
 *   bits 15:11 device
 *   bits 10:8  function
 *   bits 7:2   dword register index
 *   bits 1:0   hardwired to 0 (reserved bits 30:24 read back 0 too)
 * Writes of any width update only the byte lanes they touch, then the
 * reserved bits are masked off.
 *
 * CONFIG_DATA is the 4-byte window 0xCFC..0xCFF. The byte at 0xCFC+n is byte
 * n of the selected config dword, so byte/word/dword accesses pick the same
 * lane they name. Port I/O is split per byte, which makes unaligned accesses
 * behave the way x86 byte enables do: an unaligned dword IN at 0xCFE returns
 * bytes 2,3 of the config dword in the low half and the bytes past the
 * CONFIG_DATA window (0xD00,0xD01) as 0xFF in the high half; writes to those
 * out-of-window bytes are dropped. While bit 31 of CONFIG_ADDRESS is clear,
 * config-data reads return 0xFFFFFFFF and config-data writes are ignored.
 *
 * Devices:
 *   bus 0 / dev 0 / fn 0 is an Intel 440FX host bridge: vendor 0x8086,
 *   device 0x1237, class 0x06 (bridge) / subclass 0x00 (host bridge),
 *   revision 0x02, header type 0.
 *   Every other bus/device/function is absent: reads return all ones and
 *   writes are ignored.
 *
 * Host-bridge configuration space:
 *   0x00 vendor/device       RO 0x8086:0x1237
 *   0x04 command/status      command R/W, defined bits 0..10 only (mask
 *                            0x07FF); status is RO 0x0200 (DEVSEL timing =
 *                            medium). The upper 16 bits of a write to this
 *                            dword are ignored.
 *   0x08 revision/class      RO revision 0x02, prog-if 0x00, subclass 0x00,
 *                            class 0x06
 *   0x0C cache line/latency  R/W (one byte each)
 *   0x0E header type         RO 0x00 (type 0, single function)
 *   0x0F BIST                RO 0x00 (not BIST-capable)
 *   0x10 BAR0                unimplemented memory BAR. Its size mask is 0,
 *                            so it reads 0 before the probe, 0 ("size
 *                            mask") after writing all ones, and 0 after
 *                            restoring 0: no address bits are decoded and
 *                            no MMIO window is backed by this stub.
 *   0x14..0x27 BAR1..BAR5    unimplemented, RO 0 (writes ignored)
 *   0x2C subsystem IDs       RO 0x00000000
 *   0x34 capability pointer  RO 0x00 (no capabilities)
 *   0x3C interrupt line/pin RO 0x00 (the host bridge has no INTx)
 *   all other offsets        read 0, writes ignored
 *
 * The entry points take the absolute port number (not an offset) because
 * both the register window and the lane matter. They are declared locally in
 * port.c because xemu.h is owned elsewhere this round.
 */
#include "../xemu.h"

#include <string.h>

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu

#define PCI_ADDR_ENABLE 0x80000000u
/* Enable + bus/dev/fn/reg, with bits 30:24 and 1:0 always zero. */
#define PCI_ADDR_MASK 0x80FFFFFCu

#define PCI_VENDOR_ID 0x8086u
#define PCI_DEVICE_ID 0x1237u
#define PCI_STATUS 0x0200u         /* DEVSEL timing = medium */
#define PCI_COMMAND_MASK 0x07FFu   /* command bits 0..10 are defined */
#define PCI_BAR0_SIZE_MASK 0x00000000u /* unimplemented: no address bits */

/* Reset values are all zero, so the module is usable even before
 * xemu_pci_reset() gets wired into xemu_reset() (state.c is owned elsewhere
 * this round). */
static uint32_t pci_addr;
static uint16_t pci_command;
static uint8_t pci_cache_line;
static uint8_t pci_latency;
static uint32_t pci_bar0;

void xemu_pci_reset(void) {
  pci_addr = 0;
  pci_command = 0;
  pci_cache_line = 0;
  pci_latency = 0;
  pci_bar0 = 0;
}

/* Only bus 0 / device 0 / function 0 exists in this stub. */
static int pci_target_present(void) {
  return ((pci_addr >> 16) & 0xFF) == 0 &&
         ((pci_addr >> 11) & 0x1F) == 0 &&
         ((pci_addr >> 8) & 0x07) == 0;
}

/* Read one byte of the selected dword; `reg` is dword-aligned (bits 7:2). */
static uint8_t pci_cfg_read_byte(uint32_t reg, int lane) {
  uint32_t off = reg | (uint32_t)lane;
  switch (off) {
    case 0x00: return (uint8_t)(PCI_VENDOR_ID & 0xFF);         /* 0x86 */
    case 0x01: return (uint8_t)((PCI_VENDOR_ID >> 8) & 0xFF);  /* 0x80 */
    case 0x02: return (uint8_t)(PCI_DEVICE_ID & 0xFF);         /* 0x37 */
    case 0x03: return (uint8_t)((PCI_DEVICE_ID >> 8) & 0xFF);  /* 0x12 */
    case 0x04: return (uint8_t)(pci_command & 0xFF);
    case 0x05: return (uint8_t)(pci_command >> 8);
    case 0x06: return (uint8_t)(PCI_STATUS & 0xFF);
    case 0x07: return (uint8_t)(PCI_STATUS >> 8);
    case 0x08: return 0x02;   /* revision */
    case 0x09: return 0x00;   /* programming interface */
    case 0x0A: return 0x00;   /* subclass: host bridge */
    case 0x0B: return 0x06;   /* class: bridge */
    case 0x0C: return pci_cache_line;
    case 0x0D: return pci_latency;
    case 0x0E: return 0x00;   /* header type 0, single function */
    case 0x0F: return 0x00;   /* BIST */
    case 0x10: case 0x11: case 0x12: case 0x13:
      return (uint8_t)(pci_bar0 >> (8 * (off - 0x10)));
    default: return 0x00;     /* unimplemented / reserved: zero */
  }
}

/* Write one byte of the selected dword. Read-only registers are ignored. */
static void pci_cfg_write_byte(uint32_t reg, int lane, uint8_t v) {
  uint32_t off = reg | (uint32_t)lane;
  switch (off) {
    case 0x04:
      pci_command = (uint16_t)(((pci_command & 0xFF00u) | v) &
                               PCI_COMMAND_MASK);
      break;
    case 0x05:
      pci_command = (uint16_t)(((pci_command & 0x00FFu) |
                                ((uint16_t)(v & 0x07u) << 8)) &
                               PCI_COMMAND_MASK);
      break;
    case 0x0C: pci_cache_line = v; break;
    case 0x0D: pci_latency = v; break;
    case 0x10: case 0x11: case 0x12: case 0x13: {
      int shift = 8 * (int)(off - 0x10);
      pci_bar0 = (pci_bar0 & ~(0xFFu << shift)) | ((uint32_t)v << shift);
      /* Size mask 0: the probe's all-ones write reads back as 0. */
      pci_bar0 &= PCI_BAR0_SIZE_MASK;
      break;
    }
    default:
      /* Vendor/device, class/revision, header type, status, BAR1..BAR5 and
       * reserved offsets are read-only or unimplemented. */
      break;
  }
}

/* One byte of a port access. Bytes outside the two register windows belong
 * to no device and read as 0xFF (writes to them are dropped). */
static uint8_t pci_io_read_byte(uint32_t port) {
  if (port >= PCI_CONFIG_ADDRESS && port <= PCI_CONFIG_ADDRESS + 3) {
    unsigned lane = port - PCI_CONFIG_ADDRESS;
    return (uint8_t)(pci_addr >> (8 * lane));
  }
  if (port >= PCI_CONFIG_DATA && port <= PCI_CONFIG_DATA + 3) {
    unsigned lane = port - PCI_CONFIG_DATA;
    if (!(pci_addr & PCI_ADDR_ENABLE)) return 0xFF;
    if (!pci_target_present()) return 0xFF;
    return pci_cfg_read_byte(pci_addr & 0xFCu, (int)lane);
  }
  return 0xFF;
}

static void pci_io_write_byte(uint32_t port, uint8_t v) {
  if (port >= PCI_CONFIG_ADDRESS && port <= PCI_CONFIG_ADDRESS + 3) {
    unsigned lane = port - PCI_CONFIG_ADDRESS;
    uint32_t mask = 0xFFu << (8 * lane);
    pci_addr = (pci_addr & ~mask) | ((uint32_t)v << (8 * lane));
    pci_addr &= PCI_ADDR_MASK;
    return;
  }
  if (port >= PCI_CONFIG_DATA && port <= PCI_CONFIG_DATA + 3) {
    if (!(pci_addr & PCI_ADDR_ENABLE)) return;
    if (!pci_target_present()) return;
    pci_cfg_write_byte(pci_addr & 0xFCu, (int)(port - PCI_CONFIG_DATA), v);
    return;
  }
  /* Out-of-window byte: dropped. */
}

int xemu_pci_in(uint32_t port, int size, uint64_t *val) {
  uint64_t v = 0;
  if (size < 1 || size > 4) {
    *val = 0;
    return -1;
  }
  for (int i = 0; i < size; i++)
    v |= (uint64_t)pci_io_read_byte(port + (uint32_t)i) << (8 * i);
  *val = v;
  return 0;
}

int xemu_pci_out(uint32_t port, int size, uint64_t val) {
  if (size < 1 || size > 4) return -1;
  for (int i = 0; i < size; i++)
    pci_io_write_byte(port + (uint32_t)i, (uint8_t)(val >> (8 * i)));
  return 0;
}
