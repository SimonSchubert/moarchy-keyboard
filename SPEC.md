# moarchy-keyboard — specification

An on-screen keyboard for the PinePhone running mobileomarchy: Qt6 + QML, themed
from Omarchy's palette, rendered on the Mali-400.

Status: **agreed and built.** The acceptance criteria below are the contract;
[RESULTS.md](RESULTS.md) records which are met, with the measurement behind each.
Numbers here are targets, not measurements — where one turned out to be wrong,
RESULTS.md says so rather than this file being quietly edited to match.

---

## 1. Why this exists

squeekboard works and behaves correctly — it raises and retracts itself, keeps
one layer surface for the life of the process, and ships a terminal layout. It is
kept in mobileomarchy today for exactly those reasons. Two things are wrong with
it here:

**It ignores the theme.** Omarchy's whole idea is that one `omarchy-theme-set`
recolours everything at once. The bar, drawer, shade and theme picker all repaint
live. The keyboard — the single largest thing on screen while you type — stays
Adwaita grey. squeekboard's styling is GTK CSS baked into its gresource, so there
is no supported way in.

**It renders in software.** GTK3 draws through cairo, and on this GPU that means
the CPU. Measured on the phone, 2026-09-05:

| | RSS | PSS | rendering |
|---|---|---|---|
| squeekboard 1.43.1 | 74 MB | 51 MB | GTK3 → cairo → CPU |
| omarchy shell (quickshell) | 379 MB | — | Qt Quick → `lima_dri.so` → Mali-400 |

Free memory at the time: 413 MB free, 857 MB available, of 1971 MB total.

A Qt6 client shares `libQt6Core`/`libQt6Gui`/`libQt6Quick`, already resident for
the shell, so its *incremental* cost should be a fraction of squeekboard's 51 MB
PSS while moving the drawing onto the GPU. That is the bet this project makes,
and AC 6.1 is where it gets tested.

### Why not a Quickshell plugin

It was the obvious first choice — a QML file next to `mobileomarchy.drawer`, no
new process, theming free. It is not possible. Quickshell 0.3.1's
`Quickshell/Wayland` module exposes layer-shell, session lock, screencopy,
toplevel management, idle inhibit and shortcuts inhibit. It has no binding for
`zwp_input_method_v2` or `zwp_virtual_keyboard_v1`, and `strings` on the binary
finds neither. A keyboard that cannot bind those protocols cannot type.

Adding them to Quickshell upstream is a legitimate but much larger project, and
would put the keyboard on Quickshell's release cadence. Rejected for now.

### Why not wvkbd

wvkbd's colours are already command-line flags, so theming it is nearly free —
this was seriously considered as the cheap route. It is rejected on the bug that
removed it from mobileomarchy in the first place: it creates a fresh layer
surface per activation and destroys only the newest on deactivate (12 created
against 11 destroyed in one session), and the leaked surface stays mapped, so the
keyboard never goes away. It also has no terminal layout and needs a restart to
change colour.

That bug is this project's most important inherited lesson. See AC 2.2.

---

## 2. Non-goals

Explicitly out of scope for v1, so the ACs stay honest:

- **Autocorrect, prediction, swipe typing.** No language model, no word list.
- **Non-Latin scripts and IME composition** (CJK, Indic). The layout format
  should not make these impossible later, but nothing here implements them.
- **Emoji picker.** Deferred.
- **Landscape.** The phone runs portrait; mobileomarchy has no rotation handling.
- **Typing into `swaylock`.** Impossible by protocol, not by choice: under
  `ext-session-lock` the compositor draws only the locker's surface and hides
  every layer-shell client. This is why `bin/mobileomarchy-system-lock` blanks
  rather than locks when no hardware keyboard is attached. Unchanged here.
- **Replacing squeekboard on anything but this phone.** Not a general-purpose
  OSK; it may assume 360×720 logical and one output.

---

## 3. Architecture

One process, three layers.

