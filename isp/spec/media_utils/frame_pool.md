<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# frame_pool — clean-room behavioural spec (`video_buffer_manager`)

Phase 3c of the media-utils reimplementation. This unit is the only stateful media-utils
component: a three-list frame-header pool. It is classified CLEAN-ROOM because
its observable behaviour is a fixed state machine and, uniquely for this tier,
its **struct layout and vtable order are hard ABI** crossed to the VI glue.
Target ABI: Lindenis V833 / sun8iw19p1, ARMv7 LE, ILP32, **4-byte enums**, musl.

Audience split: this file is the only input the
implementation team may use for this unit. It contains interface facts (type
tags, field order, prototypes, constants, offsets) and derived behaviour, but no
vendor comments and no transcription of vendor code structure.

Naming: the clean artefact is `src/utils/frame_pool.c` + `include/utils/frame_pool.h`.
The type tags and field names below are interface
facts that must be reproduced verbatim so the crossing consumers compile and the
offsets match; the *implementation* decomposition is free.

---

## 1. Exported surface

Two functions and nine method pointers. There is **no `Init`/`DeInit`**; the
lifecycle is `Create`/`Destroy` only. No `flush`, no reset, no getter for the
node count.

| Symbol | Kind | Prototype |
| --- | --- | --- |
| `VideoBufMgrCreate` | export | `VideoBufferManager *VideoBufMgrCreate(int frmNum, int frmSize)` |
| `VideoBufMgrDestroy` | export | `void VideoBufMgrDestroy(VideoBufferManager *pMgr)` |
| vtable slot 1 | member fn | `VIDEO_FRAME_INFO_S* (*GetOldestValidFrame)(VideoBufferManager *pMgr)` |
| vtable slot 2 | member fn | `VIDEO_FRAME_INFO_S* (*GetOldestUsingFrame)(VideoBufferManager *pMgr)` |
| vtable slot 3 | member fn | `VIDEO_FRAME_INFO_S* (*GetSpecUsingFrameWithAddr)(VideoBufferManager *pMgr, void *pVirAddr)` |
| vtable slot 4 | member fn | `VIDEO_FRAME_INFO_S* (*GetAllValidUsingFrame)(VideoBufferManager *pMgr)` |
| vtable slot 5 | member fn | `VIDEO_FRAME_INFO_S* (*getValidFrame)(VideoBufferManager *pMgr)` |
| vtable slot 6 | member fn | `int (*releaseFrame)(VideoBufferManager *pMgr, VIDEO_FRAME_INFO_S *pFrame)` |
| vtable slot 7 | member fn | `int (*pushFrame)(VideoBufferManager *pMgr, VIDEO_FRAME_INFO_S *pFrame)` |
| vtable slot 8 | member fn | `int (*usingFrmEmpty)(VideoBufferManager *pMgr)` |
| vtable slot 9 | member fn | `int (*waitUsingFrmEmpty)(VideoBufferManager *pMgr)` |

Call-through macros (interface facts; all are statement macros taking the
manager first). They are the only names the consumers include:

```
VideoBufMgrGetOldestUsingFrame(pMgr)
VideoBufMgrGetOldestValidFrame(pMgr)
VideoBufMgrGetSpecUsingFrameWithAddr(pMgr, pVirAddr)
VideoBufMgrGetAllValidUsingFrame(pMgr)
VideoBufMgrGetValidFrame(pMgr)
VideoBufMgrReleaseFrame(pMgr, pFrame)
VideoBufMgrPushFrame(pMgr, pFrame)
VideoBufMgrUsingEmpty(pMgr)
VideoBufMgrWaitUsingEmpty(pMgr)
```

Return conventions (constants are interface facts): `SUCCESS` = 0,
`FAILURE` = -1; `TRUE` = 1, `FALSE` = 0. Frame-returning methods return `NULL`
on empty / not-found; `usingFrmEmpty` returns a raw boolean (1 empty, 0 not), not
`SUCCESS`/`FAILURE`.

---

## 2. Types and measured ABI

All sizes/offsets below were **measured**, not guessed, by compiling the vendor
headers with the same cross toolchain the daemon uses and reading compile-time
constants out of read-only data:

