# HSLock — Security Issues

Security audit of the firmware at commit `5e317b9`, covering the serial console and
command dispatch, storage and backup, the WiFi/NTP network surface, and the
hardware/dual-core paths. Third-party code under `libs/` was reviewed only where
first-party code calls into it.

Use this as a todo list. Items are ordered by severity within each tier.

## Deployment and threat model

All electronics live **inside** the building in a protected enclosure. Only the
**keypad and the LED** are outside. The serial console is reachable from inside only.

| # | Attacker position | Reaches |
|---|---|---|
| **A** | Outside, no credentials | The keypad matrix (rows GPIO 10-13, cols 6-9) and the LED wiring |
| **B** | Network — same LAN, on-path, or controlling DHCP/DNS | WiFi association, DNS, NTP (UDP/123) |
| **C** | Inside the building | The USB serial console, and the board inside its enclosure |

Position **C** requires prior entry — legitimately (member, guest, contractor,
delivery), by tailgating, through a propped door, or by first exploiting **A**/**B**.
It is an insider / post-entry escalation position, not an initial-entry bypass, and
**C** items are ranked below **A**/**B** items of comparable impact.

Two things keep the **C** tier from being merely theoretical:

1. **The consequence crosses back out.** The console prints every 20-byte TOTP seed
   (`get-key-secret`, `export-keys`). One brief presence inside yields permanent
   credentials for every enrolled user, usable from *outside*, surviving membership
   revocation, with no audit trail and no revocation signal.
2. **"Inside" is a broad population** for a hackerspace — members, guests and
   visitors are not one trust level, and none of them should be able to read every
   other member's seed.

The keypad ribbon carries only matrix rows/columns and the LED line, so it does not
expose the console. It does let an attacker outside splice in an automated key
injector, which is what makes **H1** practical rather than theoretical.

## Accepted risks

Decisions recorded so they are not re-raised.

### TOTP codes are replayable within their ~90 s window — accepted

`shared/totp.c:42-58` is stateless, so with `TOTP_WINDOW 1` (`totp.h:10`) a code is
accepted for T-1/T/T+1 and stays usable for ~90 s, unlimited times. RFC 6238 §5.2
would have us reject an already-consumed step.

**Accepted:** whoever just entered stays near the door for several minutes and would
notice anyone following them in, so the live window is covered by direct observation.

**One dependency this creates.** Replay protection was also the second line of
defence against **C3** (clock rewind). Without it, C3's persisted monotonic time
floor becomes the *sole* control preventing a code captured hours or days ago from
being replayed — and in that scenario the original user is long gone, so the
observation argument does not apply. **C3 must be fixed**, and it is no longer
defence in depth.

If you ever want the window tightened without adding state, `TOTP_WINDOW 0` cuts it
from 90 s to 30 s and removes two thirds of H1's guess surface — worth revisiting
once the clock is trustworthy, but not required by this decision.

## Status legend

- **[verified]** — traced in the code and confirmed directly.
- **[verify-on-hw]** — the code path is confirmed, but impact depends on Pico SDK or
  silicon behaviour that could not be exercised here (the ARM toolchain and
  `PICO_SDK_PATH` are absent). Confirm on a device.
- **[fixed]** — confirmed on hardware and remediated; kept here for history.
- **[ignored]** — reviewed and deliberately not fixing; rationale recorded inline.

The two bugs recorded in `test/FUZZING.md` (littlefs `lookahead_size` overflow,
`to_key_record` non-bool load) were re-checked and are still fixed. Not listed.

---

## CRITICAL

### C1 — Keypad verdicts are discarded by the SDK lockout IRQ handler — the door may never open **[A · availability]** [fixed]

**Confirmed on hardware: a correct keypad code did not open the door.** Fixed together
with C2 below by replacing the core1→core0→core1 FIFO messages with a lock-free
shared-memory mailbox (`shared/door_verify.h`) that never touches the SIO FIFO
`multicore_lockout_victim_init()` claims. `multicore_lockout_victim_init()` itself is
kept (`core1.c`) since `flash_safe_execute` still needs it.

`core1.c:97` calls `multicore_lockout_victim_init()`, then `core1.c:71-82` waits for
core 0's verdict by *polling* `multicore_fifo_rvalid()` in thread mode.

`multicore_lockout_victim_init()` installs an exclusive `SIO_IRQ_PROC1` handler and
enables that IRQ; the SDK's handler drains the RX FIFO and discards every word that
is not its lockout magic, and the SDK documents that the FIFO "may not be used for
any other purpose after this". `SIO_IRQ_PROC1` is level-asserted while the FIFO is
non-empty, and core 1 spends nearly all of its wait inside `sleep_ms(5)` with
interrupts enabled — so the IRQ should win the race against the poll essentially
every time, and each verdict pushed at `main.c:81` is eaten.

If so, **every keypad attempt — including correct codes — falls through to the 2 s
timeout at `core1.c:83-87` and the door never opens.** Fail-closed, so not a bypass,
but a total availability failure of the primary entry path. Git history is consistent
with a regression in `7eb99ed`, which moved verification to core 0 over the FIFO; the
earlier `805a84b` ("Unlocking door works") verified in place on core 1 and never used
the core0→core1 direction.

This predicts a failure obvious enough that you would likely have noticed it, so
**test this first**. If the keypad does work on hardware, the premise is wrong
somewhere and C2 becomes the live concern.

- [x] Verify on hardware whether a correct keypad code opens the door. Confirmed broken.
- [x] Stop using the inter-core FIFO for application messaging. Use a shared-memory
      mailbox (`volatile` slots + sequence numbers, or `queue_t` from `pico_util`), or
      a `hardware_spinlock` + `__dmb()` handshake. Keep
      `multicore_lockout_victim_init()` — `flash_safe_execute` requires it.

### C2 — A stale verdict opens the door for any digits typed at the keypad **[A]** [fixed]

Fixed together with C1: `shared/door_verify.h`'s mailbox carries a monotonically
increasing per-attempt sequence number that is never reused. `core1.c` only accepts a
response whose `response_seq` echoes the exact `request_seq` of the attempt currently
waiting, so a late/stale reply for an earlier attempt can never be mistaken for the
verdict on a later one. The old fixed-size raw-FIFO word framing (and its missing
`else`) no longer exists, so that desync class is gone structurally rather than patched.

`shared/fifo_protocol.h:12-13` makes the verdict a bare `0x00`/`0x01` — no sequence
number, no key-id echo, no message tag. `core1.c:83-87` gives up on timeout **without
draining**, and `core1.c:73-74` pops whatever word is present on the next attempt. So
a verdict computed for attempt *N* is accepted as the verdict for attempt *N+1*.

Exploitable entirely from outside. Core 0 must be blocked > 2 s, which a network
attacker arranges via the unbounded NTP wait at `main.c:45-49` (H4):

