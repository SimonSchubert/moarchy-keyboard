# Verification status

Against [SPEC.md](SPEC.md), measured on the PinePhone (`alarm@192.168.0.18`,
sway, Qt 6.11.2, quickshell 0.3.1) on 2026-09-05.

Nothing below is inferred. Every PASS has a measurement or a screenshot behind
it; everything unproven says so.

## Summary

| | count |
|---|---|
| Passed | 31 |
| Failed | 2 |
| Partial | 3 |
| Not yet verified | 9 |

45 criteria: SPEC.md's 44 plus AC 4b, added when testing showed that "the
surface stays mapped when retracted" and "the retracted keyboard does not
swallow every touch" are two different claims and only one of them was written
down.

The keyboard works. It raises and retracts itself, types into both kinds of
Wayland client, recolours live, and keeps exactly one layer surface. Both
failures are targets I set without evidence — 25 MB of PSS and 800 ms to first
paint — and both are corrected against measurements below rather than defended.

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
| 30 | **Two fingers at once** | A second finger landing while the first was still held produced two emissions, not one — the case that reads as dropped letters when typing at speed |
| 4b | **A retracted keyboard is not an invisible wall** | Touches pass through to the app when down and are blocked when up. Both directions, because one proves half |
| 11 | **Keycode path with no input method at all** | A QML probe binding no text input quit on a synthesised `q` — a real key event to a client that cannot receive `commit_string` |
| 13 | Terminal correctness | Ctrl+C killed `cat`; Escape produced `^[` and Tab a real tab |
| 14 | Backspace deletes one character | `hi` → `h` |
| 16 | **Password fields suppress previews** | With `content_purpose = password` focused, every long-press hint is gone — the same layout shows `1`–`0` and `@#$%&*-+=` on a normal field |
| 25 | **WCAG AA in every theme** | All **22** Omarchy themes pass, worst 4.64:1. Swept offline with `--check-themes`; reproducible via `scripts/fetch-themes.sh` |
| 7 | Refuses the seat rather than fighting for it | A second instance exited **1** with `another input method already holds this seat; exiting so two keyboards do not fight over it` |
| 19 | User layout overrides the shipped one | A `letters.json` in `~/.config/moarchy-keyboard/layouts/` was the one loaded, per the log |
| 41 | Dependencies available | The same `makepkg` run validated the `depends` array against an Arch ARM image holding only `qt6-base`, `qt6-declarative`, `qt6-wayland`, `layer-shell-qt`, `wayland`, `libxkbcommon` |

## Failed

**AC 36 — cold start ≤ 800 ms. Measured 1247–1281 ms, consistently.**

Timed from inside the process against one stopwatch, ending at
`QQuickWindow::frameSwapped` — actual first paint, not "we asked for one":

| phase | at |
|---|---|
| QGuiApplication | 20 ms |
| Wayland globals bound | 23 ms |
| layouts loaded | 24 ms |
| keymap compiled and uploaded | 57 ms |
| input method bound | 57 ms |
| theme parsed | 59 ms |
| **panel prepared** (QQuickView + LayerShellQt) | **465 ms** |
| **QML loaded and surface mapped** | **944 ms** |
| **FIRST FRAME** | **1267 ms** |

Everything this project wrote costs 59 ms. The remaining 1.2 s is Qt Quick:
~400 ms to build the view, ~480 ms to load and instantiate the QML, ~320 ms to
render the first frame. There is no obvious 500 ms to find in that, and 800 ms
was a number picked without knowing it. The AC should be restated against a
measurement, not defended.

**AC 35 — incremental PSS ≤ 25 MB. Measured 75.8 MB. The target was the wrong
metric and the wrong number, and the truth is more interesting than either.**

Both programs measured the same way: started fresh, shown over `sm.puri.OSK0`,
with a text field focused, then left to settle.

| | Pss | **Private_Dirty** | Rss |
|---|---|---|---|
| squeekboard 1.43.1 | 59 781 kB | **44 756 kB** | 80 384 kB |
| moarchy-keyboard, defaults | 80 624 kB | **44 280 kB** | 135 980 kB |
| moarchy-keyboard, tuned | 75 826 kB | **39 672 kB** | 130 568 kB |

So: **on private dirty pages — what adding the process actually costs a device
with 197 MB free — the keyboard is 5.1 MB better than squeekboard, about 11 %.**
On PSS it is 16 MB worse, because PSS charges it half of every Qt library page
it shares with the running quickshell — pages already resident, which stay
resident when it exits. On RSS it is much worse, because RSS counts those shared
pages in full.

