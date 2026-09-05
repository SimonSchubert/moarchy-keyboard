#pragma once

#include "qmlsingleton.h"

#include <QObject>
#include <QQmlEngine>
#include <QQuickView>
#include <QRect>

// The keyboard's one layer surface.
//
// It is created at startup and destroyed at exit, and NEVER in between. That is
// the whole point of this class, and AC 2 of the spec.
//
// The temptation is QWindow::hide() to retract the keyboard, which is wrong
// here: hiding a QWindow destroys the wl_surface, so every activate/deactivate
// cycle would build a new layer surface. wvkbd does exactly that, and because
// it destroys only the newest on deactivate it leaks one per cycle -- 12
// created against 11 destroyed in a single session on this phone -- and the
// leaked surface stays mapped, so the keyboard never goes away. That bug is why
// wvkbd is not the keyboard here, and reintroducing it would defeat the project.
//
// So retracting is three things that leave the surface alone:
//
//   exclusive zone -> 0    the focused window takes the space back
//   input region   -> off  touches fall through to whatever is underneath
//   root item      -> invisible, so the scene graph has nothing to draw
class Panel : public QObject, public MainOwnedSingleton<Panel>
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Panel)
    QML_SINGLETON


public:
    enum Mode {
        Hidden,   // nothing drawn, nothing takes touch, no space reserved
        Handle,   // only the restore handle: the keyboard was dismissed by hand
                  // while a text field is still focused, and the platform emits
                  // nothing when that field is tapped again -- so there has to
                  // be something on screen to tap
        Shown,    // the keyboard
    };
    Q_ENUM(Mode)

    // No default argument, so the QML engine cannot default-construct its own
    // instance instead of calling create(). See the static_assert below.
    explicit Panel(QObject *parent);

    // Split in two on purpose. QML is evaluated by setSource(), so every
    // context property it reads has to be in place before then -- setting them
    // afterwards yields a first frame full of `undefined`, which for a `color`
    // property means black on black.
    //
    // prepare() must come after LayerShellQt::Shell::useLayerShell(): a surface
    // takes its layer-shell role at map time and cannot be converted later.
    bool prepare(QString *error);
    bool load(const QUrl &source, QString *error);

    // Declared here rather than beside Q_OBJECT: the property type has to be a
    // name the compiler already knows, and Mode is declared just above.
    Q_PROPERTY(Mode mode READ mode NOTIFY modeChanged)

    // Read by Main.qml to inset the keys and the restore handle out of the
    // band. CONSTANT is accurate: main() applies any --gesture-strip-inset
    // before prepare(), and QML is not evaluated until load() after it.
    Q_PROPERTY(int stripInset READ stripInset CONSTANT)

    Mode mode() const { return m_mode; }
    bool isShown() const { return m_mode == Shown; }

    // Where QML has put the handle, in panel coordinates. Reported back so the
    // input region can be exactly that rectangle -- the rest of the surface has
    // to stay transparent to touch, or a dismissed keyboard becomes an
    // invisible wall across the bottom of the app.
    Q_INVOKABLE void setHandleRect(int x, int y, int width, int height);

    // The handle was tapped.
    Q_INVOKABLE void requestShow();

    // Width of the left band reserved for mobileomarchy's back-edge gesture,
    // excluded from this keyboard's input region and inset from its keys.
    // See the note in panel.cpp for why it is not simply 16.
    int backEdgeInset() const { return m_backEdgeInset; }
    void setBackEdgeInset(int px) { m_backEdgeInset = px; }

    void setPanelHeight(int px) { m_panelHeight = px; }

    // Height of the bottom band mobileomarchy's gesture strip owns. The surface
    // is grown by this and slid down by it, so the keys stay exactly where they
    // were and the only thing added is background under the home pill -- which
    // is the point: without it the keyboard stops at the top of the strip and a
    // band of wallpaper shows below the keys.
    //
    // Everything QML puts near the bottom has to be inset by it too, or it
    // lands under the pill. See the note in panel.cpp for why it is generous
    // rather than exact.
    int stripInset() const { return m_stripInset; }
    void setStripInset(int px) { m_stripInset = px; }

    // Leaves the bottom band to mobileomarchy's gesture strip.
    void setBottomMargin(int px) { m_bottomMargin = px; }
    int panelHeight() const { return m_panelHeight; }

    QQuickView *view() const { return m_view; }

public Q_SLOTS:
    void setMode(Mode mode);
    void setShown(bool shown) { setMode(shown ? Shown : Hidden); }

Q_SIGNALS:
    void modeChanged();
    void showRequested();

private:
    void applyVisibility();

    QQuickView *m_view = nullptr;
    Mode m_mode = Hidden;
    bool m_mapped = false;
    QRect m_handleRect;
    int m_backEdgeInset = -1;   // -1 until prepare() applies the default
    int m_bottomMargin = -1;
    int m_stripInset = -1;   // -1 until prepare() applies the default
    int m_panelHeight = -1;
};

// See MainOwnedSingleton in qmlsingleton.h: a default-constructible
// QML_SINGLETON is built by the engine instead of by create(), and QML then
// holds a different object from the one main() configured. That shipped once
// and drew every surface black, so it is a build error instead.
MOARCHY_SINGLETON_INVARIANT(Panel);
