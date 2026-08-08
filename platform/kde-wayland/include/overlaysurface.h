#pragma once

#include <QMargins>
#include <QObject>
#include <QRegion>
#include <QPoint>
#include <QRegion>
#include <QSize>

class QScreen;
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

    // Растягивает поверхность на рабочую область экрана и возвращает
    // положение питомца внутри неё (§FR-2).
    //
    // Нужно, чтобы во время перетаскивания окно **не двигалось**. Пока
    // оно двигалось, смещение приходилось считать в его же системе отсчёта,
    // которую композитор обновляет асинхронно: замер на 3284 событиях показал
    // средний модуль дельты 10.4 px при плавном движении руки и 9.6% дельт
    // больше 30 px — петля неустойчива по устройству, а не по коэффициентам.
    QPoint beginDrag();

    // Возвращает поверхность к размеру питомца и ставит отступы так, чтобы
    // он оказался там, куда его перетащили.
    void endDrag(const QPoint &petPosition);

    bool isDragging() const { return m_dragging; }

    // Сдвиг маски вслед за процедурным движением (ADR-009).
    //
    // Регион не пересчитывается, а переносится: пересчёт по альфа-каналу
    // стоит 10.8 мс и 19–22% ядра (ADR-002), перенос готового — 0.013–0.041 мс
    // и 0.1–0.4%. Без переноса клики попадали бы в старый силуэт: питомец
    // виден в одном месте, а ввод принимает в другом.
    void setMotionOffset(const QPoint &offset);

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

    // Экран, на котором окно по мнению Qt: при разных масштабах
    // это первое, что стоит проверить.
    QScreen *screen() const;
    QQuickWindow *window() const { return m_window; }

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
    // Возвращает поверхность к размеру и якорю питомца.
    void restoreSurface();

    QQuickWindow *m_window = nullptr;
    Corner m_corner = Corner::BottomRight;
    bool m_dragging = false;
    QSize m_restSize;
    QMargins m_margins;
    QTimer m_regionTimer;
    bool m_layerShellAvailable = false;
    // Регион в покое, то есть при нулевом смещении движения.
    QRegion m_region;
    QPoint m_motionOffset;
    int m_regionRectCount = 0;
    qreal m_lastBuildMs = 0;
};
