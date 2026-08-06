#include "spikecontroller.h"

#include <QDebug>
#include <QFile>
#include <QImage>
#include <QQuickWindow>
#include <QRectF>
#include <QRegion>

#include <unistd.h>

namespace {

// Огрубление сетки региона. 4 логических пикселя — компромисс: силуэт ещё
// узнаваем, а число прямоугольников остаётся в сотнях, а не в тысячах.
constexpr int kRegionBlock = 4;

// Полупрозрачные края и тень не должны ловить клики.
constexpr int kAlphaThreshold = 24;

quint64 readProcessCpuTicks()
{
    QFile stat(QStringLiteral("/proc/self/stat"));
    if (!stat.open(QIODevice::ReadOnly))
        return 0;

    const QByteArray content = stat.readAll();
    // comm может содержать пробелы и скобки, поэтому считаем поля после ')'.
    const int commEnd = content.lastIndexOf(')');
    if (commEnd < 0)
        return 0;

    const QList<QByteArray> fields = content.mid(commEnd + 2).split(' ');
    // После comm и state идут поля 4…; utime — 14-е поле stat, stime — 15-е,
    // то есть индексы 11 и 12 в этом срезе.
    if (fields.size() < 13)
        return 0;

    return fields[11].toULongLong() + fields[12].toULongLong();
}

int readProcessRssMib()
{
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly))
        return 0;

    const QList<QByteArray> fields = statm.readAll().split(' ');
    if (fields.size() < 2)
        return 0;

    const qint64 pages = fields[1].toLongLong();
    return int((pages * 4096) / (1024 * 1024));
}

} // namespace

SpikeController::SpikeController(QObject *parent)
    : QObject(parent)
{
    m_counterTimer.setInterval(1000);
    connect(&m_counterTimer, &QTimer::timeout, this, &SpikeController::updateCounters);

    // Пересчёт региона вынесен из обработчика кадра: grabWindow() внутри
    // frameSwapped уводит рендер в реентерабельность.
    m_grabTimer.setSingleShot(true);
    m_grabTimer.setInterval(0);
    connect(&m_grabTimer, &QTimer::timeout, this, &SpikeController::rebuildFromAlpha);

    m_lastCpuTicks = readProcessCpuTicks();
    m_cpuClock.start();
}

void SpikeController::startStartupClock(qint64 alreadyElapsedNs)
{
    m_startupOffsetNs = alreadyElapsedNs;
    m_startupClock.start();
}

void SpikeController::attach(QQuickWindow *window)
{
    m_window = window;
    if (!m_window)
        return;

    connect(m_window, &QQuickWindow::frameSwapped, this, [this] {
        ++m_frames;
        if (!m_firstFrameSeen && m_startupClock.isValid()) {
            m_firstFrameSeen = true;
            qInfo().noquote() << QStringLiteral("до первого кадра: %1 мс")
                                     .arg((m_startupOffsetNs + m_startupClock.nsecsElapsed()) / 1e6, 0, 'f', 1);
        }
        if (m_mode == RegionMode::AlphaPerFrame && !m_grabInFlight && !m_grabTimer.isActive())
            m_grabTimer.start();
    });

    m_counterTimer.start();
    applyRegion();
}

void SpikeController::setMode(RegionMode mode)
{
    m_mode = mode;
    m_alphaRegionValid = false;
    m_regionBuildMs = 0;
    m_regionRects = 0;
    applyRegion();
    emit statsChanged();
}

QString SpikeController::modeName() const
{
    return QString::fromUtf8(regionModeName(m_mode));
}

void SpikeController::setPetRect(qreal x, qreal y, qreal width, qreal height)
{
    const QRect rect = QRectF(x, y, width, height).toAlignedRect();
    if (m_petRect == rect)
        return;

    m_petRect = rect;
    if (m_mode == RegionMode::StaticRect)
        applyRegion();
}

void SpikeController::noteClick(qreal x, qreal y)
{
    ++m_clicks;
    m_note = QStringLiteral("клик %1: %2, %3").arg(m_clicks).arg(int(x)).arg(int(y));
    qInfo().noquote() << m_note;
    emit statsChanged();
}

