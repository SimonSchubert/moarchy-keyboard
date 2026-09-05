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

// A one-pixel region entirely outside the surface.
//
// Not an empty QRegion, which looks like it should mean "no input" and means
// the opposite: QWaylandWindow::setMask treats an empty mask as "unset the
// input region", and an unset input region is the whole surface. So the
// retracted keyboard would still swallow every touch along the bottom of the
// screen while drawing nothing -- an invisible wall across the app underneath.
const QRegion &noInputRegion()
{
    static const QRegion region(-1, -1, 1, 1);
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

    layerShell->setLayer(LayerShellQt::Window::LayerTop);
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

    if (QQuickItem *root = m_view->rootObject())
        root->setVisible(m_shown);
}
