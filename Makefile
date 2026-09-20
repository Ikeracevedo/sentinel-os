PREFIX   := xtensa-esp32-elf-
CC       := $(PREFIX)gcc
OBJCOPY  := $(PREFIX)objcopy
SIZE     := $(PREFIX)size

BUILD    := build
TARGET   := $(BUILD)/kernel

CFLAGS   := -Wall -Wextra -Werror -std=c11 -O2 \
            -ffreestanding -fno-builtin -nostdlib -mlongcalls \
            -Ikernel/include

LDFLAGS  := -T linker.ld -nostdlib

SRCS_C   := kernel/core/kmain.c
SRCS_S   := kernel/arch/xtensa/start.S
OBJS     := $(SRCS_C:%.c=$(BUILD)/%.o) $(SRCS_S:%.S=$(BUILD)/%.o)

.PHONY: all clean flash monitor size

all: $(TARGET).elf $(TARGET).bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

size: $(TARGET).elf
	$(SIZE) $<

flash: all
	@echo "TODO: implementar en HU-E01-04 con esptool.py elf2image + write_flash"

monitor:
	@echo "TODO: implementar en HU-E01-05 (necesita UART funcionando)"

clean:
	rm -rf $(BUILD)