1. Block UDP/123 so core 0 sits in `boot_network()` and never runs
   `core0_handle_fifo()`.
2. A legitimate user enters a correct code. Core 1 times out and beeps failure, but
   the request stays queued.
3. NTP succeeds (attacker stops blocking, or it recovers naturally). Core 0 drains the
   queue, evaluates the *legitimate* request, and pushes `FIFO_RESULT_GRANTED`.
4. The attacker enters **any** 7-9 digits + `#`. Core 1 pops the stale GRANTED and
   opens the latch.

The 43 s storage-failure alarm (`main.c:106-111`) and a 60 s `import-keys` give the
same window without needing network control.

Second desync source: `main.c:61` has no `else`, so a word whose tag is not
`FIFO_MSG_VERIFY` consumes word 1, orphans word 2, and shifts the stream by one word
permanently.

Currently masked by C1 — which is why these must be fixed **together**. Fixing verdict
delivery alone converts this into a live bypass.

- [x] Carry a nonce in the request; require the reply to echo it (32-bit `request_seq` /
      `response_seq`, not a separate message tag - see `shared/door_verify.h`).
- [x] Stale replies are structurally void: each attempt gets a new sequence number, so a
      reply matching an old one is never mistaken for the current attempt's verdict.
- [x] No FIFO framing remains to resynchronise - moot after the mailbox rewrite.

### C3 — Clock rewind replays an old code at the keypad **[A+B]** [verified]

`network/ntp.c:49-51`:

```c
if (!synced)
    return true; // no floor before first sync
```

`synced`, `last_sync_unix` and `last_sync_monotonic_us` (`ntp.c:41-43`) are RAM-only
statics, nothing is persisted, and the RP2040 RTC is not battery-backed — so every
power cycle resets the floor completely.

An attacker observes a valid code `C` at time `T` (camera on the keypad, a keypad-ribbon
tap, or a code shared over another channel). Any power interruption reboots the lock;
`boot_network()` immediately syncs with `synced == false`, and the attacker answers with
`T`. `rollback_check` waves it through with no checks, the RTC is set to `T`, and `C` is
valid again at the keypad. Repeatable indefinitely; `boot_network` retries every 5 s, so
the attacker's window is the entire boot. No console access at any step: capture outside,
spoof on the network, enter the code outside.

**This is now load-bearing.** Since in-window replay protection is an accepted risk, the
persisted floor is the only thing stopping a code captured hours or days earlier from
working — and by then the original user is long gone, so the "someone would notice"
argument does not cover it.

Scoped honestly: this needs an on-path / same-LAN / DHCP-DNS-control position.
`ntp.c:124-129` checks a real 64-bit `get_rand_64()` nonce echoed in the origin-timestamp
field, plus mode/stratum/LI, so blind off-path *injection* is genuinely blocked — unless
M9 (predictable DNS) is left unfixed, which removes that requirement.

- [ ] Persist a "maximum time ever observed" watermark to littlefs (coarse, e.g. hourly,
      to limit flash wear) and enforce it as an absolute floor **including on the first
      sync after boot**.

---

## HIGH

### H1 — Keypad brute force: no lockout, no delay, no audit trail **[A]** [ignored]

**Ignored:** the attacker does not have the time window to brute-force the PIN even
without a rate limit — decided against the cost numbers below, not because they're
wrong. Revisit if the buzzer is ever made non-blocking (see the ~18.5h estimate below)
or if M12's retained-prefix behaviour is fixed in a way that widens the practical
search window.

`core1.c:43-90` has no failure counter, lockout, or escalating delay, and nothing is
persisted. Costed from the code's own blocking sleeps: ~25 ms per digit + 200 ms beep,
7 digits ≈ 1.58 s, plus ~2.0 s `buzzer_play_fail()` ≈ **3.6 s per attempt ≈ 16.6/min**.

That is 25 attempts per 90 s window = 0.0075 % of the code space; expected ≈ 333,333
attempts ≈ **13.9 days** continuous → **≈6.9 % per 24 h, 39 % per week, 87 % per 30
days**.

The 6-digit space is not exhaustible in hours, but the *only* thing preventing it is
2.2 s of incidental blocking buzzer delay. Make the buzzer non-blocking (a natural PWM
refactor) and the rate rises to ~5/s → **expected break-in ≈ 18.5 hours.** Because the
keypad is outside, an injector spliced onto the ribbon runs this unattended with no
lockout, alarm, or audit trail — and M12's retained-prefix behaviour cuts the search
space further.

Not planned (see **[ignored]** rationale above). If revisited:

- [ ] Add a failure counter: cooldown after ~5 consecutive failures, doubling to a
      ~15 min cap.
- [ ] Persist the counter plus a monotonic timestamp to flash so a power cycle cannot
      clear it.
- [ ] Rate-limit to one attempt per TOTP step per key id.
- [ ] Keep an explicit delay if the buzzer is made non-blocking — it is currently
      load-bearing.

### H2 — Spoofed NTP timestamp of epoch 0 denies entry to everyone **[B]** [fixed]

Fixed: `network/ntp.c` now rejects any `unix_time` outside `[BUILD_UNIX_TIME,
BUILD_UNIX_TIME + NTP_SANE_MAX_AGE_S]` (20 years) unconditionally in `ntp_recv_cb`,
before `rollback_check` - so an epoch-0 (or any other absurd) timestamp never reaches
`apply_time`/the RTC, first sync or not. `clock_get_unix_time()` is now
`bool clock_get_unix_time(uint32_t *out)` (`hardware/clock.h`), so epoch 0 is a
legitimate time distinguishable from "RTC not set" at every call site
(`totp_verify`, `cmd_get_time`, `cmd_login`, `cmd_add_key`).

`hardware/clock.c:3-6` returns `0` when `rtc_get_datetime()` fails, and the arithmetic at
`clock.c:31` also returns exactly `0` for 1970-01-01 00:00:00 — the same value means both
"error" and a valid time. `ntp_recv_cb` applies the decoded timestamp with no sanity band
(`network/ntp.c:131-141`); the only gate is `rollback_check`, which returns `true`
outright before the first sync (C3).

An on-path attacker answers the first sync with `seconds_since_1900 = 2208988800`, giving
`unix_time == 0`. `totp_verify` then prints `[totp] RTC not set` and returns false for
**every** code (`shared/totp.c:43-47`) — the lock denies everyone at the keypad, while
`ntp_is_synced()` reports success. One datagram, no console access, everyone locked out.

Secondary escalation (position **C**): the same condition trips the credential-free admin
branch at `commands_system.c:152` (M1).

- [x] Reject `unix_time` outside a sane band in `ntp_recv_cb` (e.g. below the firmware
      build timestamp, or more than ~20 years after it).
- [x] Change `clock_get_unix_time()` to signal validity out-of-band
      (`bool clock_get_unix_time(uint32_t *out)`) so epoch 0 is not an error code.

