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

// 220 of the screen's 720 logical pixels, ~31%.
//
// Was 300, which made the keys 36 wide by 75 tall on a four-row layout -- more
// than twice as tall as wide, which reads as a stretched keyboard rather than a
// keyboard. Android sits nearer 40x48 on a 360dp-wide screen. With the side
// margins taking the keys to 32 wide, 200 gives 50 on a four-row layout and 40
// on the terminal layout's five -- and hands 100px back to the app.
constexpr int kDefaultPanelHeight = 200;

// Zero, and the reason is worth writing down because a margin here looks like
// the obvious fix and is not.
//
// The gesture strip -- mobileomarchy's home pill -- anchors to the bottom of the
// same Overlay layer with ExclusionMode.Auto. wlroots arranges layer surfaces in
// map order, and each surface's exclusive zone shrinks the usable area for the
// ones arranged AFTER it. Map first and you take the screen edge.
//
// So when this keyboard mapped at startup it was arranged first, its exclusive
// zone claimed the bottom of the output, and the strip was then placed in what
// was left -- floating above the keyboard, which is exactly backwards. The pill
// belongs at the screen edge under the thumb.
//
// A bottom margin does not fix that. It moves this surface up but leaves the
// reservation in place, so the strip still lands above it and the margin shows
// as a band of wallpaper below the keys. That was tried, and that is what it
// looked like.
//
// The fix is ordering, not geometry: see the deferred map in Panel::setShown.
constexpr int kDefaultBottomMargin = 0;

// The left band mobileomarchy's back-edge gesture owns: Overlay layer, left,
// full height, Style.space(16), and ExclusionMode.Ignore -- so it reserves no
// space and overlaps whatever else is there.
//
// Two surfaces in the same layer wanting the same pixels is decided by map
// order, which is a race. Excluding the band from this keyboard's input region
// settles it: a masked-out area falls through to the next surface in the layer,
// which is exactly how mobileomarchy.shade coexists with the same gesture (see
// openRegion in Shade.qml).
//
// It matters more than a stray gesture would: the back edge is how the keyboard
// is dismissed. Swallow it and there is no gesture to put the keyboard away.
//
// The gesture STRIP along the bottom needs no such treatment -- it is
// ExclusionMode.Auto, so it reserves its 20px and this bottom-anchored surface
// is placed above it rather than over it. Confirmed on the device: the panel
// sits at y 405..705 of a 720px screen, not 420..720.
//
// The width is NOT 16 logical pixels, which is the trap. mobileomarchy declares
// it as Style.space(16), and Style.space rounds a *scaled* value -- the scale
// comes from the theme's shell.toml, so the real width tracks the font size and
// is about 18 at the default scale of ~1.15 (its 20px gesture strip reserves
// 23). Masking 16 leaves a two-pixel band where a key is drawn, looks tappable,
// and is silently swallowed by the gesture instead: the same failure this mask
// exists to prevent, just narrower and therefore harder to diagnose.
//
// So the default is deliberately generous rather than exact, and settable with
// --back-edge-inset for a theme that scales further. A couple of pixels of key
// width is a cheap price for never having a dead stripe.
constexpr int kDefaultBackEdgeInset = 20;

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
    if (m_backEdgeInset < 0)
        m_backEdgeInset = kDefaultBackEdgeInset;
    if (m_panelHeight < 0)
        m_panelHeight = kDefaultPanelHeight;
    if (m_bottomMargin < 0)
        m_bottomMargin = kDefaultBottomMargin;

    m_view = new QQuickView;
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setColor(Qt::transparent);

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        *error = QStringLiteral("no screen");
        return false;
    }

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
    // Sit above the gesture strip rather than on top of it.
    layerShell->setMargins(QMargins(0, 0, 0, m_bottomMargin));
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

    // NOT shown here.
    //
    // The surface is mapped on first use instead, by setShown. Mapping at
    // startup put this keyboard ahead of mobileomarchy's gesture strip in the
    // compositor's arrangement order, and a layer surface arranged first takes
    // the screen edge -- which stranded the home pill above the keyboard.
    // Deferring the map until a text field is focused guarantees the shell's
    // surfaces are already arranged, so the strip keeps the edge and this
    // keyboard is placed above it.
    //
    // AC 2 is unaffected and still exactly true: one surface created, none ever
    // destroyed. It says the surface outlives every activate/deactivate cycle,
    // not that it must exist before the first one.
    qCInfo(lcPanel) << "panel prepared:" << m_view->width() << "x" << m_panelHeight
                    << "(surface maps on first show)";
    return true;
}

void Panel::setShown(bool shown)
{
    if (m_shown == shown)
        return;
    m_shown = shown;

    // First show maps the surface. Every later one only changes the input
    // region, the exclusive zone and the root item's visibility -- the surface
    // itself is never unmapped again.
    if (m_shown && !m_mapped) {
        m_mapped = true;
        m_view->show();
        qCInfo(lcPanel) << "layer surface up:" << m_view->width() << "x" << m_panelHeight;
    }

    applyVisibility();
}

void Panel::applyVisibility()
{
    if (!m_view)
        return;

    LayerShellQt::Window *layerShell = LayerShellQt::Window::get(m_view);
    // The zone has to cover the margin too, or the app under the keyboard is
    // resized to the wrong height and the last row sits over its content.
    if (layerShell)
        layerShell->setExclusiveZone(m_shown ? m_panelHeight + m_bottomMargin : 0);

    // Everything except the back-edge band. Not the whole surface: see
    // kBackEdgeInset.
    m_view->setMask(m_shown ? QRegion(m_backEdgeInset, 0,
                                      m_view->width() - m_backEdgeInset,
                                      m_view->height())
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
