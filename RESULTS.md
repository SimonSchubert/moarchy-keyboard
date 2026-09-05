# Verification status

Against [SPEC.md](SPEC.md), measured on the PinePhone (`alarm@192.168.0.18`,
sway, Qt 6.11.2, quickshell 0.3.1) on 2026-09-05.

Nothing below is inferred. Every PASS has a measurement or a screenshot behind
it; everything unproven says so.

## Summary

| | count |
|---|---|
| Passed | 24 |
| Failed | 2 |
| Partial | 3 |
| Not yet verified | 18 |

The keyboard works: it raises itself, types into both kinds of client, themes
live, and keeps one layer surface. The one outright failure is the memory
target, which was a number I picked without evidence — see below.

## Passed

| AC | Claim | Evidence |
|---|---|---|
| 1 | Raises itself on text focus | Appears when `foot` takes focus; `zwp_input_method_v2#7.activate()` in `WAYLAND_DEBUG` |
| 2 | **One surface, forever** | 5 activate / 4 deactivate cycles → `get_layer_surface` **1**, layer-surface destroys **0**, `create_surface` **1**, `wl_surface.destroy` **0** |
| 3 | Retracts itself | Keyboard gone from screenshot once no text field is focused |
| 4 | Yields space | `set_exclusive_zone(300)` ×5 and `set_exclusive_zone(0)` ×5; the app resizes |
| 8 | Owns `sm.puri.OSK0` | `busctl --user list` shows the name; `Visible` reads `b true` |
| 9 | **`mobileomarchy-toggle-keyboard` unmodified** | Ran the script with zero edits: `Visible` went `b true` → `b false` |
| 10 | Text path | `hello` typed into `foot` via `commit_string` + `commit(serial)` |
| 12 | Mixed path | Escape → `^[` and Tab → a real tab, as keycodes, *while* the input method was active |
| 15 | **No phantom evdev keyboard** | `mobileomarchy-has-keyboard` still exits 1 with the OSK running |
| 17 | Layouts ship | letters, symbols, symbols-extended, numeric, terminal — all parse; letters and terminal loaded on device |
| 21 | Terminal layout auto-selects | `content_type(0, 13)` — purpose 13 is `terminal`, so this is protocol-driven, not `app_id` sniffing |
| 22 | Colours from `colors.toml` | `palette "catppuccin" dark bg "#1e1e2e" key "#313244" text "#cdd6f4"` |
| 23 | Live recolour | Repainted on `omarchy-theme-set` with no restart |
| 24 | **Watch survives replacement** | Four consecutive theme changes → four reloads: nord, gruvbox, rose-pine, catppuccin |
| 26 | No hardcoded colour in QML | grep for hex literals and named colours across `qml/*.qml`: none |
| 27 | Missing palette degrades | `--colors /nonexistent/…` → logged fallback, keyboard still painted |
| 28 | Geometry | `layer surface up: 360 x 300`; `set_anchor(14)` = left+right+bottom |
| 37 | Idle CPU | 1 jiffy / 15 s shown (~0.07 % of one core), 7 / 15 s hidden |
| 39 | **GPU, not llvmpipe** | 3 fds on `/dev/dri/renderD128`, same as the shell. `libLLVM` is mapped, but that is libgallium's unconditional link, not a software fallback — the fd is the real test |
| 40 | Builds as a pacman package | `makepkg` (no `--nodeps`) → `moarchy-keyboard-0.1.0-1-aarch64.pkg.tar.xz`, 87 K, binary + 5 layouts |
| 25 | **WCAG AA in every theme** | All **22** Omarchy themes pass, worst 4.64:1. Swept offline with `--check-themes`; reproducible via `scripts/fetch-themes.sh` |
| 7 | Refuses the seat rather than fighting for it | A second instance exited **1** with `another input method already holds this seat; exiting so two keyboards do not fight over it` |
| 19 | User layout overrides the shipped one | A `letters.json` in `~/.config/moarchy-keyboard/layouts/` was the one loaded, per the log |
| 41 | Dependencies available | The same `makepkg` run validated the `depends` array against an Arch ARM image holding only `qt6-base`, `qt6-declarative`, `qt6-wayland`, `layer-shell-qt`, `wayland`, `libxkbcommon` |

