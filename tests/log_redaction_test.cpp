// What was typed must never reach the log, even with every moarchy category
// switched on. The keyboard types passwords and sudo prompts, often into a
// terminal that declares no purpose, so the logging call sites carry the
// guarantee rather than isSensitive().
//
// No compositor is needed: unbound, InputMethod is inactive and
// VirtualKeyboard has no device, which are exactly the drop paths that used to
// print the input. Each line must also match a known template, so a new call
// site that appends the input fails here even if it is not the sentinel.

#include "inputmethod.h"
#include "keyrouter.h"
#include "virtualkeyboard.h"

#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStringList>
#include <QtTest>

// main.cpp owns this category. waylandconnection.cpp only declares it.
Q_LOGGING_CATEGORY(lcStartup, "moarchy.startup")

namespace {

QStringList captured;

void capture(QtMsgType, const QMessageLogContext &, const QString &message)
{
    captured << message;
}

const QString kSentinel = QStringLiteral("hunter2-s3ntinel");

}

class LogRedaction : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QLoggingCategory::setFilterRules(QStringLiteral("moarchy.*=true"));
        qInstallMessageHandler(capture);
    }

    void cleanupTestCase() { qInstallMessageHandler(nullptr); }

    void init() { captured.clear(); }

    void textAsKeycode()
    {
        InputMethod inputMethod;
        VirtualKeyboard keyboard;
        KeyRouter router(&inputMethod, &keyboard);
        router.markRelease();
        // Single characters on the base keymap take the keycode route.
        for (const QChar ch : kSentinel)
            router.sendText(QString(ch));
        verify(QStringLiteral("no virtual keyboard; dropping a key"));
    }

    void textWithoutKeycode()
    {
        InputMethod inputMethod;
        VirtualKeyboard keyboard;
        KeyRouter router(&inputMethod, &keyboard);
        router.markRelease();
        router.sendText(kSentinel);
        router.sendText(QStringLiteral("\U0001F511"));
        verify(QStringLiteral("no keycode for the typed text"));
    }

    void chord()
    {
        InputMethod inputMethod;
        VirtualKeyboard keyboard;
        KeyRouter router(&inputMethod, &keyboard);
        router.sendChord(QStringLiteral("\U0001F511"), VirtualKeyboard::Control);
        router.sendChord(QStringLiteral("s"), VirtualKeyboard::Control);
        verify(QStringLiteral("no keycode for the chord character"));
    }

    void namedKey()
    {
        InputMethod inputMethod;
        VirtualKeyboard keyboard;
        KeyRouter router(&inputMethod, &keyboard);
        router.markRelease();
        router.sendKey(QStringLiteral("Return"), VirtualKeyboard::NoModifiers);
        QVERIFY2(!captured.join(QLatin1Char('\n')).contains(QStringLiteral("Return")),
                 qPrintable(captured.join(QLatin1Char('\n'))));
        verify(QStringLiteral("latency: release to keycode on the wire"));
    }

    void commitWithoutTextInput()
    {
        InputMethod inputMethod;
        inputMethod.commitString(kSentinel);
        verify(QStringLiteral("commitString with no active text input"));
    }

private:
    // Every captured line matches a template, contains no sentinel, and the
    // expected failure was actually logged.
    void verify(const QString &expected)
    {
        static const QList<QRegularExpression> templates = {
            QRegularExpression(QStringLiteral(
                "^latency: release to (commit_string|keycode) on the wire \\d+ ms$")),
            QRegularExpression(QStringLiteral("^no virtual keyboard; dropping a key$")),
            QRegularExpression(QStringLiteral(
                "^no keycode for the typed text and no active text input; dropped$")),
            QRegularExpression(QStringLiteral(
                "^no keycode for the chord character; dropped$")),
            QRegularExpression(QStringLiteral(
                "^commitString with no active text input; dropping it$")),
        };
        const QString all = captured.join(QLatin1Char('\n'));
        QVERIFY2(!captured.isEmpty(), "nothing was logged; the drop paths were not reached");
        QVERIFY2(all.contains(expected), qPrintable(all));
        for (const QString &line : std::as_const(captured)) {
            QVERIFY2(!line.contains(kSentinel), qPrintable(line));
            const bool known = std::any_of(templates.cbegin(), templates.cend(),
                                           [&](const QRegularExpression &re) {
                                               return re.match(line).hasMatch();
                                           });
            QVERIFY2(known, qPrintable(QStringLiteral("unexpected log line: ") + line));
        }
    }
};

QTEST_GUILESS_MAIN(LogRedaction)
#include "log_redaction_test.moc"