```
TC=<sdk>/toolchain
# arm-openwrt-linux-muslgnueabi-gcc 6.4.1 (OpenWrt/Linaro 2017.11)
# -c -O2 -DAWCHIP=0x1721 with the vendor include dirs
#   include/media, media/include/utils, include/utils, system/public/include/utils
# a probe array of sizeof/offsetof/_Alignof values, together with the
#   compile-time constants read from read-only data
```

The same command is the layout reference for the differential (§8). Enum width is
confirmed 4 bytes by `sizeof(VIDEO_FRAME_S) == 144` (three 4-byte enums inside).

### 2.1 `struct list_head`

| Size | Align | Offset | Type | Field |
| --- | --- | --- | --- | --- |
| 8 | 4 | 0 | `struct list_head *` | `next` |
| | | 4 | `struct list_head *` | `prev` |

Singly intrusive, Linux-style, no sentinel payload. The clean header may use a
different internal node type only if the crossing structs embed the same
`{next,prev}` at the same offsets.

### 2.2 `VIDEO_FRAME_S`

Size 144, align 8.

| Offset | Size | Type | Field |
| --- | --- | --- | --- |
| 0 | 4 | `unsigned int` | `mWidth` |
| 4 | 4 | `unsigned int` | `mHeight` |
| 8 | 4 | `VIDEO_FIELD_E` | `mField` |
| 12 | 4 | `PIXEL_FORMAT_E` | `mPixelFormat` |
| 16 | 4 | `VIDEO_FORMAT_E` | `mVideoFormat` |
| 20 | 4 | `COMPRESS_MODE_E` | `mCompressMode` |
| 24 | 12 | `unsigned int[3]` | `mPhyAddr[3]` |
| 36 | 12 | `void *[3]` | `mpVirAddr[3]` |
| 48 | 12 | `unsigned int[3]` | `mStride[3]` |
| 60 | 12 | `unsigned int[3]` | `mHeaderPhyAddr[3]` |
| 72 | 12 | `void *[3]` | `mpHeaderVirAddr[3]` |
| 84 | 12 | `unsigned int[3]` | `mHeaderStride[3]` |
| 96 | 2 | `short` | `mOffsetTop` |
| 98 | 2 | `short` | `mOffsetBottom` |
| 100 | 2 | `short` | `mOffsetLeft` |
| 102 | 2 | `short` | `mOffsetRight` |
| 104 | 8 | `uint64_t` | `mpts` (unit us) |
| 112 | 4 | `unsigned int` | `mExposureTime` |
| 116 | 4 | `unsigned int` | `mFramecnt` |
| 120 | 4 | `int` | `mEnvLV` |
| 124 | 4 | `unsigned int` | `mWhoSetFlag` |
| 128 | 8 | `uint64_t` | `mFlagPts` |
| 136 | 4 | `unsigned int` | `mFrmFlag` |
| 140 | 4 | — | tail padding to align 8 |

The two `uint64_t` fields force 8-byte alignment; the 4 short offset fields
(96..103) leave the 8-aligned hole at 104. Field order is ABI: consumers read
`mpVirAddr[0]`, `mId`, `mWidth/mHeight`, `mPixelFormat`, `mStride[]`,
`mOffset*`, `mFramecnt`, `mFrmFlag`.

### 2.3 `VIDEO_FRAME_INFO_S`

Size 152, align 8.

| Offset | Size | Type | Field |
| --- | --- | --- | --- |
| 0 | 144 | `VIDEO_FRAME_S` | `VFrame` |
| 144 | 4 | `unsigned int` | `mId` (unique per frame) |
| 148 | 4 | — | tail padding |

### 2.4 `VideoFrameListInfo`

Size 160, align 8. One heap node per pool slot; owns the frame header **by
value** and the queue linkage.

| Offset | Size | Type | Field |
| --- | --- | --- | --- |
| 0 | 152 | `VIDEO_FRAME_INFO_S` | `mFrame` |
| 152 | 8 | `struct list_head` | `mList` |

`mList` offset 152 is the `container_of` base used by every list walk.

### 2.5 `VideoBufferManager`

