EXTRA_CFLAGS += -I$(src)

# Define the module name and its source files
obj-m := collector.o rmid_allocator_test_module.o procfs_test_module.o
collector-objs := collector_main.o rdt.o procfs.o rmid_allocator.o
rmid_allocator_test_module-objs := rmid_allocator_test.o rmid_allocator.o
procfs_test_module-objs := procfs_test.o procfs.o

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

rmid_allocator_test: | $(BUILD_DIR)
	@echo "Building only RMID allocator test module"
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) src=$(PWD) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		rmid_allocator_test_module.ko

procfs_test: | $(BUILD_DIR)
	@echo "Building only procfs test module"
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) src=$(PWD) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		procfs_test_module.ko

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) src=$(PWD) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		clean
	rm -rf $(BUILD_DIR)
	rm -f Module.symvers Module.markers modules.order

test: all
	@echo "Running RMID allocator tests..."
	./test_rmid_allocator.sh
	@echo "Running procfs tests..."
	./test_procfs.sh

.PHONY: all clean test rmid_allocator_test procfs_test