```
  ┌──────────────────────────────────────────────────────────┐
  │ QML  layouts, key rendering, touch handling, animation   │
  │      ← Theme singleton (colours)                         │
  ├──────────────────────────────────────────────────────────┤
  │ C++  KeyRouter   decides text-vs-keycode per key press   │
  │      Theme       parses + watches colors.toml            │
  │      OskService  owns sm.puri.OSK0 on the session bus    │
  ├──────────────────────────────────────────────────────────┤
  │ C++  InputMethod       zwp_input_method_v2               │
  │      VirtualKeyboard   zwp_virtual_keyboard_v1           │
  │      Panel             LayerShellQt::Window              │
  └──────────────────────────────────────────────────────────┘
```

**Wayland access.** `QGuiApplication` (not `QApplication` — no QtWidgets), on a
**second Wayland connection of its own** rather than Qt's. Qt's would work —
`QNativeInterface::QWaylandApplication` hands out its `wl_display` and `wl_seat`
— but objects created there land on the default event queue, which Qt's platform
plugin reads on its own event thread through machinery that is internal and has
changed between minor releases. The failure mode when that shifts is an input
method that silently stops receiving `activate`, and this project has met that
class of bug too often. Two connections from one process is ordinary Wayland;
nothing crosses between them. (See `src/waylandconnection.h`. It is only sound
because the alternates popup is drawn in-panel rather than through
`get_input_popup_surface`, which would need a `wl_surface` from this connection.)

Protocol glue is generated at build time by `wayland-scanner` from **vendored**
XML in `protocols/`. Corrected from an earlier draft of this spec, which claimed
the `wlr-protocols` package supplies them: it does not. That package holds only
the `wlr-*` prefixed protocols, and no Arch package ships
`input-method-unstable-v2.xml` or `virtual-keyboard-unstable-v1.xml` at all —
they live in the wlroots source tree. `/usr/share/wayland-protocols` has
input-method **v1**, an unrelated protocol Sway does not advertise. squeekboard
and wvkbd vendor the same two files for the same reason.
`scripts/update-protocols.sh` refreshes them.

**Panel.** `LayerShellQt` (`extra/layer-shell-qt` 6.7.4, aarch64) for the layer
surface. Anchored left+right+bottom, layer `Top`, and — load-bearing —
`KeyboardInteractivity::None`, so the keyboard never takes Wayland keyboard focus
away from the app being typed into.

**Two paths out.** Both are needed; either alone leaves half the apps unable to
type. See §5.

**Theme.** Parsed straight from
`~/.local/state/omarchy/current/theme/colors.toml` — a flat `key = "value"` file
staged by `omarchy-theme-set`, so a 30-line parser, no TOML library, no IPC and
no coupling to the shell.

---

## 4. Acceptance criteria

Each is a claim that can be shown true or false on the phone.

### 4.1 Process and lifecycle

1. **Raises itself.** When a client with an active `zwp_text_input_v3` focuses a
   text field, the keyboard becomes visible with no user action.
2. **One surface, forever.** The process creates exactly one layer surface at
   startup and destroys it only at exit. Show/hide is anchor and exclusive-zone
   changes. Over 20 consecutive activate/deactivate cycles, the compositor logs
   equal counts of surface create and destroy — which for this AC means zero of
   each after startup. *(This is the wvkbd bug. It is the first thing to test.)*
3. **Retracts itself** when focus moves to a surface with no text input, and on
   `zwp_input_method_v2.deactivate`.
4. **Yields space.** Exclusive zone equals the panel height when shown and 0 when
   hidden, so the focused window resizes instead of being covered.
5. **Never steals focus.** With the keyboard up, the app under it keeps keyboard
   focus; typing on an attached USB keyboard still reaches the app.
6. **Survives compositor restart** — or, if it cannot, exits non-zero so the
   `exec_always` line restarts it. A wedged invisible keyboard is the failure to
   avoid.
