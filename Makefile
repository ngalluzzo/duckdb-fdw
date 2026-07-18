PROJ_DIR := $(CURDIR)/

EXT_NAME=duckdb_api
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Keep discovery relative to CURDIR so GNU Make does not split an absolute
# include path when a checkout directory contains spaces. Supported entry
# points invoke this Makefile from its own directory, directly or with `-C`.
EXTENSION_TEMPLATE_MAKEFILE := extension-ci-tools/makefiles/duckdb_extension.Makefile
override DUCKDB_API_NATIVE_GOALS := help bootstrap build test demo paths verify
override DUCKDB_API_REQUESTED_GOALS := $(strip $(MAKECMDGOALS))
override DUCKDB_API_REQUESTED_NATIVE_GOALS := $(filter $(DUCKDB_API_NATIVE_GOALS),$(DUCKDB_API_REQUESTED_GOALS))
override DUCKDB_API_REQUESTED_UPSTREAM_GOALS := $(filter-out $(DUCKDB_API_NATIVE_GOALS),$(DUCKDB_API_REQUESTED_GOALS))

ifneq ($(DUCKDB_API_REQUESTED_NATIVE_GOALS),)
ifneq ($(DUCKDB_API_REQUESTED_UPSTREAM_GOALS),)
$(error native goal(s) $(DUCKDB_API_REQUESTED_NATIVE_GOALS) cannot be combined with Community/upstream goal(s) $(DUCKDB_API_REQUESTED_UPSTREAM_GOALS))
endif
endif

ifneq ($(DUCKDB_API_REQUESTED_UPSTREAM_GOALS),)
ifeq ($(wildcard $(EXTENSION_TEMPLATE_MAKEFILE)),)
$(error Community/upstream goal(s) $(DUCKDB_API_REQUESTED_UPSTREAM_GOALS) require an initialized extension-ci-tools submodule)
endif
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
