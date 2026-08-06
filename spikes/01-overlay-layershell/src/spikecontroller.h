#pragma once

#include "inputregion.h"

#include <QElapsedTimer>
#include <QObject>
#include <QRect>
#include <QTimer>

class QQuickWindow;

// Сводит вместе то, ради чего затеян спайк: переключение стратегий input region,
// их стоимость и наблюдаемое поведение окна. Всё измеряемое выставлено в QML,
// чтобы цифры были видны прямо на экране во время ручной проверки.
class SpikeController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString modeName READ modeName NOTIFY statsChanged)
    Q_PROPERTY(int modeIndex READ modeIndex NOTIFY statsChanged)
    Q_PROPERTY(qreal fps READ fps NOTIFY statsChanged)
    Q_PROPERTY(qreal cpuPercent READ cpuPercent NOTIFY statsChanged)
    Q_PROPERTY(int rssMib READ rssMib NOTIFY statsChanged)
    Q_PROPERTY(int regionRects READ regionRects NOTIFY statsChanged)
    Q_PROPERTY(qreal regionBuildMs READ regionBuildMs NOTIFY statsChanged)
    Q_PROPERTY(int clicks READ clicks NOTIFY statsChanged)
    Q_PROPERTY(QString note READ note NOTIFY statsChanged)

public:
    explicit SpikeController(QObject *parent = nullptr);

    // Отсчёт для §7 («питомец виден не позднее 3 секунд») — стартует как можно
    // раньше в main(), чтобы в число попала и инициализация Qt.
    void startStartupClock(qint64 alreadyElapsedNs);
    void attach(QQuickWindow *window);
    void setMode(RegionMode mode);

    QString modeName() const;
    int modeIndex() const { return int(m_mode); }
    qreal fps() const { return m_fps; }
    qreal cpuPercent() const { return m_cpuPercent; }
    int rssMib() const { return m_rssMib; }
    int regionRects() const { return m_regionRects; }
    qreal regionBuildMs() const { return m_regionBuildMs; }
    int clicks() const { return m_clicks; }
    QString note() const { return m_note; }

    // Прямоугольник питомца в логических координатах окна — источник для
    // варианта A. Задаётся из QML, чтобы не дублировать геометрию силуэта.
    Q_INVOKABLE void setPetRect(qreal x, qreal y, qreal width, qreal height);
    Q_INVOKABLE void noteClick(qreal x, qreal y);
    Q_INVOKABLE void cycleMode();

signals:
    void statsChanged();

private:
    void applyRegion();
    void updateCounters();
    void rebuildFromAlpha();

    QQuickWindow *m_window = nullptr;
    RegionMode m_mode = RegionMode::AlphaOnce;

    QRect m_petRect;
    int m_regionRects = 0;
    qreal m_regionBuildMs = 0;
    // Для варианта C важно не последнее значение, а сколько раз за секунду
    // регион пересчитывался и во что это обошлось в сумме.
    int m_rebuildsThisSecond = 0;
    qreal m_rebuildMsThisSecond = 0;
    int m_lastRebuildCount = 0;
    qreal m_lastRebuildMsTotal = 0;
    bool m_alphaRegionValid = false;
    bool m_grabInFlight = false;

    int m_frames = 0;
    qreal m_fps = 0;
    qreal m_cpuPercent = 0;
    int m_rssMib = 0;
    quint64 m_lastCpuTicks = 0;
    QElapsedTimer m_cpuClock;

    int m_clicks = 0;
    QString m_note;

    QElapsedTimer m_startupClock;
    qint64 m_startupOffsetNs = 0;
    bool m_firstFrameSeen = false;

    QTimer m_counterTimer;
    QTimer m_grabTimer;
};
