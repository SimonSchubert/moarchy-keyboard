#include "inputmethod.h"
#include "keyrouter.h"
#include "layoutstore.h"
#include "oskservice.h"
#include "panel.h"
#include "virtualkeyboard.h"
#include "startupclock.h"
#include "theme.h"
#include "virtualkeyboard.h"
#include "waylandconnection.h"

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QQuickWindow>
#include <QStandardPaths>
#include <functional>
#include <QTextStream>

Q_LOGGING_CATEGORY(lcMain, "moarchy.main")
Q_LOGGING_CATEGORY(lcStartup, "moarchy.startup")

namespace {

QString defaultColorsPath()
{
    return QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
}

// Every startup failure exits non-zero and says why on stderr.
//
// Sway restarts us through `exec_always`, so a non-zero exit is a retry rather
// than a dead phone -- and the alternative, staying alive with no keyboard
// surface, is the failure this project has hit most: squeekboard running
// happily while logging only "No system layout present", and a phone that
// cannot type with nothing obviously wrong (AC 7).
int fail(const QString &what, const QString &detail)
{
    qCCritical(lcMain).noquote() << what << "--" << detail;
    return 1;
}

} // namespace

int main(int argc, char *argv[])
{
    StartupClock::start();

    // Three Qt Quick settings, set here rather than left to the environment
    // because they were measured on the target and this program can always
    // afford them.
    //
    //   basic render loop      -- no render thread. This keyboard animates
    //                             nothing; a thread and its scene graph
    //                             double-buffering are pure cost.
    //   transient images       -- drop decoded image data after upload. There
    //                             are no images at all in the QML, so this only
    //                             ever frees glyph and atlas scratch.
    //   V4 interpreter         -- no JIT. The JavaScript here is a hit test and
    //                             a few branches per key; JIT code pages cost
    //                             more than they save.
    //
    // Together, measured on the phone: 75.8 MB PSS and 39.7 MB private dirty
    // against 80.6 and 44.3 with the defaults -- about 4.6 MB of each. Set with
    // qputenv rather than setenv so an operator can still override them, since
    // qputenv does not replace a value already present in the environment.
    if (qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP"))
        qputenv("QSG_RENDER_LOOP", "basic");
    if (qEnvironmentVariableIsEmpty("QSG_TRANSIENT_IMAGES"))
        qputenv("QSG_TRANSIENT_IMAGES", "1");
    if (qEnvironmentVariableIsEmpty("QV4_FORCE_INTERPRETER"))
        qputenv("QV4_FORCE_INTERPRETER", "1");

    // No LayerShellQt::Shell::useLayerShell() here: deprecated since 6.6 and
    // unnecessary since 6.5, because LayerShellQt::Window::get() -- which
    // Panel::prepare() calls before the view is ever shown -- now arranges the
    // layer-shell role itself.
    QGuiApplication app(argc, argv);
    MOARCHY_MARK("QGuiApplication");
    app.setApplicationName(QStringLiteral("moarchy-keyboard"));
    app.setApplicationVersion(QStringLiteral(MOARCHY_VERSION));
    app.setDesktopFileName(QStringLiteral("moarchy-keyboard"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("A themed on-screen keyboard for mobileomarchy."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption colorsOption(
        QStringList { QStringLiteral("colors") },
        QStringLiteral("Path to the active theme's colors.toml."),
        QStringLiteral("path"), defaultColorsPath());
    parser.addOption(colorsOption);

    QCommandLineOption layoutOption(
        QStringList { QStringLiteral("layout") },
        QStringLiteral("Layout to start on."),
        QStringLiteral("name"), QStringLiteral("letters"));
    parser.addOption(layoutOption);

    QCommandLineOption dumpKeymapOption(
        QStringList { QStringLiteral("dump-keymap") },
        QStringLiteral("Print the generated xkb keymap, check it compiles, and exit. "
                       "Needs no compositor."));
    parser.addOption(dumpKeymapOption);

    QCommandLineOption checkThemesOption(
        QStringList { QStringLiteral("check-themes") },
        QStringLiteral("Walk <dir>/*/colors.toml, report WCAG contrast for every "
                       "text-on-fill pair, and exit non-zero if any falls under "
                       "AA. Needs no compositor."),
        QStringLiteral("dir"));
    parser.addOption(checkThemesOption);

    QCommandLineOption checkContrastOption(
        QStringList { QStringLiteral("check-contrast") },
        QStringLiteral("Check one colour pair: --check-contrast '<fg>,<bg>[,<alpha>]', "
                       "e.g. '#cdd6f4,#313244,0.7'. Alpha composites the foreground "
                       "over the background first, which is where most near-misses "
                       "come from. Needs no compositor."),
        QStringLiteral("fg,bg[,alpha]"));
    parser.addOption(checkContrastOption);

    QCommandLineOption roleOption(
        QStringList { QStringLiteral("role") },
        QStringLiteral("With --check-themes, measure one arbitrary pair of theme "
                       "roles across every theme instead of the keyboard's own: "
                       "--role 'foreground,lighter_background,0.7'. Role names are "
                       "colors.toml keys."),
        QStringLiteral("fgRole,bgRole[,alpha]"));
    parser.addOption(roleOption);

    QCommandLineOption backEdgeOption(
        QStringList { QStringLiteral("back-edge-inset") },
        QStringLiteral("Logical pixels along the left edge to leave to "
                       "mobileomarchy's back gesture: excluded from the input "
                       "region and inset from the keys. Default 20."),
        QStringLiteral("px"));
    parser.addOption(backEdgeOption);

    QCommandLineOption panelHeightOption(
        QStringList { QStringLiteral("panel-height") },
        QStringLiteral("Keyboard height in logical pixels. Default 200."),
        QStringLiteral("px"));
    parser.addOption(panelHeightOption);

    QCommandLineOption bottomMarginOption(
        QStringList { QStringLiteral("bottom-margin") },
        QStringLiteral("Logical pixels to leave below the keyboard for "
                       "mobileomarchy's gesture strip. Default 24."),
        QStringLiteral("px"));
    parser.addOption(bottomMarginOption);

    QCommandLineOption checkLayoutsOption(
        QStringList { QStringLiteral("check-layouts") },
        QStringLiteral("Validate every layout and exit non-zero on a problem. "
                       "Needs no compositor."));
    parser.addOption(checkLayoutsOption);

    parser.process(app);

    if (parser.isSet(checkLayoutsOption)) {
        // Checks the things the QML assumes and cannot complain about at
        // runtime. A `type: layout` key naming a layout that does not exist is
        // the motivating case: it parses, it draws, and tapping it does
        // nothing at all -- there is no error, because "no such layout" is
        // indistinguishable from "the user has not tapped it yet".
        LayoutStore store(nullptr);
        QTextStream out(stdout);
        const QStringList names = store.names();
        int problems = 0;

        for (const QString &name : names) {
            const int before = problems;
            const QVariantMap parsed = store.layout(name);
            const QVariantList rows = parsed.value(QStringLiteral("rows")).toList();
            if (rows.isEmpty()) {
                out << QStringLiteral("FAIL %1: no rows\n").arg(name);
                ++problems;
                continue;
            }

            int keys = 0;
            for (int r = 0; r < rows.size(); ++r) {
                const QVariantList rowKeys =
                    rows.at(r).toMap().value(QStringLiteral("keys")).toList();
                if (rowKeys.isEmpty()) {
                    out << QStringLiteral("FAIL %1 row %2: no keys\n").arg(name).arg(r);
                    ++problems;
                }
                for (const QVariant &keyValue : rowKeys) {
                    const QVariantMap key = keyValue.toMap();
                    const QString type =
                        key.value(QStringLiteral("type"), QStringLiteral("character")).toString();
                    ++keys;

                    if (type == QLatin1String("layout")) {
                        const QString target = key.value(QStringLiteral("layout")).toString();
                        if (!names.contains(target)) {
                            out << QStringLiteral("FAIL %1: a key switches to layout \"%2\", "
                                                  "which does not exist -- tapping it will do "
                                                  "nothing, silently\n").arg(name, target);
                            ++problems;
                        }
                    } else if (type == QLatin1String("action")) {
                        const QString keyName = key.value(QStringLiteral("key")).toString();
                        if (VirtualKeyboard::keycodeForName(keyName) == 0) {
                            out << QStringLiteral("FAIL %1: unknown named key \"%2\"\n")
                                       .arg(name, keyName);
                            ++problems;
                        }
                    } else if (type == QLatin1String("modifier")) {
                        const QString modifier = key.value(QStringLiteral("modifier")).toString();
                        if (modifier != QLatin1String("shift") && modifier != QLatin1String("ctrl")
                            && modifier != QLatin1String("alt")) {
                            out << QStringLiteral("FAIL %1: unknown modifier \"%2\"\n")
                                       .arg(name, modifier);
                            ++problems;
                        }
                    } else if (key.value(QStringLiteral("text")).toString().isEmpty()) {
                        out << QStringLiteral("FAIL %1: a %2 key emits nothing\n")
                                   .arg(name, type);
                        ++problems;
                    }
                }
            }
            // Per-layout, not cumulative: with a running total, every layout
            // after the first failure prints blank and reads as also broken.
            out << QStringLiteral("%1 %2  %3 rows, %4 keys\n")
                       .arg(problems == before ? QStringLiteral("ok  ") : QStringLiteral("FAIL"),
                            name.leftJustified(20))
                       .arg(rows.size()).arg(keys);
        }

        out << QStringLiteral("\n%1 layouts, %2 problem(s)\n").arg(names.size()).arg(problems);
        return problems == 0 ? 0 : 1;
    }

    if (parser.isSet(checkContrastOption)) {
        const QStringList parts = parser.value(checkContrastOption).split(QLatin1Char(','));
        QTextStream out(stdout);
        if (parts.size() < 2) {
            QTextStream(stderr) << "expected <fg>,<bg>[,<alpha>]\n";
            return 2;
        }

        QColor foreground(parts.at(0).trimmed());
        const QColor background(parts.at(1).trimmed());
        if (!foreground.isValid() || !background.isValid()) {
            QTextStream(stderr) << "could not parse those colours\n";
            return 2;
        }

        double alpha = 1.0;
        if (parts.size() >= 3) {
            bool ok = false;
            alpha = parts.at(2).trimmed().toDouble(&ok);
            if (!ok || alpha < 0.0 || alpha > 1.0) {
                QTextStream(stderr) << "alpha must be between 0 and 1\n";
                return 2;
            }
            // Composite before measuring. A translucent foreground is NOT the
            // colour you wrote -- it is that colour mixed with whatever is
            // behind it, and on a raised card that is a lighter surface than
            // the base background, which quietly eats the contrast margin.
            foreground = Theme::composite(foreground, background, alpha);
        }

        const double ratio = Theme::contrastOf(foreground, background);
        out << QStringLiteral("%1 on %2%3  ->  %4:1  %5\n")
                   .arg(foreground.name(),
                        background.name(),
                        alpha < 1.0 ? QStringLiteral(" (alpha %1, composited to %2)")
                                          .arg(alpha).arg(foreground.name())
                                    : QString(),
                        QString::number(ratio, 'f', 2),
                        ratio >= 4.5 ? QStringLiteral("PASS (AA normal text)")
                                     : ratio >= 3.0 ? QStringLiteral("AA large text only")
                                                    : QStringLiteral("FAIL"));
        return ratio >= 4.5 ? 0 : 1;
    }

    if (parser.isSet(checkThemesOption)) {
        const QString root = parser.value(checkThemesOption);
        QTextStream out(stdout);

        QDir dir(root);
        const QStringList themes = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (themes.isEmpty()) {
            QTextStream(stderr) << "no theme directories under " << root << "\n";
            return 1;
        }

        int checked = 0;
        int under = 0;
        int rescuedThemes = 0;
        for (const QString &name : themes) {
            const QString path = dir.filePath(name) + QStringLiteral("/colors.toml");
            if (!QFile::exists(path))
                continue;

            Theme theme(nullptr);
            theme.loadOnce(path);
            ++checked;

            QVariantMap report;
            if (parser.isSet(roleOption)) {
                const QStringList parts = parser.value(roleOption).split(QLatin1Char(','));
                if (parts.size() < 2) {
                    QTextStream(stderr) << "--role wants <fgRole>,<bgRole>[,alpha]\n";
                    return 2;
                }
                bool fgOk = false, bgOk = false;
                const QColor bg = theme.resolveColor(parts.at(1), &bgOk);
                QColor fg = theme.resolveColor(parts.at(0), &fgOk);
                if (!fgOk || !bgOk) {
                    out << QStringLiteral("SKIP %1  (could not resolve %2 or %3)\n")
                               .arg(name.leftJustified(20), parts.at(0), parts.at(1));
                    continue;
                }
                if (parts.size() >= 3)
                    fg = Theme::composite(fg, bg, parts.at(2).trimmed().toDouble());
                report.insert(parser.value(roleOption), Theme::contrastOf(fg, bg));
            } else {
                report = theme.contrastReport();
            }
            QStringList failures;
            double worst = 99.0;
            for (auto it = report.constBegin(); it != report.constEnd(); ++it) {
                const double ratio = it.value().toDouble();
                worst = qMin(worst, ratio);
                if (ratio < 4.5)
                    failures.append(QStringLiteral("%1 %2:1")
                                        .arg(it.key()).arg(ratio, 0, 'f', 2));
            }

            const int rescued = parser.isSet(roleOption) ? 0 : theme.contrastFallbacks();
            if (rescued > 0)
                ++rescuedThemes;

            out << QStringLiteral("%1 %2  worst %3:1  %4%5\n")
                       .arg(failures.isEmpty() ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                            name.leftJustified(20),
                            QString::number(worst, 'f', 2),
                            rescued > 0 ? QStringLiteral("(%1 role(s) fell back) ").arg(rescued)
                                        : QString(),
                            failures.join(QStringLiteral("; ")));
            if (!failures.isEmpty())
                ++under;
        }

        out << QStringLiteral("\n%1 themes checked, %2 under AA. "
                              "%3 needed the contrast fallback to get there.\n")
                   .arg(checked).arg(under).arg(rescuedThemes);
        return under == 0 ? 0 : 1;
    }

    if (parser.isSet(dumpKeymapOption)) {
        LayoutStore store(nullptr);
        VirtualKeyboard probe;
        QString compileError;
        const QStringList characters = store.allCharacters();
        const QString source = probe.keymapSource(characters, &compileError);

        QTextStream out(stdout);
        out << source;
        QTextStream err(stderr);
        err << "layouts: " << store.names().join(QLatin1String(", ")) << "\n"
            << "characters: " << characters.size() << ", generated keys: "
            << probe.generatedKeyCount() << "\n";
        if (!compileError.isEmpty()) {
            err << "COMPILE FAILED: " << compileError << "\n";
            return 1;
        }
        err << "keymap compiles\n";
        return 0;
    }

    // --- Wayland ------------------------------------------------------------
    WaylandConnection connection;
    QString error;
    if (!connection.open(&error))
        return fail(QStringLiteral("cannot start"), error);

    // Layouts are loaded before the keyboard, because the keymap is compiled
    // from the characters they declare -- that is what lets a long-press
    // accent, a euro sign or an em dash be typed into a terminal, which has no
    // text input to commit a string to.
    LayoutStore layouts(nullptr);

    MOARCHY_MARK("layouts loaded");

    VirtualKeyboard virtualKeyboard;
    if (!virtualKeyboard.init(&connection, layouts.allCharacters(), &error))
        return fail(QStringLiteral("virtual keyboard unavailable"), error);
    MOARCHY_MARK("keymap compiled and uploaded");

    InputMethod inputMethod;
    if (!inputMethod.init(&connection, &error))
        return fail(QStringLiteral("input method unavailable"), error);
    MOARCHY_MARK("input method bound");

    QObject::connect(&connection, &WaylandConnection::lost, &app, [&app](const QString &reason) {
        qCCritical(lcMain).noquote() << "lost the compositor:" << reason;
        app.exit(1);
    });

    QObject::connect(&inputMethod, &InputMethod::unavailable, &app, [&app] {
        qCCritical(lcMain) << "another input method already holds this seat; exiting so "
                              "two keyboards do not fight over it";
        app.exit(1);
    });

    // --- Services -----------------------------------------------------------
    KeyRouter router(&inputMethod, &virtualKeyboard);

    Theme theme(nullptr);
    theme.start(parser.value(colorsOption));
    MOARCHY_MARK("theme parsed");

    OskService osk;
    if (!osk.registerOnBus(&error)) {
        // Not fatal: the keyboard still raises and retracts itself, which is how
        // it is used 99% of the time. Only the manual toggle is lost, and saying
        // so beats dying.
        qCWarning(lcMain).noquote() << "no D-Bus toggle:" << error;
    }

    // --- Panel --------------------------------------------------------------
    Panel panel(nullptr);

    // Visibility has two inputs: what the compositor says about text focus, and
    // what someone asked for over D-Bus. A manual request -- the back gesture,
    // or mobileomarchy-toggle-keyboard -- wins for a while, so the keyboard can
    // be pushed out of the way without moving focus.
    //
    // "For a while" is the hard part, and getting it wrong left the keyboard
    // permanently stuck down: dismiss it with the back gesture and tapping the
    // text field again did nothing, for ever. The override was cleared on
    // activeChanged, which does not fire nearly often enough. It fires on a net
    // change of the active flag, and moving focus from one text field to
    // another coalesces deactivate and activate into one `done`, so the flag
    // goes true -> true and nothing is signalled. Tapping the SAME field does
    // not change it either.
    //
    // So the override is cleared by any of:
    //   * activate            -- a text input asked for a keyboard
    //   * any `done`          -- the focused client said something about its
    //                            text state, which is what tapping into a field
    //                            produces
    //   * the active flag changing, as before
    //
    // with a short grace period, so that the settling updates which arrive just
    // after a hide do not immediately undo it. Erring towards showing is
    // deliberate: a keyboard that reappears slightly too eagerly is a nuisance,
    // and one that will not come back at all makes the phone unusable.
    static bool manualOverride = false;
    static bool manualWanted = false;
    static QElapsedTimer sinceOverride;
    constexpr int kOverrideGraceMs = 700;

    auto applyVisibility = [&] {
        const bool wanted = manualOverride ? manualWanted : inputMethod.isActive();

        // Three states, not two. A keyboard dismissed by hand while a text
        // field is still focused leaves the user with nothing to tap: the
        // platform sends no event when an already-focused field is tapped
        // again, so there is nothing to wake up on. That case -- and only that
        // case -- shows a small handle instead of nothing at all.
        const Panel::Mode mode = wanted            ? Panel::Shown
                               : inputMethod.isActive() && manualOverride
                                                   ? Panel::Handle
                                                   : Panel::Hidden;
        panel.setMode(mode);
        osk.setVisible(wanted);
    };

    auto clearOverride = [&](const char *why) {
        if (!manualOverride)
            return;
        if (sinceOverride.isValid() && sinceOverride.elapsed() < kOverrideGraceMs)
            return;
        qCInfo(lcMain) << "manual override cleared by" << why;
        manualOverride = false;
        applyVisibility();
    };

    QObject::connect(&inputMethod, &InputMethod::activeChanged, &app, [&] {
        manualOverride = false;
        applyVisibility();
    });

    QObject::connect(&inputMethod, &InputMethod::activated, &app,
                     [&] { clearOverride("activate"); });

    // The handle was tapped. This is the deliberate way back, so it clears the
    // override outright rather than going through the grace period.
    QObject::connect(&panel, &Panel::showRequested, &app, [&] {
        qCInfo(lcMain) << "restore handle tapped";
        manualOverride = false;
        applyVisibility();
    });
    QObject::connect(&inputMethod, &InputMethod::stateApplied, &app,
                     [&] { clearOverride("text-input state update"); });

    QObject::connect(&osk, &OskService::visibleChanged, &app, [&] {
        if (osk.visible() == panel.isShown())
            return;
        manualOverride = true;
        manualWanted = osk.visible();
        sinceOverride.start();
        applyVisibility();
    });

    // --- QML ----------------------------------------------------------------
    if (parser.isSet(backEdgeOption)) {
        bool ok = false;
        const int px = parser.value(backEdgeOption).toInt(&ok);
        if (!ok || px < 0)
            return fail(QStringLiteral("bad --back-edge-inset"),
                        parser.value(backEdgeOption));
        panel.setBackEdgeInset(px);
    }

    for (const auto &[option, apply] : {
             std::pair { std::cref(panelHeightOption),
                         std::function<void(int)>([&panel](int px) { panel.setPanelHeight(px); }) },
             std::pair { std::cref(bottomMarginOption),
                         std::function<void(int)>([&panel](int px) { panel.setBottomMargin(px); }) },
         }) {
        if (!parser.isSet(option))
            continue;
        bool ok = false;
        const int px = parser.value(option).toInt(&ok);
        if (!ok || px < 0)
            return fail(QStringLiteral("bad geometry option"), parser.value(option));
        apply(px);
    }

    if (!panel.prepare(&error))
        return fail(QStringLiteral("cannot build the panel"), error);
    MOARCHY_MARK("panel prepared");

    // Singletons, not context properties. See the note above class Theme for
    // why: qmllint can resolve a declared singleton and cannot see a context
    // property, and on this program an unresolvable colour renders as black on
    // black with nothing logged.
    layouts.setInitialLayout(parser.value(layoutOption));

    KeyRouter::setInstance(&router);
    Theme::setInstance(&theme);
    LayoutStore::setInstance(&layouts);
    Panel::setInstance(&panel);

    if (!panel.load(QUrl(QStringLiteral("qrc:/moarchy/qml/Main.qml")), &error))
        return fail(QStringLiteral("cannot load the keyboard QML"), error);

    MOARCHY_MARK("QML loaded and surface mapped");
    qCInfo(lcMain) << "ready";

    // First actual frame, not just "we asked for one". This is the number AC 36
    // is about -- everything above is work done before the compositor has
    // anything to show.
    // Both signals, because frameSwapped alone stopped firing usefully once
    // QSG_RENDER_LOOP=basic was set for the memory saving -- the AC 35 tuning
    // and the AC 33 measurement collided, and a run recorded no frame timings
    // at all while reporting nothing wrong. afterRendering is emitted by the
    // scene graph itself rather than by the swap, so it survives the change;
    // noteFrame only reports the first arrival after a press, so connecting
    // both cannot double-count.
    const auto noteFrame = [&router] { router.noteFrame(); };
    QObject::connect(panel.view(), &QQuickWindow::afterRendering, &app, noteFrame);
    QObject::connect(panel.view(), &QQuickWindow::frameSwapped, &app, noteFrame);

    QObject::connect(panel.view(), &QQuickWindow::afterRendering, &app, [] {
        static bool first = true;
        if (first) {
            first = false;
            MOARCHY_MARK("FIRST FRAME");
        }
    });

    return app.exec();
}