Size 140, align 4 (max member align is 4; no tail padding). Heap-allocated;
consumers hold only a `VideoBufferManager *` and never embed it by value.

| Offset | Size | Type | Field |
| --- | --- | --- | --- |
| 0 | 8 | `struct list_head` | `mFreeFrmList` |
| 8 | 8 | `struct list_head` | `mValidFrmList` |
| 16 | 8 | `struct list_head` | `mUsingFrmList` |
| 24 | 24 | `pthread_mutex_t` | `mFrmListLock` |
| 48 | 48 | `pthread_cond_t` | `mCondUsingFrmEmpty` |
| 96 | 4 | `int` | `mFrameNodeNum` |
| 100 | 4 | `int` | `mbWaitUsingFrmEmptyFlag` |
| 104 | 4 | fn ptr | `GetOldestValidFrame` |
| 108 | 4 | fn ptr | `GetOldestUsingFrame` |
| 112 | 4 | fn ptr | `GetSpecUsingFrameWithAddr` |
| 116 | 4 | fn ptr | `GetAllValidUsingFrame` |
| 120 | 4 | fn ptr | `getValidFrame` |
| 124 | 4 | fn ptr | `releaseFrame` |
| 128 | 4 | fn ptr | `pushFrame` |
| 132 | 4 | fn ptr | `usingFrmEmpty` |
| 136 | 4 | fn ptr | `waitUsingFrmEmpty` |

`sizeof(pthread_mutex_t)=24`, `sizeof(pthread_cond_t)=48`, both align 4, are the
musl/toolchain values; they are libc facts, reproduced automatically by
including the same libc. Because they precede the vtable, a libc change shifts
every function-pointer offset while `sizeof` stays 140 only if the sizes stay.
The clean build must use the same toolchain and the same **base CFLAGS**
(4-byte enums); it must **not** get `-fshort-enums`.

### 2.6 Vtable order (ABI)

Declaration order in the struct is the ABI order; note it differs from the
order in which `Create` assigns the pointers. Slots are at offsets 104, 108,
112, 116, 120, 124, 128, 132, 136, matching §2.5:

| Slot | Offset | Signature |
| --- | --- | --- |
| 1 | 104 | `VIDEO_FRAME_INFO_S* (*)(VideoBufferManager*)` — `GetOldestValidFrame` |
| 2 | 108 | `VIDEO_FRAME_INFO_S* (*)(VideoBufferManager*)` — `GetOldestUsingFrame` |
| 3 | 112 | `VIDEO_FRAME_INFO_S* (*)(VideoBufferManager*, void*)` — `GetSpecUsingFrameWithAddr` |
| 4 | 116 | `VIDEO_FRAME_INFO_S* (*)(VideoBufferManager*)` — `GetAllValidUsingFrame` |
| 5 | 120 | `VIDEO_FRAME_INFO_S* (*)(VideoBufferManager*)` — `getValidFrame` |
| 6 | 124 | `int (*)(VideoBufferManager*, VIDEO_FRAME_INFO_S*)` — `releaseFrame` |
| 7 | 128 | `int (*)(VideoBufferManager*, VIDEO_FRAME_INFO_S*)` — `pushFrame` |
| 8 | 132 | `int (*)(VideoBufferManager*)` — `usingFrmEmpty` |
| 9 | 136 | `int (*)(VideoBufferManager*)` — `waitUsingFrmEmpty` |

---

## 3. Entry-point behaviour

### 3.1 `VideoBufMgrCreate(int frmNum, int frmSize)`

- `frmSize` is **ignored** (the pool stores no buffers, only headers; the real
  buffer memory belongs to the caller/VI).
- Allocate the manager, zero it. Initialise all three list heads empty.
  Initialise the mutex and cond with default attributes.
- Allocate `frmNum` `VideoFrameListInfo` nodes, zero each, append to the free
  list tail, incrementing `mFrameNodeNum`. If a node allocation fails, stop and
  keep the partial pool (no rollback, no error return): `mFrameNodeNum` is the
  count actually allocated.
- `frmNum <= 0` yields a valid manager with zero nodes.
- Assign all nine vtable pointers.
- Return the manager, or `NULL` only if the manager allocation fails.

