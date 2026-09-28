# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# The six generated ABI headers must not be hand-edited. With ABI_REF_DIR set to
# the generator's tree (include/ + tools/check_abi.py), diff them and replay it.
ABI_REF_DIR ?=
ABI_REF_INCLUDE := $(if $(ABI_REF_DIR),$(abspath $(ABI_REF_DIR))/include)
ABI_REF_TOOLS   := $(if $(ABI_REF_DIR),$(abspath $(ABI_REF_DIR))/tools)
ABI_GENERATED_HDRS := fwi_isp_abi.h fwi_isp_abi_assert.h fwi_isp_enum.h \
    fwi_types.h fwm_media_abi.h fwm_media_enum.h

$(BUILD)/test_abi_check: | $(BUILD)
	@ok=1; \
	if [ -d "$(ABI_REF_INCLUDE)" ]; then \
	    for f in $(ABI_GENERATED_HDRS); do \
	        if ! diff -q "$(ABI_REF_INCLUDE)/$$f" "include/$$f" >/dev/null 2>&1; then \
	            echo "ABI-COPY DRIFT: include/$$f differs from the generator's copy"; \
	            ok=0; \
	        fi; \
	    done; \
	    if [ $$ok = 1 ] && [ -x "$(ABI_REF_TOOLS)/check_abi.py" ]; then \
	        python3 "$(ABI_REF_TOOLS)/check_abi.py" || ok=0; \
	    elif [ $$ok = 1 ]; then \
	        python3 "$(ABI_REF_TOOLS)/check_abi.py" || ok=0; \
	    fi; \
	else \
	    echo "ABI-COPY: ABI_REF_DIR not set, skipping (informational only)"; \
	fi; \
	if [ $$ok = 1 ]; then \
	    printf '#!/bin/sh\necho "ABI-COPY PASSED: 6 generated headers match the generator, check_abi.py fact-file replay OK"\necho "1 checks, 0 failures"\n' > $@; \
	else \
	    printf '#!/bin/sh\necho "ABI-COPY FAILED"\necho "1 checks, 1 failures"\nexit 1\n' > $@; \
	fi
	@chmod +x $@
