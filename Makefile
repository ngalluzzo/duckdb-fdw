PROJ_DIR := $(CURDIR)/

EXT_NAME=duckdb_api
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Keep discovery relative to CURDIR so GNU Make does not split an absolute
# include path when a checkout directory contains spaces. Supported entry
# points invoke this Makefile from its own directory, directly or with `-C`.
EXTENSION_TEMPLATE_MAKEFILE := extension-ci-tools/makefiles/duckdb_extension.Makefile

ifneq ($(wildcard $(EXTENSION_TEMPLATE_MAKEFILE)),)
include $(EXTENSION_TEMPLATE_MAKEFILE)
else

.DEFAULT_GOAL := help

PROFILE ?= debug
NATIVE_DEV := $(PROJ_DIR)scripts/native-dev.sh

.PHONY: help bootstrap build test demo paths verify

help:
	@"$(NATIVE_DEV)" help

bootstrap:
	@"$(NATIVE_DEV)" bootstrap

build:
	@"$(NATIVE_DEV)" build "$(PROFILE)"

test:
	@"$(NATIVE_DEV)" test "$(PROFILE)"

demo:
	@"$(NATIVE_DEV)" demo "$(PROFILE)"

paths:
	@"$(NATIVE_DEV)" paths "$(PROFILE)"

verify:
	@"$(NATIVE_DEV)" verify "$(PROFILE)"

endif