Initial state: free list holds all nodes; valid and using empty;
`mbWaitUsingFrmEmptyFlag = 0`.

### 3.2 `VideoBufMgrDestroy(VideoBufferManager *pMgr)`

- `pMgr == NULL`: log and return (no-op).
- Lock. If the using list is non-empty, log a fatal-level error and proceed —
  **using nodes are never freed** (leak).
- Free every node in the valid list, then every node in the free list, counting
  them. Unlock.
- If the freed count != `mFrameNodeNum`, log a fatal-level error. (With a
  non-empty using list this always fires.)
- Destroy the cond, destroy the mutex, free the manager.

### 3.3 `pushFrame(pMgr, pFrame)` → `int`

Precondition: caller owns a frame header to publish.

1. Lock.
2. If the free list is empty: unlock, return `FAILURE`. (Pool exhausted; caller
   drops the frame.)
3. Take the free list head node, copy `*pFrame` over `node->mFrame` whole
   (`VIDEO_FRAME_INFO_S` by value), move the node to the valid list tail.
4. Unlock, return `SUCCESS`.

No signal is raised on push. `pFrame == NULL` dereferences.

### 3.4 `getValidFrame(pMgr)` → `VIDEO_FRAME_INFO_S*`

1. Lock.
2. If the valid list is empty: unlock, return `NULL`.
3. Take the valid list head node, move to the using list tail, unlock.
4. Return `&node->mFrame` (pointer into the node, node now in using).

FIFO: oldest published valid frame is returned first.

### 3.5 `releaseFrame(pMgr, pFrame)` → `int`

1. Lock.
2. Walk the using list in order. A node matches if **either**
   `node->mFrame.VFrame.mpVirAddr[0] == pFrame->VFrame.mpVirAddr[0]` **or**
   `node->mFrame.mId == pFrame->mId`. OR, short-circuit, first match wins.
3. On match: `*pFrame = node->mFrame` — **the caller's struct is overwritten
   with the stored node frame** (see §5). Remove the node from using
   (`list_del`), then append it to the free list tail.
4. No match: log an error naming `pFrame->mId`, unlock, return `FAILURE`
   (nothing moved, caller struct untouched).
5. Unlock, then: if `mbWaitUsingFrmEmptyFlag != 0` **and** the using list is
   empty, `pthread_cond_signal(&mCondUsingFrmEmpty)` exactly once.
6. Return `SUCCESS`.

This is the **only** place the manager's cond is signalled. The signal condition
is "a waiter is registered and this release emptied the using list"; interior
releases (using still non-empty) do not signal.

### 3.6 `GetAllValidUsingFrame(pMgr)` → `VIDEO_FRAME_INFO_S*`

A drainer, one node per call. Contrary to the plural name it returns a single
frame, and it moves the node to **free**, not to using.

1. Lock.
2. If using **and** valid are both empty: unlock, return `NULL`.
3. If using is non-empty: take its head node, move to free tail, return
   `&node->mFrame`.
4. Else (valid non-empty): take its head node, move to free tail, return
   `&node->mFrame`.

Precedence: all using nodes are drained before any valid node. The returned
frame's node is already reusable by `pushFrame` at return time.

### 3.7 `GetOldestValidFrame(pMgr)` / `GetOldestUsingFrame(pMgr)` → `VIDEO_FRAME_INFO_S*`

Lock; return `&head->mFrame` of the valid (resp. using) list, or `NULL` if
empty; unlock. **No move** — the node stays where it is. Pure peek.

### 3.8 `GetSpecUsingFrameWithAddr(pMgr, pVirAddr)` → `VIDEO_FRAME_INFO_S*`

Lock; walk the using list; return `&node->mFrame` for the first node whose
`node->mFrame.VFrame.mpVirAddr[0] == pVirAddr`. If none, log an error and return
`NULL`. No move. Comparison uses plane 0 only; a lookup with `pVirAddr == NULL`
matches the first using node whose plane-0 address is NULL.

### 3.9 `usingFrmEmpty(pMgr)` → `int`

Lock; `iRet = (using list is empty)`; unlock; return `iRet`. Returns 1 when
empty, 0 otherwise — a raw boolean, not `SUCCESS`/`FAILURE`. Callers negate it.

