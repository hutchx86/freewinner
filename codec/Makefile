# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# Top-level build. Each module ships a fragment in mk/<module>.mk defining its test
# binary; modules are independent.

CC       ?= gcc
CFLAGS   ?= -std=gnu11 -O2 -Wall -Wextra -fwrapv
CPPFLAGS ?= -Iinclude
LDFLAGS  ?= -lm
BUILD    := build

# FAAC source tree (caller-supplied; never committed). Override on the command
# line: make check FAAC_DIR=/path/to/faac
FAAC_DIR ?= third_party/faac

# The differential (golden) test suites and their goldens are kept private.
# To run them: make check ORACLE_TESTS=/path/to/tests GOLDEN_DIR=/path/to/golden
ORACLE_TESTS ?=
GOLDEN_DIR ?= $(CURDIR)/golden

-include $(wildcard mk/*.mk)

TESTS := $(AAC_TEST) $(VE_TEST) $(H264_HEADERS_TEST) $(H264_REG_TEST) $(H264_RC_TEST) $(H264_ISP_TEST) $(H264_GOP_TEST) $(H264_OVERLAY_TEST) $(VENC_BASE_TEST) $(FENC_TEST) $(H264_ENC_TESTS) $(H265_UNIT_TESTS) $(H265_ENC_TESTS)

.PHONY: all check clean
all: $(TESTS)

# Tests exit 2 when a golden under $(GOLDEN_DIR) is absent/unparseable; an absent
# golden SKIPs by default, or fails hard when ALLOW_GOLDENS=1.
check: all
	@rc=0; for t in $(TESTS); do echo "== $$t =="; $$t; s=$$?; \
	  if [ $$s -eq 2 ] && [ "$${ALLOW_GOLDENS:-0}" != "1" ]; then \
	    echo "   SKIP: goldens withheld (set ALLOW_GOLDENS=1 to run)"; \
	  elif [ $$s -ne 0 ]; then \
	    echo "   FAIL (exit $$s)"; rc=1; \
	  fi; \
	done; exit $$rc

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
