# OS Assignment 1 (Easy) — xv6 Reference Solution

Implementation of Assignment 1 on top of **xv6-public** (x86), the MIT 6.828
2018 release. Base commit `eeb7b41`.

> The stock xv6 `README` (no extension) is upstream's and is left untouched —
> `mkfs` bakes it into the filesystem image.

## What's implemented

| Part | Feature | Where | Status |
|------|---------|-------|--------|
| 1 | xv6 builds and boots | `Makefile` | Done |
| 2 | `sys_toggle` — flip syscall tracing on/off | `syscall.c` | Done |
| 2 | `sys_print_count` — per-syscall counts, sorted | `syscall.c` | Done |
| 2 | `sys_add` — sum of two integers | `sysproc.c` | Done |
| 2 | `sys_ps` — list running processes | `proc.c`, `sysproc.c` | Done |
| 3 | `sys_send` / `sys_recv` — unicast IPC, blocking receive | `proc.c`, `sysproc.c` | Done |
| 3 | `sys_send_multi` — multicast IPC | `sysproc.c` | Done |
| 4 | Distributed sum, 8 processes (unicast) | `assign1_8.c` | Done |
| 4 | Two-phase variance (multicast) | `assign1_8.c` | Done |
| 5 | Report | — | Not started |

Six new system calls (numbers 22–28) and six new user programs. Full detail per
part below.

## Requirements

```bash
sudo apt-get install -y qemu-system-x86 expect
```

- **`qemu-system-x86`** — runs xv6. On Ubuntu ≤ 20.04 this package was called
  `qemu`; that metapackage no longer exists.
- **`expect`** — only needed for scripted testing and the grader's `check.sh`.
  Not required to run xv6 by hand.

`gcc -m32` must work. If it doesn't: `sudo apt-get install gcc-multilib`.

## Running

From this directory:

```bash
make qemu-nox
```

That single command compiles the kernel, builds the filesystem image, and boots
xv6 in your terminal — `qemu-nox` depends on both `xv6.img` and `fs.img`, so
there is no need to run `make` separately.

**Quit with `Ctrl-A` then `x`** — press `Ctrl-A`, release, then press `x`.

Wait for the prompt before typing:

```
init: starting sh
$
```

Characters sent before that line appears are swallowed by the boot sequence.

Type `ls` at the `$` prompt to see every available program.

### Other build targets

```bash
make              # kernel + xv6.img only
make fs.img       # filesystem image with the user programs
make clean        # remove all build output
```

After editing any source file, just re-run `make qemu-nox` — it rebuilds what
changed. If a build looks stale or produces a confusing link error:

```bash
make clean && make qemu-nox
```

Avoid plain `make qemu`: it opens a separate graphical window and needs X.
`qemu-nox` runs in the terminal and is what `check.sh` drives.

### Building from a fresh clone

```bash
sudo apt-get install -y qemu-system-x86 expect
git clone https://github.com/Hobbbit31/OS.git
cd OS/xv6-public
make qemu-nox
```

Build artifacts are not committed, so the first build compiles everything.

### Modern-gcc patch

gcc ≥ 12 raises two false-positive warnings that xv6's `-Werror` makes fatal.
The `Makefile` adds:

```make
CFLAGS += -Wno-array-bounds -Wno-infinite-recursion
```

- `-Wno-array-bounds` — `mp.c` scans fixed BIOS addresses.
- `-Wno-infinite-recursion` — `sh.c`'s `runcmd()` never returns by design.

Without these the kernel build dies in `mp.c`, and the userland build dies
later in `sh.c` during `make fs.img`.

`ld: warning: ... has a LOAD segment with RWX permissions` is expected and
harmless.

## System calls added

| # | Call | Signature | Part |
|---|------|-----------|------|
| 22 | `toggle` | `int toggle(void)` | 2 |
| 23 | `print_count` | `int print_count(void)` | 2 |
| 24 | `add` | `int add(int, int)` | 2 |
| 25 | `ps` | `int ps(void)` | 2 |
| 26 | `send` | `int send(int sender_pid, int rec_pid, void *msg)` | 3 |
| 27 | `recv` | `int recv(void *msg)` | 3 |
| 28 | `send_multi` | `int send_multi(int sender_pid, int rec_pids[], void *msg)` | 3 |