### 3.10 `waitUsingFrmEmpty(pMgr)` → `int`

1. Lock.
2. `mbWaitUsingFrmEmptyFlag = TRUE`.
3. While the using list is non-empty: log (throttled by level) and
   `pthread_cond_wait(&mCondUsingFrmEmpty, &mFrmListLock)`.
4. `mbWaitUsingFrmEmptyFlag = FALSE`; unlock; return `SUCCESS`.

If the using list is already empty on entry the function returns immediately.
The loop predicate is list emptiness (not the flag), so no wakeup is lost; the
flag exists only so `releaseFrame` knows whether to signal. A `cond_signal`
(not broadcast) and a single shared flag mean this function supports **one
concurrent waiter only** (see §9).

---

## 4. State machine

Each of the `mFrameNodeNum` nodes is in exactly one of three lists; the three
lists partition the pool at all times.

```
            pushFrame (copy frame in)
   FREE  ------------------------------>  VALID
     ^                                     |
     |                                     | getValidFrame
     | releaseFrame (copy frame out)       v
     +--------------------------------- USING
       ^
       |  GetAllValidUsingFrame
       +-- (prefers USING head; else VALID head)
```

| Transition | Method | Source | Destination | Frame data | Order |
| --- | --- | --- | --- | --- | --- |
| free→valid | `pushFrame` | free head (must be non-empty) | valid tail | caller struct → node | FIFO |
| valid→using | `getValidFrame` | valid head (must be non-empty) | using tail | unchanged | FIFO |
| using→free | `releaseFrame` | first using match (required) | free tail | node → caller struct (overwrite) | match order |
| using→free | `GetAllValidUsingFrame` | using head | free tail | unchanged | FIFO |
| valid→free | `GetAllValidUsingFrame` | valid head (only if using empty) | free tail | unchanged | FIFO |
| (none) | `GetOldest*`, `GetSpec*` | any list | same list | unchanged | peek |

Invariants:

- Node count is constant: `len(free) + len(valid) + len(using) == mFrameNodeNum`
  (holds from `Create`; `Destroy` is terminal).
- Every take/append is head→tail, so each list is FIFO and all three are FIFO
  between producer and consumer.
- `mFrameNodeNum` is written only by `Create`; never mutated afterwards.
- `pushFrame` is the only way a frame enters the pool; `releaseFrame` is the
  only way a node returns from using to free with the caller-visible copy.

Mutex / cond usage:

| Object | Guards | Notes |
| --- | --- | --- |
| `mFrmListLock` | all three lists, `mbWaitUsingFrmEmptyFlag` | held across every list mutation and each predicate check |
| `mCondUsingFrmEmpty` | paired with `mFrmListLock` | signalled only by `releaseFrame` (last using node) |

Exact signal condition: `mbWaitUsingFrmEmptyFlag != 0 && list_empty(mUsingFrmList)`
evaluated in `releaseFrame` after unlocking, then one `pthread_cond_signal`.
Wait condition: `while (!list_empty(mUsingFrmList)) pthread_cond_wait(...)`.

Note: the VI consumer has a **separate** frame-available signal
(`cdx_sem_t mSemWaitInputFrame`, embedded by value in the VI channel struct) that
the component raises after a successful `pushFrame`. That semaphore is not part
of this manager and does not share the manager's cond. Do not merge them.

---

## 5. Ownership and the caller-overwrite contract

- The manager owns only the frame **headers** (`VideoFrameListInfo` nodes and the
  `VIDEO_FRAME_INFO_S` inside them). It never allocates or frees pixel buffers;
  `mPhyAddr[]` / `mpVirAddr[]` are borrowed from the caller and shared.
- `pushFrame` copies the full 152-byte `VIDEO_FRAME_INFO_S` into the node.
- `releaseFrame` copies the stored node frame back into the caller's struct:
  `*pFrame = node->mFrame`. This is intentional and load-bearing: the consumer
  passes back the same pointer it received from `getValidFrame`, so the rewrite
  restores any fields the consumer may have modified. A clean implementation
  **must keep this write-back**; a no-op release fails the differential.
