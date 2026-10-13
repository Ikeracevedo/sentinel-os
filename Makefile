PREFIX   := xtensa-esp32-elf-
CC       := $(PREFIX)gcc
SIZE     := $(PREFIX)size
ESPTOOL  := esptool.py

BUILD    := build
TARGET   := $(BUILD)/kernel

PORT     := $(or $(lastword $(sort $(wildcard /dev/ttyUSB*))),/dev/ttyUSB0)
BAUD     := 460800
FLASH_ADDR := 0x1000

EXTRA    :=

CFLAGS   := -Wall -Wextra -Werror -std=c11 -O2 \
            -ffreestanding -fno-builtin -nostdlib -mlongcalls \
            -Ikernel/include $(EXTRA)

LDFLAGS  := -T linker.ld -nostdlib

SRCS_C   := kernel/core/kmain.c kernel/core/panic.c kernel/core/time.c kernel/arch/xtensa/irq.c kernel/drivers/gpio.c kernel/drivers/uart.c kernel/lib/kprintf.c kernel/lib/memops.c kernel/core/task.c kernel/core/sched.c kernel/core/sync.c kernel/core/selftest.c kernel/core/tests_e04.c kernel/core/tests_e05.c kernel/fs/vfs.c kernel/fs/procfs.c kernel/shell/shell.c kernel/shell/cmds.c kernel/mm/heap.c
SRCS_S   := kernel/arch/xtensa/start.S kernel/arch/xtensa/vectors.S
OBJS     := $(SRCS_C:%.c=$(BUILD)/%.o) $(SRCS_S:%.S=$(BUILD)/%.o)

.PHONY: all clean flash monitor size test

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
	python3 -m serial.tools.miniterm $(PORT) 57600

HOSTCC     := gcc
HOSTFLAGS  := -std=c11 -Wall -Wextra -g -Ikernel/include

# Tests en el PC: heap.c y vfs.c son C puro y se compilan sin cambios (-DHEAP_HOST / -DVFS_HOST).
test:
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTFLAGS) -DHEAP_HOST tests/test_heap.c kernel/mm/heap.c -o $(BUILD)/test_heap
	./$(BUILD)/test_heap
	$(HOSTCC) $(HOSTFLAGS) -DHEAP_HOST -DVFS_HOST tests/test_vfs.c kernel/fs/vfs.c kernel/mm/heap.c -o $(BUILD)/test_vfs
	./$(BUILD)/test_vfs

clean:
	rm -rf $(BUILD)
