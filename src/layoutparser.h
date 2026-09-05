#pragma once

#include "keyspec.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// Turns one layout file into one LayoutSpec, applying every default exactly
// once so that nothing downstream has to.
//
// It owns the judgements that are about the FORMAT: is this JSON, does it have
// rows, does a row have keys, is that a type this build knows, does a key that
// is supposed to emit something actually emit something. It deliberately does
// not own the cross-cutting ones -- whether a `layout` key names a layout that
// exists, or whether an `action` key names a keycode -- because those need the
// set of layouts and the xkb tables respectively, and dragging either in here
// would put Wayland-adjacent headers behind the JSON parser.
//
// `problems` is reporting, not gating. `valid` means "parsed, and has at least
// one row with at least one key", so a key with an unrecognised modifier is
// reported and still drawn. Promoting a problem to invalidity would let one
// typo in a user layout blank the whole keyboard, which is a far worse failure
// than the typo.
namespace LayoutParser {

// `name` is the layout's name (its filename stem), used for nothing but the
// spec itself; `problems` is appended to, never cleared.
LayoutSpec parse(const QByteArray &json, const QString &name, QStringList *problems);

// The JSON spelling of a type, for messages. The parser reads these strings;
// this is the only other place that knows them.
QString spellingOf(KeyType::Kind type);

} // namespace LayoutParser
