# The sway session environment, for commands started over ssh.
#
# Source this rather than relying on a file left in /tmp: a reboot clears /tmp,
# and a missing env file does not fail loudly -- WAYLAND_DISPLAY is simply unset,
# Qt falls back to the xcb platform plugin, fails to reach a display, and aborts
# with SIGABRT. The test suite then reports every criterion as skipped, which
# reads like the keyboard refusing to start.
#
# /proc/<sway>/environ is not readable here even as the same user, so these are
# reconstructed from the sockets in the runtime directory.

export XDG_RUNTIME_DIR=/run/user/1000

_moa_wayland=$(ls "$XDG_RUNTIME_DIR"/wayland-[0-9] 2>/dev/null | head -1)
if [[ -z $_moa_wayland ]]; then
  echo "no wayland socket in $XDG_RUNTIME_DIR -- is sway running?" >&2
  return 1 2>/dev/null || exit 1
fi
export WAYLAND_DISPLAY=$(basename "$_moa_wayland")
export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
export SWAYSOCK=$(ls "$XDG_RUNTIME_DIR"/sway-ipc.*.sock 2>/dev/null | head -1)

# Fail fast and clearly rather than letting Qt abort with a platform-plugin
# error that says nothing about the real cause.
if [[ -z $SWAYSOCK ]]; then
  echo "no sway IPC socket in $XDG_RUNTIME_DIR" >&2
  return 1 2>/dev/null || exit 1
fi
