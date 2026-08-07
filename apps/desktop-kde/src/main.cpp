#include "corebridge.h"
#include "idleadapter.h"
#include "mockeventsource.h"
#include "overlaysurface.h"
#include "petviewmodel.h"
#include "poweradapter.h"
#include "sessionadapter.h"
#include "settings.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QIcon>
#include <QLocale>
#include <QLoggingCategory>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTimer>

Q_LOGGING_CATEGORY(logApp, "openpet.app")

namespace {

// Периодичность опроса «не пора ли вернуться в покой». Раз в секунду —
// достаточно точно для ttl в секундах и не создаёт нагрузки.
constexpr int kSettleIntervalMs = 1000;

OpenPetPowerState toContract(PowerAdapter::Kind kind)
{
    switch (kind) {
    case PowerAdapter::Kind::Charging:
        return OPENPET_POWER_CHARGING;
    case PowerAdapter::Kind::Discharging:
        return OPENPET_POWER_DISCHARGING;
    case PowerAdapter::Kind::Full:
        return OPENPET_POWER_FULL;
    case PowerAdapter::Kind::Unknown:
        break;
    }
    return OPENPET_POWER_UNKNOWN;
}

OpenPetSessionState toContract(SessionAdapter::State state)
{
    switch (state) {
    case SessionAdapter::State::Locked:
        return OPENPET_SESSION_LOCKED;
    case SessionAdapter::State::Sleeping:
        return OPENPET_SESSION_SLEEPING;
    case SessionAdapter::State::Resumed:
        return OPENPET_SESSION_RESUMED;
    case SessionAdapter::State::Active:
        break;
    }
    return OPENPET_SESSION_ACTIVE;
}

} // namespace

