// SPDX-License-Identifier: AGPL-3.0-only
/* frame_pool.c - frame pool / video buffer manager */
#include "utils/frame_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__attribute__((weak)) void
media_utils_log_error(const char *reason, unsigned int detail)
{
    fprintf(stderr, "media_utils: %s (%u)\n", reason ? reason : "?", detail);
}

__attribute__((weak)) void *media_utils_pool_alloc(size_t size)
{
    return calloc(1, size);
}

__attribute__((weak)) void media_utils_pool_free(void *ptr)
{
    free(ptr);
}

static VideoFrameListInfo *node_of(struct list_head *l)
{
    return list_entry(l, VideoFrameListInfo, mList);
}

static void init_lists(VideoBufferManager *m)
{
    INIT_LIST_HEAD(&m->mFreeFrmList);
    INIT_LIST_HEAD(&m->mValidFrmList);
    INIT_LIST_HEAD(&m->mUsingFrmList);
}

static VIDEO_FRAME_INFO_S *get_oldest_valid(VideoBufferManager *m)
{
    VIDEO_FRAME_INFO_S *f = NULL;

    if (m == NULL)
        return NULL;
    pthread_mutex_lock(&m->mFrmListLock);
    if (!list_empty(&m->mValidFrmList))
        f = &node_of(m->mValidFrmList.next)->mFrame;
    pthread_mutex_unlock(&m->mFrmListLock);
    return f;
}

static VIDEO_FRAME_INFO_S *get_oldest_using(VideoBufferManager *m)
{
    VIDEO_FRAME_INFO_S *f = NULL;

    if (m == NULL)
        return NULL;
    pthread_mutex_lock(&m->mFrmListLock);
    if (!list_empty(&m->mUsingFrmList))
        f = &node_of(m->mUsingFrmList.next)->mFrame;
    pthread_mutex_unlock(&m->mFrmListLock);
    return f;
}

static VIDEO_FRAME_INFO_S *get_spec_using(VideoBufferManager *m, void *addr)
{
    VIDEO_FRAME_INFO_S *f = NULL;
    struct list_head *pos;

    if (m == NULL)
        return NULL;
    pthread_mutex_lock(&m->mFrmListLock);
    list_for_each(pos, &m->mUsingFrmList) {
        VideoFrameListInfo *n = node_of(pos);
        if (n->mFrame.VFrame.mpVirAddr[0] == addr) {
            f = &n->mFrame;
            break;
        }
    }
    pthread_mutex_unlock(&m->mFrmListLock);
    if (f == NULL)
        media_utils_log_error("GetSpecUsingFrameWithAddr: not found", 0);
    return f;
}

static VIDEO_FRAME_INFO_S *get_all_valid_using(VideoBufferManager *m)
{
    VIDEO_FRAME_INFO_S *f = NULL;
    VideoFrameListInfo *n;

    if (m == NULL)
        return NULL;
    pthread_mutex_lock(&m->mFrmListLock);
    if (!list_empty(&m->mUsingFrmList))
        n = node_of(m->mUsingFrmList.next);
    else if (!list_empty(&m->mValidFrmList))
        n = node_of(m->mValidFrmList.next);
    else
        n = NULL;
    if (n != NULL) {
        list_del(&n->mList);
        list_add_tail(&n->mList, &m->mFreeFrmList);
        f = &n->mFrame;
    }
    pthread_mutex_unlock(&m->mFrmListLock);
    return f;
}

static VIDEO_FRAME_INFO_S *get_valid(VideoBufferManager *m)
{
    VIDEO_FRAME_INFO_S *f = NULL;
    VideoFrameListInfo *n;

    if (m == NULL)
        return NULL;
    pthread_mutex_lock(&m->mFrmListLock);
    if (!list_empty(&m->mValidFrmList)) {
        n = node_of(m->mValidFrmList.next);
        list_del(&n->mList);
        list_add_tail(&n->mList, &m->mUsingFrmList);
        f = &n->mFrame;
    }
    pthread_mutex_unlock(&m->mFrmListLock);
    return f;
}

static int release_frame(VideoBufferManager *m, VIDEO_FRAME_INFO_S *f)
{
    struct list_head *pos;
    VideoFrameListInfo *hit = NULL;
    int wake = 0;

    if (m == NULL || f == NULL)
        return FAILURE;
    pthread_mutex_lock(&m->mFrmListLock);
    list_for_each(pos, &m->mUsingFrmList) {
        VideoFrameListInfo *n = node_of(pos);
        if (n->mFrame.VFrame.mpVirAddr[0] == f->VFrame.mpVirAddr[0] ||
            n->mFrame.mId == f->mId) {
            hit = n;
            break;
        }
    }
    if (hit == NULL) {
        pthread_mutex_unlock(&m->mFrmListLock);
        media_utils_log_error("releaseFrame: not in using list", 0);
        return FAILURE;
    }
    *f = hit->mFrame;
    list_del(&hit->mList);
    list_add_tail(&hit->mList, &m->mFreeFrmList);
    if (m->mbWaitUsingFrmEmptyFlag && list_empty(&m->mUsingFrmList))
        wake = 1;
    pthread_mutex_unlock(&m->mFrmListLock);
    if (wake)
        pthread_cond_signal(&m->mCondUsingFrmEmpty);
    return SUCCESS;
}

