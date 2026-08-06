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

    // Чужие или испорченные значения не должны делать питомца невидимым.
    settings.scale = qBound(0.75, settings.scale, 2.0);
    settings.marginRight = qBound(0, settings.marginRight, 4000);
    settings.marginBottom = qBound(0, settings.marginBottom, 4000);

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
}
