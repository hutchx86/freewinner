// SPDX-License-Identifier: AGPL-3.0-only
#ifndef UTILS_MSGQUEUE_H
#define UTILS_MSGQUEUE_H
#include "media_utils_abi.h"

int  message_create(message_queue_t *q);
void message_destroy(message_queue_t *q);
int  put_message(message_queue_t *q, message_t *msg);
int  putMessageWithData(message_queue_t *q, message_t *msg);
int  get_message(message_queue_t *q, message_t *msg);
int  TMessage_WaitQueueNotEmpty(message_queue_t *q, unsigned int timeout_ms);

#endif