Adding a syscall to xv6-public touches six places: `syscall.h` (number),
`usys.S` (user stub), `user.h` (prototype), `syscall.c` (extern + dispatch
table entry), the implementation, and `Makefile` (user program).

## Part 2 — System call tracing

Tracing state is **kernel-global** (the spec says "two states within the
kernel"), held as statics in `syscall.c`:

- `toggle()` flips `TRACE_OFF` ⇄ `TRACE_ON`. Entering `TRACE_ON` **zeroes the
  counters**, so a count always covers "since the last transition to
  `TRACE_ON`".
- Counting happens in `syscall()` **before dispatch**, so a call that turns
  tracing off is still attributed to the window it ran in.
- `print_count()` emits `<name> <count>`, one space, alphabetically ascending,
  non-zero counts only, no header or trailing lines. Sorting is an O(n²)
  selection pass over the 25-entry name table — trivial at this size and it
  avoids allocating scratch space in the kernel.

`syscall_names[]` in `syscall.c` must stay index-aligned with `syscalls[]`.

### Documented assumptions

Two behaviours are not determinable from the assignment PDF:

1. **`sys_toggle` and `sys_print_count` are not counted.** The spec's example
   output lists neither. Controlled by `#define EXCLUDE_TRACE_CONTROL 1` in
   `syscall.c` — set to `0` to count them.
2. **Counts persist after toggling off.** `print_count` following a
   `TRACE_ON → TRACE_OFF` transition prints the last window rather than
   nothing.

## Part 3 — IPC

### Unicast

Every process carries a fixed-size mailbox (`struct msgqueue` in `proc.h`):
a circular buffer of `MSGQMAX` (64) slots of `MSGSIZE` (8) bytes.

- `send()` — non-blocking. Looks the receiver up in `ptable`, copies 8 bytes,
  `wakeup1()`s it. Returns `-1` if the receiver does not exist or the queue is
  full. `ptable.lock` is held across lookup and copy so the receiver cannot
  exit underneath, and so `wakeup1` can be used instead of `wakeup` (which
  would try to take the lock again).
- `recv()` — blocking. Sleeps on the queue address until a message arrives.
  The sleep sits in a `while` loop because `wakeup1` wakes every sleeper on the
  channel. Returns `-1` if the process is killed while blocked.

Buffering is what lets a sender run before the receiver reaches `recv()`, and
it is what makes the 7-workers-to-1-coordinator fan-in of Part 4 work without
a rendezvous.

Mailboxes are reset in `allocproc()` — `ptable` slots are recycled, so a new
process must not inherit the previous occupant's queue.

`sender_pid` is redundant (the kernel knows `myproc()`) but the spec fixes the
signature, so it is accepted and passed through.

### Multicast

`send_multi()` delivers one message to every pid in `rec_pids[]`.

The spec describes doing this with a software interrupt (`sys_msg_mcast`)
driving a user-space signal handler. **xv6 has no signal mechanism** — that
route means adding a trap vector and rewriting the target's trapframe `eip` to
divert into a handler and back. The assignment's own closing note permits
building multicast on `sys_recv` instead, which is what this implementation
does: each receiver finds the message in its mailbox on its next `recv()`.

The given signature carries **no length** for `rec_pids[]`, so the array is
walked with `fetchint()` (which validates each word individually) until a
non-positive entry or `MAXRECV` (16), whichever comes first. **Callers must
terminate the array** — `assign1_8.c` writes a `0` sentinel after the last pid.

Delivery continues past a failure so one dead receiver cannot strand the rest;
the call returns `-1` if any single delivery failed.

## Part 4 — Distributed sum

```
assign1_8 <type> <input_file_name>     # type 0 = unicast, 1 = multicast
```

The array is loaded once by the parent before forking, so every worker
inherits its own copy and no worker touches the filesystem. The parent is the
coordinator; `NWORKERS` (8) children each take a contiguous slice. The
remainder goes to the last worker, so the partitioning stays correct if
`NWORKERS` stops dividing the element count.

Partial results travel as an 8-byte payload of `{worker id, value}` — packing
the id alongside the value uses the whole fixed-size message and lets the
coordinator tell partial results apart.

### Unicast (type 0)

Workers sum their slice and send it back. `recv()` blocks, so completion order
does not matter; the mailbox buffers whatever arrives early.

### Multicast (type 1) — two phases

1. Workers unicast partial sums; the coordinator computes the mean.
2. The coordinator **multicasts** the mean. Workers unblock, compute the sum of
   squared deviations over their slice, and unicast those back. The coordinator
   totals them and prints the variance.

Workers stay alive between phases, blocked in `recv()`. They need not already
be blocked when the multicast goes out — the mailbox buffers it, so there is no
rendezvous requirement and no lost-wakeup race.

### Fixed-point arithmetic

**xv6 has no floating point.** The kernel does not preserve FPU/SSE state
across context switches, and `printf` has no `%f`. The mean of 1000 elements
valued 0–9 is not an integer, so it travels as a fixed-point value scaled by
`SCALE` (100). Squared deviations are then scaled by `SCALE²`, which makes the
variance come out pre-multiplied by 10⁴; `print_variance()` renders that as a
zero-padded 4-place decimal.

Overflow check with `SCALE=100`: the largest deviation is `9*100 = 900`, so
`d*d ≤ 810000`, and 1000 elements sum to at most `8.1e8` — inside a signed
`int`.

Accuracy: for the bundled `arr`, this yields `8.6018` against a true variance
of `8.601791`.

### Undetermined details

Three things are **not** derivable from the assignment PDF, which defers them
to a sample program that was not supplied:

1. **Output format.** Isolated in `print_sum()` and `print_variance()` — change
   those two functions to match.
2. **Whether the coordinator is one of the 8 processes.** This build uses 8
   workers plus the parent as coordinator, which makes `1000 / 8 = 125` divide
   exactly. Change `NWORKERS` if the graders count the coordinator.
3. **Input file format.** Assumed to be whitespace-separated decimal integers.

## Test programs

All are run from the xv6 shell after `make qemu-nox`.

| Command | What it does | Exercises |
|---------|--------------|-----------|
| `user_toggle` | Toggles syscall tracing on/off | `sys_toggle` |
| `print_count` | Prints per-syscall counts for the current window | `sys_print_count` |
| `test_add <int> <int>` | Prints the sum of two integers | `sys_add` |
| `test_ps` | Lists live processes as `pid:<n> name:<name>` | `sys_ps` |
| `test_ipc` | Parent/child message round trip | `sys_send`, `sys_recv` |
| `assign1_8 0 arr` | Distributed sum, 8 workers, unicast | `sys_send`, `sys_recv` |
| `assign1_8 1 arr` | Sum + variance, two-phase | `sys_send_multi` too |

`user_toggle` and `print_count` are required by the spec, and `assign1_8` is
the file the assignment asks students to submit; the `test_*` programs are
local scaffolding.

`arr` is the Part 4 input: 1000 whitespace-separated values in 0–9, generated
deterministically so results are reproducible across rebuilds. It is baked
into the filesystem image by the `fs.img` rule, which the assignment requires
be changed to `./mkfs fs.img README arr $(UPROGS)`.

## How to test

Boot with `make qemu-nox`, wait for the `$` prompt, then run the commands
below. Every output shown here was captured from an actual run.

### Part 2 — `sys_add`

```
$ test_add 12 30
42
$ test_add -7 -8
-15
$ test_add 100 -1
99
```

### Part 2 — `sys_ps`

```
$ test_ps
pid:1 name:init
pid:2 name:sh
pid:5 name:test_ps
```

Only live processes appear. The pid varies with how many programs have run
before it — earlier commands consume pids.

### Part 2 — syscall tracing

```
$ user_toggle          # tracing ON, counters zeroed
$ echo hi
hi
$ print_count
sys_exec 2
sys_exit 2
sys_fork 2
sys_read 20
sys_sbrk 2
sys_wait 2
sys_write 7
$ user_toggle          # tracing OFF
```

Counts come from the shell forking and exec'ing `echo`, not from `echo` alone.
Names are alphabetical, `sys_` prefixed, non-zero only. Running `user_toggle`
twice restarts the window from zero.

### Part 3 — unicast IPC

```
$ test_ipc
child received: hello
parent received: 42
```

Parent sends a string to the child, child replies with an integer. The parent
sends *before* the child is guaranteed to have called `recv()`, so this
exercises mailbox buffering rather than a rendezvous.

### Part 4 — distributed sum and variance

```
$ assign1_8 0 arr
Sum: 4453
$ assign1_8 1 arr
Sum: 4453
Variance: 8.6018
```

Cross-checked against the same computation on the host: sum `4453`, mean
`4.453`, variance `8.601791`. The reported `8.6018` is the four-decimal
fixed-point rendering.

### Cross-checking the IPC layer with the tracer

The two features verify each other — run a distributed sum with tracing on:

```
$ user_toggle
$ assign1_8 0 arr
Sum: 4453
$ print_count
...
sys_recv 8
sys_send 8
...
```

Exactly 8 sends and 8 receives for one unicast run: 8 workers each reporting
once, the coordinator collecting 8. Any stray, lost, or duplicated message
would show up as a different count.

### Note on `atoi`

xv6's `ulib.c` `atoi()` stops at the first non-digit, so `"-5"` parses as `0`.
`test_add.c` parses the sign itself rather than patching `ulib.c`, since the
graders' own test programs link against the stock version.

## Automated testing

xv6 has no way to pipe a script into the shell, so tests are driven with
`expect` — the same mechanism the grader's `check.sh` uses.

Save as `run_tests.exp` and run with `expect run_tests.exp`:

```tcl
set timeout 150
spawn make qemu-nox

# Wait for the shell. Sending input before this line appears loses
# characters to the boot sequence.
expect "init: starting sh"
expect "$ "

send "test_add 12 30\r";    expect "$ "
send "test_add -7 -8\r";    expect "$ "
send "test_ps\r";           expect "$ "
send "test_ipc\r";          expect "$ "
send "assign1_8 0 arr\r";   expect "$ "
send "assign1_8 1 arr\r";   expect "$ "
send "user_toggle\r";       expect "$ "
send "assign1_8 0 arr\r";   expect "$ "
send "print_count\r";       expect "$ "

send "\x01x"                ;# Ctrl-A x quits QEMU
expect eof
```

Piping instead of using `expect` (`printf 'ls\n' | make qemu-nox`) mostly
works but races the boot: the first character is usually eaten before the
shell is listening.

## Verifying a clean checkout

To confirm nothing needed is missing from the repository:

```bash
git clone https://github.com/Hobbbit31/OS.git /tmp/verify
cd /tmp/verify/xv6-public
make qemu-nox
```

A fresh clone carries no build artifacts, so this compiles everything from
source. If any file were missing from the commit, the build would fail.

## Files modified from stock xv6

| File | Change |
|------|--------|
| `Makefile` | gcc warning suppressions; `UPROGS` and `EXTRA` entries; `arr` added to the `fs.img` rule |
| `syscall.h` | Numbers 22–28; `TRACE_ON` / `TRACE_OFF` |
| `syscall.c` | Externs, dispatch entries, name table, trace state, counting hook, `sys_toggle`, `sys_print_count` |
| `usys.S` | User stubs |
| `user.h` | Prototypes |
| `sysproc.c` | `sys_add`, `sys_ps`, `sys_send`, `sys_recv`, `sys_send_multi` |
| `proc.h` | `struct msgqueue`, `MSGSIZE`, `MSGQMAX`, `MAXRECV`, mailbox field |
| `proc.c` | Mailbox init in `allocproc`; `ps`, `send`, `recv` |
| `defs.h` | Kernel prototypes |

New files: `user_toggle.c`, `print_count.c`, `test_add.c`, `test_ps.c`,
`test_ipc.c`, `assign1_8.c`, `arr`.

## Status

| Part | Status |
|------|--------|
| 1 — Install & test | Done |
| 2 — System calls | Done |
| 3 — Unicast IPC | Done |
| 3 — Multicast IPC | Done (via `sys_recv`, the permitted alternative) |
| 4 — Distributed sum | Done |
| 4 — Variance phase | Done |
| 5 — Report | Not started |

Pending confirmation against `check_scripts.tar.gz`: output formats, worker
count, input file format, and the two Part 2 tracing assumptions above.
