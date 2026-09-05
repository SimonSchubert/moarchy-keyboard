#include "panel.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlContext>
#include <QQuickItem>
#include <QRegion>
#include <QScreen>

#include <LayerShellQt/Window>

Q_LOGGING_CATEGORY(lcPanel, "moarchy.panel")

namespace {

// Open question 2 in SPEC.md, answered with a default rather than left blocking:
// 4 character rows plus a function row, at a height that leaves the app usable
// above it. 300 of the screen's 720 logical pixels is ~42%, which is where
// Android and iOS both sit on a phone this shape.
constexpr int kPanelHeight = 300;

// The input region for a retracted keyboard: one pixel, in the corner.
//
// Two wrong answers here, and the failure mode of both is the same and is
// nasty -- an invisible wall across the bottom third of every app, swallowing
// touches while drawing nothing.
//
// An empty QRegion looks like it should mean "no input" and means the opposite:
// QWaylandWindow::setMask treats an empty mask as "unset the input region", and
// an unset input region is the WHOLE surface.
//
// A region outside the surface, QRegion(-1, -1, 1, 1), should work -- wl_region
// accepts rectangles outside the surface and the compositor intersects them --
// but it depends on Qt passing the rect through rather than clipping it to the
// window first, and if Qt ever clips, the result is an empty region and
// therefore the first bug again.
//
// So: one real pixel, inside the surface, which cannot be clipped to nothing.
// It costs a single dead pixel at the top-left corner of where the panel sits
// while the keyboard is down, and in exchange the behaviour does not depend on
// how a Qt internal treats out-of-bounds rectangles.
const QRegion &noInputRegion()
{
    static const QRegion region(0, 0, 1, 1);
    return region;
}

} // namespace

Panel::Panel(QObject *parent)
    : QObject(parent)
{
}

bool Panel::prepare(QString *error)
{
    m_view = new QQuickView;
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setColor(Qt::transparent);

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        *error = QStringLiteral("no screen");
        return false;
    }

    m_panelHeight = kPanelHeight;
    const int width = screen->geometry().width();

    LayerShellQt::Window *layerShell = LayerShellQt::Window::get(m_view);
    if (!layerShell) {
        *error = QStringLiteral("LayerShellQt returned no window -- was "
                                "useLayerShell() called before the view was built?");
        return false;
    }

    // Overlay, not Top.
    //
    // sway renders a fullscreen window ABOVE the Top layer, so on Top this
    // keyboard is invisible behind any fullscreen app -- and mobileomarchy
    // fullscreens things routinely (every TUI launched through
    // mobileomarchy-launch-tui, for one). The failure is total: a text field in
    // a fullscreen app gets a keyboard that is mapped, has an exclusive zone,
    // reports itself visible on D-Bus, and cannot be seen or touched.
    //
    // Found by accident -- a fullscreen test probe hid the keyboard and the
    // symptom looked like a keyboard bug, which it was, just not the one being
    // tested.
    layerShell->setLayer(LayerShellQt::Window::LayerOverlay);
    layerShell->setAnchors(LayerShellQt::Window::Anchors(
        LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight
        | LayerShellQt::Window::AnchorBottom));

    // Load-bearing. With any other value the compositor gives the keyboard
    // Wayland keyboard focus, which takes it away from the app being typed into
    // -- so the text field loses its cursor the moment the keyboard appears,
    // and a hardware keyboard stops reaching the app (AC 5).
    layerShell->setKeyboardInteractivity(
        LayerShellQt::Window::KeyboardInteractivityNone);

    layerShell->setScope(QStringLiteral("moarchy-keyboard"));
    layerShell->setExclusiveZone(0);
    layerShell->setDesiredSize(QSize(width, m_panelHeight));

    m_view->resize(width, m_panelHeight);
    return true;
}

bool Panel::load(const QUrl &source, QString *error)
{
    m_view->setSource(source);

    if (m_view->status() == QQuickView::Error) {
        QStringList messages;
        for (const QQmlError &qmlError : m_view->errors())
            messages.append(qmlError.toString());
        *error = messages.join(QLatin1String("; "));
        return false;
    }

    // Mapped once, here, and left mapped for the life of the process.
    m_view->show();
    applyVisibility();

    qCInfo(lcPanel) << "layer surface up:" << m_view->width() << "x" << m_panelHeight;
    return true;
}

void Panel::setShown(bool shown)
{
    if (m_shown == shown)
        return;
    m_shown = shown;
    applyVisibility();
}

void Panel::applyVisibility()
{
    if (!m_view)
        return;

    LayerShellQt::Window *layerShell = LayerShellQt::Window::get(m_view);
    if (layerShell)
        layerShell->setExclusiveZone(m_shown ? m_panelHeight : 0);

    m_view->setMask(m_shown ? QRegion(0, 0, m_view->width(), m_view->height())
                            : noInputRegion());

    QQuickItem *root = m_view->rootObject();
    if (root)
        root->setVisible(m_shown);

    // Logged because "the panel says it is shown and the screen says it is
    // black" needs the intermediate facts to be separable: whether the root
    // item exists, whether it is visible, and whether it has a size. A root
    // with zero width paints nothing while every flag above it reads correct.
    qCInfo(lcPanel) << "visibility ->" << (m_shown ? "shown" : "hidden")
                    << "root" << (root ? "yes" : "MISSING")
                    << "rootVisible" << (root && root->isVisible())
                    << "rootSize" << (root ? root->width() : -1)
                                  << "x" << (root ? root->height() : -1)
                    << "viewSize" << m_view->width() << "x" << m_view->height()
                    << "exposed" << m_view->isExposed();
}