- `getValidFrame` etc. return a pointer into a live node. The pointer is valid
  only while the node stays in using: after `releaseFrame` the node is free and
  can be reused by `pushFrame`, so the pointer is stale. Callers must not retain
  it.
- `GetAllValidUsingFrame` returns a pointer to a node it has already moved to
  free; it can be reused concurrently. Consume immediately.

---

## 6. Edge / null / error behaviour

| Situation | Result |
| --- | --- |
| `Create` manager alloc fails | `NULL` |
| `Create` with `frmNum <= 0` | valid manager, 0 nodes; every push fails, every get returns NULL |
| `Create` node alloc fails midway | valid manager with a partial pool, no error |
| `pushFrame` on full pool (free empty) | `FAILURE`, pool unchanged |
| `pushFrame(NULL frame)` | undefined (dereference) |
| `getValidFrame` on empty valid | `NULL` |
| `releaseFrame` no matching node | `FAILURE`, pool unchanged, caller struct unchanged, logs |
| `releaseFrame` on a node not in using (already free/valid) | `FAILURE` (same as no-match) |
| `releaseFrame` match by `mId` only (virAddr differs) | matches, overwrites caller struct |
| `releaseFrame` match by `mpVirAddr[0]` only (mId differs) | matches, overwrites caller struct |
| two using nodes share `mpVirAddr[0]` or `mId` | first in using order wins |
| `GetAllValidUsingFrame` both lists empty | `NULL` |
| `GetOldest*` on empty list | `NULL` |
| `GetSpecUsingFrameWithAddr` not found | `NULL`, logs |
| `usingFrmEmpty` | 1 if empty else 0 |
| `waitUsingFrmEmpty` already empty | returns `SUCCESS` immediately |
| `waitUsingFrmEmpty` with two concurrent waiters | only one is guaranteed to wake (§9) |
| `Destroy(NULL)` | no-op, logs |
| `Destroy` with using non-empty | using nodes leaked; count-mismatch error logged; still destroys primitives and frees manager |

---

## 7. Required ABI and clean-tree status

- **Enums must be 4 bytes.** `VIDEO_FIELD_E`, `PIXEL_FORMAT_E`,
  `VIDEO_FORMAT_E`, `COMPRESS_MODE_E` cross inside `VIDEO_FRAME_INFO_S` to VI
  glue that is also 4-byte-enum. Compile the clean
  unit with the base media-utils CFLAGS; do **not** apply
  `-fshort-enums -DISP521_RTOS_ALGO=1`.
- **Integer widths / layout.** Reproduce §2 exactly: `list_head` `{next,prev}`,
  the three lists at 0/8/16, `mFrameNodeNum`/`mbWaitUsingFrmEmptyFlag` after the
  pthread objects, and the nine function pointers in the §2.6 order.
- **Clean media ABI header.** The clean tree carries the media ABI in
  `include/media_utils_abi.h`: `struct list_head`, `VIDEO_FRAME_S`,
  `VIDEO_FRAME_INFO_S`, `VideoFrameListInfo`, `cdx_sem_t`, `VideoBufferManager`
  and its ops vtable, and the nine `VideoBufMgr*` call-through macros declared
  in this spec. `include/utils/frame_pool.h` re-exports it and declares
  `VideoBufMgrCreate`/`VideoBufMgrDestroy`. Both the clean
  `src/utils/frame_pool.c` and the VI glue replacement include it, exactly as the
  vendor header is used today.
- `cdx_sem_t` (embedded by value in the VI channel struct at
  `VideoVirVi_Component.c`) is a sibling unit (tsemaphore phase 2); measured
  here for the cross-check: size 76, align 4 — `pthread_cond_t` at 0 (48),
  `pthread_mutex_t` at 48 (24), `unsigned int semval` at 72. It is not defined
  by this unit but the VI header must have it at the same offsets.

---

## 8. Host differential plan

Goal: prove the clean `frame_pool` is byte- and behaviour-identical to the
vendor manager under a scripted sequence, including the layout crossing.