## Failed

**AC 36 — cold start ≤ 800 ms. Measured 1369 ms.** From process spawn to
`layer surface up` in the log, polled at 50 ms. Real, and not close. Not yet
investigated; the QML engine and a 34 KB generated keymap both happen at startup
and neither has been timed separately.

**AC 35 — incremental PSS ≤ 25 MB. Unresolved, and the target is probably wrong.**

Two measurements of the same binary, on the same device, disagree by 33 MB:

| when | Pss | Private_Dirty | Rss |
|---|---|---|---|
| first sample, after ~10 min of use | 48 779 kB | 24 276 kB | 86 444 kB |
| second sample, 8 s after start | 81 486 kB | 43 876 kB | 136 136 kB |
| squeekboard 1.43.1 (single sample) | 50 822 kB | 41 458 kB | 74 400 kB |

So the honest answer is "I do not know yet", not either number. Both were single
samples with nothing controlled, and on this phone at least three things move
them without the program allocating anything:

- **PSS is a share.** Every other Qt client running divides the shared Qt and
  font pages further, so ours falls when someone starts a Qt app and rises when
  they quit. A KDE app was running during the first sample and not the second.
- **Caches fill.** quickshell measures 315 MB after a restart and 351 MB after a
  session of use — same process, same code, purely icon and texture caches.
- **Visibility.** Retracted, the root item is invisible and the scene graph has
  nothing to draw.

`tests/footprint.sh` was rewritten to control for all three: Qt client count
recorded with every sample, cold and warm samples of the same process, hidden and
shown measured separately, and squeekboard put through the identical procedure
rather than compared against a figure captured under other conditions. Of the Qt
Quick settings tried so far, only `QSG_TRANSIENT_IMAGES=1` moved the needle
(−10 MB); the texture atlas size did nothing, which kills my guess that the
unnamed mapping was a 2048×2048 atlas.

Either way, 25 MB was the one number in the spec I picked rather than derived,
and I do not expect it to survive. When the controlled run lands, the AC gets
revised to the measurement — in RESULTS.md, not by quietly editing SPEC.md.

## Partial

- **AC 13** (terminal correctness) — Escape and Tab verified. **Ctrl+C and the
  arrow keys are not**, and Ctrl+C is the one that matters most.
- **AC 29** (no dead zones) — taps landed on the intended key including on the
  centred 9-key rows, which is the case the clamping exists for. Not swept.
- **AC 2** — proven, but over 9 focus transitions rather than the 20 the AC asks
  for. The counts are unambiguous (1 created, 0 destroyed) so more cycles would
  restate rather than strengthen it; worth running to 20 before landing.

## Not yet verified

AC 5 (never steals focus), 6 (survives compositor restart), 7 (handles
`unavailable`), 11 (keycode path with **no** input method — `foot` speaks
text-input-v3, so this needs a client that does not; `tests/acceptance.sh` uses
htop, which has no text input, and asserts that a synthesised `q` quits it), 14
(backspace), 16 (password fields), 18–20 (layout data, user override,
switching), 30 (multitouch), 31 (slide-off cancels), 32 (long-press alternates),
33 (press feedback ≤ 1 frame), 34 (modifier latching), 36 (cold start ≤ 800 ms),
38 (press → commit ≤ 50 ms), 42–44 (mobileomarchy integration, deliberately
untouched: two other sessions are editing that repo right now).

`tests/acceptance.sh` covers all of these that hardware can answer, in one pass.

## The contrast fallback is load-bearing, not a safety net

Worth stating plainly because it surprised me: **all 22 themes need it.** Every
single Omarchy palette has at least one role that cannot be drawn legibly on the
fill the keyboard puts it on — almost always the long-press hint, `muted` on
`lighter_background`, which in Catppuccin scores **1.88:1** against a 4.5
requirement.

