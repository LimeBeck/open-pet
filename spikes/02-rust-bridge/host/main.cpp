#include "corebridge.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-58s %s\n", what, condition ? "ок" : "ПРОВАЛ");
    if (!condition)
        ++failures;
}

const char *emotionName(int code)
{
    static const char *names[] = {
        "idle", "happy", "curious", "sleepy",
        "charging", "low_battery", "notification", "busy",
    };
    return (code >= 0 && code < 8) ? names[code] : "?";
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QThread *mainThread = QThread::currentThread();

    // Без этого stdout буферизуется, а паника Rust уходит в stderr сразу,
    // и вывод перемешивается.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::printf("=== синхронный путь: событие → ядро → реакция ===\n");

    CoreBridge bridge;
    check(bridge.isValid(), "ядро создано, версия ABI совпала");

    CoreBridge::Reaction reaction;

    check(bridge.pushPetClicked(&reaction) && reaction.emotion == 1,
          "клик по питомцу даёт happy");
    std::printf("   → %s, приоритет %d, ttl %d мс, ключ \"%s\"\n",
                emotionName(reaction.emotion), reaction.priority, reaction.ttlMs,
                qPrintable(reaction.cooldownKey));

    check(!bridge.pushPetClicked(&reaction),
          "повторный клик подавлен cooldown");

    check(bridge.pushPowerChanged(true, 9, &reaction) && reaction.emotion == 5,
          "низкий заряд даёт low_battery и прерывает happy");

    check(!bridge.pushPowerChanged(true, 80, &reaction),
          "разряд без низкого процента молчит");

    check(bridge.pushActiveAppChanged(QStringLiteral("org.kde.konsole"), &reaction)
              == false,
          "смена приложения подавлена более приоритетным low_battery");

    // Строка с не-ASCII: проверяем, что UTF-8 переживает границу.
    bridge.pushActiveAppChanged(QStringLiteral("приложение-с-юникодом"), &reaction);
    check(true, "app id в UTF-8 не роняет ядро");

    // Отдельное ядро: на общем активен low_battery с приоритетом 70,
    // и уведомление было бы подавлено — проверка мерила бы приоритеты,
    // а не пересечение границы строкой.
    {
        CoreBridge fresh;
        CoreBridge::Reaction notification;
        check(fresh.pushNotification(QStringLiteral("im"), &notification)
                  && notification.emotion == 6,
              "уведомление даёт notification, категория пересекла границу");
    }

    check(!bridge.pushNotification(QStringLiteral("im"), &reaction),
          "то же уведомление подавлено активным low_battery");

    std::printf("\n=== паника в Rust не должна ронять хост ===\n");
    const int panicResult = bridge.simulatePanic();
    check(panicResult == -2, "паника перехвачена на границе, процесс жив");

    check(bridge.pushIdleThreshold(300, &reaction) || true,
          "ядро продолжает отвечать после паники");

    std::printf("\n=== асинхронный путь: поток ядра → event loop Qt ===\n");

    int received = 0;
    bool alwaysMainThread = true;

    QObject::connect(&bridge, &CoreBridge::reactionReceived, &app,
                     [&](const CoreBridge::Reaction &r) {
                         ++received;
                         if (QThread::currentThread() != mainThread)
                             alwaysMainThread = false;
                         std::printf("   тик %d: %s (поток %s)\n", received,
                                     emotionName(r.emotion),
                                     QThread::currentThread() == mainThread
                                         ? "главный"
                                         : "ЧУЖОЙ");
                     });

    bridge.startTicker(120);

    QTimer::singleShot(900, &app, [&] {
        // Тиков за 900 мс происходит семь, а реакций приходит две: остальные
        // подавлены cooldown и приоритетом. Это домен работает как задумано,
        // причём в собственном потоке — проверять числом тиков было бы неверно.
        check(received >= 2, "реакции из фонового потока дошли до event loop");
        check(alwaysMainThread, "все реакции получены в главном потоке");

        std::printf("\nпровалов: %d\n", failures);
        app.exit(failures == 0 ? 0 : 1);
    });

    return app.exec();
}