### H3 — Unbounded forward clock jump permanently wedges the lock **[B]** [verified]

`network/ntp.c:131-136`. `rollback_check` only ever checks a *lower* bound; forward jumps
are entirely unvalidated, and `unix_time = seconds_since_1900 - NTP_DELTA` has no guard,
so any value below `NTP_DELTA` wraps to the far future.

One on-path packet carrying year 2100 sets `last_sync_unix ≈ 4.1e9`. From then on
`floor ≈ 4.1e9`, so every honest response (≈1.7e9) is rejected forever, the RTC sits
decades ahead, `totp_verify` never matches, and the lock denies all users until a power
cycle. One spoofed datagram = persistent denial of a physical door.

Related, same line: the 60 s `NTP_ROLLBACK_EPSILON_S` is applied *per sync* with no
cumulative budget, and the anchor is rewritten on every accepted response (`ntp.c:72-73`).
Returning exactly `floor` walks the clock 60 s further behind on each sync, unbounded.
`wifi.c:58` calls `ntp_sync()` on every reconnect, bypassing `NTP_RESYNC_INTERVAL_S`, and
WPA2 here has no management-frame protection — so deauth/re-associate cycles harvest 60 s
of rollback per ~30 s, feeding C3.

- [ ] Bound both directions before `rollback_check`: reject below build time or more than
      N years after it.
- [ ] Cap the accepted forward step per sync once `synced` (e.g. > 1 day).
- [ ] Guard the subtraction explicitly: `if (seconds_since_1900 < NTP_DELTA) reject;`
- [ ] Shrink the epsilon to well under one TOTP step, add a cumulative backward budget per
      boot, and compute `floor` in saturating arithmetic (it currently underflows to
      ~4.29e9 when `last_sync_unix + elapsed_s < 60`, freezing a bogus clock).
- [ ] Remove the unconditional `ntp_sync()` at `wifi.c:58`; let `ntp_task` schedule it.

### H4 — Boot blocks forever on NTP, and the watchdog cannot recover a hung core 0 **[B]** [verified]

`main.c:43-49` loops unbounded until the first NTP sync. `console_init()` and the service
loop are at `main.c:119-127`, i.e. *after* `boot_network()`. So while NTP is blocked:
`core0_handle_door_verify()` never runs (every keypad entry hits core 1's 2 s timeout — **door
dead**) and `console_task()` never runs, so there is no recovery path even from inside.

The watchdog does not help: `watchdog_enable(8000, true)` at `main.c:95`, but
`watchdog_update()` is called **only from core 1** (`core1.c:72,144`), unconditionally.
The RP2040 watchdog is system-wide, so core 1 happily feeds it while core 0 is wedged and
the device never resets out of the hang. It protects nothing it was presumably added to
protect.

The attacker picks the failure mode: blackhole UDP/123 or poison DNS for `pool.ntp.org` →
permanent DoS. Deny WiFi *association* instead → `main.c:35-39` early-returns,
`rtc_init()` never runs, and M1's credential-free admin opens up for anyone who then gets
inside.

Margin note on the legitimate grant path: last feed at `core1.c:72`, then
`buzzer_play_success()` 200 ms + `latch_open()` 5000 ms = **5.2 s** with no feed, only
2.8 s under the limit. Raising `LATCH_OPEN_DELAY` past ~7.5 s, or a `flash_safe_execute`
lockout landing in that window, resets the board mid-unlock.

- [ ] Bound the boot sync attempts, then enter a clearly-degraded state that keeps the
      console alive (deny door access) and let `ntp_task` retry.
- [ ] Feed the watchdog from core 0's loop; have core 1 prove liveness via a shared
      heartbeat counter core 0 checks before petting, so either core hanging causes a reset.
- [ ] Make `latch_open()` non-blocking (alarm callback to de-energise).

### H5 — Pressing keys during a core-0 block bricks the lock in a reboot loop **[A]** [verified]

`core1.c:66-67` uses `multicore_fifo_push_blocking` unbounded. The FIFO is 8 entries deep
per direction and each submission pushes 2 words, so 4 queued attempts fill it and the 5th
blocks — before reaching the `watchdog_update()` at `core1.c:72`, so the 8 s watchdog
resets the board.

Core 0's long blocking windows make this trivial to hit from outside: 43 s storage-failure
alarm (`main.c:106-111`), unbounded NTP retry (`main.c:45-49`), 60 s `import-keys` paste,
15 s `format-storage` confirm, 7 s `test`. On the storage-failure and NTP-failure paths the
condition recurs after reboot → **permanent reboot loop caused by nothing more than
pressing keys at the keypad.**

- [ ] Never block core 1 on a push: drop the request if a verdict is outstanding, or use
      `multicore_fifo_push_timeout_us()` and fail with a beep.
- [ ] Drain the FIFO from inside every long core-0 wait loop.

### H6 — Backups are neither encrypted nor authenticated; CRC-32 is not a MAC **[C → leaves the building]**

`storage/backup.h:17-31`, `storage/backup.c:12-16,127-131`. The blob is plaintext
`backup_key_t` records — `secret[20]` is the raw HMAC-SHA1 seed — protected only by a CRC
any attacker can recompute.

Ranked high despite needing console access to *create*, because a backup's whole purpose
is to exist off-device: on an operator's laptop, in a repo, in chat, in an email. Once it
leaves the building the inside-only constraint no longer protects it.

1. **Confidentiality.** `cmd_export_keys` prints every seed for every key
   (`commands_backup.c:22-24`). Anyone obtaining a blob derives valid codes for every key
   including admin keys, indefinitely, and enters from outside. No revocation signal exists.
2. **Integrity.** An attacker who gets a blob accepted sets `is_admin = 1` on a record
   whose `secret` they chose, fixes the CRC, and gains permanent admin. `to_key_record`
   copies `is_admin` straight through (`backup.c:37-54`) and `storage_key_save` writes a
   *fresh valid* checksum (`storage.c:71`), so the injected record is indistinguishable
   from a legitimate one. `backup_import` applies **no privilege check on `is_admin`** —
   import is the only way to set that bit without `set-key-admin`.

- [ ] Encrypt-then-MAC the payload (PBKDF2/HKDF + AES-GCM or ChaCha20-Poly1305 via the
      already-linked mbedTLS); reject on MAC failure before any parsing. Keep the CRC only
      as a paste-corruption hint.
- [ ] At minimum, refuse to import `is_admin = 1` records without per-key operator
      re-confirmation.

### H7 — Secrets are plaintext at rest and extractable over USB without any login **[C]** [verify-on-hw]

`storage/storage.c:45,70` writes `secret[20]` verbatim into littlefs; there is no
encryption anywhere (only an `lfs_crc` integrity checksum at `storage.c:56`). WiFi SSID and
password are likewise plaintext (`storage.h:35-38`). The RP2040 has no secure boot, no
flash encryption, no OTP key store, and no readback protection.