7. **Handles `unavailable`.** If another input method is already bound, log it
   and exit non-zero rather than sitting silent. *(squeekboard's "No system
   layout present" silence cost real debugging time; do not repeat the shape of
   that bug.)*

### 4.2 D-Bus compatibility

8. **Owns `sm.puri.OSK0`** at `/sm/puri/OSK0` with a readable `Visible` property
   and a `SetVisible(b)` method, matching the interface Phosh defines and
   squeekboard implements.
9. `bin/mobileomarchy-toggle-keyboard` in mobileomarchy works **unmodified**.
   Verified by running it with no edits to that file.

### 4.3 Input paths

10. **Text path.** With a `text-input-v3` client focused, a printable key sends
    `commit_string` followed by `commit(serial)`, where serial is the count of
    `done` events received on the input-method object. The character appears once.
11. **Keycode path.** With no input method active, the same key produces the same
    character via `zwp_virtual_keyboard_v1.key()` against a keymap the client
    uploads once at startup.
12. **Mixed.** While an input method *is* active, non-text keys (Escape, Tab,
    arrows, Ctrl/Alt chords) still go out as keycodes, not as text.
13. **Terminal correctness.** In the terminal, `Ctrl+C` interrupts, `Tab`
    completes, `Escape` leaves vi insert mode, and the arrow keys walk shell
    history.
14. **Backspace.** Deletes one character in both paths, including immediately
    after a `commit_string` with no intervening `done`.
15. **No phantom evdev device.** `bin/mobileomarchy-has-keyboard` still exits 1
    with the OSK running — nothing in `/proc/bus/input/devices` gains
    KEY_ESC+KEY_1+KEY_Q+KEY_SPACE. *(Guards against ever "fixing" input by
    reaching for uinput, which would make the lock screen falsely believe a real
    keyboard is attached.)*
16. **Password fields.** On `content_type` hint `password` (or purpose
    `password`/`pin`), no long-press preview is drawn and no character is echoed
    anywhere but the field.

### 4.4 Layouts

17. Ships at minimum: **letters**, **shifted letters**, **numbers/symbols**,
    **extended symbols**, and **terminal** (Esc, Tab, Ctrl, Alt, arrows, `|`,
    `/`, `-`, `~`).
18. Layouts are **data, not code** — one declarative file per layout, loadable
    without recompiling.
19. A user layout in `~/.config/moarchy-keyboard/layouts/` overrides a shipped
    one of the same name.
20. Layout switching is one tap, and the current layout is visible without
    typing to find out.
21. **Terminal layout auto-selects** for terminal clients (by `app_id`, checked
    via the compositor), and manual selection overrides the guess until focus
    changes.

### 4.5 Theming

22. **Colours come from** `~/.local/state/omarchy/current/theme/colors.toml`,
    using `background`, `foreground`, `accent`, `lighter_background`, `muted`,
    `selection` and `mode` at minimum.
23. **Live recolour.** `omarchy-theme-set` repaints the keyboard with no restart,
    while it is on screen, within 1 second of the file being written.
24. **The watch survives replacement.** `colors.toml` is rewritten, not edited in
    place, so the watch must be re-armed after every change and the parent
    directory watched too. Verified by changing theme **three times in a row**
    and seeing all three take effect. *(A `QFileSystemWatcher` on the file alone
    fires once and then goes deaf. This AC exists because that bug is silent and
    looks like "theming works" in a single test.)*
25. **Legible in every shipped theme.** Key text against key fill meets WCAG AA
    (4.5:1) in all Omarchy themes; a theme that cannot reach it falls back to a
    computed contrasting foreground rather than rendering grey-on-grey.
26. **No hardcoded colour** anywhere in the QML. Verified by grep.
27. Missing or malformed `colors.toml` yields a documented built-in palette and a
    log line — never an unpainted or invisible keyboard.

### 4.6 Touch and interaction

