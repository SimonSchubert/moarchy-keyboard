#pragma once

#include <QObject>
#include <QQuickView>

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
class Panel : public QObject
{
    Q_OBJECT

public:
    explicit Panel(QObject *parent = nullptr);

    // Split in two on purpose. QML is evaluated by setSource(), so every
    // context property it reads has to be in place before then -- setting them
    // afterwards yields a first frame full of `undefined`, which for a `color`
    // property means black on black.
    //
    // prepare() must come after LayerShellQt::Shell::useLayerShell(): a surface
    // takes its layer-shell role at map time and cannot be converted later.
    bool prepare(QString *error);
    bool load(const QUrl &source, QString *error);

    bool isShown() const { return m_shown; }

    // Width of the left band reserved for mobileomarchy's back-edge gesture,
    // excluded from this keyboard's input region and inset from its keys.
    // See the note in panel.cpp for why it is not simply 16.
    int backEdgeInset() const { return m_backEdgeInset; }
    void setBackEdgeInset(int px) { m_backEdgeInset = px; }

    void setPanelHeight(int px) { m_panelHeight = px; }

    // Leaves the bottom band to mobileomarchy's gesture strip.
    void setBottomMargin(int px) { m_bottomMargin = px; }
    int panelHeight() const { return m_panelHeight; }

    QQuickView *view() const { return m_view; }

public Q_SLOTS:
    void setShown(bool shown);

private:
    void applyVisibility();

    QQuickView *m_view = nullptr;
    bool m_shown = false;
    bool m_mapped = false;
    int m_backEdgeInset = -1;   // -1 until prepare() applies the default
    int m_bottomMargin = -1;
    int m_panelHeight = -1;
};
