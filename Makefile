TARGET     = main
MCU        = ch32v305
ARCH       = rv32imac_zicsr_zifencei
ABI        = ilp32

CPU_FREQ   = 144000000

OPTS       = -O2
DEFS       =
LIBS       =

# Toolchain
TOOLCHAIN_PREFIX   = riscv64-elf-

CC       = $(TOOLCHAIN_PREFIX)gcc
OBJCOPY  = $(TOOLCHAIN_PREFIX)objcopy
OBJDUMP  = $(TOOLCHAIN_PREFIX)objdump
SIZE     = $(TOOLCHAIN_PREFIX)size

VENDOR = vendor
PLATFORM = platform

LDSCRIPT   = $(PLATFORM)/linker/Link.ld

# Source
SRCS = \
$(PLATFORM)/system/system_ch32v30x.c \
$(PLATFORM)/startup/startup_ch32v30x_D8C.S \
$(VENDOR)/core/core_riscv.c \
$(VENDOR)/debug/debug.c \
src/main.c

SRCS += $(wildcard $(VENDOR)/peripheral/src/*.c)

# Build
BUILD_DIR  = build

OBJS       = $(SRCS:%.c=$(BUILD_DIR)/%.o)
OBJS       := $(OBJS:%.S=$(BUILD_DIR)/%.o)

# Compile flags
CFLAGS  = -g -std=gnu11 $(OPTS)
CFLAGS += -march=$(ARCH) -mabi=$(ABI)
CFLAGS += -Wall #-ffreestanding -nostdlib
CFLAGS += -I./$(VENDOR)/core -I./$(VENDOR)/debug -I./$(VENDOR)/peripheral/inc -I./$(PLATFORM)/system 
CFLAGS += $(DEFS)

# Linker flags
LDFLAGS = -T $(LDSCRIPT)
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map
LDFLAGS += -march=$(ARCH) -mabi=$(ABI)
LDFLAGS += -nostartfiles
LDFLAGS += #-nostdlib

# Targets
all: $(BUILD_DIR)/$(TARGET).bin

# Link
$(BUILD_DIR)/$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@ $(LIBS)
	$(SIZE) $@

# Binary
$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# Object build
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Disassembly
lst: $(BUILD_DIR)/$(TARGET).elf
	$(OBJDUMP) -h -S $< > $(BUILD_DIR)/$(TARGET).lst

# Flash
flash: $(BUILD_DIR)/$(TARGET).bin
	wchisp flash $<

DEV ?= /dev/sda
# Write the image of a filesystem
image:
	./scripts/fs_image.sh $(DEV)

# Clean
clean:
	rm -rf $(BUILD_DIR)
