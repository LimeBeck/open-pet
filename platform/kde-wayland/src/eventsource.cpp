#include "eventsource.h"

QString capabilityStateName(CapabilityState state)
{
    switch (state) {
    case CapabilityState::Available:
        return QStringLiteral("available");
    case CapabilityState::PermissionRequired:
        return QStringLiteral("permission_required");
    case CapabilityState::Unsupported:
        return QStringLiteral("unsupported");
    case CapabilityState::Degraded:
        return QStringLiteral("degraded");
    }
    return QStringLiteral("unsupported");
}

void EventSource::setCapability(CapabilityState state, const QString &reason)
{
    if (m_capability == state)
        return;

    m_capability = state;
    emit capabilityChanged(state, reason);
}