**Build.** Cross-compile with the daemon's musl toolchain (GCC 6.4.1,
`arm-openwrt-linux-muslgnueabi`, `-DAWCHIP=0x1721`, base CFLAGS, 4-byte enums).
Link two ARM test binaries that share one driver translation unit:
`bin_vendor` with the vendor `video_buffer_manager.o`; `bin_clean` with the clean
`frame_pool.o`. Run both under `qemu-arm` (static; the private differential
harness provides the qemu build). The driver must not include the vendor
`.c`; it links one implementation at a time and drives through the public
macros/prototypes only.

**Determinism.** Fill every `VIDEO_FRAME_INFO_S` with a deterministic byte
pattern (per-field distinct values, so a swapped field is visible). Drive all
sequences single-threaded except the wait cases, which use a two-thread
handshake with the same schedule in both binaries (the parent thread calls
`waitUsingFrmEmpty`, the child performs the releases; use a barrier to fix
ordering). qemu-user schedules threads non-deterministically in general, so the
wait test asserts only the observable condition (return happens after the last
release, not before), and repeats it enough to catch a missing signal.

**Comparison at every step.** After each operation dump a canonical record:

1. Return value / returned pointer-NULL-ness.
2. A full `VIDEO_FRAME_INFO_S` byte image of the caller's frame (for
   `releaseFrame`, comparing before/after proves the overwrite; for
   `getValidFrame`, the returned frame contents).
3. A canonical image of the manager: for each list, walk `next`/`prev` and
   record node identity (allocation index) and order; plus `mFrameNodeNum`,
   `mbWaitUsingFrmEmptyFlag`, and each vtable slot's symbol identity.
4. Raw struct bytes for the layout check: `memcpy` of the whole
   `VideoBufferManager` and of each node. Because list links, pthread internals,
   and function addresses are run-specific, canonicalise them first: mask the
   three list-head and node-link pointer values to a token, mask the
   mutex/cond byte ranges, and map each vtable slot to its expected function
   index. Compare the canonicalised images. A pointer-free layout probe (§2)
   additionally asserts `sizeof`/`offsetof`/`_Alignof` for
   `VIDEO_FRAME_S`, `VIDEO_FRAME_INFO_S`, `VideoFrameListInfo`,
   `VideoBufferManager`, `list_head`; the vendor-header values (this spec) and
   the clean-header values must be equal.

**Scripted cases** (run in order; each must match vendor):

| # | Case | Asserts |
| --- | --- | --- |
| 1 | `Create(0, 0)` | `mFrameNodeNum==0`, three empty lists, push FAILURE, both gets NULL, `usingFrmEmpty==1` |
| 2 | `Create(4, 0)` | 4 nodes, free=4/valid=0/using=0, list order = alloc order |
| 3 | push 4 distinct frames | valid order == push order, free empty, `mFrameNodeNum` stable |
| 4 | push 5th | FAILURE, all lists unchanged |
| 5 | get x4 | returned frames FIFO == pushed, using order == valid order, valid empty, 5th get NULL |
| 6 | release each with its own struct | SUCCESS, caller struct overwritten with node image, using drains in order, free grows to 4, signal fired on last |
| 7 | release unknown (fake id/addr) | FAILURE, pool + caller struct unchanged |
| 8 | release matching by `mId` only | SUCCESS, overwrite observed |
| 9 | release matching by `mpVirAddr[0]` only | SUCCESS, overwrite observed |
| 10 | two using nodes sharing `mId` | first using node wins |
| 11 | interleave push/get then `GetAllValidUsingFrame` to exhaustion | all using drained before any valid; each returns to free; order matches; then NULL; caller frame contents match |
| 12 | `GetOldestValidFrame`/`GetOldestUsingFrame`/`GetSpecUsingFrameWithAddr` | found → same node/order; not found → NULL; no list move (order unchanged) |
| 13 | `GetSpecUsingFrameWithAddr(NULL)` with a NULL-addr using node | matches that node |
| 14 | `waitUsingFrmEmpty` on empty pool | returns SUCCESS with no block |
| 15 | `waitUsingFrmEmpty` with k using; release k-1 then last | no return until last release; flag back to 0 after |
| 16 | exhaustion concurrency: `waitUsingFrmEmpty` while pushes keep the pool valid but using non-empty | stays blocked; returns only when using empties |
| 17 | `Destroy` with nodes in free/valid/using | vendor and clean agree on freed-node count and leak behaviour (using nodes never returned to free); `Destroy(NULL)` no-op |
| 18 | partial `Create` (fail the 3rd node alloc via an injectable allocator) | `mFrameNodeNum==2`, pool usable, no rollback |

