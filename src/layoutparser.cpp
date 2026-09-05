#include "layoutparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace {

KeyType::Kind typeFor(const QString &spelling)
{
    if (spelling == QLatin1String("character")) return KeyType::Character;
    if (spelling == QLatin1String("space"))     return KeyType::Space;
    if (spelling == QLatin1String("action"))    return KeyType::Action;
    if (spelling == QLatin1String("modifier"))  return KeyType::Modifier;
    if (spelling == QLatin1String("layout"))    return KeyType::Layout;
    return KeyType::Unknown;
}

// The characters the layouts label these keys with. Kept as the source of
// truth so an existing layout -- including a user's -- gets the drawn icons
// with no edit at all; `"icon": "enter"` says the same thing explicitly, and
// `"icon": "none"` opts out and draws the character as text.
KeyIcon::Kind iconForLabel(const QString &label)
{
    if (label.size() != 1)
        return KeyIcon::NoIcon;
    switch (label.at(0).unicode()) {
    case 0x21B5: return KeyIcon::Enter;       // U+21B5 RETURN
    case 0x232B: return KeyIcon::Backspace;   // U+232B ERASE TO THE LEFT
    case 0x21E7: return KeyIcon::Shift;       // U+21E7 UPWARDS WHITE ARROW
    case 0x2190: return KeyIcon::ArrowLeft;
    case 0x2192: return KeyIcon::ArrowRight;
    case 0x2191: return KeyIcon::ArrowUp;
    case 0x2193: return KeyIcon::ArrowDown;
    default:     return KeyIcon::NoIcon;
    }
}

KeyIcon::Kind iconFor(const QString &spelling, bool *known)
{
    *known = true;
    if (spelling == QLatin1String("none"))      return KeyIcon::NoIcon;
    if (spelling == QLatin1String("enter"))     return KeyIcon::Enter;
    if (spelling == QLatin1String("backspace")) return KeyIcon::Backspace;
    if (spelling == QLatin1String("shift"))     return KeyIcon::Shift;
    if (spelling == QLatin1String("left"))      return KeyIcon::ArrowLeft;
    if (spelling == QLatin1String("right"))     return KeyIcon::ArrowRight;
    if (spelling == QLatin1String("up"))        return KeyIcon::ArrowUp;
    if (spelling == QLatin1String("down"))      return KeyIcon::ArrowDown;
    *known = false;
    return KeyIcon::NoIcon;
}

KeyModifier::Kind modifierFor(const QString &spelling)
{
    if (spelling == QLatin1String("shift")) return KeyModifier::Shift;
    if (spelling == QLatin1String("ctrl"))  return KeyModifier::Ctrl;
    if (spelling == QLatin1String("alt"))   return KeyModifier::Alt;
    return KeyModifier::Unknown;
}

