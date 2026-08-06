#include "overlaysurface.h"

#include "inputregion.h"

#include <LayerShellQt/Window>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QLoggingCategory>
#include <QQuickWindow>

Q_LOGGING_CATEGORY(logOverlay, "openpet.overlay")

namespace {

using LayerWindow = LayerShellQt::Window;

// LayerShellQt объявляет флаги без Q_DECLARE_OPERATORS_FOR_FLAGS,
// поэтому `|` между значениями enum пришлось бы приводить вручную.
LayerWindow::Anchors combine(LayerWindow::Anchor first, LayerWindow::Anchor second)
{
    return LayerWindow::Anchors(first) | second;
}

LayerWindow::Anchors anchorsFor(OverlaySurface::Corner corner)
{
    switch (corner) {
    case OverlaySurface::Corner::BottomLeft:
        return combine(LayerWindow::AnchorBottom, LayerWindow::AnchorLeft);
    case OverlaySurface::Corner::TopRight:
        return combine(LayerWindow::AnchorTop, LayerWindow::AnchorRight);
    case OverlaySurface::Corner::TopLeft:
        return combine(LayerWindow::AnchorTop, LayerWindow::AnchorLeft);
    case OverlaySurface::Corner::BottomRight:
        break;
    }
    return combine(LayerWindow::AnchorBottom, LayerWindow::AnchorRight);
}

} // namespace

OverlaySurface::OverlaySurface(QQuickWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
    // Пересчёт региона вынесен из обработчика кадра: grabWindow() внутри
    // frameSwapped уводит рендер в реентерабельность. Задержка нужна ещё
    // и затем, чтобы новая анимация успела отрисоваться.
    m_regionTimer.setSingleShot(true);
    m_regionTimer.setInterval(80);
    connect(&m_regionTimer, &QTimer::timeout, this, &OverlaySurface::rebuildRegion);
}

bool OverlaySurface::configure(Corner corner, const QMargins &margins)
{
    if (!m_window)
        return false;

    // Прозрачный фон: без него поверхность зальётся чёрным и вопрос
    // про клики по прозрачным пикселям потеряет смысл.
    m_window->setColor(Qt::transparent);

    if (QGuiApplication::platformName() != QLatin1String("wayland")) {
        // Понятная ошибка совместимости вместо молчаливой деградации (§10).
        qCCritical(logOverlay,
                   "layer-shell требует Wayland, а платформа сейчас «%s»",
                   qPrintable(QGuiApplication::platformName()));
        return false;
    }

    LayerWindow *layer = LayerWindow::get(m_window);
    if (!layer) {
        qCCritical(logOverlay, "LayerShellQt недоступен: overlay создать нельзя");
        return false;
    }

    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(anchorsFor(corner));
    layer->setMargins(margins);
    // Ноль, а не -1: питомец не резервирует место и не двигает чужие окна,
    // но и не лезет под панель.
    layer->setExclusiveZone(0);
    // Клавиатура питомцу не нужна: он ничего не вводит.
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
    layer->setScope(QStringLiteral("open-pet"));
    layer->setCloseOnDismissed(false);

    m_layerShellAvailable = true;

    connect(m_window, &QQuickWindow::widthChanged, this, &OverlaySurface::scheduleRegionUpdate);
    connect(m_window, &QQuickWindow::heightChanged, this, &OverlaySurface::scheduleRegionUpdate);

    return true;
}

void OverlaySurface::scheduleRegionUpdate()
{
    // Аварийный выключатель для диагностики: с ним окно ловит ввод целиком,
    // зато видно, связана ли проблема отрисовки с расчётом региона.
    if (qEnvironmentVariableIsSet("OPENPET_NO_REGION"))
        return;

    if (m_window)
        m_regionTimer.start();
}

void OverlaySurface::rebuildRegion()
{
    if (!m_window || !m_window->isVisible())
        return;

    QElapsedTimer timer;
    timer.start();

    const QImage frame = m_window->grabWindow();
    if (frame.isNull()) {
        qCWarning(logOverlay, "кадр пуст: регион не обновлён, окно ловит ввод целиком");
        return;
    }

    const qreal dpr = m_window->devicePixelRatio();
    const QRegion region = openpet::regionFromAlpha(frame, openpet::RegionOptions {}, dpr);

    if (region.isEmpty()) {
        // Пустой регион означал бы, что питомец не ловит даже собственные
        // клики. Лучше оставить прошлый.
        qCWarning(logOverlay, "регион пуст: питомец не попал в кадр, оставлен прежний");
        return;
    }

    m_window->setMask(region);
    m_regionRectCount = int(region.rectCount());
    m_lastBuildMs = timer.nsecsElapsed() / 1e6;

    emit regionUpdated(m_regionRectCount, m_lastBuildMs);
}
