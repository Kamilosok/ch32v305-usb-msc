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
- Linux & Windows compatibility

## Memory backend

Currently the project uses the internal MCU FLASH as storage, thus it's limited to **64 KiB** of memory. Support for external storage media (EEPROM memory, SD cards, etc.)
may be added in the future.

Due to FLASH erase granularity limitations:

- writes are page-buffered
- storage longevity is limited
- performance is not optimized

## Project structure

```bash
├── ch32v30x-bsp    # Board Support Package submodule
├── include         # USB MSC, SCSI, and USB headers
├── scripts         # Scripts for testing
├── src             # Main program and protocol and implementation source
├── LICENSE         # Project license
├── Makefile        # Build system
├── README.md       # This file
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

The code in the `/ch32v30x-bsp` submodule is licensed separately, see the submodule's **`README.md`**.

---

## Academic context

This project also serves as the practical component of my bachelor's thesis provided in `Thesis.pdf` conducted at Univeristy of Wrocław. The thesis is written in polish.
