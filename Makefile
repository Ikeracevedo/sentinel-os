PREFIX   := xtensa-esp32-elf-
CC       := $(PREFIX)gcc
SIZE     := $(PREFIX)size
ESPTOOL  := esptool.py

BUILD    := build
TARGET   := $(BUILD)/kernel

PORT     := /dev/ttyUSB0
BAUD     := 460800
FLASH_ADDR := 0x1000

CFLAGS   := -Wall -Wextra -Werror -std=c11 -O2 \
            -ffreestanding -fno-builtin -nostdlib -mlongcalls \
            -Ikernel/include

LDFLAGS  := -T linker.ld -nostdlib

SRCS_C   := kernel/core/kmain.c kernel/drivers/gpio.c
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
	$(ESPTOOL) --chip esp32 elf2image \
		--flash_mode dio --flash_freq 40m --flash_size 4MB \
		-o $@ $<

size: $(TARGET).elf
	$(SIZE) $<

flash: all
	$(ESPTOOL) --chip esp32 --port $(PORT) --baud $(BAUD) \
		write_flash $(FLASH_ADDR) $(TARGET).bin

monitor:
	python3 -m serial.tools.miniterm $(PORT) 115200

clean:
	rm -rf $(BUILD)
