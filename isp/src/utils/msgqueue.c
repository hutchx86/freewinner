// SPDX-License-Identifier: AGPL-3.0-only
/* msgqueue.c - unit U FIFO message queue (21-utils.md §3) */
#include "utils/msgqueue.h"
#include "utils/systime.h"
#include <stdlib.h>
#include <string.h>

void media_utils_log_error(const char *reason, unsigned int detail);

static message_t *alloc_node(void)
{
    message_t *m = calloc(1, sizeof(*m));
    if (m != NULL)
        INIT_LIST_HEAD(&m->mList);
    return m;
}

static int grow_idle(message_queue_t *q, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        message_t *m = alloc_node();
        if (m == NULL)
            return FAILURE;
        list_add_tail(&m->mList, &q->mIdleMessageList);
    }
    return SUCCESS;
}

static void free_idle(message_queue_t *q)
{
    struct list_head *pos, *n;
    list_for_each_safe(pos, n, &q->mIdleMessageList) {
        message_t *m = list_entry(pos, message_t, mList);
        list_del(&m->mList);
        free(m);
    }
}

int message_create(message_queue_t *q)
{
    pthread_condattr_t ca;

    if (q == NULL)
        return FAILURE;
    if (pthread_mutex_init(&q->mutex, NULL) != 0)
        return FAILURE;
    if (pthread_condattr_init(&ca) != 0) {
        pthread_mutex_destroy(&q->mutex);
        return FAILURE;
    }
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    if (pthread_cond_init(&q->mCondMessageQueueChanged, &ca) != 0) {
        pthread_condattr_destroy(&ca);
        pthread_mutex_destroy(&q->mutex);
        return FAILURE;
    }
    pthread_condattr_destroy(&ca);
    INIT_LIST_HEAD(&q->mIdleMessageList);
    INIT_LIST_HEAD(&q->mReadyMessageList);
    q->message_count = 0;
    q->mWaitMessageFlag = 0;
    if (grow_idle(q, MAX_MESSAGE_ELEMENTS) != SUCCESS) {
        free_idle(q);
        pthread_cond_destroy(&q->mCondMessageQueueChanged);
        pthread_mutex_destroy(&q->mutex);
        return FAILURE;
    }
    return SUCCESS;
}

void message_destroy(message_queue_t *q)
{
    struct list_head *pos, *n;

    if (q == NULL)
        return;
    pthread_mutex_lock(&q->mutex);
    list_for_each_safe(pos, n, &q->mReadyMessageList) {
        message_t *m = list_entry(pos, message_t, mList);
        if (m->mpData != NULL)
            free(m->mpData);
        list_del(&m->mList);
        m->mpData = NULL;
        m->mDataSize = 0;
        list_add_tail(&m->mList, &q->mIdleMessageList);
        q->message_count--;
    }
    if (q->message_count != 0)
        media_utils_log_error("message_destroy: count not zero", 0);
    free_idle(q);
    pthread_mutex_unlock(&q->mutex);
    pthread_cond_destroy(&q->mCondMessageQueueChanged);
    pthread_mutex_destroy(&q->mutex);
}

int putMessageWithData(message_queue_t *q, message_t *msg)
{
    message_t *node;

    if (q == NULL || msg == NULL)
        return FAILURE;
    pthread_mutex_lock(&q->mutex);
    if (list_empty(&q->mIdleMessageList)) {
        if (grow_idle(q, MAX_MESSAGE_ELEMENTS) != SUCCESS) {
            pthread_mutex_unlock(&q->mutex);
            return FAILURE;
        }
    }
    node = list_first_entry(&q->mIdleMessageList, message_t, mList);
    list_del(&node->mList);
    node->command = msg->command;
    node->para0 = msg->para0;
    node->para1 = msg->para1;
    if (msg->mpData != NULL && msg->mDataSize >= 0) {
        node->mpData = malloc((size_t)msg->mDataSize);
        if (node->mpData == NULL) {
            list_add_tail(&node->mList, &q->mIdleMessageList);
            pthread_mutex_unlock(&q->mutex);
            return FAILURE;
        }
        memcpy(node->mpData, msg->mpData, (size_t)msg->mDataSize);
        node->mDataSize = msg->mDataSize;
    } else {
        node->mpData = NULL;
        node->mDataSize = 0;
    }
    list_add_tail(&node->mList, &q->mReadyMessageList);
    q->message_count++;
    if (q->mWaitMessageFlag)
        pthread_cond_signal(&q->mCondMessageQueueChanged);
    pthread_mutex_unlock(&q->mutex);
    return SUCCESS;
}

int put_message(message_queue_t *q, message_t *msg)
{
    message_t stripped;

    if (q == NULL || msg == NULL)
        return FAILURE;
    memset(&stripped, 0, sizeof(stripped));
    stripped.command = msg->command;
    stripped.para0 = msg->para0;
    stripped.para1 = msg->para1;
    stripped.mpData = NULL;
    stripped.mDataSize = 0;
    return putMessageWithData(q, &stripped);
}

int get_message(message_queue_t *q, message_t *msg)
{
    message_t *node;

    if (q == NULL || msg == NULL)
        return FAILURE;
    pthread_mutex_lock(&q->mutex);
    if (list_empty(&q->mReadyMessageList)) {
        pthread_mutex_unlock(&q->mutex);
        return FAILURE;
    }
    node = list_first_entry(&q->mReadyMessageList, message_t, mList);
    msg->command = node->command;
    msg->para0 = node->para0;
    msg->para1 = node->para1;
    msg->mpData = node->mpData;
    msg->mDataSize = node->mDataSize;
    node->mpData = NULL;
    node->mDataSize = 0;
    list_del(&node->mList);
    list_add_tail(&node->mList, &q->mIdleMessageList);
    q->message_count--;
    pthread_mutex_unlock(&q->mutex);
    return SUCCESS;
}

int TMessage_WaitQueueNotEmpty(message_queue_t *q, unsigned int timeout_ms)
{
    int count;

    if (q == NULL)
        return 0;
    pthread_mutex_lock(&q->mutex);
    q->mWaitMessageFlag = 1;
    if (timeout_ms == 0) {
        while (list_empty(&q->mReadyMessageList))
            pthread_cond_wait(&q->mCondMessageQueueChanged, &q->mutex);
    } else if (list_empty(&q->mReadyMessageList)) {
        pthread_cond_wait_timeout(&q->mCondMessageQueueChanged, &q->mutex,
                                  timeout_ms);
    }
    q->mWaitMessageFlag = 0;
    count = q->message_count;
    pthread_mutex_unlock(&q->mutex);
    return count;
}