KeySpec parseKey(const QJsonObject &object, QStringList *problems)
{
    KeySpec key;

    const QString typeSpelling =
        object.value(QLatin1String("type")).toString(QStringLiteral("character"));
    key.m_type = typeFor(typeSpelling);
    if (key.m_type == KeyType::Unknown)
        problems->append(QStringLiteral("unknown key type \"%1\"").arg(typeSpelling));

    key.m_text = object.value(QLatin1String("text")).toString();
    key.m_shiftedText = key.m_text.toUpper();

    // `label` present-but-empty is not the same as absent: the space bar
    // declares "label": "" precisely so that it draws no glyph, and falling
    // back to its text would print a space-shaped nothing anyway -- but the
    // distinction matters for every other key, so it is honoured rather than
    // collapsed into isEmpty().
    if (object.contains(QLatin1String("label"))) {
        key.m_label = object.value(QLatin1String("label")).toString();
        key.m_shiftedLabel = key.m_label;
    } else {
        key.m_label = key.m_text;
        key.m_shiftedLabel = key.m_shiftedText;
    }

    key.m_glyph = object.value(QLatin1String("glyph")).toString();
    key.m_iconFont = object.value(QLatin1String("font")).toString() == QLatin1String("omarchy");

    // An explicit `icon` wins; otherwise the label says which one.
    if (object.contains(QLatin1String("icon"))) {
        bool known = false;
        const QString spelling = object.value(QLatin1String("icon")).toString();
        key.m_icon = iconFor(spelling, &known);
        if (!known)
            problems->append(QStringLiteral("unknown icon \"%1\"").arg(spelling));
    } else {
        key.m_icon = iconForLabel(key.m_label);
    }

    const QJsonArray alternates = object.value(QLatin1String("alt")).toArray();
    key.m_alt.reserve(alternates.size());
    for (const QJsonValue &alternate : alternates)
        key.m_alt.append(alternate.toString());

    // Only when the key actually declares one: a missing field reads as 0.0
    // through toDouble(), which would give a zero-width key rather than the
    // single unit every layout assumes.
    if (object.contains(QLatin1String("width")))
        key.m_width = object.value(QLatin1String("width")).toDouble(1.0);

    key.m_repeats = object.value(QLatin1String("repeats")).toBool();
    key.m_accent = object.value(QLatin1String("accent")).toBool();
    key.m_key = object.value(QLatin1String("key")).toString();
    key.m_layout = object.value(QLatin1String("layout")).toString();

    if (key.m_type == KeyType::Modifier) {
        const QString spelling = object.value(QLatin1String("modifier")).toString();
        key.m_modifier = modifierFor(spelling);
        if (key.m_modifier == KeyModifier::Unknown)
            problems->append(QStringLiteral("unknown modifier \"%1\"").arg(spelling));
    }

    // A character or space key with nothing to emit draws fine and does
    // nothing at all when tapped, which is invisible without this check.
    if ((key.m_type == KeyType::Character || key.m_type == KeyType::Space)
        && key.m_text.isEmpty()) {
        problems->append(QStringLiteral("a %1 key emits nothing").arg(typeSpelling));
    }

    return key;
}

} // namespace

QString LayoutParser::spellingOf(KeyType::Kind type)
{
    switch (type) {
    case KeyType::Character: return QStringLiteral("character");
    case KeyType::Space:     return QStringLiteral("space");
    case KeyType::Action:    return QStringLiteral("action");
    case KeyType::Modifier:  return QStringLiteral("modifier");
    case KeyType::Layout:    return QStringLiteral("layout");
    case KeyType::Unknown:   break;
    }
    return QStringLiteral("unknown");
}

LayoutSpec LayoutParser::parse(const QByteArray &json, const QString &name,
                               QStringList *problems)
{
    LayoutSpec spec;
    spec.m_name = name;

    QJsonParseError parseError {};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        problems->append(QStringLiteral("not valid JSON: %1 at offset %2")
                             .arg(parseError.errorString())
                             .arg(parseError.offset));
        return spec;
    }

    const QJsonObject object = document.object();
    spec.m_label = object.value(QLatin1String("label")).toString();

    const QJsonArray rows = object.value(QLatin1String("rows")).toArray();
    if (rows.isEmpty()) {
        problems->append(QStringLiteral("no rows"));
        return spec;
    }

    int keyCount = 0;
    spec.m_rows.reserve(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const QJsonArray keys = rows.at(r).toObject().value(QLatin1String("keys")).toArray();
        if (keys.isEmpty())
            problems->append(QStringLiteral("row %1: no keys").arg(r));

        KeyRowSpec row;
        row.m_keys.reserve(keys.size());
        for (const QJsonValue &key : keys) {
            row.m_keys.append(parseKey(key.toObject(), problems));
            row.m_units += row.m_keys.constLast().width();
            ++keyCount;
        }
        spec.m_rows.append(row);
    }

    // An explicit `columns` wins; otherwise the widest row is the measure, with
    // a floor of 1 so a pathological layout cannot divide by zero.
    const double declared = object.value(QLatin1String("columns")).toDouble(0.0);
    if (declared > 0.0) {
        spec.m_columns = declared;
    } else {
        qreal widest = 1.0;
        for (const KeyRowSpec &row : std::as_const(spec.m_rows))
            widest = qMax(widest, row.units());
        spec.m_columns = widest;
    }

    spec.m_valid = keyCount > 0;
    return spec;
}
