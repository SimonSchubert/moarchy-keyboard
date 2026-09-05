#pragma once

#include <QList>
#include <QQmlEngine>
#include <QString>
#include <QStringList>

// What a key IS, written down once.
//
// It used to be written down five times. A layout went JSON -> QVariantMap ->
// an untyped `var` in QML, and then KeyCap.qml, KeyRow.qml, Keyboard.qml,
// --check-layouts and allCharacters() each re-derived the same fields with
// their own `!== undefined ?:` or `value(key, default)` chains. Five
// re-derivations of one shape, and nothing keeping them honest: the validator
// and the renderer were free to disagree about a default, and no test could
// have caught it.
//
// So the defaults are applied ONCE, by LayoutParser, and everything downstream
// reads a field that is always present. `width` is 1.0 rather than undefined.
// `label` is already resolved. `type` is an enum rather than a string that
// three files spell independently.
//
// Value types (Q_GADGET + QML_VALUE_TYPE), not QObjects. A QObject per key
// would add 30-45 allocations to a process whose memory criterion already
// fails, plus ownership plumbing to stop the JS engine collecting them -- in
// exchange for a reference identity nothing here wants. These are immutable
// parse products, so every property is CONSTANT: there is no setter to want,
// and moc's generated setter would need an operator== on QList<KeySpec> that a
// gadget does not have.
//
// The point of the typing, beyond the de-duplication: qmllint can resolve
// `spec.width` and cannot resolve a field on a `var`. On this program an
// unresolvable name becomes `undefined`, an `undefined` assigned to a `color`
// is #000000, and the result is a black glyph on a black key with nothing
// logged. scripts/lint.sh fails the build on any warning; this puts the key
// specs inside its reach.

namespace KeyType {
Q_NAMESPACE
QML_ELEMENT

enum Kind {
    Character,   // emits its text
    Space,
    Action,      // a named key: Escape, BackSpace, Left, ...
    Modifier,    // shift / ctrl / alt
    Layout,      // switches to another layout
    Unknown,     // the JSON said something this build does not know
};
Q_ENUM_NS(Kind)
} // namespace KeyType

namespace KeyModifier {
Q_NAMESPACE
QML_ELEMENT

// NOT `None`: X11 defines it as a macro, and this header is compiled in a
// translation unit that reaches xkbcommon.
enum Kind {
    NoModifier,
    Shift,
    Ctrl,
    Alt,
    Unknown,
};
Q_ENUM_NS(Kind)
} // namespace KeyModifier

class KeySpec
{
    Q_GADGET
    QML_VALUE_TYPE(keyspec)

    Q_PROPERTY(KeyType::Kind type READ type CONSTANT)
    Q_PROPERTY(QString text READ text CONSTANT)
    // text.toUpper(), precomputed. Saves a JS toUpperCase() per key label on
    // every shift toggle and one more per keystroke on the emit path.
    //
    // QString::toUpper() is not JavaScript's String.prototype.toUpperCase().
    // They agree on every character any shipped layout can emit -- ASCII
    // letters, symbols, space -- and long-press alternates are never shifted,
    // since commitAlternate() sends them raw. A future non-Latin layout is
    // where the two could diverge, so this is the line to remember.
    Q_PROPERTY(QString shiftedText READ shiftedText CONSTANT)
    // Already resolved: the explicit `label` if the JSON had one, else `text`.
    Q_PROPERTY(QString label READ label CONSTANT)
    Q_PROPERTY(QString shiftedLabel READ shiftedLabel CONSTANT)
    Q_PROPERTY(QString glyph READ glyph CONSTANT)
    // The key asked to be drawn from Omarchy's icon font. Whether it CAN be is
    // a runtime question -- the font may not have loaded -- so that part stays
    // in the QML.
    Q_PROPERTY(bool iconFont READ iconFont CONSTANT)
    Q_PROPERTY(QStringList alt READ alt CONSTANT)
    Q_PROPERTY(bool hasAlternates READ hasAlternates CONSTANT)
    Q_PROPERTY(qreal width READ width CONSTANT)
    Q_PROPERTY(bool repeats READ repeats CONSTANT)
    Q_PROPERTY(bool accent READ accent CONSTANT)
    Q_PROPERTY(KeyModifier::Kind modifier READ modifier CONSTANT)
    // The named key, for type == Action.
    Q_PROPERTY(QString key READ key CONSTANT)
    // The target, for type == Layout.
    Q_PROPERTY(QString layout READ layout CONSTANT)

public:
    KeyType::Kind type() const { return m_type; }
    QString text() const { return m_text; }
    QString shiftedText() const { return m_shiftedText; }
    QString label() const { return m_label; }
    QString shiftedLabel() const { return m_shiftedLabel; }
    QString glyph() const { return m_glyph; }
    bool iconFont() const { return m_iconFont; }
    QStringList alt() const { return m_alt; }
    bool hasAlternates() const { return !m_alt.isEmpty(); }
    qreal width() const { return m_width; }
    bool repeats() const { return m_repeats; }
    bool accent() const { return m_accent; }
    KeyModifier::Kind modifier() const { return m_modifier; }
    QString key() const { return m_key; }
    QString layout() const { return m_layout; }

    // Public because LayoutParser is the only thing that fills one in, and a
    // dozen setters to say so would be noise.
    KeyType::Kind m_type = KeyType::Character;
    KeyModifier::Kind m_modifier = KeyModifier::NoModifier;
    QString m_text;
    QString m_shiftedText;
    QString m_label;
    QString m_shiftedLabel;
    QString m_glyph;
    QString m_key;
    QString m_layout;
    QStringList m_alt;
    qreal m_width = 1.0;
    bool m_iconFont = false;
    bool m_repeats = false;
    bool m_accent = false;
};

class KeyRowSpec
{
    Q_GADGET
    QML_VALUE_TYPE(keyrow)

    Q_PROPERTY(QList<KeySpec> keys READ keys CONSTANT)
    // The row's total width in key-widths. KeyRow.qml computed this in an
    // interpreted JS loop, per row, on every layout switch -- into a property
    // that nothing ever read.
    Q_PROPERTY(qreal units READ units CONSTANT)

public:
    QList<KeySpec> keys() const { return m_keys; }
    qreal units() const { return m_units; }

    QList<KeySpec> m_keys;
    qreal m_units = 0.0;
};

class LayoutSpec
{
    Q_GADGET
    QML_VALUE_TYPE(keylayout)

    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString label READ label CONSTANT)
    Q_PROPERTY(QList<KeyRowSpec> rows READ rows CONSTANT)
    // How many key-widths the widest row spans -- NOT a hardcoded 10.
    //
    // The letters, symbols and terminal layouts are all 10 wide, so a constant
    // looked right and was: until the numeric layout, which is 4 wide. At
    // width/10 its keys came out 36px in a 360px panel, huddled in the middle
    // of the screen with 108px of dead space either side. A layout may declare
    // `columns` explicitly; otherwise it is measured here.
    Q_PROPERTY(qreal columns READ columns CONSTANT)
    // Parsed, and has at least one row with at least one key. A
    // default-constructed LayoutSpec is the "no such layout, or a broken one"
    // value, which is what QML gets when a lookup fails.
    Q_PROPERTY(bool valid READ valid CONSTANT)

public:
    QString name() const { return m_name; }
    QString label() const { return m_label; }
    QList<KeyRowSpec> rows() const { return m_rows; }
    qreal columns() const { return m_columns; }
    bool valid() const { return m_valid; }

    QString m_name;
    QString m_label;
    QList<KeyRowSpec> m_rows;
    qreal m_columns = 1.0;
    bool m_valid = false;
};
