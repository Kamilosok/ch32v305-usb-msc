TARGET     = main
MCU        = ch32v305
ARCH       = rv32imacf_zicsr_zifencei
ABI        = ilp32

CPU_FREQ   = 48000000

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
$(VENDOR)/debug/debug.c

SRCS += $(wildcard src/*.c)

SRCS += $(wildcard $(VENDOR)/peripheral/src/*.c)

INCLUDES = \
-I./include \
-I./$(VENDOR)/core \
-I./$(VENDOR)/debug \
-I./$(VENDOR)/peripheral/inc \
-I./$(PLATFORM)/system

# Build
BUILD_DIR  = build

OBJS       = $(SRCS:%.c=$(BUILD_DIR)/%.o)
OBJS       := $(OBJS:%.S=$(BUILD_DIR)/%.o)

# Compile flags
CFLAGS  = -g -std=gnu11 $(OPTS)
CFLAGS += -march=$(ARCH) -mabi=$(ABI)
CFLAGS += -Wall #-ffreestanding -nostdlib
CFLAGS += $(INCLUDES)
CFLAGS += $(DEFS)

# Linker flags
LDFLAGS = -T $(LDSCRIPT)
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map
LDFLAGS += -march=$(ARCH) -mabi=$(ABI)
LDFLAGS += -nostartfiles
LDFLAGS += #-nostdlib

# Targets
all: $(BUILD_DIR)/$(TARGET).bin

# Debug
debug: CFLAGS += -DDO_DEBUG # -g3 -O0 # No space in flash right now ):
debug: clean all

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

# Clean
clean:
	rm -rf $(BUILD_DIR)