28. **Geometry.** Panel is sized for the 360×720 logical surface (720×1440
    physical at `output * scale 2`, DSI-1, 60 Hz).
29. **No dead zones.** Hit areas tessellate the panel — every touch inside it
    activates exactly one key. Visual gaps between keys belong to the nearest key.
30. **Multi-touch.** A second finger landing while the first is still down
    registers both, in order. Fast typing must not drop keys.
31. **Slide-off cancels.** A press that moves off its key before release emits
    nothing.
32. **Long-press alternates.** Holding a key with alternates shows them and the
    release selects; ~400 ms threshold, tunable.
33. **Press feedback within one frame** (≤16.7 ms at 60 Hz), independent of how
    long the commit takes.
34. **Modifiers latch.** Shift/Ctrl/Alt apply to the next key and release;
    double-tap locks; the state is visible.

### 4.7 Performance and footprint

35. **Incremental PSS ≤ 25 MB** with the shell already running — measured as
    `Pss` in `/proc/<pid>/smaps_rollup`, against squeekboard's 51 MB on the same
    device. *(Target chosen as "clearly better than half"; renegotiable on
    evidence, but not silently.)*
36. **Cold start to first paint ≤ 800 ms** on the A64.
37. **Idle CPU 0%** — no animation timers, no polling, no repaints when nothing
    is happening. Verified over 60 s with `top`.
38. **Key press to `commit_string` on the wire ≤ 50 ms.**
39. **GPU rendering confirmed**, not assumed: the process opens the lima DRI
    driver rather than falling back to software.

### 4.8 Packaging and integration

40. Builds reproducibly in mobileomarchy's existing aarch64 container
    (`docker/build-packages.sh`) and installs as a pacman package, as walker and
    elephant already do.
41. Depends only on what the phone already has or can get from `extra`:
    `qt6-base`, `qt6-declarative`, `qt6-wayland`, `layer-shell-qt`, `wayland`
    (which also supplies `wayland-scanner`), `libxkbcommon`. **Not**
    `wlr-protocols` — it does not contain these protocols; see §3.
42. mobileomarchy's change is **one line** in `default/sway/autostart.conf`
    (`exec squeekboard` → `exec moarchy-keyboard`), plus removing `squeekboard`
    and `libbsd` from `mobileomarchy-base.packages`.
43. The two GSettings keys squeekboard needed
    (`org.gnome.desktop.a11y.applications screen-keyboard-enabled` and
    `org.gnome.desktop.input-sources sources`) are **not** required, and
    `install/config.sh`'s gating block can be deleted.
44. `bin/mobileomarchy-selftest` gains a check that the keyboard owns
    `sm.puri.OSK0`.
45. The panel's background runs under mobileomarchy's gesture strip, so no band
    of wallpaper shows below the keys. The keys themselves do not move: the
    surface is grown by `--gesture-strip-inset` and given a negative bottom
    margin of the same size, and the key area is inset by it again. Measured on
    a 720x1440 panel: the rows below the keys read the panel background to the
    last physical row, where they read wallpaper before.
46. Running under the strip costs the app nothing. sway reduces the usable area
    by `exclusive_zone + margin.bottom`, so the zone carries `+ stripInset` to
    cancel the negative margin and the reservation stays exactly
    `panelHeight`. Without that compensation it comes out 24px short, and the
    surface arranged below settles over the top key row and clips it — which is
    how it was found.
47. Nothing tappable sits in the band. The keys are inset out of it, and so is
    the restore handle, which is the only way back from a dismissed keyboard
    and would otherwise land under the home pill.
48. No input region is masked for either gesture band. This panel is on `Top`
    and both the strip and the back edge are on `Overlay`, so they take their
    touches first — the same reason `kDefaultBackEdgeInset` is 0.

---

## 5. The two input paths, in detail

The single most important design point, and where a naive implementation breaks.

