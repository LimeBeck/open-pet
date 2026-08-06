#include "petviewmodel.h"

#include "corebridge.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(logCore)

namespace {

// Порядок совпадает с OpenPetEmotion в контракте и с Emotion::name() в ядре.
// Три места, которые обязаны сходиться, — цена плоского C ABI (ADR-001).
const char *const kEmotionNames[] = {
    "idle", "happy", "curious", "sleepy",
    "charging", "low_battery", "notification", "busy",
};

constexpr int kEmotionCount = int(std::size(kEmotionNames));

} // namespace

PetViewModel::PetViewModel(CoreBridge *core, QObject *parent)
    : QObject(parent)
    , m_core(core)
{
    if (!m_core)
        return;

    m_emotion = m_core->currentEmotion();

    connect(m_core, &CoreBridge::reactionReceived, this,
            [this](const CoreBridge::Reaction &reaction) { applyEmotion(reaction.animation); });

    connect(m_core, &CoreBridge::settled, this,
            [this](int emotion) { applyEmotion(emotion); });
}

QString PetViewModel::emotionName() const
{
    if (m_emotion < 0 || m_emotion >= kEmotionCount)
        return QStringLiteral("idle");
    return QString::fromLatin1(kEmotionNames[m_emotion]);
}

void PetViewModel::applyEmotion(int emotion)
{
    if (emotion == m_emotion)
        return;

    m_emotion = emotion;
    emit emotionChanged();
}

bool PetViewModel::paused() const
{
    return m_core && m_core->isPaused();
}

void PetViewModel::setPaused(bool paused)
{
    if (!m_core || m_core->isPaused() == paused)
        return;

    m_core->setPaused(paused);
    emit pausedChanged();
}

void PetViewModel::setScale(qreal scale)
{
    // Диапазон из §FR-1: 75–200%.
    const qreal clamped = qBound(0.75, scale, 2.0);
    if (qFuzzyCompare(clamped, m_scale))
        return;

    m_scale = clamped;
    emit scaleChanged();
}

void PetViewModel::setReducedMotion(bool reduced)
{
    if (m_reducedMotion == reduced)
        return;

    m_reducedMotion = reduced;
    emit reducedMotionChanged();
}

void PetViewModel::handleClick()
{
    if (m_core)
        m_core->pushPetClicked();
}
