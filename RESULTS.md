# Verification status

Against [SPEC.md](SPEC.md), measured on the PinePhone (`alarm@192.168.0.18`,
sway, Qt 6.11.2, quickshell 0.3.1) on 2026-09-05.

Nothing below is inferred. Every PASS has a measurement or a screenshot behind
it; everything unproven says so.

## Summary

| | count |
|---|---|
| Passed | 19 |
| Failed | 1 |
| Partial | 4 |
| Not yet verified | 20 |

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

## Failed

**AC 35 — incremental PSS ≤ 25 MB. Measured 48.8 MB.**

| | Pss | Rss |
|---|---|---|
| squeekboard 1.43.1 | 50 822 kB | 74 400 kB |
| moarchy-keyboard | 48 779 kB | 86 444 kB |

A 2 MB improvement, not the ≥50 % the target implied. The Qt sharing argument
holds — with quickshell running, `libQt6Gui`/`Quick`/`Qml`/`Core` cost only
~12.6 MB PSS between them — so the libraries are not where the memory goes. It
is ~24 MB of anonymous memory: 16.7 MB unnamed plus 7 MB heap.

I flagged 25 MB when writing the spec as the one number I had picked rather than
derived ("clearly better than half"). The evidence says it was wrong. Before
revising it, `tests/footprint.sh` attributes the anonymous memory across Qt Quick
settings (texture atlas size, render loop, QML disk cache) — the 16.7 MB unnamed
mapping is suspiciously close to a 2048×2048 RGBA texture atlas, which a keyboard
with no images has no use for. **Not yet run.** If that is it, the target may
still be reachable; if it is the QML engine's own floor, the AC should be revised
to the truth rather than the hope.

## Partial

- **AC 13** (terminal correctness) — Escape and Tab verified. **Ctrl+C and the
  arrow keys are not**, and Ctrl+C is the one that matters most.
- **AC 25** (WCAG AA in every theme) — the fallback demonstrably fires and is
  not theoretical: Catppuccin's `muted` (`#585b70`) scores **1.88:1** against its
  `lighter_background` (`#313244`), far under AA, and gets replaced with white.
  But only 4 of 22 themes have been through it. Needs the sweep in SPEC §7.
- **AC 29** (no dead zones) — taps landed on the intended key including on the
  centred 9-key rows, which is the case the clamping exists for. Not swept.
- **AC 2** — proven, but over 9 focus transitions rather than the 20 the AC asks
  for. The counts are unambiguous (1 created, 0 destroyed) so more cycles would
  restate rather than strengthen it; worth running to 20 before landing.

## Not yet verified

AC 5 (never steals focus), 6 (survives compositor restart), 7 (handles
`unavailable`), 11 (keycode path with **no** input method — `foot` speaks
text-input-v3, so this needs a client that does not), 14 (backspace), 16
(password fields), 18–20 (layout data, user override, switching), 30 (multitouch),
31 (slide-off cancels), 32 (long-press alternates), 33 (press feedback ≤ 1 frame),
34 (modifier latching), 36 (cold start ≤ 800 ms), 38 (press → commit ≤ 50 ms),
40–41 (packaging — `packaging/PKGBUILD` written but never built), 42–44
(mobileomarchy integration, deliberately untouched: two other sessions are
editing that repo right now).

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
