# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# Intrusive message queue; default 4-byte enums, never -fshort-enums.
# malloc/free are wrapped to fault-inject create unwind and check destroy leaks.
$(BUILD)/test_msgqueue: src/utils/msgqueue.c tests/test_msgqueue.c include/utils/msgqueue.h include/media_utils_abi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/utils/msgqueue.c tests/test_msgqueue.c -o $@ $(LDFLAGS) -pthread -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=free
