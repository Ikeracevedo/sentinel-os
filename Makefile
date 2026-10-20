PREFIX   := xtensa-esp32-elf-
.DEFAULT_GOAL := all
CC       := $(PREFIX)gcc
SIZE     := $(PREFIX)size
ESPTOOL  := esptool.py

TRACE    ?= 1
TRANSPORT ?= 0
BUILD    := build
ifeq ($(TRACE),0)
BUILD    := build/trace-off
endif
TARGET   := $(BUILD)/kernel

PORT     := $(or $(lastword $(sort $(wildcard /dev/ttyUSB*))),/dev/ttyUSB0)
BAUD     := 460800
FLASH_ADDR := 0x1000

EXTRA    :=

CFLAGS   := -Wall -Wextra -Werror -std=c11 -O2 \
            -ffreestanding -fno-builtin -nostdlib -mno-longcalls -MMD -MP \
            -Ikernel/include -DSENTINEL_TRACE=$(TRACE) -DSENTINEL_TRANSPORT=$(TRANSPORT) $(EXTRA)

LDFLAGS  := -T linker.ld -nostdlib

SRCS_C   := kernel/core/kmain.c kernel/core/panic.c kernel/core/time.c kernel/core/frame.c kernel/arch/xtensa/irq.c kernel/drivers/gpio.c kernel/drivers/uart.c kernel/lib/kprintf.c kernel/lib/memops.c kernel/core/task.c kernel/core/sched.c kernel/core/sync.c kernel/core/graph.c kernel/core/detector.c kernel/core/selftest.c kernel/core/tests_e04.c kernel/core/tests_e05.c kernel/core/trace.c kernel/fs/vfs.c kernel/fs/procfs.c kernel/shell/shell.c kernel/shell/cmds.c kernel/shell/trace_cmd.c kernel/shell/inject_cmd.c kernel/mm/heap.c
SRCS_S   := kernel/arch/xtensa/start.S kernel/arch/xtensa/vectors.S

# En TRACE=0 no se enlazan ni las funciones ni los 32 KB del ring buffer.
ifeq ($(TRACE),0)
SRCS_C   := $(filter-out kernel/core/trace.c kernel/shell/trace_cmd.c,$(SRCS_C))
endif

ifeq ($(TRANSPORT),1)
ifeq ($(TRACE),0)
$(error TRANSPORT=1 requiere TRACE=1)
endif
SRCS_C += kernel/core/transport.c kernel/core/downlink.c
endif

# La prueba del ring buffer se incluye solo cuando se pide expresamente.
TRACE_BOARD_TEST ?= 0
ifeq ($(TRACE_BOARD_TEST),1)
SRCS_C += tests/test_trace_board.c
LDFLAGS += -Wl,--wrap=shell_start
endif

# La prueba de sondas del planificador se construye por separado.
TRACE_SCHED_BOARD_TEST ?= 0
ifeq ($(TRACE_SCHED_BOARD_TEST),1)
SRCS_C += tests/test_trace_sched_board.c
LDFLAGS += -Wl,--wrap=shell_start
endif

# La misma carga de heap y mutex mide el coste de TRACE=0 frente a TRACE=1.
TRACE_COST_BOARD_TEST ?= 0
ifeq ($(TRACE_COST_BOARD_TEST),1)
SRCS_C += tests/test_trace_cost_board.c
LDFLAGS += -Wl,--wrap=shell_start
endif

# Prueba de latidos del timer, incluso mientras solo corre idle.
TRACE_HEARTBEAT_BOARD_TEST ?= 0
ifeq ($(TRACE_HEARTBEAT_BOARD_TEST),1)
SRCS_C += tests/test_trace_heartbeat_board.c
LDFLAGS += -Wl,--wrap=shell_start
endif

# Cinco tareas durante 60 s por corrida, para comparar TRACE=0 y TRACE=1.
TRACE_OVERHEAD_BOARD_TEST ?= 0
ifeq ($(TRACE_OVERHEAD_BOARD_TEST),1)
SRCS_C += tests/test_trace_overhead_board.c
CFLAGS += -DTRACE_OVERHEAD_BENCH=1
LDFLAGS += -Wl,--wrap=shell_start
endif

# E08: prueba acotada de tramas reales en UART, sin shell concurrente.
FRAME_BOARD_TEST ?= 0
ifeq ($(FRAME_BOARD_TEST),1)
SRCS_C += tests/test_frame_board.c
CFLAGS += -DFRAME_BOARD_TEST=1
endif

# Flujo determinista a ritmo sostenido, con UART bloqueante, para aislar
# cable/USB del emisor por interrupcion de TRANSPORT=1.
FRAME_BOARD_STRESS ?= 0
ifeq ($(FRAME_BOARD_STRESS),1)
ifneq ($(FRAME_BOARD_TEST),1)
$(error FRAME_BOARD_STRESS=1 requiere FRAME_BOARD_TEST=1)
endif
FRAME_BOARD_DRAIN_CHUNK ?= 0
FRAME_BOARD_STRESS_SLEEP_MS ?= 10
CFLAGS += -DFRAME_BOARD_STRESS=1 \
          -DFRAME_BOARD_DRAIN_CHUNK=$(FRAME_BOARD_DRAIN_CHUNK) \
          -DFRAME_BOARD_STRESS_SLEEP_MS=$(FRAME_BOARD_STRESS_SLEEP_MS)
endif

OBJS     := $(SRCS_C:%.c=$(BUILD)/%.o) $(SRCS_S:%.S=$(BUILD)/%.o)
-include $(OBJS:.o=.d)

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

# Pruebas en WSL: heap, VFS y ring buffer de traza.
test:
	@mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTFLAGS) -O2 -Werror -DGRAPH_HOST tests/test_graph.c kernel/core/graph.c -o $(BUILD)/test_graph
	./$(BUILD)/test_graph
	$(HOSTCC) $(HOSTFLAGS) -Werror tests/test_frame.c kernel/core/frame.c -o $(BUILD)/test_frame
	./$(BUILD)/test_frame
	$(HOSTCC) $(HOSTFLAGS) -Werror tests/test_downlink.c kernel/core/downlink.c kernel/core/frame.c -o $(BUILD)/test_downlink
	./$(BUILD)/test_downlink
	python3 -m unittest discover -s tests -p test_bridge.py
	python3 -m unittest discover -s tests -p test_e10_graph_board.py
	python3 -m unittest discover -s tests -p test_e10_nominal_board.py
	python3 -m unittest discover -s tests -p test_e10_frag_board.py
	python3 -m unittest discover -s tests -p test_e10_hog_board.py
	$(HOSTCC) $(HOSTFLAGS) -DHEAP_HOST tests/test_heap.c kernel/mm/heap.c -o $(BUILD)/test_heap
	./$(BUILD)/test_heap
	$(HOSTCC) $(HOSTFLAGS) -DHEAP_HOST -DVFS_HOST tests/test_vfs.c kernel/fs/vfs.c kernel/mm/heap.c -o $(BUILD)/test_vfs
	./$(BUILD)/test_vfs
	$(HOSTCC) $(HOSTFLAGS) -O2 -Werror -DTRACE_HOST tests/test_trace.c kernel/core/trace.c -o $(BUILD)/test_trace
	./$(BUILD)/test_trace
	$(HOSTCC) $(HOSTFLAGS) -O2 -Werror -DTRACE_HOST tests/test_trace_filter.c kernel/core/trace.c -o $(BUILD)/test_trace_filter
	./$(BUILD)/test_trace_filter

clean:
	rm -rf $(BUILD)
