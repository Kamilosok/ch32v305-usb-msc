<!-- markdownlint-disable MD041 -->
![Top language](https://img.shields.io/github/languages/top/Kamilosok/ch32v305-usb-msc)

# USB Mass Storage on CH32V305/7

This project emulates a USB 2.0 Full-Speed Mass Storage Class Device using the Bulk-Only Transport protocol and a subset of the SPC-3 command set on the **WCH QingKe RISC-V CH32V305/7 microcontrollers**.

The device appears to the host system as a small flash drive.

## Features

- USB 2.0 Full-Speed device
- USB MSC Bulk-Only Transport (BOT)
- FAT12 filesystem support
- Internal FLASH storage backend
- SCSI command handling
- No RTOS or external USB stack
- Bare-metal implementation

## Memory backend

Currently the project uses the internal MCU FLASH as storage, thus it's limited to **64 KiB** of memory. Support for external storage media (EEPROM memory, SD cards, etc.)
may be added in the future.

Due to FLASH erase granularity limitations:

- writes are page-buffered
- storage longevity is limited
- performance is not optimized

## Project structure

```bash
├── include         # USB MSC, SCSI, and USB headers
├── platform        # Platform-specific startup, linker, and system initialization code
├── scripts         # Scripts for testing
├── src             # Main program and protocol and implementation source
├── vendor          # Vendor-provided WCH core and peripheral libraries
├── LICENSE         # Project license
├── Makefile        # Build system
├── README.md       # This file
├── THIRD_PARTY.md  # Third-party disclaimer and licensing notes
└── Thesis.pdf      # The thesis this project supports
```

## Usage

### Build

```bash
make
```

### Flash

After entering flash mode on the MCU:

```bash
make flash
```

### Disassembly

```bash
make lst
```

### Clean build

```bash
make clean
```

## Third Party Code

The code in the `/platform` and `/vendor` directories is licensed separately, for additional information see [THIRD_PARTY.md](THIRD_PARTY.md).

---

## Academic context

This project also serves as the practical component of my bachelor's thesis provided in `Thesis.pdf` conducted at Univeristy of Wrocław. The thesis is written in polish.
