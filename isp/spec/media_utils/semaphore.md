<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# media-utils phase 2 — `semaphore` (counted condition-variable semaphore)

Behaviour-only spec. Source unit: the platform counting semaphore. Live exports:
`cdx_sem_init`, `cdx_sem_deinit`, `cdx_sem_down`, `cdx_sem_down_timedwait`,
`cdx_sem_up`. The remaining six exports are dead and **omitted** (phase 1).

## 1. ABI

`cdx_sem_t` is carried by the shared clean ABI:
`{ pthread_cond_t condition; pthread_mutex_t mutex; unsigned int semval; }`.
Field order is ABI (VI glue embeds it by value).

## 2. Interface

```c
int  cdx_sem_init(cdx_sem_t *tsem, unsigned int val);
void cdx_sem_deinit(cdx_sem_t *tsem);
void cdx_sem_down(cdx_sem_t *tsem);
int  cdx_sem_down_timedwait(cdx_sem_t *tsem, unsigned int timeout);  /* ms */
void cdx_sem_up(cdx_sem_t *tsem);
```

## 3. Behaviour

**`cdx_sem_init(tsem, val)`** — create `condition` with the **monotonic** clock
(`pthread_condattr_setclock(CLOCK_MONOTONIC)`), then `mutex`, then set
`semval = val`. Return `-1` if either pthread init fails, else `0`. If the
condition init fails the mutex is not initialised; if the mutex init fails the
condition is left initialised (reproduce; no cleanup).

**`cdx_sem_deinit(tsem)`** — lock `mutex`, destroy `condition`, unlock, destroy
`mutex`.

**`cdx_sem_down(tsem)`** — lock; while `semval == 0` wait on `condition`;
then `semval--`; unlock.

**`cdx_sem_down_timedwait(tsem, timeout)`** — lock. If `semval == 0`, compute an
absolute **monotonic** deadline as `now + timeout` (ms → s + ns with carry) and
`pthread_cond_timedwait`. Return the wait's return code (`0`/`ETIMEDOUT`/other).
After the wait — and also when `semval` was already non-zero, in which case no
wait occurs and the returned code is `0` — decrement `semval` **only if
`semval > 0`**. Unlock, return the code.

**`cdx_sem_up(tsem)`** — lock; `semval++`; signal `condition`; unlock.

## 4. Omitted (phase 1)

The platform semaphore's up-unique, reset, wait, timed-wait, signal and
get-value operations.

## 5. Verification plan

- Host unit test: init with 0/1/N; `up` then `down` returns and decrements;
  `down` blocks while zero and is released by a second thread's `up`;
  `down_timedwait` on a zero semaphore returns `ETIMEDOUT` and does **not**
  decrement; `down_timedwait` on a non-zero semaphore returns `0` immediately and
  decrements; deinit does not crash.
- Differential: qemu-arm scripted single-threaded sequence (init/up/down/
  timedwait-timeout/deinit) comparing every `semval` transition and return code
  between vendor and clean; the blocking-release path covered by the host test.
