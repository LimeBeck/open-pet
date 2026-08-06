#pragma once

#include <QMargins>
#include <QObject>
#include <QRegion>
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

    bool isLayerShellAvailable() const { return m_layerShellAvailable; }

    // Диагностика для §7: сколько прямоугольников в текущем регионе
    // и во что обошёлся последний пересчёт.
    int regionRectCount() const { return m_regionRectCount; }
    qreal lastRegionBuildMs() const { return m_lastBuildMs; }

signals:
    void regionUpdated(int rectCount, qreal buildMs);

private:
    void rebuildRegion();

    QQuickWindow *m_window = nullptr;
    QTimer m_regionTimer;
    bool m_layerShellAvailable = false;
    int m_regionRectCount = 0;
    qreal m_lastBuildMs = 0;
};
