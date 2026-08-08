#include "petviewmodel.h"

#include "corebridge.h"
#include "llmclient.h"
#include "overlaysurface.h"

#include <QLoggingCategory>
#include <QScreen>
#include <QGuiApplication>
#include <QScreen>

Q_DECLARE_LOGGING_CATEGORY(logApp)

#include <QLoggingCategory>
#include <QScreen>

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
// Потолок поднят: реплики от модели длиннее шаблонных, и шести секунд
// на полсотни знаков не хватало.
constexpr int kBubbleMaxMs = 9000;

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

    connect(m_core, &CoreBridge::fidgeted, this, [this](int animation) {
        // При reduced motion питомец не дёргается сам: §7 требует, чтобы
        // движение можно было убрать, а самопроизвольное — первое, что мешает.
        if (!m_reducedMotion)
            applyAnimation(animation);
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

void PetViewModel::applyAnimation(int animation)
{
    if (!m_core)
        return;

    // Имя берётся из таблицы эмоций, но эмоция питомца при этом не меняется:
    // микродвижение — это смена позы без события.
    const int index = (animation < 0 || animation >= kEmotionCount) ? 0 : animation;
    const auto frames = m_core->animationFor(QString::fromLatin1(kEmotionNames[index]));
    if (frames == m_animation)
        return;

    m_animation = frames;
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

    // Пузырь живёт столько, сколько нужно прочитать, а не столько, сколько
    // держится эмоция. Это разные вещи: клик даёт эмоцию на 2.5 секунды,
    // а фразу от модели на полсотни знаков за это время не прочесть.
    //
    // 55 мс на знак — примерно 18 знаков в секунду, темп беглого чтения
    // текста, которого не ждёшь. Плюс полторы секунды на заметить.
    const int readingMs = 1500 + text.size() * 55;
    m_phraseTimer.start(qBound(kBubbleMinMs, qMax(ttlMs, readingMs), kBubbleMaxMs));
}

void PetViewModel::dismissPhrase()
{
    m_phraseTimer.stop();

    if (m_phrase.isEmpty())
        return;

    m_phrase.clear();
    emit phraseChanged();
}

void PetViewModel::handleDragStart()
{
    if (m_core)
        m_core->pushPetDragged();
}

QPoint PetViewModel::beginDrag()
{
    return m_overlay ? m_overlay->beginDrag() : QPoint();
}

void PetViewModel::endDrag(int x, int y)
{
    if (m_overlay)
        m_overlay->endDrag(QPoint(x, y));
}

void PetViewModel::finishDrag()
{

    // Положение сохраняется на отпускании, а не на каждом движении:
    // иначе перетаскивание через весь экран писало бы конфиг сотни раз.
    emit placementSettled();
}

void PetViewModel::refreshAnimation()
{
    applyEmotion(m_emotion);
}

void PetViewModel::handleClick()
{
    if (m_core)
        m_core->pushPetClicked();
}