The enclosure raises the bar for a casual visitor, but not for a member who is inside
routinely. Ranked high because it is the cleanest **one-time inside access → permanent
building-wide compromise** path, and it needs no console login at all — only the port:

1. `pico_enable_stdio_usb(hslock 1)` (`CMakeLists.txt:122`) with no override of the SDK's
   reset-interface options anywhere in the build. `pico_stdio_usb` includes, by default,
   the ability to reset the chip over USB — so `stty -F /dev/ttyACM0 1200` or
   `picotool reboot -f -u` drops the device to BOOTSEL, then `picotool save -a dump.bin`
   yields the whole image including every seed and the WiFi password. Confirm against your
   SDK version; the exact default depends on it.
2. Hold BOOTSEL on power-up (or short the flash CS pin) — same result, no firmware
   cooperation at all.
3. SWD: SWCLK/SWDIO/GND pads are exposed → dump memory while running, or desolder the
   flash and read it with a clip.

- [ ] At minimum set `PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=0` and
      `PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0`.
- [ ] Encrypt secrets at rest under a device-bound KEK (`pico_get_unique_board_id()` +
      compile-time key, via mbedTLS AES-GCM). This only raises the bar to "read the
      firmware too" — treat flash extraction as a compromise event and design for key
      rotation.
- [ ] Add a tamper switch on the enclosure that wipes storage. If this threat model is
      central, RP2350 (OTP + secure boot + signed images) is the right part.

---

## MEDIUM

### M1 — `cmd_login` grants admin with no credentials on three branches **[C]** [verified]

`serial/commands_system.c:141-174`. Each branch sets `admin_mode = true` before any key
lookup or TOTP check, ignoring `argv[1]`/`argv[2]` entirely:

| line | condition | trigger |
|---|---|---|
| `:143` | no WiFi config stored | wins even when valid admin keys exist |
| `:152` | RTC unset and uptime ≥ 5 min (`BOOT_BYPASS_WINDOW_US`, `:23`) | inducible from **B** |
| `:168` | no enabled+valid admin key | reachable via M3/M4 |

The most *convenient* escalation in the codebase, and a network attacker can pre-position
its precondition so only brief inside access is needed. Tracing `main.c:35-39`: if WiFi
credentials exist but `wifi_connect()` fails, `boot_network()` returns early, so
`ntp_init()` — the only caller of `rtc_init()` — never runs and `clock_get_unix_time()`
returns 0 (`hardware/clock.c:5-6`). The console still comes up. So: make the AP
unreachable, power-cycle, wait 5 minutes, type `login 0 0` → full admin. The handler even
prints a countdown telling the attacker when to retry (`:159-161`). Cash out with
`get-key-secret` or `export-keys`. Not documented in `docs/COMMANDS.md`.

- [ ] Never grant admin from absent time or absent config. Fail closed.
- [ ] Reorder so credential verification wins whenever any enabled+valid admin key exists;
      restrict open mode to a genuinely unprovisioned device.
- [ ] Persist last-known-good unix time to flash and restore at boot.
- [ ] Gate any remaining bootstrap path on physical proof-of-presence, and make it one-shot
      provisioning rather than a persistent fail-open state.

### M2 — `test` actuates the latch with no authentication **[C]** [verified]

`serial/commands.c:49` registers `test` with `requires_admin = false`;
`serial/commands_system.c:113` calls `latch_open()` — the same call the authenticated grant
path uses (`core1.c:77`).

From inside, the door can generally be opened by hand anyway, so this is not the bypass it
would be from outside. What remains is real though: any visitor with a USB cable, holding
no credentials, can admit an accomplice from outside with no audit trail.
`docs/COMMANDS.md:21` documents it as `user` access, so it is intentional-but-wrong.

- [ ] Set `requires_admin = true` for `test`, or split it into a non-actuating self-test
      (LED/light/buzzer) plus an admin-only `test-latch`.

### M3 — A 16-byte backup blob wipes all keys and drops the device into fail-open admin **[C]** [verified]

`storage/backup.c:113` bounds `key_count` only from *above*, so `key_count == 0` passes:
`expected` = 16, and `backup_checksum(keys, 0)` is `lfs_crc(0xFFFFFFFF, p, 0)` =
`0xFFFFFFFF`. A 24-base64-char blob (`magic=0x4C4C5348`, `version=1`, `key_count=0`,
`checksum=0xFFFFFFFF`) clears every gate. The delete loop at `:159-161` then runs
unconditionally and the write loop does nothing.

`cmd_login` subsequently finds `any_admin == false` and takes the bootstrap branch
(`commands_system.c:168-174`) → admin for any credentials. A one-time admin compromise
becomes a permanent, reboot-surviving, credential-free backdoor. Note `cmd_import_keys` has
**no `CONFIRM` prompt**, unlike `cmd_format_storage`, despite being equally destructive.

- [ ] Require `key_count >= 1`, and at least one enabled admin record in the blob, before
      touching storage.
- [ ] Add a `CONFIRM` gate to `import-keys`.

### M4 — `format-storage` is unauthenticated and chains to fail-open admin **[C]** [verified]

`serial/commands.c:68` registers it with `requires_admin = false`, and `commands.c:110`
allow-lists it while storage is unmounted. The handler's only guard is "is storage mounted"
(`commands_system.c:219-224`) — not identity.

The mount-failure precondition is the design intent (you cannot `login` when storage is
down, since `cmd_login` refuses at `:135`), so the unauthenticated entry point is
defensible. The fallout is not: after `format-storage` + `CONFIRM`, all keys and the WiFi
config are destroyed — locking out every legitimate keyholder — and the device reboots into
a state where `login 0 0` succeeds via **both** M1 branches. The attacker then runs
`add-key 0 me` + `set-key-admin 0` and owns the lock. The mount failure cannot be triggered
from the console, but it occurs naturally after flash wear.

- [ ] Require physical proof-of-presence for `format-storage`, or derive the confirm token
      from the board's unique ID rather than the fixed `CONFIRM`.
- [ ] Force a provisioning state after format in which the door cannot open until an admin
      key is enrolled.

### M5 — One-byte out-of-bounds write in the `import-keys` base64 decode **[C, admin]** [verified]

`serial/commands_backup.c:73-75` calls `base64_decode(b64_buf, b64_len, import_buf)`;
`libs/base64/base64.c:43-66` takes no output-capacity argument and writes `in_len/4*3`
bytes when no `=` padding is present.

