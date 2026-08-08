#include "activeappadapter.h"
#include "autostart.h"
#include "corebridge.h"
#include "idleadapter.h"
#include "llmclient.h"
#include "mediaadapter.h"
#include "mockeventsource.h"
#include "notificationadapter.h"
#include "overlaysurface.h"
#include "petviewmodel.h"
#include "poweradapter.h"
#include "sessionadapter.h"
#include "secretstore.h"
#include "settings.h"
#include "settingscontroller.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QIcon>
#include <QLocale>
#include <QLoggingCategory>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QFile>
#include <QQuickWindow>
#include <QStandardPaths>
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

OpenPetMediaState toContract(MediaAdapter::State state)
{
    switch (state) {
    case MediaAdapter::State::Playing:
        return OPENPET_MEDIA_PLAYING;
    case MediaAdapter::State::Paused:
        return OPENPET_MEDIA_PAUSED;
    case MediaAdapter::State::Stopped:
        break;
    }
    return OPENPET_MEDIA_STOPPED;
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

    // Стиль Qt Quick Controls по умолчанию не следует системной палитре:
    // в тёмной теме подписи оказываются тёмными на тёмном и не читаются.
    // Стиль KDE берёт цвета из системы, поэтому окно настроек выглядит
    // как остальные окна Plasma.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    qCInfo(logApp).noquote() << "стиль Quick Controls:" << QQuickStyle::name();

    // Автозапуск управляется галочкой в настройках, но им же полезно
    // управлять из скрипта — при упаковке и при проверке.
    if (qEnvironmentVariableIsSet("OPENPET_AUTOSTART")) {
        const bool wanted = qEnvironmentVariable("OPENPET_AUTOSTART") != QLatin1String("off");
        const bool ok = Autostart::setEnabled(wanted);
        qCInfo(logApp, "автозапуск %s: %s", wanted ? "включён" : "выключен",
               ok ? "готово" : "НЕ УДАЛОСЬ");
        return ok ? 0 : 1;
    }

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

    // LLM выключена, пока пользователь явно не задал провайдера (§7):
    // без настройки приложение не делает ни одного сетевого запроса.
    //
    // Источник истины — сохранённые настройки. Переменные окружения только
    // перекрывают их для проверки: раньше читалось исключительно окружение,
    // и провайдер, включённый в окне, после перезапуска молча пропадал.
    const QString llmEnv = qEnvironmentVariable("OPENPET_LLM");
    const int llmKind = llmEnv.isEmpty() ? settings.llmKind
        : llmEnv == QLatin1String("ollama")                  ? 1
        : llmEnv == QLatin1String("openai")                  ? 2
        : llmEnv == QLatin1String("vertex")                  ? 3
        : llmEnv == QLatin1String("aistudio")                ? 4
                                                             : 0;

    const auto envOr = [](const char *name, const QString &fallback) {
        const QString value = qEnvironmentVariable(name);
        return value.isEmpty() ? fallback : value;
    };

    if (llmKind > 0) {
        core.setLlmProvider(llmKind,
                            envOr("OPENPET_LLM_URL", settings.llmBaseUrl),
                            envOr("OPENPET_LLM_MODEL", settings.llmModel),
                            envOr("OPENPET_LLM_PROJECT", settings.llmProject),
                            envOr("OPENPET_LLM_REGION", settings.llmRegion));
        qCInfo(logApp, "LLM включена: вид %d, ядро подтверждает: %d", llmKind,
               core.isLlmEnabled());
    }

    LlmClient llm(&core);
    llm.setProviderKind(llmKind);
    // Путь к учётным данным Google: стандартный ADC, если пользователь
    // не указал другой. Файл читается по надобности, а не при запуске.
    llm.setVertexCredentialsPath(envOr(
        "OPENPET_GOOGLE_ADC",
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/gcloud/application_default_credentials.json")));
    llm.setProxy(settings.proxyMode, settings.proxyHost, settings.proxyPort, settings.proxyUser,
                 SecretStore::read(QStringLiteral("proxy-password")), settings.proxyBypassLocal);
    // Ключ читается из KWallet; окружение перекрывает его для проверки (§FR-7).
    llm.setApiKey(envOr("OPENPET_LLM_KEY", SecretStore::read(QStringLiteral("llm-api-key"))));

    // Проверка связи из терминала: нажать кнопку в окне нечем ни при
    // проверке, ни в скрипте установки.
    if (qEnvironmentVariableIsSet("OPENPET_HEALTHCHECK")) {
        QObject::connect(&llm, &LlmClient::modelsListed, [](const QStringList &models) {
            qCInfo(logApp).noquote()
                << QStringLiteral("моделей у провайдера: %1").arg(models.size());
            for (const QString &name : models)
                qCInfo(logApp).noquote() << "  " << name;
        });

        QObject::connect(&llm, &LlmClient::healthChecked,
                         [](bool ok, bool modelFound, const QString &detail) {
                             qCInfo(logApp).noquote()
                                 << QStringLiteral("проверка связи: %1").arg(detail);
                             QCoreApplication::exit(ok && modelFound ? 0 : 1);
                         });
        QTimer::singleShot(0, &llm, &LlmClient::checkHealth);
        return app.exec();
    }

    // Восстановление выбранного пакета (§9). Архив ставится заново и заново
    // проходит проверку: доверять собственному кэшу больше, чем валидатору,
    // значит обходить его.
    if (!settings.activePackId.isEmpty()) {
        const QString packDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/packs/") + settings.activePackId;

        QFile stored(packDir + QStringLiteral("/pack.zip"));
        if (stored.open(QIODevice::ReadOnly)) {
            QString sheetPath;
            const auto restored = core.installPack(stored.readAll(), &sheetPath);
            if (restored.accepted && !sheetPath.isEmpty()) {
                qCInfo(logApp).noquote()
                    << QStringLiteral("восстановлен пакет: %1").arg(core.activePackId());
            } else {
                // Пакет испортился между запусками — откат на встроенного (§10).
                qCWarning(logApp).noquote()
                    << QStringLiteral("пакет не восстановлен, откат: %1").arg(restored.report);
                core.rollbackPack();
            }
        }
    }

    PetViewModel viewModel(&core);
    viewModel.setLlmClient(&llm);
    {
        // Питомец берёт лист активного пакета: восстановленный пакет должен
        // показаться сразу, а не после первого события.
        const QString sheetFile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/packs/") + core.activePackId() + QStringLiteral("/sheet.png");
        if (!settings.activePackId.isEmpty() && QFile::exists(sheetFile))
            viewModel.setSheetSource(QUrl::fromLocalFile(sheetFile));
    }

    viewModel.setScale(settings.scale);
    viewModel.setReducedMotion(settings.reducedMotion);

    SettingsController settingsController(&core, &llm);

    // Импорт из терминала: открыть диалог выбора файла в скрипте нечем,
    // а проверять импорт надо.
    if (qEnvironmentVariableIsSet("OPENPET_IMPORT_PACK")) {
        QObject::connect(&settingsController, &SettingsController::changed, [&] {
            qCInfo(logApp).noquote() << settingsController.packStatus();
            QCoreApplication::exit(settingsController.packStatus().startsWith(QLatin1Char('✓'))
                                       ? 0
                                       : 1);
        });
        QTimer::singleShot(0, [&] {
            settingsController.importPack(
                QUrl::fromLocalFile(qEnvironmentVariable("OPENPET_IMPORT_PACK")));
        });
        return app.exec();
    }


    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("petModel"), &viewModel);
    engine.rootContext()->setContextProperty(QStringLiteral("settingsModel"), &settingsController);
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
        // Сигнал приходит и на микродвижение, при котором состояние не меняется.
        // Писать «состояние» на каждое такое движение значит наполнять журнал
        // событиями, которых не было, — а разбирать по нему потом чужие жалобы.
        static QString lastLogged;
        if (viewModel.emotionName() == lastLogged)
            return;
        lastLogged = viewModel.emotionName();

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
    QObject::connect(&core, &CoreBridge::fidgeted, [](int animation) {
        qCDebug(logApp, "движение в покое: анимация %d", animation);
    });

    QTimer settleTimer;
    QObject::connect(&settleTimer, &QTimer::timeout, &core, &CoreBridge::settle);
    settleTimer.start(kSettleIntervalMs);

    // Видимость складывается из двух независимых причин: пользователь мог
    // спрятать питомца сам, а мог заблокироваться экран. Разблокировка
    // не должна возвращать питомца, которого спрятали намеренно.
    bool userWantsPet = true;
    bool suspended = false;

    const auto applyVisibility = [&] {
        const bool shouldShow = userWantsPet && !suspended;
        if (window->isVisible() == shouldShow)
            return;

        window->setVisible(shouldShow);

        if (shouldShow) {
            overlay.scheduleRegionUpdate();
            settleTimer.start(kSettleIntervalMs);
        } else {
            // Скрытая поверхность не получает frame callbacks, и анимация
            // останавливается сама. Таймер возврата в покой без зрителя
            // тоже не нужен — это лишние пробуждения процессора.
            settleTimer.stop();
        }
    };

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
                     [&](SessionAdapter::State state) {
                         // За экраном блокировки и во сне питомца не видно.
                         // Ядро на это состояния не меняет — реакции всё равно
                         // никто не увидит; смысл события в том, чтобы
                         // перестать тратить кадры и батарею (§14).
                         const bool invisible = state == SessionAdapter::State::Locked
                             || state == SessionAdapter::State::Sleeping;

                         if (suspended != invisible) {
                             suspended = invisible;
                             qCInfo(logApp, invisible ? "сессия скрыта: отрисовка остановлена"
                                                      : "сессия вернулась: отрисовка возобновлена");
                             applyVisibility();
                         }

                         core.pushSessionChanged(toContract(state));
                     });

    MediaAdapter mediaSource;
    reportCapability(&mediaSource);
    QObject::connect(&mediaSource, &MediaAdapter::mediaChanged,
                     [&core](MediaAdapter::State state) {
                         core.pushMediaChanged(toContract(state));
                     });

    NotificationAdapter notificationSource;
    reportCapability(&notificationSource);
    QObject::connect(&notificationSource, &NotificationAdapter::notificationOccurred,
                     [&core] {
                         // Категории нет: сигнал её не содержит, и выдумывать
                         // её было бы враньём в модели событий.
                         core.pushNotification(QString());
                     });

    ActiveAppAdapter activeAppSource;
    reportCapability(&activeAppSource);
    QObject::connect(&activeAppSource, &ActiveAppAdapter::activeAppChanged,
                     &core, &CoreBridge::pushActiveAppChanged);

    if (settings.sourceIdle)
        idleSource.start();
    if (settings.sourcePower)
        powerSource.start();
    if (settings.sourceSession)
        sessionSource.start();
    if (settings.sourceMedia)
        mediaSource.start();
    if (settings.sourceNotification)
        notificationSource.start();
    if (settings.sourceActiveApp)
        activeAppSource.start();

    if (!ActiveAppAdapter::isScriptInstalled()) {
        qCInfo(logApp).noquote()
            << QStringLiteral("активное приложение недоступно: KWin-скрипт не установлен. "
                              "Установка — %1")
                   .arg(ActiveAppAdapter::scriptInstallPath());
    }

    // Заглушка остаётся доступной для проверки состояний, которых не даёт
    // ни один настоящий источник: активное приложение, уведомления и медиа
    // появятся только в M4.
    MockEventSource mockEvents(&core);
    if (qEnvironmentVariableIsSet("OPENPET_MOCK_EVENTS"))
        mockEvents.start();

    // Диагностический выключатель трея: он тянет за собой стек виджетов,
    // и надо было проверить, не он ли мешает отрисовке overlay.
    const bool trayEnabled = !qEnvironmentVariableIsSet("OPENPET_NO_TRAY");

    QObject::connect(&settingsController, &SettingsController::petPackChanged,
                     [&](const QString &sheetPath) {
                         // Пустой путь — вернулись к встроенному питомцу,
                         // его лист лежит в ресурсах приложения.
                         viewModel.setSheetSource(sheetPath.isEmpty()
                                                      ? QUrl(QStringLiteral(
                                                            "qrc:/qt/qml/OpenPet/Ui/lime.png"))
                                                      : QUrl::fromLocalFile(sheetPath));
                         viewModel.refreshAnimation();
                     });

    QObject::connect(&settingsController, &SettingsController::applied, [&] {
        const Settings &fresh = settingsController.current();
        viewModel.setScale(fresh.scale);
        viewModel.setReducedMotion(fresh.reducedMotion);
        viewModel.setPaused(fresh.paused);
        overlay.scheduleRegionUpdate();
        // Ключ мог быть только что сохранён в кошелёк — перечитываем,
        // иначе он подхватится лишь со следующего запуска.
        llm.setApiKey(SecretStore::read(QStringLiteral("llm-api-key")));
        llm.setProxy(fresh.proxyMode, fresh.proxyHost, fresh.proxyPort, fresh.proxyUser,
                     SecretStore::read(QStringLiteral("proxy-password")), fresh.proxyBypassLocal);
        // Провайдер мог смениться в окне — иначе он подхватится лишь
        // со следующего запуска, как это уже случилось однажды.
        core.setLlmProvider(fresh.llmKind, fresh.llmBaseUrl, fresh.llmModel, fresh.llmProject,
                            fresh.llmRegion);
        llm.setProviderKind(fresh.llmKind);
    });

    // Трей (§4.1): показать/скрыть, настройки, пауза реакций, выход.
    QSystemTrayIcon tray;
    tray.setIcon(QIcon::fromTheme(QStringLiteral("face-smile"),
                                  QIcon::fromTheme(QStringLiteral("applications-games"))));
    tray.setToolTip(QStringLiteral("open-pet"));

    QMenu menu;

    QAction *toggleAction = menu.addAction(QObject::tr("Скрыть питомца"));
    QObject::connect(toggleAction, &QAction::triggered, [&] {
        userWantsPet = !userWantsPet;
        toggleAction->setText(userWantsPet ? QObject::tr("Скрыть питомца")
                                           : QObject::tr("Показать питомца"));
        applyVisibility();
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

    // Открытие окна вынесено в функцию: его дёргают и из трея, и из ключа
    // диагностики, потому что нажать пункт меню из скрипта нельзя.
    const auto openSettings = [&] {
        // Окно создаётся по требованию и живёт до закрытия: держать его
        // в памяти всё время работы ради редкого обращения незачем.
        static QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OpenPet/Ui/SettingsWindow.qml")));
        static QObject *window = nullptr;

        if (!window) {
            window = component.create(engine.rootContext());
            if (!window) {
                qCWarning(logApp).noquote() << "окно настроек не открылось:" << component.errorString();
                return;
            }
        }

        QMetaObject::invokeMethod(window, "show");
        QMetaObject::invokeMethod(window, "raise");
    };

    QAction *settingsAction = menu.addAction(QObject::tr("Настройки…"));
    QObject::connect(settingsAction, &QAction::triggered, openSettings);

    if (qEnvironmentVariableIsSet("OPENPET_SETTINGS"))
        QTimer::singleShot(0, openSettings);

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
