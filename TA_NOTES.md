# TA Notes — OS Assignment 1 (Easy)

Internal working notes for preparing the reference solution and the student
release. **Not for distribution** — this file lives outside `xv6-public/` on
purpose so it can never be swept into a submission tarball.

Source: `OS_A1_easy.pdf` (LaTeX, generated 14 Feb 2023, 6 pages, 40 marks).

---

## 1. Assignment structure

| Part | Topic | Marks |
|------|-------|-------|
| 1 | Install and test xv6 | 3 |
| 2 | System calls — trace/toggle/print_count, add, ps | 7 |
| 3 | IPC — unicast + multicast | 10 |
| 4 | Distributed sum + variance across 8 processes | 10 |
| 5 | Report (max 10 pages) | 10 |
| | **Total** | **40** |

Marks sum correctly.

## 2. Platform — get this right before release

The assignment targets **xv6-public (x86)**, *not* xv6-riscv. The PDF link
resolves to `https://pdos.csail.mit.edu/6.828/2018/xv6.html`. Confirmed by
`make qemu-nox`, `date.h`, `dot-bochsrc`, `.gdbinit.tmpl`, and the flat root
directory with no `kernel/` `user/` split.

A student who clones `mit-pdos/xv6-riscv` will find that **none** of the
Makefile instructions in the PDF apply. Pin this in the first Piazza post.

**There is no `xv6-rev11` tag.** Repo tags stop at `xv6-rev9`. "Version 11"
refers to the book/OS revision matching the 2018 6.828 release — plain
`master` (commit `eeb7b41`) is correct. Students will hunt for a tag and file
Piazza questions; pre-empt it.

## 3. Environment — the prof's install guide is stale

The Drive install guide was written for Ubuntu ~18.04/20.04. On Ubuntu 22.04+
(verified on 26.04 / gcc 15.2) three of its four steps are wrong:

| Guide says | Correct on modern Ubuntu |
|---|---|
| `sudo apt-get install qemu` | Metapackage removed after 20.04 → `qemu-system-x86` |
| `cd xv6` | Clone produces `xv6-public/` |
| `make` | **Fails** — see below |
| `libc6-dev:i386` | Usually unnecessary; check `gcc -m32` works first |

### The two gcc-15 build failures

Both are false positives that xv6's `-Werror` turns fatal. They surface in
this order, and **the second one only appears at `make fs.img`, after the
kernel has already built successfully** — so it reads like an unrelated
problem. This is the single most likely source of setup-week Piazza traffic.

```
mp.c:83:10: error: array subscript -48806446 is outside array bounds
            of 'void[2147483647]' [-Werror=array-bounds=]

sh.c:127:7: note: recursive call
            [-Werror=infinite-recursion]
```

Fix — one line in the xv6 `Makefile`, after the `CFLAGS` block:

```make
CFLAGS += -Wno-array-bounds -Wno-infinite-recursion
```

`mp.c` scans fixed BIOS addresses; `sh.c`'s `runcmd()` never returns by design.
Neither warning indicates a real defect.

Harmless and expected: `ld: warning: _sh has a LOAD segment with RWX
permissions` — modern `ld` complaining about xv6's flat binaries.

`expect` is required by `check.sh` (`sudo apt-get install expect`).

## 4. Spec ambiguities — decide these before release

Each of these changes expected output and is **not** resolvable from the PDF.
They must be checked against `check_scripts.tar.gz` and then answered in the
handout so every student implements the same thing.

1. **Are `sys_toggle` / `sys_print_count` themselves counted?**
   Current reference solution: **no**. The spec's example output lists
   neither, and counting `print_count` while it executes is self-referential.
   Controlled by `#define EXCLUDE_TRACE_CONTROL 1` in `syscall.c`.

2. **Do counts survive `TRACE_ON → TRACE_OFF`?**
   Current: **yes** — counting stops, data persists, so `print_count` after
   toggling off still prints the last window. The alternative reading (print
   nothing when off) is equally supportable from the wording.

3. **Are zero-count syscalls printed?**
   Current: **no**. "system calls that have been invoked" implies non-zero
   only, but say it explicitly.

4. **Is tracing state global or per-process?**
   Current: **global**, per "two states within the kernel". This becomes
   observable in Part 4, which forks heavily.

5. **Is the coordinator one of the 8 processes?**
   "Limit the number of processes to 8… should run for 8 processes" does not
   say whether that is 1 coordinator + 7 workers, or 8 workers + 1 coordinator.
   The provided sample program should settle it — confirm and state it.

6. **`sys_send` blocking semantics.** Mailbox depth and behaviour when full
   are undefined. Current reference: 64-message circular buffer per process,
   `send` is non-blocking and returns -1 when full.

7. **Variance output format.** See §5 — this is the big one.

8. **`sys_send_multi` has no length parameter.** The given signature is
   `int sys_send_multi(int sender_pid, int rec_pids[], void *msg)` — nothing
   tells the kernel how many pids the array holds. Every student will invent a
   convention (sentinel? fixed 8? global constant?), and the check script will
   only accept one. Current reference: walked with `fetchint()` until a
   non-positive entry or a cap of 16, so callers must terminate the array.
   **Publish whichever convention the graders use.**

9. **Input file format for Part 4.** Assumed whitespace-separated decimal
   integers. Confirm against the supplied `arr`/sample program.

## 5. The floating-point problem in Part 4

**xv6 has no floating point.** The kernel does not save FPU/SSE state across
context switches, and xv6's `printf` supports only `%d %x %p %s %c` — there is
no `%f`.