The arithmetic lands exactly on the boundary. `sizeof(backup_key_t)` = 2+16+20+1+1+4 = 44
(packed), header 16, `BACKUP_MAX_KEYS` = 128, so `import_buf` = 16 + 128·44 = **5648**
bytes. `b64_buf` = `BASE64_ENCODED_LEN(5648)` = ((5648+2)/3)·4+1 = **7533**, so the guard
at `:55` admits `b64_len` up to **7532**, which is divisible by 4 and passes the
`in_len % 4` gate at `base64.c:43`. With no `=` in the final quad the decoder emits
7532/4·3 = **5649** bytes — one byte past `import_buf`, value attacker-controlled
(`(c << 6) | d`). `backup_import` then reads one byte past it too.

`base64.h:11` documents the correct requirement, and `BASE64_DECODED_LEN` is never used
anywhere in the tree — the caller sizes the destination by the *blob* size, violating the
library's own contract by exactly one byte. `import_buf`, `b64_buf` and `export_buf` are
adjacent statics, so the clobbered byte lands in another parse buffer or the next module's
BSS.

The fuzzers miss it because `test/Makefile:331-335` sets no `-max_len`, so libFuzzer's
4096-byte default cannot reach the ~7545 bytes required, and `fuzz_backup.c` calls
`backup_import` directly, never crossing the base64 layer.

- [ ] Add a capacity parameter:
      `int base64_decode(const char *in, size_t in_len, unsigned char *out, size_t out_cap)`,
      returning `-1` when output would exceed `out_cap`.
- [ ] Reject over-long input up front:
      `if (BASE64_DECODED_LEN(b64_len) > sizeof(import_buf)) reject;`
- [ ] Make `commands_backup.c:55` abort the import rather than silently dropping an
      over-long line.
- [ ] Add a `-max_len` to the console fuzzer so this length class is reachable.

### M6 — Import is non-atomic: everything is deleted before anything is written **[C + accidental]** [verified]

`storage/backup.c:159-170`. The delete loop completes before the write loop starts, and
`:166-169` returns mid-write on the first `storage_key_save` failure, leaving storage
partially wiped with no rollback — which then trips the same fail-open bootstrap path as M3.
Reachable **accidentally**: a power loss or flash error mid-import destroys the key database.

- [ ] Stage new records to temporary paths (or a `/keys.new` directory) and only unlink the
      old ones after every write succeeds.

### M7 — Serial `login` has no brute-force resistance **[C]** [verified]

`commands_system.c:118-200`: every failure arm is `printf` + `buzzer_play_auth_error()` +
`return`, with no counter, lockout, or escalating delay. The only throttle is the buzzer's
~1.2 s of blocking `sleep_ms` (`hardware/buzzer.c:26-31,50-53`) → ~48 attempts/min ≈
69,000/day. With the 3-step window, p ≈ 3×10⁻⁶ per guess → **≈21 % success per day, ≈80 %
within a week** against a known admin key id (and L2 leaks which ids are valid). Nothing
survives reboot. Lower than H1 only because it needs sustained inside presence.

- [ ] Share H1's failure counter and cooldown across both authentication paths.

### M8 — TOTP secrets come from a non-cryptographic PRNG **[C → crosses out]** [verify-on-hw]

`shared/random.c:4-13` fills 20 bytes from three `get_rand_64()` calls, used at
`commands_keys.c:182`. `get_rand_64()` is `pico_rand`; the RP2040 has no hardware TRNG, so
this is a 128-bit xoroshiro128** software PRNG seeded from ROSC sampling / timer / board ID
with light per-call mixing. It is not a CSPRNG and the SDK does not claim to be one.

The concrete concern: xoroshiro128**'s output function is invertible and its state is only
128 bits — smaller than the 160 bits consumed per secret. A holder of one
legitimately-issued secret (which `get-key-secret` prints, and which any member enrolling a
key receives) therefore learns ≥2 full consecutive outputs, enough to recover state and
both rewind and predict the stream **if** the per-call entropy injection is guessable. The
three draws happen in immediate succession inside one function, so the mixing between them
is minimal. A member who knows roughly when another key was enrolled could then derive that
key and enter from outside. Confirm the exact `pico_rand` behaviour for your SDK version —
that decides whether this is theoretical or practical.

- [ ] Condition the entropy: collect ≥64 draws spread over time plus
      `pico_get_unique_board_id()`, feed them through mbedTLS `mbedtls_ctr_drbg`/`hmac_drbg`
      (already linked), take 20 bytes from the DRBG.
- [ ] Give `generate_secret()` a length parameter — it hard-codes a 20-byte write with no
      bound from the caller.

### M9 — `LWIP_RAND` undefined → predictable DNS transaction IDs and source ports **[B]** [verified]

`lwipopts.h` never defines `LWIP_RAND`, and `srand()` appears nowhere in the tree. lwIP's
default is `((u32_t)rand())`, and newlib's `rand()` without `srand()` produces an identical
sequence every boot. `LWIP_DNS_SECURE` defaults to
`RAND_XID | NO_MULTIPLE_OUTSTANDING | RAND_SRC_PORT`, all drawing from `LWIP_RAND()` — so
the DNS transaction ID and source port for the Nth query after boot are predictable to
anyone with the same firmware. DHCP `xid` likewise.

Since `pool.ntp.org` is resolved by DNS (`ntp.c:206`) and the resolved address is the *only*
authentication of the NTP peer, an off-path DNS poison hands the attacker the clock and
therefore C3/H2/H3 — **removing the on-path requirement** from the whole NTP tier. This is
what erodes the otherwise-sound nonce check.

- [ ] `#define LWIP_RAND() ((u32_t)get_rand_32())` in `lwipopts.h` (`pico_rand` is already
      linked).
- [ ] Consider pinning NTP server IPs to remove DNS from the trust path.
- [ ] Bind the NTP pcb to a randomised local port rather than lwIP's sequential
      `udp_new_port()` allocator.

### M10 — lwIP called without `cyw43_arch_lwip_begin/end`; callback state races **[B]** [verified]

The target links `pico_cyw43_arch_lwip_threadsafe_background` (`CMakeLists.txt:118`), where
callbacks arrive from a low-priority IRQ and every thread-context lwIP call must be wrapped.
Only the DNS call is (`ntp.c:205-207`). Unwrapped: `udp_remove` (`ntp.c:192,214,228,235`),
`udp_new_ip_type`/`udp_recv` (`:196,202`), and the whole `dns_found_cb` body on the
DNS-cache-hit path (`:159-177`, reached from `:211` after `cyw43_arch_lwip_end()`).
`udp_remove` unlinks the pcb from lwIP's global list — if the background IRQ is inside
`udp_input()` walking that list, the result is list corruption or use-after-free, and the
attacker chooses when datagrams arrive.

Same root cause: `ntp_state` (`ntp.c:36`) is written from IRQ and spun on in thread context
without `volatile`; `ntp_recv_cb` calls `rtc_set_datetime` from IRQ concurrently with core
0's `rtc_get_datetime` inside `totp_verify` (an attacker-timed torn read denies a valid
code); `printf` is called from IRQ on all seven paths while core 0 may hold the stdio lock;
and `cyw43_arch_poll()` (`ntp.c:222`) is a no-op in this arch — the wait loop was written
for the poll variant.