static int push_frame(VideoBufferManager *m, VIDEO_FRAME_INFO_S *f)
{
    VideoFrameListInfo *n;

    if (m == NULL || f == NULL)
        return FAILURE;
    pthread_mutex_lock(&m->mFrmListLock);
    if (list_empty(&m->mFreeFrmList)) {
        pthread_mutex_unlock(&m->mFrmListLock);
        return FAILURE;
    }
    n = node_of(m->mFreeFrmList.next);
    list_del(&n->mList);
    n->mFrame = *f;
    list_add_tail(&n->mList, &m->mValidFrmList);
    pthread_mutex_unlock(&m->mFrmListLock);
    return SUCCESS;
}

static int using_empty(VideoBufferManager *m)
{
    int empty;

    if (m == NULL)
        return FALSE;
    pthread_mutex_lock(&m->mFrmListLock);
    empty = list_empty(&m->mUsingFrmList);
    pthread_mutex_unlock(&m->mFrmListLock);
    return empty ? TRUE : FALSE;
}

static int wait_using_empty(VideoBufferManager *m)
{
    if (m == NULL)
        return FAILURE;
    pthread_mutex_lock(&m->mFrmListLock);
    m->mbWaitUsingFrmEmptyFlag = 1;
    while (!list_empty(&m->mUsingFrmList))
        pthread_cond_wait(&m->mCondUsingFrmEmpty, &m->mFrmListLock);
    m->mbWaitUsingFrmEmptyFlag = 0;
    pthread_mutex_unlock(&m->mFrmListLock);
    return SUCCESS;
}

VideoBufferManager *VideoBufMgrCreate(int frmNum, int frmSize)
{
    VideoBufferManager *m;
    int i;

    (void)frmSize;
    m = media_utils_pool_alloc(sizeof(*m));
    if (m == NULL)
        return NULL;
    init_lists(m);
    if (pthread_mutex_init(&m->mFrmListLock, NULL) != 0) {
        media_utils_pool_free(m);
        return NULL;
    }
    if (pthread_cond_init(&m->mCondUsingFrmEmpty, NULL) != 0) {
        pthread_mutex_destroy(&m->mFrmListLock);
        media_utils_pool_free(m);
        return NULL;
    }
    m->mFrameNodeNum = 0;
    m->mbWaitUsingFrmEmptyFlag = 0;
    m->mOps.GetOldestValidFrame = get_oldest_valid;
    m->mOps.GetOldestUsingFrame = get_oldest_using;
    m->mOps.GetSpecUsingFrameWithAddr = get_spec_using;
    m->mOps.GetAllValidUsingFrame = get_all_valid_using;
    m->mOps.getValidFrame = get_valid;
    m->mOps.releaseFrame = release_frame;
    m->mOps.pushFrame = push_frame;
    m->mOps.usingFrmEmpty = using_empty;
    m->mOps.waitUsingFrmEmpty = wait_using_empty;
    for (i = 0; i < frmNum; i++) {
        VideoFrameListInfo *n =
            media_utils_pool_alloc(sizeof(VideoFrameListInfo));
        if (n == NULL)
            break;
        INIT_LIST_HEAD(&n->mList);
        list_add_tail(&n->mList, &m->mFreeFrmList);
        m->mFrameNodeNum++;
    }
    return m;
}

void VideoBufMgrDestroy(VideoBufferManager *m)
{
    struct list_head *pos, *n;
    int freed = 0;

    if (m == NULL) {
        media_utils_log_error("VideoBufMgrDestroy: NULL", 0);
        return;
    }
    pthread_mutex_lock(&m->mFrmListLock);
    if (!list_empty(&m->mUsingFrmList))
        media_utils_log_error("VideoBufMgrDestroy: using not empty", 0);
    list_for_each_safe(pos, n, &m->mValidFrmList) {
        VideoFrameListInfo *node = node_of(pos);
        list_del(&node->mList);
        media_utils_pool_free(node);
        freed++;
    }
    list_for_each_safe(pos, n, &m->mFreeFrmList) {
        VideoFrameListInfo *node = node_of(pos);
        list_del(&node->mList);
        media_utils_pool_free(node);
        freed++;
    }
    if (freed != m->mFrameNodeNum)
        media_utils_log_error("VideoBufMgrDestroy: node count mismatch",
                              (unsigned)freed);
    pthread_mutex_unlock(&m->mFrmListLock);
    pthread_cond_destroy(&m->mCondUsingFrmEmpty);
    pthread_mutex_destroy(&m->mFrmListLock);
    media_utils_pool_free(m);
}
