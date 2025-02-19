EXTRA_CFLAGS += -I$(src)

# Define the module name and its source files
obj-m := collector.o
collector-objs := memory_collector.o rdt.o procfs.o

# Always set architecture to x86_64 (not just x86)
ARCH := x86_64

# Check if KVERSION is provided on command line
ifdef KVERSION
    # Use provided version
    KERNEL_VERSION := $(KVERSION)
else
    # Check if we're in the dev container by looking for a specific file
    # that would only exist in the container (like our Dockerfile)
    ifneq (,$(wildcard /.dockerenv))
        # In container - use the version we installed
        KERNEL_VERSION := 6.8.0-52-generic
    else
        # Not in container - use current kernel
        KERNEL_VERSION := $(shell uname -r)
    endif
endif

KDIR := /lib/modules/$(KERNEL_VERSION)/build
BUILD_DIR := $(PWD)/build

# Set cross-compilation by default in container
ifneq (,$(wildcard /.dockerenv))
    CROSS_COMPILE := x86_64-linux-gnu-
endif

# Also set cross-compilation on ARM machines
ifeq ($(shell uname -m),arm64)
    CROSS_COMPILE := x86_64-linux-gnu-
endif

all: | $(BUILD_DIR)
	@echo "Building for kernel version: $(KERNEL_VERSION)"
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) src=$(PWD) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		modules

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) src=$(PWD) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		clean
	rm -rf $(BUILD_DIR)
	rm -f Module.symvers Module.markers modules.order

.PHONY: all clean