- [ ] Either switch to `pico_cyw43_arch_lwip_poll`, or wrap every lwIP call in
      `cyw43_arch_lwip_begin/end`, mark shared statics `volatile`, and reduce `ntp_recv_cb`
      to "copy 48 bytes + set a flag" with validation, `printf` and the RTC write done from
      the main loop.

### M11 — One spoofed datagram aborts an entire NTP sync **[B]** [verified]

All seven rejection paths in `ntp_recv_cb` (`ntp.c:85-129`) set
`ntp_state = NTP_STATE_FAILED` and return, so `ntp_sync`'s loop exits and tears down the
pcb, discarding the still-in-flight legitimate response. The attacker must source from
`server_addr:123` (trivial on-path/same-LAN). Chained with H4 this is a permanent remote
brick from a low-rate spoof.

- [ ] On validation failure, `return` **without** touching `ntp_state` — drop the datagram
      and keep waiting until the deadline.

### M12 — Partial keypad entry never times out, and silent overflow is an oracle **[A]** [verified]

`input_clear()` runs only on `*` (`core1.c:112`) or via `process_input` — there is no idle
timer, so abandoned digits persist for hours and the next person's keystrokes concatenate
onto them. An attacker who observes a user typing their id and first digits, then interrupts
them, returns and completes the buffer — each retained digit removes a factor of 10 from
H1's search space. Because `core1.c:130-134` caps the buffer *silently* (no beep at 9
digits), pressing a key and hearing no beep reveals that the buffer is full.

- [ ] Clear the buffer after ~10 s of inactivity.
- [ ] Beep distinctly on overflow so the full state is not an oracle.

### M13 — Deleted secrets remain recoverable in the littlefs log **[C]** [verified]

`storage_key_delete` (`storage.c:329`) only calls `lfs_remove`, which appends a delete tag.
With `cache_size = 256` / `block_size = 4096` and `inline_max` unset, a 48-byte record is
stored **inline in the `/keys` metadata block pair**, an append-only log. The 20 secret
bytes stay readable until that pair is compacted *and* the stale block erased. Worse, every
`rename-key` / `enable-key` / `disable-key` / `set-key-admin` uses `LFS_O_TRUNC` + rewrite,
appending a **new** inline copy while the old one (same secret) remains. So **revoking a key
does not actually revoke it** against anyone who later dumps flash (H7). `storage_format` is
the only real erase, and it is refused while storage mounts.

- [ ] Overwrite the record in place with zeros and `lfs_file_sync` before `lfs_remove`, so
      the plaintext is no longer the newest value.
- [ ] Document that only `storage_format` guarantees erasure.

### M14 — Stale DNS callback clobbers live NTP state **[B]** [verified]

On DNS timeout `ntp_sync` returns (`ntp.c:224-229`) leaving `dns_found_cb` registered with
no generation token. When the reply arrives — timing controlled by whoever answers DNS — the
callback fires during a *later* sync and overwrites `server_addr` (`:156`),
re-`udp_connect`s the live pcb to an attacker-chosen address (`:159`), regenerates
`ntp_nonce` (`:172-174`) and resets `ntp_state = NTP_STATE_WAITING`. The in-flight
legitimate response then fails the origin check and the sync dies.

- [ ] Carry a request-generation counter through the callback argument and drop stale
      callbacks; ignore datagrams when `ntp_state != NTP_STATE_WAITING`.

### M15 — `import-keys` dumps every secret to the console twice, before reading input **[C]** [verified]

`serial/commands_backup.c:32` and `:84` both call `cmd_export_keys`. Merely *typing*
`import-keys` — even if the operator then aborts, times out, or supplies invalid base64 —
emits the full plaintext key database twice. With H6 (no encryption) this maximises exposure
in scrollback and session logs, which are exactly what leaves the building.

- [ ] Delete the call at `:32`; emit the safety backup once, immediately before the
      destructive write, and only after the blob validates.

### M16 — `get-key` prints the raw seed while its own help says "no secret" **[C]** [verified]

`serial/commands.c:57` describes `get-key` as "Show key details (no secret)", but
`commands_keys.c:80-84` hex-dumps all 20 bytes. That description is what `help` shows the
operator (`commands.c:89`), and `docs/COMMANDS.md:40` contradicts the in-firmware text —
confirming the mismatch is unintentional. An operator who trusts it pastes `get-key` output
into a ticket or chat and leaks a working credential out of the building. Unlike
`get-key-secret`, it also prints the secret when `is_checksum_valid` is false.

- [ ] Remove the secret dump from `cmd_get_key` (it already exists in `get-key-secret`).

### M17 — Admin session has no idle timeout **[C]** [verified]

`admin_mode` is cleared only by explicit `logout` (`commands_system.c:202-206`) and USB
disconnect (`commands.c:20-25` via `console.c:66-71`). There is no time-based expiry. On a
permanently attached host — common for this device — `stdio_usb_connected()` stays true and
one login persists for weeks; anyone who later reaches that host or cable inherits admin
without needing M1 at all.

- [ ] Record a deadline at login, clear `admin_mode` after ~5 min of inactivity, refresh on
      each successful admin command.

---

## LOW

### L1 — Unauthenticated `[door]` prints leak key enumeration and holder names **[C · privacy]** [verified]

`main.c:69-77` prints, per keypad press, whether a key id exists, is corrupt, or is
disabled, and the holder's name on every grant — to the USB console with no admin check and
no `stdio_usb_connected()` gate. That is a live occupancy/attendance log for anyone inside
with a cable, plus a way to enumerate valid ids before mounting H1/M7. `storage.c`'s
`[storage] key %u checksum mismatch` leaks the same way. (No code or secret is printed here
— the earlier `code=%s` print was correctly removed in `7eb99ed`.)

- [ ] Compile these behind a `HSLOCK_DEBUG` flag, off in release.
- [ ] Log grant/deny to an in-flash audit ring readable only in admin mode.

### L2 — Response-timing oracle distinguishes valid admin key ids **[C]** [verified]

`commands_system.c:176-195`. The message text is identical on all arms (good), but the
`printf` precedes the constant 1.2 s `buzzer_play_auth_error()`, so the delay masks nothing
— the attacker times when the line arrives. A non-existent id returns after a littlefs
lookup failure; an enabled admin id returns only after up to 3 HMAC-SHA1 computations. The
code comparison itself is a `uint32_t ==` (`totp.c:53`), so there is no byte-wise leak. The
delta is small over USB CDC, but it collapses M7's 128-id search to a single id, and the fix
is cheap.

- [ ] Run `totp_verify` against a fixed dummy secret on every failure path so all arms do
      identical work, then emit the identical message and delay.

