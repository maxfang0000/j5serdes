# top-level makefile to populate build and output directories.
# this makefile is designed to be project-agnostic.

.PHONY: all default test config test-config clean

RP := realpath
ifeq ($(shell uname),Darwin)
RP := grealpath
endif

PROJECT := $(shell $(RP) -m . | rev | cut -d'/' -f1 | rev)

ifneq ($(wildcard ./build_config),)
include build_config
else
include build_config.template
endif

RP_CHECK := $(shell command -v $(RP) 2>/dev/null)
ifeq ($(RP_CHECK),)
$(error gnu realpath is not found under the name '$(RP)'. please install, \
or override the command name in build_config with 'RP'. on macos, it could \
be installed with 'brew install coreutils')
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

SRC_IN_FILES := $(shell find ./src -type f -name '*.in')
TEST_IN_FILES := $(shell find ./test -type f -name '*.in')

define GENERATE_CONFIG_MK
	@echo "... setting build options in $(1)/config.mk"
	@echo "CFG_SRC_DIR := $(shell $(RP) -m $(2))" > $(1)/config.mk
	@echo "CFG_BUILD_DIR := $(1)" >> $(1)/config.mk
	@echo "CFG_INSTALL_DIR := $(INSTALL_DIR)" >> $(1)/config.mk
	@echo "CFG_INSTALL_INC_DIR := $(INSTALL_INC_DIR)" >> $(1)/config.mk
	@echo "CFG_INSTALL_LIB_DIR := $(INSTALL_LIB_DIR)" >> $(1)/config.mk
	@echo "CFG_INSTALL_BIN_DIR := $(INSTALL_BIN_DIR)" >> $(1)/config.mk
	@echo "CFG_DEBUG := $(DEBUG)" >> $(1)/config.mk
	@echo "CFG_OPT := $(OPT)" >> $(1)/config.mk
	@echo "CFG_VERSION := $(VERSION)" >> $(1)/config.mk
endef

define APPEND_TEST_CONFIG_MK
	@echo "CFG_TARGET_INC_DIR := $(INSTALL_INC_DIR)" >> $(1)/config.mk
	@echo "CFG_TARGET_LIB_DIR := $(INSTALL_LIB_DIR)" >> $(1)/config.mk
endef

default: config

all: test

test: config test-config

$(BUILD_DIR):
	@echo "... creating build directory $@"
	@mkdir -p $@


config: $(BUILD_DIR) $(SRC_IN_FILES)
	@echo "... build directory is $(BUILD_DIR)"
	@echo "... populating make files"
	@for in_file in $(SRC_IN_FILES); do                                          \
		dest_path=$$(echo $$in_file | cut -d'/' -f3-);                             \
		out_file=$$(basename $$dest_path .in);                                     \
		mkdir -p $(BUILD_DIR)/$$(dirname $$dest_path);                             \
		cp $$in_file $(BUILD_DIR)/$$(dirname $$dest_path)/$$out_file;              \
	done
	@touch $(BUILD_DIR)/.update_mk
	$(call GENERATE_CONFIG_MK,$(BUILD_DIR),./src)

TEST_DIR := $(BUILD_DIR)/test

$(TEST_DIR):
	@echo "... creating test directory $@"
	@mkdir -p $@

test-config: $(TEST_DIR) $(TEST_IN_FILES)
	@echo "... test directory is $(TEST_DIR)"
	@echo "... populating test make files"
	@for in_file in $(TEST_IN_FILES); do                                         \
		dest_path=$$(echo $$in_file | cut -d'/' -f3-);                             \
		out_file=$$(basename $$dest_path .in);                                     \
		mkdir -p $(BUILD_DIR)/test/$$(dirname $$dest_path);                        \
		cp $$in_file $(BUILD_DIR)/test/$$(dirname $$dest_path)/$$out_file;         \
	done
	@touch $(BUILD_DIR)/.update_mk
	$(call GENERATE_CONFIG_MK,$(BUILD_DIR)/test,./test)
	$(call APPEND_TEST_CONFIG_MK,$(BUILD_DIR)/test)

clean:
	@rm -rf $(BUILD_DIR)
	@rm -rf build

