obj-m += memory_collector.o

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

all: | $(BUILD_DIR)
	@echo "Building for kernel version: $(KERNEL_VERSION)"
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) src=$(PWD) modules

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) src=$(PWD) clean
	rm -rf $(BUILD_DIR)

.PHONY: all clean