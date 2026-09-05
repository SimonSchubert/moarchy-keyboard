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

// The bottom band mobileomarchy's gesture strip owns, which this surface now
// draws *under* rather than stopping above.
//
// Not a contradiction of the essay above, and the sign is the whole difference.
// That one is about a POSITIVE bottom margin, which moves this surface up and
// leaves the reservation where it was -- wallpaper below the keys, which is the
// bug. This is a NEGATIVE one of the same size as the growth applied to the
// surface, so the surface slides down by exactly what it grew: the keys occupy
// the top panelHeight and do not move by a pixel, and everything gained is
// background running to the screen edge behind the home pill.
//
// wlroots makes this legal rather than a trick: layer-shell margins are
// int32_t, and for a bottom-anchored surface it computes
// `box.y = bounds.y + bounds.height - box.height - margin.bottom` with no
// clamping. sway delegates to it and adds no validation of its own.
//
// The exclusive zone deliberately does NOT include this. The strip reserves its
// own band; counting it here as well would push every app up twice.
//
// No mask goes with it, for the same reason kDefaultBackEdgeInset is now 0:
// this surface is on Top and the strip is on Overlay, so the strip is above it
// and takes those touches first. The pill keeps working with no input region
// of ours involved.
//
// Generous rather than exact, and too small is the failure that matters: the
// keys would sit over the strip's top edge and those touches would vanish with
// nothing on screen to explain it. Too large costs only background nobody can
// see. mobileomarchy declares the strip as Style.space(20), and Style.space
// rounds a *scaled* value whose scale comes from the theme's shell.toml --
// measured 20 on the default theme, reported as 23 on a larger one.
constexpr int kDefaultStripInset = 24;

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
// The gesture STRIP along the bottom needs no masking either, and for the same
// reason this inset is 0: Overlay sits above Top, so both gesture surfaces take
// their touches before this one sees them. It does get a geometry treatment
// though -- see kDefaultStripInset, which runs this surface's background under
// the strip so there is no band of wallpaper below the keys. The panel now
// spans y 405..720 of a 720px screen rather than 405..700.
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
constexpr int kDefaultBackEdgeInset = 0;

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
    if (m_stripInset < 0)
        m_stripInset = kDefaultStripInset;

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

    // Top, not Overlay -- and the reason is the gesture strip, not this
    // keyboard.
    //
    // Exclusive zones resolve layer by layer, Overlay downwards. The strip is
    // an Overlay surface, so from Top it is resolved FIRST and keeps the screen
    // edge for free, with no dependence on map order. That is how squeekboard
    // behaved and why the home pill was always at the bottom before this
    // keyboard existed.
    //
    // Moving to Overlay put both surfaces in one layer, where the order is map
    // order, and the guarantee evaporated: this keyboard took the edge and
    // stranded the pill at 497..520 between the app and the keys. No geometry
    // fixes that -- a bottom margin moves the surface but not the reservation,
    // so the strip still lands above and the margin shows as wallpaper. It was
    // tried; that is what it looked like.
    //
    // The cost, and it is a real one: sway renders a fullscreen window above
    // Top, so a text field in a fullscreen app gets a keyboard that is mapped,
    // reserves space, reports itself visible on D-Bus, and cannot be seen. That
    // was equally true of squeekboard, so it is a pre-existing limitation
    // rather than a regression -- but it is worth fixing properly one day, by
    // switching to Overlay only while a fullscreen window is focused
    // (zwlr_layer_surface_v1.set_layer since version 2 allows it without
    // remapping).
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
    // Sit above the gesture strip rather than on top of it.
    // Grown by the strip inset and slid down by it, so the keys land exactly
    // where they did before and only background is added underneath. See
    // kDefaultStripInset.
    const int surfaceHeight = m_panelHeight + m_stripInset;
    layerShell->setMargins(QMargins(0, 0, 0, m_bottomMargin - m_stripInset));
    layerShell->setDesiredSize(QSize(width, surfaceHeight));

    m_view->resize(width, surfaceHeight);
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

void Panel::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;

    // First appearance of any kind maps the surface. Every later change only
    // adjusts the input region, the exclusive zone and what QML draws -- the
    // surface itself is never unmapped again.
    if (m_mode != Hidden && !m_mapped) {
        m_mapped = true;
        m_view->show();
        qCInfo(lcPanel) << "layer surface up:" << m_view->width() << "x" << m_panelHeight;
    }

    applyVisibility();
    Q_EMIT modeChanged();
}

void Panel::setHandleRect(int x, int y, int width, int height)
{
    const QRect rect(x, y, width, height);
    if (rect == m_handleRect)
        return;
    m_handleRect = rect;
    qCDebug(lcPanel) << "handle rect ->" << rect;
    if (m_mode == Handle)
        applyVisibility();
}

void Panel::requestShow()
{
    Q_EMIT showRequested();
}

void Panel::applyVisibility()
{
    if (!m_view)
        return;

    LayerShellQt::Window *layerShell = LayerShellQt::Window::get(m_view);
    // Only the full keyboard reserves space. The handle floats over the app --
    // it is a few dozen pixels and resizing every window for it would be worse
    // than the problem it solves.
    //
    // + m_stripInset, and leaving it out is a silent 24px bug. sway reduces the
    // usable area by `exclusive_zone + margin.bottom`, not by the zone alone,
    // and prepare() gives this surface a NEGATIVE bottom margin of exactly
    // m_stripInset so its background can run under the gesture strip. Without
    // the compensation the reservation comes out at panelHeight - stripInset:
    // measured 176 against a 200px panel, which let the drawer's surface settle
    // 24px over the top key row and clip it. The keys themselves never moved,
    // which is what made it look like a paint bug rather than an arrangement
    // one.
    //
    // Reads the same for a non-zero --bottom-margin: the wanted reduction is
    // panelHeight + bottomMargin, the applied margin is bottomMargin -
    // stripInset, and the difference is panelHeight + stripInset either way.
    if (layerShell)
        layerShell->setExclusiveZone(m_mode == Shown ? m_panelHeight + m_stripInset : 0);

    // The input region follows the mode exactly. In Handle mode it is the
    // handle and nothing else: the rest of the surface is still mapped and
    // still transparent, and if it took touches it would be an invisible wall
    // across the bottom of whatever is underneath.
    switch (m_mode) {
    case Shown:
        m_view->setMask(QRegion(m_backEdgeInset, 0,
                                m_view->width() - m_backEdgeInset,
                                m_view->height()));
        break;
    case Handle:
        m_view->setMask(m_handleRect.isValid() ? QRegion(m_handleRect)
                                               : noInputRegion());
        break;
    case Hidden:
        m_view->setMask(noInputRegion());
        break;
    }

    QQuickItem *root = m_view->rootObject();
    if (root)
        root->setVisible(m_mode != Hidden);

    // Logged because "the panel says it is shown and the screen says it is
    // black" needs the intermediate facts to be separable: whether the root
    // item exists, whether it is visible, and whether it has a size. A root
    // with zero width paints nothing while every flag above it reads correct.
    qCInfo(lcPanel) << "visibility ->"
                    << (m_mode == Shown ? "shown" : m_mode == Handle ? "handle" : "hidden")
                    << "root" << (root ? "yes" : "MISSING")
                    << "rootVisible" << (root && root->isVisible())
                    << "rootSize" << (root ? root->width() : -1)
                                  << "x" << (root ? root->height() : -1)
                    << "viewSize" << m_view->width() << "x" << m_view->height()
                    << "exposed" << m_view->isExposed();
}