### L3 — Pre-login `status` discloses configuration and inventory **[C]** [verified]

`commands.c:48` (`requires_admin = false`) with `commands_system.c:25-75` gives any
unauthenticated console user the git hash / build date / dirty flag, the unique board ID
(`:35-41`), the configured WiFi SSID (`:46`), NTP state, and key counts (`:72`). No secrets,
and less useful than it first appears (the SSID is scannable from outside anyway), but it is
target-selection data for M1's bootstrap path.

- [ ] Move board ID and key counts behind `requires_admin`.

### L4 — `set-wifi` writes 128 bytes of partly uninitialised stack to flash [verified]

`commands_network.c:21` declares `wifi_config_t cfg;` uninitialised. The `strncpy`s and
explicit terminators are correct, but bytes between each string's NUL and the end of its
64-byte field are never initialised, and `storage_wifi_set` writes all
`sizeof(wifi_config_t)` = 128 bytes (`storage.c:258`). Stack residue — plausibly fragments
of a `key_record_t` from a preceding `get-key`/`login` on the same core-0 stack — is
persisted to flash and recoverable via H7.

- [ ] `wifi_config_t cfg = {0};` and zeroise after use.

### L5 — Plaintext secrets linger in static BSS after every command [verified]

`backup.c:61,157`, `commands_backup.c:11,37,73`, `commands_keys.c:15`,
`commands_system.c:63,123`. Every `list-keys`, `status`, `login`, `export-keys` and
`import-keys` leaves the whole key database — seeds included — resident in BSS indefinitely,
amplifying any disclosure primitive (including M5) and any crash dump. `core1.c:60-63`
deliberately zeroes the keypad buffer, so the intent exists but is not applied to the
storage layer.

- [ ] Wipe these buffers at the end of each handler through a `volatile` pointer so the
      `memset` is not optimised away; same for the `key_record_t` locals in the
      `storage.c` / `backup.c` conversion helpers.

### L6 — base64 decoder accepts non-canonical padding [verified]

`libs/base64/base64.c:38,56-57`. `=` is accepted anywhere, including the first two positions
of any quad and in non-final quads (`"AA==AAAA"` → 4 bytes; `"AA=A"` → 2 bytes, skipping
byte 2 but emitting byte 3), and the final symbol's unused bits are never required to be
zero. Many distinct strings map to the same blob. No memory-safety impact by itself (the
`% 4` gate at `:43` prevents OOB reads), but it matters once H6 adds a MAC computed over
decoded bytes.

- [ ] Accept `=` only in positions 2-3 of the final quad, require the trailing bits to be
      zero, reject it elsewhere.

### L7 — `storage_key_save` does not bound `key->id` [verified]

`storage.c:306-322` accepts any `uint16_t`. Ids above `KEY_ID_MAX` (127) would be persisted
and appear in `storage_key_list`, `backup_export` and `cmd_login`'s `any_admin` scan while
being unmanageable by `get-key`/`delete-key`/`unset-key-admin` — an unremovable admin key.
**No reachable path today**: all nine `commands_keys.c` handlers gate on `id > KEY_ID_MAX`
and `backup_import:149-154` validates every record id. Worth closing because this one
missing check is all that stands between a future caller and an unrevocable credential.

- [ ] `if (key->id > KEY_ID_MAX) return false;` at the top of `storage_key_save`.

### L8 — `storage_wifi_get` does not enforce NUL termination on flash contents [verified]

`storage.c:235-246` reads 128 bytes and uses them directly as C strings via
`printf("%s", wifi.ssid)` (`commands_system.c:46`) and
`cyw43_arch_wifi_connect_timeout_ms` (`wifi.c:21`). Not reachable today — the only writer
zero-pads both fields — but the read path trusts flash unconditionally, so bit-rot or a
future writer turns it into an over-read of the password into the SSID print.

- [ ] Force `ssid[WIFI_SSID_MAX-1] = '\0'` and `password[WIFI_PASSWORD_MAX-1] = '\0'` before
      returning.

### L9 — 60 s blocking paste loop starves the keypad **[C causes A-side outage]** [verified]

`commands_backup.c:43-65` spins for up to 60 s on core 0, the only servicer of
`core0_handle_door_verify` — so every keypad unlock attempt fails for that whole window (core 1
gives up after 2 s). `cmd_format_storage` has the same shape with 15 s. Admin-gated and
self-clearing, but see H5 for the reboot-loop variant.

- [ ] Drop the timeout to ~10 s, or pump `core0_handle_door_verify()` inside the loop.

### L10 — Parallel `COMMANDS`/`HANDLERS` arrays with a dead `handler` field [verified]

`commands.c:31-39` declares a `handler` function-pointer member, but all 23 initialisers
supply only 6 fields, so it is silently `NULL` and never called; dispatch indexes a separate
`HANDLERS[]` by the same index (`:138`). Both arrays currently have exactly 23 entries, so
there is **no live bug**. But nothing ties them together, so adding a command without adding
its handler yields an out-of-bounds function-pointer read and an indirect call through it —
with the admin and arg-count checks taken from the wrong row.

- [ ] Delete the dead `handler` member and add
      `_Static_assert(sizeof(HANDLERS)/sizeof(HANDLERS[0]) == NUM_COMMANDS, "table/handler mismatch");`

### L11 — `lwipopts.h` exposes unused remote surface and ships debug/stats in release **[B]** [verified]

- `LWIP_TCP 1` (`:48`) and `LWIP_RAW 1` (`:31`): the firmware uses only UDP/DNS/DHCP, so the
  entire lwIP TCP input path is dead remote attack surface plus ~10 KB on a door lock.
  `LWIP_TCP_KEEPALIVE` (`:51`) likewise.
- `LWIP_ICMP 1` (`:30`): the lock answers pings, aiding LAN fingerprinting.
- `CMakeLists.txt` never sets `CMAKE_BUILD_TYPE` and `build.sh` runs plain
  `cmake -DPICO_BOARD=pico_w ..`, so `NDEBUG` is **not** defined and
  `LWIP_DEBUG`/`LWIP_STATS`/`LWIP_STATS_DISPLAY` (`:56-60`) compile into shipped firmware.
  All per-module flags are `LWIP_DBG_OFF` so nothing prints today, but the machinery is
  resident and one flag flip leaks network state.
- `MEM_SIZE 4000` (`:23`) versus `TCP_SND_BUF`/`TCP_WND` at 8·1460 = 11680 (`:32-34`) and
  `MEMP_NUM_TCP_SEG 32` — allocations the heap can never satisfy.
- `DHCP_DOES_ARP_CHECK 0` / `LWIP_DHCP_DOES_ACD_CHECK 0` (`:53-54`) disable address-conflict
  detection, so a LAN attacker can force an IP conflict and knock the device off the network
  (feeds H4).

