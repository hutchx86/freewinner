# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# media-utils phase 2: intrusive message queue.  Built with the base CFLAGS,
# i.e. default 4-byte enums; never add -fshort-enums to this object.  pthread is
# required for the queue mutex/cond, and malloc/free are wrapped so the test can
# fault-inject the create unwind and check the destroy path for leaks.
$(BUILD)/test_msgqueue: src/utils/msgqueue.c tests/test_msgqueue.c include/utils/msgqueue.h include/media_utils_abi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/utils/msgqueue.c tests/test_msgqueue.c -o $@ $(LDFLAGS) -pthread -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=free
