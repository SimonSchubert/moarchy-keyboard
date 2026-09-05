# Landing this in mobileomarchy

Not applied yet, and deliberately so: two other Claude sessions are editing
`~/Projects/mobileomarchy` on branch `fix/provisioning-silent-failures` right
now. This is the exact change to make once the keyboard is proven, written down
so it can be reviewed before it touches a shared tree.

It is four edits and one addition. **`bin/mobileomarchy-toggle-keyboard` is not
one of them** — that script drives `sm.puri.OSK0`, this keyboard owns that name,
and it was verified working with zero edits. That was a design constraint, not
luck.

---

## 1. `default/sway/autostart.conf` — swap the exec

Replace the block at lines 51–62 (the comment plus `exec squeekboard`):

```diff
-# On-screen keyboard. It raises itself when a text field takes focus and
-# retracts when focus moves to something without one -- no toggle, no gesture.
-#
-# That works because Sway advertises zwp_input_method_manager_v2 and
-# zwp_text_input_manager_v3, so an OSK binding them is handed activate /
-# deactivate as apps focus text. (phosh's own stevia does *not* work here: it
-# gates surface creation on zphoc_device_state_v1, a phoc-private protocol Sway
-# will never have. squeekboard needs no such thing.)
-#
-# Needs two gsettings, applied by install/config.sh -- without them squeekboard
-# starts, binds nothing, and logs only "No system layout present".
-exec squeekboard
+# On-screen keyboard. It raises itself when a text field takes focus and
+# retracts when focus moves to something without one -- no toggle, no gesture.
+#
+# That works because Sway advertises zwp_input_method_manager_v2 and
+# zwp_text_input_manager_v3, so an OSK binding them is handed activate /
+# deactivate as apps focus text. (phosh's own stevia does *not* work here: it
+# gates surface creation on zphoc_device_state_v1, a phoc-private protocol Sway
+# will never have.)
+#
+# moarchy-keyboard replaced squeekboard, which behaved correctly but ignored the
+# theme and drew through cairo on the CPU. This one reads the active theme's
+# colors.toml directly and recolours live on omarchy-theme-set, renders on the
+# Mali, and needs no gsettings at all -- see ~/Projects/moarchy-keyboard.
+#
+# `exec`, not `exec_always`: a config reload would start a second instance, and
+# although it exits non-zero on its own (one input method per seat), the flash
+# of a second keyboard is not worth it.
+exec moarchy-keyboard
```

## 2. `mobileomarchy-base.packages` — swap the package

Replace lines 48–67 (from `# squeekboard is the keyboard...` through `libbsd`):

```diff
-# squeekboard is the keyboard because it raises and retracts itself correctly.
-# ... (the wvkbd leak note, the two-gsettings note)
-squeekboard
-# squeekboard's binary links libbsd.so.0, but the Arch Linux ARM package does
-# not declare it ...
-libbsd
+# moarchy-keyboard is the keyboard. It binds zwp_input_method_v2 for apps that
+# speak text-input-v3 and zwp_virtual_keyboard_v1 for everything that does not,
+# keeps ONE layer surface for the life of the process, and reads the Omarchy
+# palette straight out of ~/.local/state/omarchy/current/theme/colors.toml.
+#
+# It is not in any repo: built from ~/Projects/moarchy-keyboard by
+# docker/build-packages.sh, like walker and elephant.
+#
+# squeekboard was here and behaved correctly -- self-raising, one surface, a
+# terminal layout -- but it ignores the theme (GTK CSS baked into its gresource)
+# and draws through cairo on the CPU. wvkbd was here before that and leaked a
+# layer surface per activation, which is why it went.
+#
+# libbsd went with squeekboard: it was only here because squeekboard's Arch ARM
+# package fails to declare it and died at exec without it.
+moarchy-keyboard
```

Keep `layer-shell-qt` somewhere in this file — it is a new runtime dependency and
is **not** currently installed by a stock provision.

## 3. `install/config.sh` — delete the gsettings gating

Delete lines 78–116 entirely, the whole `--- On-screen keyboard gating ---`
block: the `gset()` helper, both `gsettings set` calls, and the read-back check.

moarchy-keyboard reads neither `org.gnome.desktop.a11y.applications
screen-keyboard-enabled` nor `org.gnome.desktop.input-sources sources`. They
existed because squeekboard silently did nothing without them, which is exactly
the class of failure that block was written to catch — and with squeekboard gone
they are dead weight that still costs a `dbus-run-session` at install time.

## 4. `docker/build-packages.sh` — build it

It currently builds from AUR only. moarchy-keyboard is a local tree, so it needs
a different path: either add a bind mount and run `makepkg` in
`packaging/`, or publish the repo and add `moarchy-keyboard` to the `PACKAGES`
array once it has a PKGBUILD upstream. The former is right while this is
unreleased.

## 5. `bin/mobileomarchy-selftest` — add a check

Whatever shape that file has by then, the assertion is:

```bash
busctl --user get-property sm.puri.OSK0 /sm/puri/OSK0 sm.puri.OSK0 Visible
```

Non-zero exit means no on-screen keyboard is on the bus, which on a phone with
no hardware keyboard means the device cannot be typed into at all — worth failing
loudly rather than discovering by trying.

---

## What to check after landing

- `mobileomarchy-toggle-keyboard` still works **unmodified**.
- `mobileomarchy-has-keyboard` still exits 1 with the OSK running. If it ever
  starts exiting 0, `mobileomarchy-system-lock` will begin locking a phone that
  cannot type its own password.
- A back-edge swipe over the keyboard passes through rather than being swallowed
  (`KeyboardInteractivityNone` should ensure it, but the gesture layer is
  another session's work and worth confirming together).
