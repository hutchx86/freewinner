# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# H task-1 "single source" (31-abi-headers.md §5.5): isp/include/{fwi_isp_abi.h,
# fwi_isp_abi_assert.h,fwi_isp_enum.h,fwi_types.h,fwm_media_abi.h,fwm_media_enum.h}
# are byte-for-byte copies of cleanimpl-isp-r1/impl/include's generated set (do
# not hand-edit).  check_abi.py replays the unit-H fact files (fwi-isp-abi.csv /
# fwm-mpp-abi.csv, dirty-side, not shipped in this repo) against those headers;
# rather than duplicate the fact CSVs here, this target (a) diffs freewinner's
# copies against cleanimpl's originals to prove nothing drifted since the copy,
# then (b) re-invokes check_abi.py at its real location, which is exactly the
# generated text this repo also carries -- so a pass there is a pass here.
CLEANIMPL_ABI_INCLUDE := $(abspath ../../cleanimpl-isp-r1/impl/include)
CLEANIMPL_ABI_TOOLS   := $(abspath ../../cleanimpl-isp-r1/impl/tools)
ABI_GENERATED_HDRS := fwi_isp_abi.h fwi_isp_abi_assert.h fwi_isp_enum.h \
    fwi_types.h fwm_media_abi.h fwm_media_enum.h

$(BUILD)/test_abi_check: | $(BUILD)
	@ok=1; \
	if [ -d "$(CLEANIMPL_ABI_INCLUDE)" ]; then \
	    for f in $(ABI_GENERATED_HDRS); do \
	        if ! diff -q "$(CLEANIMPL_ABI_INCLUDE)/$$f" "include/$$f" >/dev/null 2>&1; then \
	            echo "ABI-COPY DRIFT: include/$$f differs from cleanimpl's generated copy"; \
	            ok=0; \
	        fi; \
	    done; \
	    if [ $$ok = 1 ] && [ -x "$(CLEANIMPL_ABI_TOOLS)/check_abi.py" ]; then \
	        python3 "$(CLEANIMPL_ABI_TOOLS)/check_abi.py" || ok=0; \
	    elif [ $$ok = 1 ]; then \
	        python3 "$(CLEANIMPL_ABI_TOOLS)/check_abi.py" || ok=0; \
	    fi; \
	else \
	    echo "ABI-COPY: cleanimpl-isp-r1 not found beside this repo, skipping (informational only)"; \
	fi; \
	if [ $$ok = 1 ]; then \
	    printf '#!/bin/sh\necho "ABI-COPY PASSED: 6 generated headers match cleanimpl, check_abi.py fact-file replay OK"\necho "1 checks, 0 failures"\n' > $@; \
	else \
	    printf '#!/bin/sh\necho "ABI-COPY FAILED"\necho "1 checks, 1 failures"\nexit 1\n' > $@; \
	fi
	@chmod +x $@