- [ ] `LWIP_TCP 0`, `LWIP_RAW 0`, `LWIP_TCP_KEEPALIVE 0`, `LWIP_ICMP 0`.
- [ ] Set `CMAKE_BUILD_TYPE=Release` (or `-DNDEBUG`) for shipped builds.
- [ ] Re-derive `MEM_SIZE` from actual UDP/DNS/DHCP needs.

### L12 — Latch fail-safe behaviour is undocumented [verified]

`latch.c:6-7`: `gpio_init()` drives the pin low before `gpio_set_dir(OUT)`, so GPIO 16 is
de-energised on reboot — correct for a **fail-secure** strike. But a **fail-safe** strike or
maglock on the same pin would *unlock* on every watchdog reset and throughout H5's reboot
loop.

- [ ] Add an explicit `gpio_put(LATCH_PIN, false)` in `latch_init()` and document the
      required strike type.

---

## Checked and found clean

Recorded so nobody re-audits these.

- **Keypad input buffer** (`core1.c`): `input_len` is hard-bounded at `INPUT_MAX_LEN` (9)
  before every write into `input_buf[10]`; `id_len = input_len - 6` ∈ [1,3], so `strncpy`
  into `id_str[4]` / `code_str[7]` stays in bounds with the NUL preserved by the `{0}`
  initialisers; `atoi` inputs are at most `"999"`/`"999999"` so no integer overflow. Holding
  a key produces exactly one event (`keypad.c:73-76`). **This is the one parser an outside
  attacker reaches directly, and it is sound.**
- **`ntp_recv_cb` buffer handling**: `ntp.c:85` correctly guards on `p->tot_len` (not
  `p->len`) and `:93` uses `pbuf_copy_partial`, which handles chained pbufs and clamps. Every
  read — `buf[0]`, `buf[1]`, `buf[24..31]`, `buf[40..43]` — is inside the 48-byte stack
  array. `pbuf_free` precedes all use of the copy. **The other remotely-reachable parser,
  also sound.**
- **NTP peer validation is genuinely sound against blind off-path attackers**:
  `ntp.c:172-174` writes a 64-bit `get_rand_64()` nonce into the transmit timestamp and
  `:125` requires the server to echo it, plus `udp_connect` + `ip_addr_cmp` source filtering
  and `mode == 4`, `LI != 3`, `1 <= stratum <= 15` checks. C3/H2/H3 need an
  on-path/same-LAN/DNS-control position — unless M9 is left unfixed, which removes that
  requirement.
- **Console tokeniser and line buffer** (`console.c:24-45,92-93`): `argc < MAX_ARGS` bounds
  `argv[8]`, excess tokens are dropped not overflowed, and `input_len < INPUT_BUF_SIZE - 1`
  caps at 255 so `input_buf[255] = '\0'` is in bounds. No command has `max_args > 2`, so
  truncation cannot smuggle args past the arg-count check.
- **`backup_import` bounds**: `key_count` is capped *before* the `expected` size arithmetic
  and the checksum call, so no integer overflow and no OOB read; truncation is rejected;
  names are verified NUL-terminated and ids range-checked before storage is touched. (The
  missing *lower* bound is M3.)
- **No uninitialised-memory disclosure to flash**: `key_record_stored_t` has no padding at
  any of its 7 field offsets, `backup_key_t` is packed with all six fields assigned, and
  `backup_export` returns a length covering only what it wrote. (`set-wifi` is the exception
  — L4.)
- **`base64_encode` / `base32_encode` sizing** is exact for all input residues; 20 bytes → 32
  chars + NUL = `SECRET_B32_LEN`; `uri[200]` is `snprintf`-bounded.
- **Format strings**: every first-party `printf` uses a literal format; attacker data only
  ever reaches `%s`/`%u`. `commands_keys.c:144`'s `printf(dark ? "██" : "  ")` is non-literal
  but both operands are constants with no conversion specifiers.
- **`totp_at` dynamic truncation** (`totp.c:32-35`): `offset ∈ [0,15]`, max index 18 of
  `hmac[20]`; `POW10[6]` is in range for a 7-element array.
- **`mbedtls_config.h`**: enables only MD + SHA1 + SHA256, no TLS, no ciphersuites. HMAC-SHA1
  is the RFC 6238 standard construction and is *not* broken by SHA-1 collision attacks —
  flagging it would be a false positive.
- **`clock.c` conversion arithmetic** round-trips correctly with bounded loops and no
  `days_in_month` over-index. Its only defect is the epoch-0 collision (H2).
- **WiFi credentials are never printed**: `wifi.c:20` logs only the SSID, no command reads the
  password back, `set-wifi` echoes nothing, and both fields are length-checked and explicitly
  terminated. `wifi_connect` hardcodes `CYW43_AUTH_WPA2_AES_PSK` with no open/WEP fallback.
- **Admin state hygiene**: cleared on USB disconnect (`console.c:66-71`) and does not survive
  reboot. `(uint16_t)strtoul` truncation in `cmd_login` grants nothing, since the id must
  still resolve to an enabled admin key and pass TOTP.
- **`format-storage` confirm buffer** (`commands_system.c:230-249`): `char confirm[10]`,
  `len < 7`, `strncmp(..., 7)` — in bounds.

---

## Suggested order of work

Ordered by "can someone outside the building use this", then by cost.

1. **C1** — verify on hardware first. If the keypad is dead, nothing else about the lock
   matters, and the fix is entangled with C2.
2. **C2** — fix *together with* C1. Restoring verdict delivery without request correlation
   turns a masked bug into a live outside bypass.
3. **C3** — the persisted time floor. Now the sole control against out-of-window replay.
4. ~~**H1** — the keypad lockout counter.~~ **Ignored** — see H1 entry.
5. **H2, H3, M9, M11** — the rest of the NTP trust model, as one change: sanity band, forward
   cap, drop-don't-fail, seeded `LWIP_RAND`. M9 belongs here because leaving it unfixed means
   the others don't need an on-path position.
6. **H4, H5** — availability and watchdog correctness; both are remotely or
   keypad-triggerable bricks.
7. **M1, M2, M4** — the credential-free admin cluster. Cheap fixes that close the escalation
   making brief inside access so valuable.
8. **H6, H7, M13** — secret handling at rest and in backups; this is what turns one visit
   inside into permanent building-wide compromise.
9. **M3, M5, M6** — import-path memory safety and fail-open.
10. Remaining M and L items.

## Not verifiable in this environment

The ARM toolchain, Pico SDK (`PICO_SDK_PATH` is unset) and host test dependencies are absent
here, so nothing was executed: no firmware build, no `make -C test asan`/`valgrind`/
`coverage`, no fuzzing. Every finding comes from reading the code. The **[verify-on-hw]**
items (C1, H7, M8) depend on SDK or silicon behaviour that should be confirmed on a device.
