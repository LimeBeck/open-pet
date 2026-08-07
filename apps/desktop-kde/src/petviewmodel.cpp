#include "petviewmodel.h"

#include "corebridge.h"
#include "llmclient.h"

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
    m_animation = m_core->animationFor(emotionName());

    m_phraseTimer.setSingleShot(true);
    connect(&m_phraseTimer, &QTimer::timeout, this, &PetViewModel::dismissPhrase);

    connect(m_core, &CoreBridge::reactionReceived, this,
            [this](const CoreBridge::Reaction &reaction) {
                applyEmotion(reaction.animation);
                requestPhrase(reaction.phrase, reaction.ttlMs);
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
    // Раскладка перечитывается вместе с состоянием: при смене Pet Pack
    // те же состояния могут лежать в других строках листа.
    if (m_core)
        m_animation = m_core->animationFor(emotionName());
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

void PetViewModel::setSheetSource(const QUrl &source)
{
    if (m_sheetSource == source)
        return;

    m_sheetSource = source;
    emit sheetSourceChanged();
}

void PetViewModel::setLlmClient(LlmClient *client)
{
    m_llm = client;
    if (!m_llm)
        return;

    connect(m_llm, &LlmClient::phraseReady, this, [this](const QString &phrase) {
        // Опоздавший ответ не показывается: состояние уже сменилось,
        // и реплика была бы не про то.
        if (m_awaitingGeneration != m_generation)
            return;
        showPhrase(phrase, m_pendingTtlMs);
        m_pendingTemplate.clear();
    });

    connect(m_llm, &LlmClient::phraseFailed, this, [this](const QString &) {
        if (m_awaitingGeneration != m_generation)
            return;
        // Прозрачный откат на шаблон (§FR-6): пользователь не должен
        // замечать, что модель не ответила.
        showPhrase(m_pendingTemplate, m_pendingTtlMs);
        m_pendingTemplate.clear();
    });
}

void PetViewModel::requestPhrase(const QString &fallback, int ttlMs)
{
    ++m_generation;

    if (fallback.isEmpty())
        return;

    if (!m_llm || !m_llm->isEnabled()) {
        showPhrase(fallback, ttlMs);
        return;
    }

    // Шаблон придерживается до ответа модели: подмена текста в уже открытом
    // пузыре читалась бы хуже, чем короткая пауза перед его появлением.
    m_pendingTemplate = fallback;
    m_pendingTtlMs = ttlMs;
    m_awaitingGeneration = m_generation;
    m_llm->requestPhrase();
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
