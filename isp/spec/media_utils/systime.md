<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# media-utils phase 2 — `systime` (time + condition-wait helpers)

Behaviour-only spec. Source unit: the platform system base. Live exports:
`pthread_cond_wait_timeout`, `CDX_SetTimeUs`, `CDX_GetTimeUs`.
`CDX_GetSysTimeUsMonotonic` is dead and is **omitted** (phase 1).

## 1. Interface

```c
int     pthread_cond_wait_timeout(pthread_cond_t *condition,
                                  pthread_mutex_t *mutex,
                                  unsigned int msecs);
int64_t CDX_GetTimeUs(void);
int     CDX_SetTimeUs(int64_t timeUs);
```

## 2. Behaviour

**`pthread_cond_wait_timeout(cond, mutex, msecs)`** — wait on `cond` with an
absolute timeout computed from the **monotonic** clock:
1. `clock_gettime(CLOCK_MONOTONIC, &ts)`;
2. add `msecs/1000` seconds and `(msecs%1000)*1000000` nanoseconds; normalise
   `tv_nsec` into `tv_sec` (carry the overflow, then take the remainder);
3. `pthread_cond_timedwait(cond, mutex, &ts)`;
4. return the `pthread_cond_timedwait` return code unchanged (`0` on signal,
   `ETIMEDOUT` on expiry, other error codes otherwise). No logging side effect
   is part of the contract.

**`CDX_GetTimeUs()`** — wall-clock microseconds:
`gettimeofday(&tv)`; return `(int64_t)tv.tv_usec + tv.tv_sec * 1000000`.

**`CDX_SetTimeUs(timeUs)`** — wall-clock set:
`tv_sec = timeUs/1000000`, `tv_usec = timeUs%1000000`; `settimeofday(&tv, NULL)`;
return `0` on success, `-1` if it fails.

## 3. Omitted (phase 1)

`CDX_GetSysTimeUsMonotonic` (a monotonic microsecond clock with no current
caller), and the compiled-out `dumpCallStack` (`#if 0`).

## 4. Verification plan

- Host unit test: `pthread_cond_wait_timeout` expiry returns `ETIMEDOUT` and
  takes at least the requested interval (bounded); signal from a helper thread
  returns `0`; `CDX_GetTimeUs` is non-decreasing across calls and wall-clock
  based; `CDX_SetTimeUs` returns 0 and read-back is within a second (run last /
  restore via `settimeofday` guard, or skip the mutation in the unit suite and
  cover only the argument decomposition).
- Differential: qemu-arm run comparing `pthread_cond_wait_timeout` expiry code
  and the timeout-path elapsed bound, and `CDX_GetTimeUs` deltas between the two
  implementations.
