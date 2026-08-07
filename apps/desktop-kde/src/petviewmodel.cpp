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

// Сколько держать пузырь с репликой. Привязывать его к ttl состояния нельзя:
// сон длится час, и реплика висела бы всё это время. Нижняя граница нужна,
// чтобы короткие состояния успевали быть прочитанными (§FR-6).
constexpr int kBubbleMinMs = 2500;
constexpr int kBubbleMaxMs = 6000;

} // namespace

PetViewModel::PetViewModel(CoreBridge *core, QObject *parent)
    : QObject(parent)
    , m_core(core)
{
    if (!m_core)
        return;

    m_emotion = m_core->currentEmotion();

    m_phraseTimer.setSingleShot(true);
    connect(&m_phraseTimer, &QTimer::timeout, this, &PetViewModel::dismissPhrase);

    connect(m_core, &CoreBridge::reactionReceived, this,
            [this](const CoreBridge::Reaction &reaction) {
                applyEmotion(reaction.animation);
                showPhrase(reaction.phrase, reaction.ttlMs);
            });

    connect(m_core, &CoreBridge::settled, this, [this](int emotion) {
        applyEmotion(emotion);
        // Возврат в покой убирает и реплику: питомец успокоился, говорить
        // больше не о чем.
        dismissPhrase();
    });
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

void PetViewModel::showPhrase(const QString &text, int ttlMs)
{
    if (text.isEmpty()) {
        // Молчаливая реакция не должна гасить реплику, которая ещё читается:
        // пусть висит свой срок.
        return;
    }

    m_phrase = text;
    emit phraseChanged();

    m_phraseTimer.start(qBound(kBubbleMinMs, ttlMs, kBubbleMaxMs));
}

void PetViewModel::dismissPhrase()
{
    m_phraseTimer.stop();

    if (m_phrase.isEmpty())
        return;

    m_phrase.clear();
    emit phraseChanged();
}

void PetViewModel::handleClick()
{
    if (m_core)
        m_core->pushPetClicked();
}
