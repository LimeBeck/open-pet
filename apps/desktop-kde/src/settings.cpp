#include "settings.h"

#include <QSettings>

namespace {

QString cornerToString(OverlaySurface::Corner corner)
{
    switch (corner) {
    case OverlaySurface::Corner::BottomLeft:
        return QStringLiteral("bottom-left");
    case OverlaySurface::Corner::TopRight:
        return QStringLiteral("top-right");
    case OverlaySurface::Corner::TopLeft:
        return QStringLiteral("top-left");
    case OverlaySurface::Corner::BottomRight:
        break;
    }
    return QStringLiteral("bottom-right");
}

OverlaySurface::Corner cornerFromString(const QString &value)
{
    if (value == QLatin1String("bottom-left"))
        return OverlaySurface::Corner::BottomLeft;
    if (value == QLatin1String("top-right"))
        return OverlaySurface::Corner::TopRight;
    if (value == QLatin1String("top-left"))
        return OverlaySurface::Corner::TopLeft;
    return OverlaySurface::Corner::BottomRight;
}

} // namespace

Settings Settings::load()
{
    QSettings store;
    Settings settings;

    settings.corner = cornerFromString(
        store.value(QStringLiteral("overlay/corner"), QStringLiteral("bottom-right")).toString());
    settings.marginRight = store.value(QStringLiteral("overlay/marginRight"), 8).toInt();
    settings.marginBottom = store.value(QStringLiteral("overlay/marginBottom"), 8).toInt();
    settings.scale = store.value(QStringLiteral("overlay/scale"), 1.0).toReal();
    settings.paused = store.value(QStringLiteral("behavior/paused"), false).toBool();
    settings.reducedMotion = store.value(QStringLiteral("a11y/reducedMotion"), false).toBool();
    settings.idleSeconds = store.value(QStringLiteral("behavior/idleSeconds"), 300).toInt();

    settings.sourceIdle = store.value(QStringLiteral("sources/idle"), true).toBool();
    settings.sourcePower = store.value(QStringLiteral("sources/power"), true).toBool();
    settings.sourceSession = store.value(QStringLiteral("sources/session"), true).toBool();
    settings.sourceMedia = store.value(QStringLiteral("sources/media"), true).toBool();
    settings.sourceNotification = store.value(QStringLiteral("sources/notification"), true).toBool();
    settings.sourceActiveApp = store.value(QStringLiteral("sources/activeApp"), true).toBool();

    settings.llmKind = store.value(QStringLiteral("llm/kind"), 0).toInt();
    settings.llmBaseUrl = store.value(QStringLiteral("llm/baseUrl")).toString();
    settings.llmModel = store.value(QStringLiteral("llm/model")).toString();
    settings.llmProject = store.value(QStringLiteral("llm/project")).toString();
    settings.llmRegion = store.value(QStringLiteral("llm/region")).toString();
    settings.llmTimeoutMs = store.value(QStringLiteral("llm/timeoutMs"), 2500).toInt();

    settings.proxyMode = store.value(QStringLiteral("proxy/mode"), 0).toInt();
    settings.proxyHost = store.value(QStringLiteral("proxy/host")).toString();
    settings.proxyPort = store.value(QStringLiteral("proxy/port"), 0).toInt();
    settings.proxyUser = store.value(QStringLiteral("proxy/user")).toString();
    settings.proxyBypassLocal = store.value(QStringLiteral("proxy/bypassLocal"), true).toBool();

    // Чужие или испорченные значения не должны делать питомца невидимым.
    settings.scale = qBound(0.75, settings.scale, 2.0);
    settings.marginRight = qBound(0, settings.marginRight, 4000);
    settings.marginBottom = qBound(0, settings.marginBottom, 4000);
    settings.idleSeconds = qBound(5, settings.idleSeconds, 3600);
    settings.llmKind = qBound(0, settings.llmKind, 3);
    settings.llmTimeoutMs = qBound(500, settings.llmTimeoutMs, 60000);
    settings.proxyMode = qBound(0, settings.proxyMode, 2);
    settings.proxyPort = qBound(0, settings.proxyPort, 65535);

    return settings;
}

void Settings::save() const
{
    QSettings store;
    store.setValue(QStringLiteral("overlay/corner"), cornerToString(corner));
    store.setValue(QStringLiteral("overlay/marginRight"), marginRight);
    store.setValue(QStringLiteral("overlay/marginBottom"), marginBottom);
    store.setValue(QStringLiteral("overlay/scale"), scale);
    store.setValue(QStringLiteral("behavior/paused"), paused);
    store.setValue(QStringLiteral("a11y/reducedMotion"), reducedMotion);
    store.setValue(QStringLiteral("behavior/idleSeconds"), idleSeconds);

    store.setValue(QStringLiteral("sources/idle"), sourceIdle);
    store.setValue(QStringLiteral("sources/power"), sourcePower);
    store.setValue(QStringLiteral("sources/session"), sourceSession);
    store.setValue(QStringLiteral("sources/media"), sourceMedia);
    store.setValue(QStringLiteral("sources/notification"), sourceNotification);
    store.setValue(QStringLiteral("sources/activeApp"), sourceActiveApp);

    store.setValue(QStringLiteral("llm/kind"), llmKind);
    store.setValue(QStringLiteral("llm/baseUrl"), llmBaseUrl);
    store.setValue(QStringLiteral("llm/model"), llmModel);
    store.setValue(QStringLiteral("llm/project"), llmProject);
    store.setValue(QStringLiteral("llm/region"), llmRegion);
    store.setValue(QStringLiteral("llm/timeoutMs"), llmTimeoutMs);

    store.setValue(QStringLiteral("proxy/mode"), proxyMode);
    store.setValue(QStringLiteral("proxy/host"), proxyHost);
    store.setValue(QStringLiteral("proxy/port"), proxyPort);
    store.setValue(QStringLiteral("proxy/user"), proxyUser);
    store.setValue(QStringLiteral("proxy/bypassLocal"), proxyBypassLocal);
}

void Settings::resetLocalData()
{
    // Стираются настройки и история показов. Импортированные Pet Pack
    // не трогаются: §9 требует для них отдельного подтверждения.
    QSettings store;
    store.clear();
    store.sync();
}