int main(int argc, char *argv[])
{
    // Отсчёт до появления питомца (§7, «не позднее 3 секунд») начинается
    // здесь, чтобы в него попала и инициализация Qt.
    QElapsedTimer startupClock;
    startupClock.start();

    // QApplication, а не QGuiApplication: QSystemTrayIcon и QMenu — виджеты,
    // и без полноценного приложения виджетов трей падает при создании меню.
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("open-pet"));
    app.setOrganizationName(QStringLiteral("open-pet"));
    app.setApplicationDisplayName(QStringLiteral("open-pet"));
    app.setDesktopFileName(QStringLiteral("org.openpet.DesktopPet"));
    // Закрытие последнего окна не должно завершать приложение: питомца можно
    // скрыть и вернуть из трея.
    app.setQuitOnLastWindowClosed(false);

    const Settings settings = Settings::load();

    CoreBridge core;
    if (!core.isValid()) {
        qCCritical(logApp, "ядро недоступно, запуск невозможен");
        return 1;
    }

    core.setPaused(settings.paused);
    // Язык реплик берётся из системной локали. Неизвестные теги ядро
    // приводит к английскому — fallback обязан быть определён всегда (§7).
    core.setLocale(QLocale::system().name());

    QObject::connect(&core, &CoreBridge::diagnostic, [](int level, const QString &message) {
        // Диагностика ядра обезличена по построению: сообщения формирует
        // само ядро, пользовательского содержимого в них нет (§9).
        if (level >= 2)
            qCCritical(logApp).noquote() << message;
        else
            qCWarning(logApp).noquote() << message;
    });

    PetViewModel viewModel(&core);
    viewModel.setScale(settings.scale);
    viewModel.setReducedMotion(settings.reducedMotion);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("petModel"), &viewModel);
    engine.loadFromModule("OpenPet.Ui", "Main");

    if (engine.rootObjects().isEmpty()) {
        qCCritical(logApp, "не удалось загрузить QML");
        return 1;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window) {
        qCCritical(logApp, "корневой объект QML не является Window");
        return 1;
    }

    OverlaySurface overlay(window);
    if (!overlay.configure(settings.corner,
                           QMargins(0, 0, settings.marginRight, settings.marginBottom))) {
        // §10: понятная ошибка совместимости, а не молчаливый запуск
        // обычного окна.
        qCCritical(logApp, "overlay недоступен на этой системе");
        return 2;
    }

    window->show();

    // Регион считается заново при смене анимации — это и есть вариант B
    // из ADR-002: стоимость привязана к смене состояния, а не к кадру.
    QObject::connect(&viewModel, &PetViewModel::emotionChanged,
                     &overlay, &OverlaySurface::scheduleRegionUpdate);
    QObject::connect(&viewModel, &PetViewModel::scaleChanged,
                     &overlay, &OverlaySurface::scheduleRegionUpdate);
    // Пузырь меняет видимую область, значит и область попаданий: без этого
    // клик по реплике уходил бы на рабочий стол (ADR-002).
    QObject::connect(&viewModel, &PetViewModel::phraseChanged,
                     &overlay, &OverlaySurface::scheduleRegionUpdate);
    overlay.scheduleRegionUpdate();

    QObject::connect(&overlay, &OverlaySurface::regionUpdated, [](int rects, qreal ms) {
        qCDebug(logApp, "input region обновлён: %d прямоуг. за %.2f мс", rects, ms);
    });

    QObject::connect(&viewModel, &PetViewModel::phraseChanged, [&viewModel] {
        // Текст реплики — не пользовательское содержимое: он выбран ядром
        // из встроенного каталога (§9).
        if (!viewModel.phrase().isEmpty())
            qCDebug(logApp).noquote() << "реплика:" << viewModel.phrase();
    });

    QObject::connect(&viewModel, &PetViewModel::emotionChanged, [&viewModel] {
        // Имя состояния — не пользовательское содержимое, а фиксированный
        // словарь из восьми значений (§9).
        qCDebug(logApp).noquote() << "состояние:" << viewModel.emotionName();
    });

    bool firstFrameReported = false;
    int frames = 0;
    QObject::connect(window, &QQuickWindow::frameSwapped, [&] {
        ++frames;
        if (firstFrameReported)
            return;
        firstFrameReported = true;
        qCInfo(logApp, "питомец виден через %.0f мс после старта",
               startupClock.nsecsElapsed() / 1e6);
    });

    // Частота кадров нужна не ради красивого числа: без неё «низкий CPU»
    // неотличим от «анимация встала» (§7).
    QTimer frameTimer;
    QObject::connect(&frameTimer, &QTimer::timeout, [&] {
        qCDebug(logApp, "кадров за 5 с: %d (%.1f/с)", frames, frames / 5.0);
        frames = 0;
    });
    frameTimer.start(5000);

    // Состояние поверхности. Пригодилось сразу: «0 кадров и exposed=0»
    // при выключенном экране — это не поломка, а KWin, переставший слать
    // frame callbacks. Без этой строки такое легко принять за баг.
    QTimer::singleShot(2000, [window] {
        qCDebug(logApp, "окно: visible=%d exposed=%d geom=%dx%d экран=%s",
                window->isVisible(), window->isExposed(),
                window->width(), window->height(),
                window->screen() ? qPrintable(window->screen()->name()) : "нет");
    });

    // Возврат в покой по истечении ttl: питомец не залипает в состоянии.
    QTimer settleTimer;
    QObject::connect(&settleTimer, &QTimer::timeout, &core, &CoreBridge::settle);
    settleTimer.start(kSettleIntervalMs);

    // Источники событий (§FR-3). Каждый публикует своё состояние здоровья:
    // недоступность — штатная ситуация, а не отказ запуска (§10).
    const auto reportCapability = [](EventSource *source) {
        QObject::connect(source, &EventSource::capabilityChanged,
                         [source](CapabilityState state, const QString &reason) {
                             qCInfo(logApp).noquote()
                                 << QStringLiteral("источник %1: %2%3")
                                        .arg(source->name(), capabilityStateName(state),
                                             reason.isEmpty() ? QString()
                                                              : QStringLiteral(" — ") + reason);
                         });
    };

    // Порог простоя можно укоротить для проверки, не трогая настройки:
    // ждать пять минут ради одного события неразумно.
    const int idleSeconds = qEnvironmentVariableIntValue("OPENPET_IDLE_SECONDS") > 0
        ? qEnvironmentVariableIntValue("OPENPET_IDLE_SECONDS")
        : settings.idleSeconds;

    IdleAdapter idleSource(idleSeconds);
    reportCapability(&idleSource);
    QObject::connect(&idleSource, &IdleAdapter::idleThresholdReached,
                     &core, &CoreBridge::pushIdleThreshold);
    QObject::connect(&idleSource, &IdleAdapter::activityResumed,
                     &core, &CoreBridge::pushActivityResumed);

    PowerAdapter powerSource;
    reportCapability(&powerSource);
    QObject::connect(&powerSource, &PowerAdapter::powerChanged,
                     [&core](bool onBattery, int percent, PowerAdapter::Kind kind) {
                         core.pushPowerChanged(onBattery, percent, toContract(kind));
                     });

    SessionAdapter sessionSource;
    reportCapability(&sessionSource);
    QObject::connect(&sessionSource, &SessionAdapter::sessionChanged,
                     [&core](SessionAdapter::State state) {
                         core.pushSessionChanged(toContract(state));
                     });

    idleSource.start();
    powerSource.start();
    sessionSource.start();

    // Заглушка остаётся доступной для проверки состояний, которых не даёт
    // ни один настоящий источник: активное приложение, уведомления и медиа
    // появятся только в M4.
    MockEventSource mockEvents(&core);
    if (qEnvironmentVariableIsSet("OPENPET_MOCK_EVENTS"))
        mockEvents.start();

    // Диагностический выключатель трея: он тянет за собой стек виджетов,
    // и надо было проверить, не он ли мешает отрисовке overlay.
    const bool trayEnabled = !qEnvironmentVariableIsSet("OPENPET_NO_TRAY");

    // Трей (§4.1): показать/скрыть, настройки, пауза реакций, выход.
    QSystemTrayIcon tray;
    tray.setIcon(QIcon::fromTheme(QStringLiteral("face-smile"),
                                  QIcon::fromTheme(QStringLiteral("applications-games"))));
    tray.setToolTip(QStringLiteral("open-pet"));

    QMenu menu;

    QAction *toggleAction = menu.addAction(QObject::tr("Скрыть питомца"));
    QObject::connect(toggleAction, &QAction::triggered, [&] {
        const bool nowVisible = !window->isVisible();
        window->setVisible(nowVisible);
        toggleAction->setText(nowVisible ? QObject::tr("Скрыть питомца")
                                         : QObject::tr("Показать питомца"));
        if (nowVisible)
            overlay.scheduleRegionUpdate();
    });

    QAction *pauseAction = menu.addAction(QObject::tr("Пауза реакций"));
    pauseAction->setCheckable(true);
    pauseAction->setChecked(settings.paused);
    QObject::connect(pauseAction, &QAction::toggled, [&](bool checked) {
        // Пауза прекращает реакции, но оставляет трей и самого питомца
        // на экране (§FR-2).
        viewModel.setPaused(checked);
        Settings updated = Settings::load();
        updated.paused = checked;
        updated.save();
    });

    menu.addSeparator();

    QAction *settingsAction = menu.addAction(QObject::tr("Настройки…"));
    // Окно настроек — этап M7. Пункт показан неактивным намеренно:
    // так видно, что он запланирован, а не забыт.
    settingsAction->setEnabled(false);

    menu.addSeparator();

    QAction *quitAction = menu.addAction(QObject::tr("Выход"));
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    tray.setContextMenu(&menu);
    if (trayEnabled)
        tray.show();

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        // Без трея выйти из приложения было бы нечем: у окна нет ни рамки,
        // ни клавиатуры.
        qCWarning(logApp, "системный трей недоступен: выход только сигналом");
    }

    return app.exec();
}
