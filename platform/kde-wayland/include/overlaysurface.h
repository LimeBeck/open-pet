#pragma once

#include <QMargins>
#include <QObject>
#include <QRegion>
#include <QPoint>
#include <QTimer>

class QQuickWindow;

// Layer-shell overlay и его input region.
//
// Единственное место, знающее про Wayland и LayerShellQt. Всё остальное
// приложение работает с обычным QQuickWindow.
//
// Стратегия региона — вариант B из ADR-002: регион считается по альфа-каналу
// один раз на анимацию, а не покадрово. Покадровый пересчёт стоил 19–22%
// ядра против цели §7 в 2%.
class OverlaySurface : public QObject
{
    Q_OBJECT

public:
    enum class Corner {
        BottomRight,
        BottomLeft,
        TopRight,
        TopLeft,
    };

    explicit OverlaySurface(QQuickWindow *window, QObject *parent = nullptr);

    // Настраивает поверхность и показывает её. Вызывается один раз,
    // до появления окна на экране.
    bool configure(Corner corner, const QMargins &margins);

    // Пересчитать регион: вызывается при смене анимации, масштаба
    // или геометрии окна.
    void scheduleRegionUpdate();

    // Сдвигает питомца на dx, dy пикселей (§FR-2).
    //
    // У layer-shell поверхности нет координат: положение задаётся якорем
    // и отступами. Поэтому перетаскивание — это правка отступов, а знак
    // сдвига зависит от того, к какому углу привязан питомец.
    //
    // Возвращает фактическое смещение: у краёв экрана оно меньше
    // запрошенного, и вызывающий должен знать об этом, чтобы курсор
    // не «уезжал» от питомца.
    QPoint moveBy(int dx, int dy);

    // Переносит питомца на следующий монитор по кругу (§FR-1).
    //
    // Не привязано к курсору намеренно: под Wayland клиент не знает
    // настоящей позиции указателя. Проверено — QCursor::pos() у layer-shell
    // поверхности возвращает координаты, не соответствующие экрану,
    // на котором питомец находится.
    bool moveToNextScreen();

    Corner corner() const { return m_corner; }
    QMargins margins() const { return m_margins; }

    bool isLayerShellAvailable() const { return m_layerShellAvailable; }

    // Диагностика для §7: сколько прямоугольников в текущем регионе
    // и во что обошёлся последний пересчёт.
    int regionRectCount() const { return m_regionRectCount; }
    qreal lastRegionBuildMs() const { return m_lastBuildMs; }

signals:
    void regionUpdated(int rectCount, qreal buildMs);
    // Питомец переехал: хосту нужно сохранить положение, а не гадать.
    void placementChanged(Corner corner, const QMargins &margins);

private:
    void rebuildRegion();

    QQuickWindow *m_window = nullptr;
    Corner m_corner = Corner::BottomRight;
    QMargins m_margins;
    QTimer m_regionTimer;
    bool m_layerShellAvailable = false;
    int m_regionRectCount = 0;
    qreal m_lastBuildMs = 0;
};