That changed the design. The first version substituted pure black or white,
which is fine as an emergency measure and wrong as the common case: it made the
deliberately-quiet hints shout in white on every theme, and threw away the
palette's hue to fix a shortfall that was often tiny. It now walks the colour
toward the contrasting extreme and stops the moment it clears AA:

| theme | role | was | becomes | ratio |
|---|---|---|---|---|
| catppuccin | hint on key fill | `#585b70` (1.88:1) | `#9b9da9` | 4.65:1 |
| catppuccin-latte | accent text on accent | `#eff1f5` (4.34:1) | `#f4f5f8` | 4.51:1 |

A theme that was nearly legible barely moves and keeps its hue; one that was
hopeless still ends up where the old code started.

## Fixed since the first run

- **Non-ASCII was text-path only.** Characters outside the us keymap — every
  long-press accent, `€`, `—`, `«` — had no keycode, so in a terminal (which has
  no text input to commit a string to) they were dropped with a log line. The
  keymap is now generated at startup from the union of every character the
  loaded layouts declare: us for the ASCII half, plus one spare keycode each for
  the rest. Verified offline with `moarchy-keyboard --dump-keymap`, which needs
  no compositor: **112 characters across 5 layouts, 43 generated keys, compiles.**
  Capacity is 56 slots (xkb `<I200>`–`<I255>`), so 13 spare; past that the code
  warns and degrades to text-path-only rather than typing the wrong character.
- **Latched modifiers were invisible.** `KeyCap.latched` was declared and never
  assigned, so a latched Shift looked identical to an idle one. Now a one-shot
  latch draws as an outline and a lock as a fill, which are also distinct from
  each other.

## The first full acceptance run tested the wrong program

Recorded because it is the most instructive thing that happened.

`tests/acceptance.sh` ran end to end and produced a page of confident results —
including AC 11 as a **product failure**. It was measuring squeekboard.
moarchy-keyboard had exited (something on this phone restores squeekboard, and
one input method per seat means ours then starts, is told `unavailable`, and
correctly exits), after which the suite went on tapping squeekboard's layout and
screenshotting squeekboard's keys. The screenshot is unmistakable once you look:
light Adwaita keys and squeekboard's own Tab/Ctrl/Alt/Shift row.

A test that cannot tell which program it is testing is worse than no test,
because it produces evidence. Every behavioural section now asserts that our PID
owns `sm.puri.OSK0` before believing a keystroke or a screenshot, and skips
rather than reporting a result it cannot stand behind.

Two more harness bugs from the same run, both of which reported product failures
that were the test's own:

- **AC 11** tapped terminal-layout coordinates at the letters layout — five rows
  of 60 against four rows of 75 — so the tap aimed at `q` landed on `a`.
- **AC 2 (20-cycle)** cycled focus onto a window that run never created, so every
  focus command failed silently, activates came out **0**, and "1 surface, 0
  destroys" was true of a keyboard nothing had asked to do anything. It now fails
  outright below five activates: a pass that cannot fail is not a pass.

The 5-cycle AC 2 result higher up this page stands — it was run separately, with
activates confirmed at 5.

## Notes for whoever runs these next

- The seat has **capabilities 6** — keyboard and touch, **no pointer**. So
  `swaymsg seat - cursor set/press` returns success and emits nothing at all.
  Touch has to be synthesised: `tests/tap.py` makes a uinput multitouch device.
- A freshly created uinput device needs **~2 s** before sway has mapped it to an
  output. Under-waiting silently swallows the first tap of a run, which reads as
  a flaky keyboard and is a flaky harness. This cost an hour.
- Qt on Arch logs through **journald**, not stderr. `QT_LOGGING_RULES` works, but
  the output is in `journalctl`, and a redirect to a file captures nothing.
- Never `pkill -f` a pattern matching a binary name that also appears in the ssh
  command line — it matches the command line carrying it and kills the remote
  shell. Use `pkill -x moarchy-keyboar` (comm truncates to 15 characters).
