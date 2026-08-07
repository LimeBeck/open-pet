#pragma once

#include "eventsource.h"

#include <QDBusVariant>

// Питание и батарея через UPower по D-Bus (§FR-3).
//
// Наблюдается устройство DisplayDevice — сводка по всем батареям, которую
// UPower собирает сам. Читаются только состояние заряда и процент;
// серийные номера, модель и производитель устройства не запрашиваются.
class PowerAdapter : public EventSource
{
    Q_OBJECT

public:
    // Собственное перечисление, а не коды UPower: числа протокола не должны
    // расползаться по приложению — платформенные детали живут в адаптере.
    enum class Kind {
        Unknown,
        Charging,
        Discharging,
        Full,
    };
    Q_ENUM(Kind)

    explicit PowerAdapter(QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("power"); }
    void start() override;

signals:
    // `percent` равен -1, если процент недоступен: у стационарной машины
    // батареи может не быть вовсе.
    void powerChanged(bool onBattery, int percent, PowerAdapter::Kind kind);

private slots:
    void onPropertiesChanged(const QString &interface,
                             const QVariantMap &changed,
                             const QStringList &invalidated);

private:
    void publishIfChanged();

    bool m_hasLast = false;
    bool m_lastOnBattery = false;
    int m_lastPercent = -1;
    Kind m_lastKind = Kind::Unknown;
};
