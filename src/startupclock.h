#pragma once

#include <QElapsedTimer>
#include <QLoggingCategory>

// One stopwatch for the whole startup, so the milestones are comparable.
//
// AC 36 wants first paint within 800 ms and the first measurement was 1369 ms.
// Guessing which of five startup phases owns that is how you end up optimising
// the fast one: on an A53, compiling an xkb keymap with disk includes, building
// a QML engine, and connecting to Wayland twice are all plausible candidates and
// they differ by an order of magnitude.
//
// `moarchy.startup` is a logging category so this costs nothing when off.
namespace StartupClock {

inline QElapsedTimer &timer()
{
    static QElapsedTimer clock;
    return clock;
}

inline void start() { timer().start(); }
inline qint64 elapsed() { return timer().isValid() ? timer().elapsed() : -1; }

} // namespace StartupClock

Q_DECLARE_LOGGING_CATEGORY(lcStartup)

#define MOARCHY_MARK(what) \
    qCInfo(lcStartup) << (what) << "at" << StartupClock::elapsed() << "ms"
