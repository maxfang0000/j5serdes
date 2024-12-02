.PHONY: all default config clean

ifneq ($(wildcard ./build_config),)
-include build_config
else
-include build_config.template
endif

ifndef $(INSTALL_INC_DIR)
INSTALL_INC_DIR := $(INSTALL_DIR)/include
endif

ifndef $(INSTALL_LIB_DIR)
INSTALL_LIB_DIR := $(INSTALL_DIR)/lib
endif

ifndef $(INSTALL_BIN_DIR)
INSTALL_BIN_DIR := $(INSTALL_DIR)/bin
endif

RP := realpath

ifeq ($(shell uname),Darwin)
	RP := grealpath
endif

default: config

all: utest

utest: config utest-config

config: src/configure
	@if [ ! -e $(BUILD_DIR) ]; then                                    \
	  echo "... creating build directory $(BUILD_DIR)";                \
	  mkdir -p $(BUILD_DIR);                                           \
	fi
	@cd $(BUILD_DIR) &&                                                \
	$(CURDIR)/src/configure prefix=$(shell $(RP) -m $(INSTALL_DIR))    \
	INSTALL_INC_DIR=$(shell $(RP) -m $(INSTALL_INC_DIR))               \
	INSTALL_LIB_DIR=$(shell $(RP) -m $(INSTALL_LIB_DIR))               \
	INSTALL_BIN_DIR=$(shell $(RP) -m $(INSTALL_BIN_DIR))               \
	DEBUG=$(DEBUG) OPT=$(OPT) #&& make

utest-config: utest/configure
	@if [ ! -e $(BUILD_DIR)/utest ]; then                              \
	  echo "... creating build directory $(BUILD_DIR)/utest";          \
	  mkdir -p $(BUILD_DIR)/utest;                                     \
	fi
	@cd $(BUILD_DIR)/utest &&                                          \
	$(CURDIR)/utest/configure prefix=$(shell $(RP) $(BUILD_DIR)/utest) \
	DEBUG=$(DEBUG) OPT=$(OPT)                                          \
	TARGET_INC_DIR=$(shell $(RP) -m $(INSTALL_INC_DIR))                \
	TARGET_LIB_DIR=$(shell $(RP) -m $(INSTALL_LIB_DIR)) #&& make

src/configure: src/configure.ac
	@echo "... generate configure script"
	@cd ./src && autoconf
	@chmod 550 $@

utest/configure: utest/configure.ac
	@echo "... generate configure script for utest"
	@cd ./utest && autoconf
	@chmod 550 $@

clean:
	@rm -rf $(BUILD_DIR) src/configure utest/configure
	@rm -rf build

