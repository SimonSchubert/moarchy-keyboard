#include "inputmethod.h"
#include "keyrouter.h"
#include "layoutstore.h"
#include "oskservice.h"
#include "panel.h"
#include "theme.h"
#include "virtualkeyboard.h"
#include "waylandconnection.h"

#include <QCommandLineParser>
#include <QDir>
#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QStandardPaths>
#include <QTextStream>

Q_LOGGING_CATEGORY(lcMain, "moarchy.main")

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
    // No LayerShellQt::Shell::useLayerShell() here: deprecated since 6.6 and
    // unnecessary since 6.5, because LayerShellQt::Window::get() -- which
    // Panel::prepare() calls before the view is ever shown -- now arranges the
    // layer-shell role itself.
    QGuiApplication app(argc, argv);
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

    parser.process(app);

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

            Theme theme;
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
        LayoutStore store;
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
    LayoutStore layouts;

    VirtualKeyboard virtualKeyboard;
    if (!virtualKeyboard.init(&connection, layouts.allCharacters(), &error))
        return fail(QStringLiteral("virtual keyboard unavailable"), error);

    InputMethod inputMethod;
    if (!inputMethod.init(&connection, &error))
        return fail(QStringLiteral("input method unavailable"), error);

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

    Theme theme;
    theme.start(parser.value(colorsOption));

    OskService osk;
    if (!osk.registerOnBus(&error)) {
        // Not fatal: the keyboard still raises and retracts itself, which is how
        // it is used 99% of the time. Only the manual toggle is lost, and saying
        // so beats dying.
        qCWarning(lcMain).noquote() << "no D-Bus toggle:" << error;
    }

    // --- Panel --------------------------------------------------------------
    Panel panel;

    // Visibility has two inputs: what the compositor says about text focus, and
    // what someone asked for over D-Bus. The manual request wins until focus
    // changes, so `mobileomarchy-toggle-keyboard` can push the keyboard out of
    // the way without moving focus -- and the next time you tap a text field it
    // behaves normally again rather than staying stuck down.
    static bool manualOverride = false;
    static bool manualWanted = false;

    auto applyVisibility = [&] {
        const bool wanted = manualOverride ? manualWanted : inputMethod.isActive();
        panel.setShown(wanted);
        osk.setVisible(wanted);
    };

    QObject::connect(&inputMethod, &InputMethod::activeChanged, &app, [&] {
        manualOverride = false;
        applyVisibility();
    });

    QObject::connect(&osk, &OskService::visibleChanged, &app, [&] {
        if (osk.visible() == panel.isShown())
            return;
        manualOverride = true;
        manualWanted = osk.visible();
        applyVisibility();
    });

    // --- QML ----------------------------------------------------------------
    if (!panel.prepare(&error))
        return fail(QStringLiteral("cannot build the panel"), error);

    // Singletons, not context properties. See the note above class Theme for
    // why: qmllint can resolve a declared singleton and cannot see a context
    // property, and on this program an unresolvable colour renders as black on
    // black with nothing logged.
    layouts.setInitialLayout(parser.value(layoutOption));

    KeyRouter::setInstance(&router);
    Theme::setInstance(&theme);
    LayoutStore::setInstance(&layouts);

    if (!panel.load(QUrl(QStringLiteral("qrc:/moarchy/qml/Main.qml")), &error))
        return fail(QStringLiteral("cannot load the keyboard QML"), error);

    qCInfo(lcMain) << "ready";
    return app.exec();
}
