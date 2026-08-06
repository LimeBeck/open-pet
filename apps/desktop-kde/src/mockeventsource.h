#pragma once

#include <QObject>
#include <QTimer>

class CoreBridge;

// Заглушка источников событий на время M1.
//
// Настоящие адаптеры появляются в M3 (idle, UPower, сессия) и M4 (активное
// приложение, MPRIS, уведомления). До тех пор скелету нужен хоть какой-то
// поток событий, чтобы было видно, что путь адаптер → ядро → UI живой.
//
// Источник намеренно не читает ничего из системы: он проигрывает
// фиксированный сценарий. Это не «временно упрощённый UPower», а честная
// заглушка, которую целиком выбросят.
class MockEventSource : public QObject
{
    Q_OBJECT

public:
    explicit MockEventSource(CoreBridge *core, QObject *parent = nullptr);

    void start(int intervalMs = 10000);
    void stop();
    bool isRunning() const { return m_timer.isActive(); }

private:
    void step();

    CoreBridge *m_core = nullptr;
    QTimer m_timer;
    int m_index = 0;
};
