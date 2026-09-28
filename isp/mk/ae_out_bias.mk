# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
$(BUILD)/test_ae_out_bias: src/tables/ae_out_bias.c tests/test_ae_out_bias.c include/freeisp/ae_out_bias.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)