Part 4 asks for the **mean** (used as the multicast payload in phase 1) and the
**variance**. With 1000 elements valued 0–9 the sum is ≤ 9000 and the mean is
~4.5 — *not* an integer. So students must use integer or fixed-point
arithmetic, and their answer has to match the hidden `check.sh` expectation
exactly.

Whatever convention the check script uses (truncated integer mean? scaled
fixed-point? variance × 1000?) **must** be published in the handout, or marks
will turn on guesswork. Resolve this from the check scripts before release.

### What the reference solution does

Fixed-point with `SCALE = 100`. The mean travels as `(sum * 100) / n`; squared
deviations come out scaled by `SCALE²`, so the variance arrives pre-multiplied
by 10⁴ and is printed as a zero-padded 4-place decimal.

Overflow is safe: the largest deviation is `9*100 = 900`, so `d*d ≤ 810000`
and 1000 elements total at most `8.1e8`, inside a signed `int`. Students who
reach for `SCALE = 1000` **will overflow** — `(9000-4453)² × 1000 ≈ 2.1e10`.
Worth a warning in the handout, since the failure is silent and produces a
plausible-looking wrong number.

On the bundled test array: sum `4453`, true variance `8.601791`, reported
`8.6018`.

### Also worth noting

The natural student instinct is to compute the mean as an integer (`4`) and
carry on. That is not *wrong* per the text, but it changes the variance
materially (8.80 vs 8.60). Another reason the convention must be stated.

## 6. Defects in the PDF to correct

- **p.6 — `_assig1_N` vs `assign1_8.c`.** The Makefile snippet lists
  `_assig1_1 … _assig1_8` (missing the `n`), but the file to submit is
  `assign1_8.c`. As printed these do not match and the build fails. Verify
  against the real tarball and fix the PDF.
- **p.5 — "submit only the `assign1_8.c` file"** contradicts the submission
  steps, which tar the entire tree. Reword to "the only source file *you
  write* for Part 4 is `assign1_8.c`".
- The `fs.img` rule change adds a file literally named **`arr`** to the image.
  It must exist in the root directory or `make` fails. Say so.

## 7. Student-facing gotchas (Piazza post material)

- The two `-Wno-` flags above, with the note that the `sh.c` one appears late.
- xv6-public, not xv6-riscv. No `rev11` tag; use `master`.
- **`atoi()` in `ulib.c` does not handle negative numbers.** It stops at the
  first non-digit, so `"-5"` silently parses as `0`. Anyone writing a CLI test
  program for `sys_add` will see wrong sums and blame their syscall. Parse the
  sign in your own program rather than patching `ulib.c` — the graders' test
  programs link against the stock version.
- Adding a syscall to xv6-public touches **six** files: `syscall.h`, `usys.S`,
  `user.h`, `syscall.c` (extern + dispatch table), the implementation file,
  and `Makefile`. Missing any one gives a confusing failure.
- Use `make qemu-nox`, not `make qemu` — no X needed, and it is what `check.sh`
  drives. Quit with `Ctrl-A` then `x`.

## 8. Recommended work order

Parts 3 and 4 interleave rather than running in sequence. Multicast is the
riskiest piece in the assignment; unicast should be fully validated first.

1. Environment + boot (Part 1)
2. Part 2 syscalls — teaches the six-file pattern everything else needs
3. Part 3 **unicast only**
4. Part 4 **unicast sum** — proves IPC works under 8 processes
5. Part 3 **multicast**
6. Part 4 **variance** phase
7. Run `check.sh`
8. Report, written from notes kept along the way

Rough student effort: ~25–30 hours. Two weeks is reasonable; one is not.

## 9. Note on multicast

`sys_send_multi` is specified to use a software interrupt (`sys_msg_mcast`)
invoking a user-space signal handler. **xv6 has no signal mechanism** — doing
this properly means adding a trap vector and rewriting the target's trapframe
`eip` to divert into a handler and back. That is a substantial piece of work.

The *Note* at the end of §3 explicitly permits building multicast on top of
`sys_recv` instead, so realistically almost everyone takes that path. Decide up
front whether the signal-handler route counts as "extra" under Part 5's *"say
what you have done that is extra"*, and say so in the handout.

## 10. Outstanding

- [ ] **Obtain `check_scripts.tar.gz`** — Drive id `1F5RYOUnyfr6RbBSkyQ3w9Zhc6tGj5HKe`.
      This is the grading contract and settles §4 items 1, 2, 5 and all of §5.
      Blocking for finalising Part 4.
- [ ] Verify all four Drive links still resolve and are shared "anyone with
      link" — they are from 2023.
- [ ] Obtain the "sample program" referenced in Part 4.
- [ ] Decide whether the `-Wno-` Makefile patch ships to students or is
      documented in the Piazza post only.

## 11. Reference solution status

| Part | Status |
|------|--------|
| 1 — Install & test | Done, boots and verified |
| 2 — System calls | Done, all four verified in xv6 |
| 3 — Unicast IPC | Done, round-trip verified |
| 3 — Multicast IPC | Done via `sys_recv` (the permitted alternative) |
| 4 — Distributed sum | Done, sum verified against host computation |
| 4 — Variance phase | Done, two-phase, verified |
| 5 — Report | Not started |

All implementation is complete; ~30/40 marks' worth is built and verified.
Remaining work is the report, plus reconciling §4 and §5 against the check
scripts.

See `xv6-public/README.md` for implementation detail.