void SpikeController::cycleMode()
{
    setMode(RegionMode((int(m_mode) + 1) % 4));
}

void SpikeController::applyRegion()
{
    if (!m_window)
        return;

    switch (m_mode) {
    case RegionMode::FullWindow:
        // Пустая маска возвращает поведение по умолчанию: окно ловит всё.
        m_window->setMask(QRegion());
        m_regionRects = 0;
        break;

    case RegionMode::StaticRect:
        if (m_petRect.isValid()) {
            m_window->setMask(QRegion(m_petRect));
            m_regionRects = 1;
        }
        break;

    case RegionMode::AlphaOnce:
        if (!m_alphaRegionValid)
            m_grabTimer.start();
        break;

    case RegionMode::AlphaPerFrame:
        m_grabTimer.start();
        break;
    }
}

void SpikeController::rebuildFromAlpha()
{
    if (!m_window)
        return;

    m_grabInFlight = true;

    QElapsedTimer timer;
    timer.start();

    const QImage frame = m_window->grabWindow();
    if (frame.isNull()) {
        // Важный отрицательный результат: если layer-shell поверхность
        // не отдаёт содержимое, варианты B и C из ADR-002 отпадают целиком.
        m_note = QStringLiteral("grabWindow() вернул пустое изображение");
        m_grabInFlight = false;
        emit statsChanged();
        return;
    }

    const qreal dpr = m_window->devicePixelRatio();
    const QRegion region = regionFromAlpha(frame, int(kRegionBlock * dpr), kAlphaThreshold, dpr);

    if (region.isEmpty()) {
        m_note = QStringLiteral("регион пуст — питомец не попал в кадр?");
        m_grabInFlight = false;
        emit statsChanged();
        return;
    }

    m_window->setMask(region);
    m_regionRects = int(region.rectCount());
    // Время включает и захват кадра, и построение региона: для бюджета §7
    // важна общая стоимость приёма, а не отдельно взятый цикл по пикселям.
    m_regionBuildMs = timer.nsecsElapsed() / 1e6;
    ++m_rebuildsThisSecond;
    m_rebuildMsThisSecond += m_regionBuildMs;
    m_alphaRegionValid = true;
    m_grabInFlight = false;

    emit statsChanged();
}

void SpikeController::updateCounters()
{
    const qint64 elapsedMs = m_cpuClock.restart();
    if (elapsedMs <= 0)
        return;

    m_fps = m_frames * 1000.0 / elapsedMs;
    m_frames = 0;

    const quint64 ticks = readProcessCpuTicks();
    const long ticksPerSecond = sysconf(_SC_CLK_TCK);
    if (ticksPerSecond > 0 && ticks >= m_lastCpuTicks) {
        const qreal cpuSeconds = qreal(ticks - m_lastCpuTicks) / ticksPerSecond;
        m_cpuPercent = cpuSeconds * 1000.0 / elapsedMs * 100.0;
    }
    m_lastCpuTicks = ticks;

    m_rssMib = readProcessRssMib();

    // Числа спайка должны переживать закрытие окна: HUD хорош для глаз,
    // а в ADR попадает то, что осталось в логе.
    m_lastRebuildCount = m_rebuildsThisSecond;
    m_lastRebuildMsTotal = m_rebuildMsThisSecond;
    m_rebuildsThisSecond = 0;
    m_rebuildMsThisSecond = 0;

    const qreal avgMs = m_lastRebuildCount > 0 ? m_lastRebuildMsTotal / m_lastRebuildCount : 0;

    qInfo().noquote() << QStringLiteral("%1 | fps %2 | cpu %3% | rss %4 МиБ | регион %5 прямоуг. | пересчётов %6, средн. %7 мс, всего %8 мс/с")
                             .arg(modeName())
                             .arg(m_fps, 0, 'f', 1)
                             .arg(m_cpuPercent, 0, 'f', 1)
                             .arg(m_rssMib)
                             .arg(m_regionRects)
                             .arg(m_lastRebuildCount)
                             .arg(avgMs, 0, 'f', 2)
                             .arg(m_lastRebuildMsTotal, 0, 'f', 1);

    emit statsChanged();
}