Cases 14–16 are the only threaded ones; run each ≥100 iterations to bound the
signal-loss window. Case 18 needs the clean implementation to take a pluggable
allocator (or the driver to fault-inject via a wrapper); if the clean unit does
not expose one, record it as an untestable divergence and assert the
deterministic subset (case 2 shape) instead.

Acceptance: zero mismatches in all cases and the layout probe, under qemu-arm
with the marker toolchain. Record the exact commands in the harness run plan.

---

## 9. Ambiguities and hazards

1. **Crossed layout is hard ABI.** VI walks `mpCapMgr->mUsingFrmList` directly
   (`VideoVirVi_Component.c`) and calls all nine vtable slots through the
   macros. The clean header must keep the field order of §2.5/§2.6. The manager
   is held by pointer, so VI does not embed it by value; the pthread-field
   offsets only have to match within the same libc build (they precede the
   vtable).
2. **Caller-overwrite in `releaseFrame`.** The write-back `*pFrame = node->mFrame`
   mutates the caller's struct. Any clean implementation that treats the output
   frame as pass-through-only will diverge. This is the single most likely
   clean-room mistake.
3. **OR match on `mpVirAddr[0]` or `mId`.** A node matches on either key, first
   using node wins. Stale pointers or reused nodes can match the wrong frame.
   The differential must cover both single-key matches and a collision.
4. **`GetAllValidUsingFrame` returns a frame whose node is already free.** The
   name implies "collect all"; it returns one. The node is immediately
   push-reusable, so the returned pointer is racy by construction. Preserve the
   using-before-valid priority and the one-per-call behaviour.
5. **Single-waiter `waitUsingFrmEmpty`.** One `pthread_cond_signal`, one boolean
   flag. With two concurrent waiters, one may never be woken after the last
   release clears the flag. The vendor code is safe only under its one-waiter
   usage; the clean replacement must reproduce the same observable behaviour
   (do not "improve" it to broadcast).
6. **Signal check outside the lock.** `releaseFrame` evaluates
   `mbWaitUsingFrmEmptyFlag && list_empty(mUsingFrmList)` after unlocking. Under
   the intended single-waiter pattern the list predicate keeps it correct, but it
   is a formal data race. Reproduce the observable signal timing, not the
   unlocked read.
7. **`mbWaitUsingFrmEmptyFlag` is set even when the list is already empty** and
   then immediately cleared. A `releaseFrame` in that window can signal a cond
   nobody waits on; harmless, but the flag's transient value is observable in
   the differential if probed.
8. **`Destroy` leaks using nodes.** They are neither freed nor returned to free.
   The node-count mismatch log fires whenever using is non-empty. A clean
   implementation that frees them would over-free or change the count.
9. **Partial allocation / negative `frmNum`.** No rollback; `frmNum <= 0` gives
   an empty but valid pool. `frmSize` is ignored.
10. **No validation of inputs.** `pushFrame` with a NULL frame and the getters
    with a NULL manager dereference. The spec does not require defensive checks;
    matching vendor means matching the crash.
11. **`usingFrmEmpty` boolean vs `SUCCESS`.** Callers use
    `if (!VideoBufMgrUsingEmpty(pMgr))`; returning -1/0 incorrectly flips the
    test. Return 1/0.
12. **`mFrame` is copied whole, including the 4-byte tail padding of
    `VIDEO_FRAME_INFO_S`.** The differential should compare all 152 bytes (the
    padding content is carried by the compiler's copy) rather than only the
    semantically meaningful fields.
13. **VI reads the using list without the manager lock.** In
    `DoVideoViReturnAllValidFrames` the walk of `mUsingFrmList` is protected
    only by the component's state lock, not `mFrmListLock`. This is a consumer
    property, not a manager one, but it means the clean manager cannot rely on
    exclusive access to its lists from VI; keep node mutation atomic-ish and do
    not add manager-side state that assumes quiescence.