AC 35 as written fails and should be rewritten against `Private_Dirty` with a
target derived from this table rather than from optimism.

Two corrections to earlier numbers on this page, both mine:

- The first squeekboard figure (37 MB PSS) was a squeekboard that had never been
  shown — its cold and warm samples were byte-identical, which should have been
  the clue. Comparing a drawn Qt keyboard against an undrawn GTK one made this
  project look twice as bad as it is.
- The 33 MB discrepancy between two of this keyboard's own samples is resolved,
  and the gestures session's hypothesis was right. Hidden is 64–70 MB PSS, shown
  is 76–81 MB, and the count of other Qt clients moves the shared share on top of
  that. Both readings were correct measurements of different states.

The tuning is now compiled in: `QSG_RENDER_LOOP=basic` (nothing here animates),
`QSG_TRANSIENT_IMAGES=1` (there are no images at all), `QV4_FORCE_INTERPRETER=1`
(the JavaScript is a hit test and a few branches). Worth 4.6 MB of both PSS and
private dirty, and overridable from the environment.

## Partial

- **AC 2 (20 cycles)** — the claim is proven and has never once wavered: **1
  layer surface created and 0 destroyed, in every run.** But the driver reaches
  3–6 activate/deactivate cycles rather than 20, because the windows it needs
  take longer to open than any fixed wait on a device this loaded. The counts are
  unambiguous, so more cycles would restate rather than strengthen it — but the
  AC says 20 and it has not seen 20.
- **AC 20 (the current layout is visible without typing)** — switching is one
  tap. "Visible" is satisfied only by convention: the layout keys show where you
  can go, not where you are, exactly as every phone keyboard does. Honest to call
  that partial rather than met.
- **AC 29 (no dead zones)** — hit areas tessellate by construction, and taps land
  on the intended key including on the centred nine-key rows that the clamping
  exists for. Not swept across the whole panel.

## Not yet verified

AC 6 (survives compositor restart), 31 (slide-off cancels), 32 (long-press
alternates), 33 (press feedback within one frame — first attempt gave 11/14/18 ms
against a 17 ms bar, so it is borderline rather than unknown), 34 (modifier
latching), 38 (press → commit — the first attempt measured 90 ms and 90 ms was
exactly the synthetic hold the test used, because emission happens on release;
now measured from release and not yet re-run), and 42–44 (mobileomarchy integration, deliberately
untouched — three other sessions were editing that repo today; the change is
written up in `packaging/mobileomarchy-integration.md` for review).

31–34 are the touch-interaction criteria. `tests/tap.py` now has the gestures
they need — a second finger landing while the first is held, and dragging between
points — and `tests/text-probe.qml` gives them an ordinary text field to type
into rather than a terminal, which is what made the first attempt measure the
wrong keys. Nothing about them is known to be broken; they are simply not
finished, and this page does not count untested as passing.

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

## The harness is the flakiest part of this, and that is worth saying

Across seven full runs, every single "product failure" this suite reported turned
out to be its own bug, with two exceptions that were real and are now fixed (the
`Top` layer and the sticky terminal layout). The list: testing squeekboard
instead of the keyboard; terminal-layout coordinates at the letters layout;
cycling focus onto a window that run never created; a probe reporting through a
file that `XMLHttpRequest` never wrote; a probe made fullscreen, which in sway
renders above the layer this keyboard was on; matching windows by an `app_id`
that Qt never sets; and four separate fixed sleeps that were long enough once and
not the next time.

The pattern is one thing: **a test that does not assert its own preconditions
will eventually measure something other than what it names.** The guards added in
response — assert which process owns `sm.puri.OSK0` before believing a
keystroke, poll for windows instead of sleeping, fail below five activates, and
distinguish "the probe never opened" from "the probe saw nothing" — are worth
more than the results they produced.

## The device ran out of battery mid-run

Recorded because it is the last thing that happened and it was avoidable. The
phone was handed over at 6% on the charger; I observed 5%, watched it for 90
seconds, saw it had not moved, and started a short run anyway on the reasoning
that charging meant rising. It did not rise — the panel and compositor were
drawing more than the cable supplied — and the device died partway through the
touch section.

`Charging` is a direction, not a headroom figure, and a capacity that has not
moved in 90 seconds is the measurement, not noise. A claim on shared hardware
needs power headroom as well as availability.

The AC 30 result below survives, because it was measured in the run before this
one. AC 31, 32, 33 and 38 do not: they were re-pointed at an ordinary text field
after the first attempt measured them against a terminal, and that re-run is the
one that died.

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
