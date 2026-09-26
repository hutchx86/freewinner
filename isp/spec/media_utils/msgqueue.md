<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# media-utils phase 2 — `msgqueue` (intrusive message queue)

Behaviour-only spec. Source unit: the platform message queue. Live exports:
`message_create`, `message_destroy`, `get_message`, `put_message`,
`putMessageWithData`, `TMessage_WaitQueueNotEmpty`. The queue's flush operation
and message-count query are dead and **omitted** (phase 1).

## 1. ABI

`message_t` and `message_queue_t` are carried by the shared clean ABI; field
order is ABI (VI glue embeds the queue by value). `MAX_MESSAGE_ELEMENTS == 8`.

```c
typedef struct message_t {
    int id; int command; int para0; int para1;
    void *mpData; int mDataSize;
    struct list_head mList;
} message_t;
typedef struct message_queue_t {
    struct list_head mIdleMessageList;
    struct list_head mReadyMessageList;
    int message_count;
    pthread_mutex_t mutex;
    pthread_cond_t  mCondMessageQueueChanged;
    int mWaitMessageFlag;
} message_queue_t;
```

## 2. Interface

```c
int  message_create(message_queue_t *q);
void message_destroy(message_queue_t *q);
int  put_message(message_queue_t *q, message_t *msg_in);
int  get_message(message_queue_t *q, message_t *msg_out);
int  putMessageWithData(message_queue_t *q, message_t *msg_in);
int  TMessage_WaitQueueNotEmpty(message_queue_t *q, unsigned int timeout);
```

## 3. Behaviour

- **`message_create`** — init `mutex`; init `mCondMessageQueueChanged` with the
  **monotonic** clock; `mWaitMessageFlag = 0`; `INIT_LIST_HEAD` both lists;
  append `MAX_MESSAGE_ELEMENTS` freshly `malloc`ed nodes to the idle list;
  `message_count = 0`. Return `0`, or `-1` on a pthread-init or malloc failure
  (destroy what was created in the failure path).
- **`message_destroy`** — lock. For each ready node: free its `mpData`, set
  `mDataSize = 0`, move to idle, decrement `message_count`. If
  `message_count != 0` after draining the ready list, emit a diagnostic. Free
  every idle node. Re-init both list heads. Unlock, destroy the cond, destroy
  the mutex.
- **`put_message(q, msg_in)`** — ignores `msg_in->mpData`: builds a local message
  with `command/para0/para1` copied and `mpData = NULL`, `mDataSize = 0`, then
  calls `putMessageWithData`. (The queued copy never carries a payload.)
- **`get_message(q, msg_out)`** — lock; if the ready list is empty, unlock and
  return `-1` (does **not** block). Otherwise copy the first ready node's
  `command/para0/para1/mpData/mDataSize` into `*msg_out` (shallow; the payload
  pointer is transferred), move the node to the idle list, decrement
  `message_count`, unlock, return `0`.
- **`putMessageWithData(q, msg_in)`** — lock; if the idle list is empty, append
  another `MAX_MESSAGE_ELEMENTS` nodes; take the first idle node and deep-copy
  `command/para0/para1` and, when `mpData != NULL && mDataSize >= 0`,
  `malloc(mDataSize)` and `memcpy` the payload (on malloc failure return `-1`).
  On success move the node to ready, increment `message_count`, and if
  `mWaitMessageFlag` is set signal the condition; unlock and return `0`
  (`-1` on the copy failure, and on the grow failure before the copy).
- **`TMessage_WaitQueueNotEmpty(q, timeout)`** — lock; set
  `mWaitMessageFlag = 1`. If `timeout <= 0`, block on the condition until the
  ready list is non-empty. Otherwise, if the ready list is empty, wait with the
  monotonic `pthread_cond_wait_timeout` for `timeout` ms (ignore its return
  code). Clear `mWaitMessageFlag = 0`, read `message_count`, unlock, return it.

## 4. Omitted (phase 1)

The queue's flush operation and message-count query (no current caller).

## 5. Verification plan

- Host unit test: create → 8 idle nodes; `put_message` drops `mpData` while
  `putMessageWithData` deep-copies (mutate the source afterwards and check the
  queued copy); FIFO `get_message`; `get_message` on empty returns `-1` without
  blocking; `message_count` tracks puts/gets; the idle list grows past 8 with 9+
  outstanding puts; `TMessage_WaitQueueNotEmpty` returns immediately when ready
  and unblocks/expiry via a helper thread; destroy frees payloads and nodes.
- Differential: qemu-arm scripted single-threaded sequence (create / put /
  putMessageWithData / get / count / destroy) comparing return codes, the
  `message_count` and the list-head consistency between vendor and clean; the
  blocking/wait paths covered by the host test.
