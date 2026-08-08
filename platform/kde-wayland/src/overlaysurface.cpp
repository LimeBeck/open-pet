#include "overlaysurface.h"

#include "inputregion.h"

#include <LayerShellQt/Window>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
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

    m_corner = corner;
    m_margins = margins;

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

QPoint OverlaySurface::beginDrag()
{
    if (!m_window || !m_layerShellAvailable)
        return {};

    if (m_dragging) {
        // Предыдущее перетаскивание не завершилось: отпускание может
        // не прийти вовсе, если захват мыши потерян. Отказывать здесь —
        // значит ломать перетаскивание навсегда до перезапуска, что
        // однажды и случилось. Поэтому состояние восстанавливается.
        qCWarning(logOverlay, "предыдущее перетаскивание не завершилось, восстанавливаю");
        restoreSurface();
    }

    LayerWindow *layer = LayerWindow::get(m_window);
    if (!layer)
        return {};

    const QScreen *screen = m_window->screen();
    const QSize area = screen ? screen->availableSize() : QSize(1920, 1080);

    m_restSize = m_window->size();

    // Где питомец находится сейчас в координатах рабочей области.
    const bool anchoredRight = m_corner == Corner::BottomRight || m_corner == Corner::TopRight;
    const bool anchoredBottom = m_corner == Corner::BottomRight || m_corner == Corner::BottomLeft;

    const int x = anchoredRight ? area.width() - m_restSize.width() - m_margins.right()
                                : m_margins.left();
    const int y = anchoredBottom ? area.height() - m_restSize.height() - m_margins.bottom()
                                 : m_margins.top();

    // Поверхность растягивается на всю рабочую область: якорь по четырём
    // сторонам заставляет композитор выдать её размером с экран.
    m_dragging = true;
    layer->setMargins(QMargins());
    layer->setAnchors(LayerWindow::Anchors(LayerWindow::AnchorTop) | LayerWindow::AnchorBottom
                      | LayerWindow::AnchorLeft | LayerWindow::AnchorRight);

    qCDebug(logOverlay).noquote()
        << QStringLiteral("перетаскивание: поверхность %1x%2, питомец в %3,%4")
               .arg(area.width())
               .arg(area.height())
               .arg(x)
               .arg(y);

    return QPoint(x, y);
}

void OverlaySurface::restoreSurface()
{
    if (!m_window)
        return;

    LayerWindow *layer = LayerWindow::get(m_window);
    if (!layer)
        return;

    m_dragging = false;

    // Размер возвращается явно. Пока поверхность была привязана к четырём
    // сторонам, композитор выдал ей размер экрана и сам обратно его
    // не уменьшит: при двух якорях размер задаёт клиент.
    layer->setAnchors(anchorsFor(m_corner));
    layer->setDesiredSize(m_restSize);
    layer->setMargins(m_margins);
    m_window->resize(m_restSize);

    scheduleRegionUpdate();
}

void OverlaySurface::endDrag(const QPoint &petPosition)
{
    if (!m_window || !m_dragging)
        return;

    LayerWindow *layer = LayerWindow::get(m_window);
    if (!layer) {
        m_dragging = false;
        return;
    }

    const QScreen *screen = m_window->screen();
    const QSize area = screen ? screen->availableSize() : QSize(1920, 1080);

    const bool anchoredRight = m_corner == Corner::BottomRight || m_corner == Corner::TopRight;
    const bool anchoredBottom = m_corner == Corner::BottomRight || m_corner == Corner::BottomLeft;

    // Обратный перевод: из положения внутри рабочей области — в отступы
    // от того угла, к которому привязан питомец.
    const int right = area.width() - petPosition.x() - m_restSize.width();
    const int bottom = area.height() - petPosition.y() - m_restSize.height();

    QMargins updated;
    if (anchoredRight)
        updated.setRight(qMax(0, right));
    else
        updated.setLeft(qMax(0, petPosition.x()));
    if (anchoredBottom)
        updated.setBottom(qMax(0, bottom));
    else
        updated.setTop(qMax(0, petPosition.y()));

    m_margins = updated;
    restoreSurface();

    emit placementChanged(m_corner, m_margins);
}

QScreen *OverlaySurface::screen() const
{
    return m_window ? m_window->screen() : nullptr;
}

QPoint OverlaySurface::moveBy(int dx, int dy)
{
    if (!m_window || !m_layerShellAvailable)
        return {};

    LayerWindow *layer = LayerWindow::get(m_window);
    if (!layer)
        return {};

    // Знак зависит от якоря: у правого края увеличение отступа двигает
    // питомца влево, у левого — вправо. Без этого перетаскивание работало бы
    // зеркально в двух углах из четырёх.
    const bool anchoredRight = m_corner == Corner::BottomRight || m_corner == Corner::TopRight;
    const bool anchoredBottom = m_corner == Corner::BottomRight || m_corner == Corner::BottomLeft;

    const QScreen *screen = m_window->screen();
    const QSize available = screen ? screen->availableSize() : QSize(4000, 4000);

    // Питомец не должен уезжать за край: там его не достать мышью,
    // а настройка окажется испорченной без видимой причины.
    const int maxX = qMax(0, available.width() - m_window->width());
    const int maxY = qMax(0, available.height() - m_window->height());

    const int oldX = anchoredRight ? m_margins.right() : m_margins.left();
    const int oldY = anchoredBottom ? m_margins.bottom() : m_margins.top();

    const int newX = qBound(0, anchoredRight ? oldX - dx : oldX + dx, maxX);
    const int newY = qBound(0, anchoredBottom ? oldY - dy : oldY + dy, maxY);

    if (newX == oldX && newY == oldY)
        return {};

    QMargins updated = m_margins;
    if (anchoredRight)
        updated.setRight(newX);
    else
        updated.setLeft(newX);
    if (anchoredBottom)
        updated.setBottom(newY);
    else
        updated.setTop(newY);

    m_margins = updated;
    layer->setMargins(m_margins);
    emit placementChanged(m_corner, m_margins);

    // Возвращается смещение в координатах экрана, а не в отступах: у края
    // оно меньше запрошенного, и без этого курсор оторвался бы от питомца.
    return QPoint(anchoredRight ? oldX - newX : newX - oldX,
                  anchoredBottom ? oldY - newY : newY - oldY);
}

bool OverlaySurface::moveToNextScreen()
{
    if (!m_window || !m_layerShellAvailable)
        return false;

    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.size() < 2)
        return false;

    const int current = screens.indexOf(m_window->screen());
    QScreen *target = screens.at((current + 1) % screens.size());

    LayerWindow *layer = LayerWindow::get(m_window);
    if (!layer)
        return false;

    // Поверхность layer-shell принадлежит выходу с момента создания.
    // Смена выхода у показанной поверхности композитором игнорируется,
    // поэтому её приходится пересоздавать: скрыть, назначить экран, показать.
    const bool wasVisible = m_window->isVisible();
    if (wasVisible)
        m_window->setVisible(false);

    layer->setScreen(target);
    m_window->setScreen(target);

    if (wasVisible)
        m_window->setVisible(true);

    const QString actual = m_window->screen() ? m_window->screen()->name() : QStringLiteral("?");
    qCInfo(logOverlay).noquote()
        << QStringLiteral("перенос на экран %1, фактически %2").arg(target->name(), actual);

    scheduleRegionUpdate();
    return m_window->screen() == target;
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