Wayland has two halves of text input. Apps that implement `zwp_text_input_v3`
(GTK4, Qt6, most modern toolkits) negotiate with an input method: the keyboard
sends **strings**, and the app never sees a key event. Apps that do not — X11
apps under Xwayland, terminals, anything older — only understand **key events**,
which need `zwp_virtual_keyboard_v1` to synthesise.

wvkbd does only the second. squeekboard does both. This must do both:

| focused client | printable key | Escape / Tab / arrows / Ctrl chord |
|---|---|---|
| has text-input-v3 | `commit_string` + `commit(serial)` | virtual-keyboard keycode |
| has not | virtual-keyboard keycode | virtual-keyboard keycode |

Two traps:

**The serial.** `zwp_input_method_v2.commit` takes the number of `done` events
received on that object. Track it as a counter incremented in the `done` handler,
not as a Wayland object serial and not as a timestamp. Getting it wrong drops
input silently, which reads as "the keyboard misses keystrokes sometimes" and is
miserable to debug after the fact.

**The keymap.** `zwp_virtual_keyboard_v1.keymap` wants an fd to an XKB keymap,
sent once at startup. Build it with `xkb_keymap_new_from_names`, serialise, write
to a memfd, keep the fd for the life of the process. Every keycode sent
afterwards is an index into *that* keymap, not into whatever the compositor's
own layout happens to be.

---

## 6. Open questions — answered by default

These went unanswered when work started, so rather than block, each was decided
with a default and built. All five are cheap to change; none is load-bearing.

1. **Haptics.** *Not built.* squeekboard pulled in `feedbackd` for key vibration
   and the phone has a vibrator, but it is a new runtime dependency for a
   preference, and a keyboard that buzzes is a keyboard that drains. Easy to add
   later behind a setting.
2. **Panel height.** *200 of the 720 logical pixels*, ~28%, settable with
   `--panel-height`. Started at 300, which was reported as keys that were too
   tall and measured 36 wide by 75 high -- more than twice as tall as wide, which
   reads as a stretched keyboard rather than a keyboard. 200 gives 50 on a
   four-row layout and 40 on the terminal layout''s five, against keys 32 wide.
3. **Number row.** *Not always present.* Digits live behind a long-press on the
   top letter row and on the symbols layout, the Android convention. An
   always-present number row costs a fifth of the panel height, and on a screen
   this size that comes out of the app.
4. **Reading the theme's `shell.toml` for radius and spacing.** *No.* The
   keyboard keeps its own geometry. `shell.toml` describes bar and menu chrome —
   `size-horizontal`, control borders, a typographic scale — and none of it maps
   onto a key grid whose proportions are set by thumbs, not by type. Colour is
   the part of a theme a keyboard should follow, and colour comes from
   `colors.toml`.
5. **Landscape.** *Confirmed out of scope*, per §2. The phone runs portrait and
   mobileomarchy has no rotation handling to hook into.

## 7. How this gets verified

Not a unit-test suite — most of these ACs are about protocol behaviour against a
real compositor, and a mock proves nothing about Sway.

- **Protocol-level ACs (4.1, 4.3)** — a harness client that binds
  `zwp_text_input_v3` and asserts what arrives, run under Sway on the phone, plus
  `WAYLAND_DEBUG=1` for surface create/destroy counts in AC 2.
- **Theming (4.5)** — script that walks every Omarchy theme, applies it, screenshots
  the keyboard with `grim`, and checks contrast. Extends
  `scripts/test-themes.sh`.
- **Touch (4.6)** — manual, on the device, against a written checklist. There is
  no substitute.
- **Performance (4.7)** — `smaps_rollup`, `top`, and timestamped
  `WAYLAND_DEBUG` output. Recorded per commit that touches rendering.
- **Integration (4.8)** — a full `scripts/provision.sh` run onto a clean SD card,
  ending in typing into a terminal and a GTK app.

Screenshots come back over ssh with `grim`; a wallpaper-only capture means the
sway session died, not that grim is broken.
