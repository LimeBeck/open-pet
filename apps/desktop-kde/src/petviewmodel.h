#pragma once

#include "corebridge.h"

#include <QObject>
#include <QMargins>
#include <QPoint>
#include <QVariantList>
#include <QString>
#include <QUrl>
#include <QTimer>

// ViewModel питомца: то, что видит QML.
//
// Граница здесь такая же строгая, как у FFI, только с другой стороны:
// QML не знает ни о ядре, ни о событиях рабочего стола — только о текущей
// эмоции и о том, приостановлены ли реакции.
class LlmClient;
class OverlaySurface;

class PetViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int emotion READ emotion NOTIFY emotionChanged)
    Q_PROPERTY(QString emotionName READ emotionName NOTIFY emotionChanged)
    Q_PROPERTY(QString phrase READ phrase NOTIFY phraseChanged)
    // Раскладка кадров приходит из ядра вместе с состоянием: QML не знает,
    // как устроен Pet Pack, и переживёт появление второго формата (ADR-005).
    Q_PROPERTY(int animationRow READ animationRow NOTIFY emotionChanged)
    Q_PROPERTY(int animationStartColumn READ animationStartColumn NOTIFY emotionChanged)
    Q_PROPERTY(int animationFrames READ animationFrames NOTIFY emotionChanged)
    Q_PROPERTY(int animationFrameDuration READ animationFrameDuration NOTIFY emotionChanged)
    Q_PROPERTY(QUrl sheetSource READ sheetSource NOTIFY sheetSourceChanged)

    // Процедурное движение (ADR-009). Список точек отдаётся в QML как есть:
    // ядро их уже проверило, интерполяция — дело хоста.
    Q_PROPERTY(QVariantList motionKeyframes READ motionKeyframes NOTIFY emotionChanged)
    Q_PROPERTY(int motionDurationMs READ motionDurationMs NOTIFY emotionChanged)
    Q_PROPERTY(bool motionLoops READ motionLoops NOTIFY emotionChanged)
    // Резерв поверхности под траекторию, в логических пикселях.
    Q_PROPERTY(int motionLeft READ motionLeft CONSTANT)
    Q_PROPERTY(int motionTop READ motionTop CONSTANT)
    Q_PROPERTY(int motionRight READ motionRight CONSTANT)
    Q_PROPERTY(int motionBottom READ motionBottom CONSTANT)
    Q_PROPERTY(int cellWidth READ cellWidth NOTIFY emotionChanged)
    Q_PROPERTY(int cellHeight READ cellHeight NOTIFY emotionChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(qreal scale READ scale WRITE setScale NOTIFY scaleChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)

public:
    explicit PetViewModel(CoreBridge *core, QObject *parent = nullptr);

    int emotion() const { return m_emotion; }
    QString emotionName() const;

    QString phrase() const { return m_phrase; }

    int animationRow() const { return m_animation.row; }
    int animationStartColumn() const { return m_animation.startColumn; }
    int animationFrames() const { return m_animation.frames; }
    int animationFrameDuration() const { return m_animation.frameDurationMs; }
    QUrl sheetSource() const { return m_sheetSource; }

    QVariantList motionKeyframes() const;
    int motionDurationMs() const { return m_motion.durationMs; }
    bool motionLoops() const { return m_motion.loops; }
    int motionLeft() const { return m_envelope.left(); }
    int motionTop() const { return m_envelope.top(); }
    int motionRight() const { return m_envelope.right(); }
    int motionBottom() const { return m_envelope.bottom(); }
    void setSheetSource(const QUrl &source);

    int cellWidth() const { return m_animation.cellWidth; }
    int cellHeight() const { return m_animation.cellHeight; }

    bool paused() const;
    void setPaused(bool paused);

    qreal scale() const { return m_scale; }
    void setScale(qreal scale);

    bool reducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool reduced);

    // Единственное намерение, которое UI отправляет вниз (§FR-2).
    // Перечитывает раскладку кадров у ядра, не меняя эмоции. Нужно после
    // смены Pet Pack: состояние то же, а строки листа уже другие.
    void refreshAnimation();

    Q_INVOKABLE void handleClick();

    // Перетаскивание (§FR-2). Разбито на четыре вызова намеренно: реплика
    // должна прозвучать один раз за перетаскивание, а не на каждый пиксель.
    Q_INVOKABLE void handleDragStart();
    // Начало перетаскивания: поверхность растягивается на экран,
    // возвращается положение питомца внутри неё. Дальше QML двигает
    // питомца сам, не трогая окно, — так исчезает петля обратной связи,
    // из-за которой смещение считалось в движущейся системе отсчёта.
    Q_INVOKABLE QPoint beginDrag();
    Q_INVOKABLE void endDrag(int x, int y);
    Q_INVOKABLE void finishDrag();

    void setOverlay(OverlaySurface *overlay) { m_overlay = overlay; }

    // Пузырь закрывается по клику, не только по таймеру (§FR-6).
    Q_INVOKABLE void dismissPhrase();

    // Необязательная зависимость: без неё питомец говорит только шаблонами.
    void setLlmClient(LlmClient *client);

signals:
    void emotionChanged();
    void phraseChanged();
    void sheetSourceChanged();
    void pausedChanged();
    void scaleChanged();
    void reducedMotionChanged();
    // Питомца отпустили: хосту пора записать новое положение.
    void placementSettled();

private:
    void applyEmotion(int emotion);
    // Микродвижение меняет только анимацию: эмоция остаётся прежней,
    // потому что ничего не произошло.
    void applyAnimation(int animation);

    void showPhrase(const QString &text, int ttlMs);
    void requestPhrase(const QString &fallback, int ttlMs);

    CoreBridge *m_core = nullptr;
    int m_emotion = 0;
    QString m_phrase;
    CoreBridge::Animation m_animation;
    // Встроенный питомец лежит в ресурсах, импортированный — на диске.
    // QML это различие не касается: он получает готовый URL.
    QUrl m_sheetSource = QUrl(QStringLiteral("qrc:/qt/qml/OpenPet/Ui/lime.png"));
    QTimer m_phraseTimer;

    // Необязательная зависимость: без неё питомец говорит только шаблонами.
    LlmClient *m_llm = nullptr;
    OverlaySurface *m_overlay = nullptr;
    CoreBridge::Motion m_motion;
    QMargins m_envelope;

    // Шаблон, ждущий, пока ответит модель. Если она не ответит или ответит
    // негодно, покажется именно он (§FR-6).
    QString m_pendingTemplate;
    int m_pendingTtlMs = 0;
    // Номер поколения: ответ, опоздавший к следующему состоянию,
    // не показывается — он уже не про то, что происходит.
    quint64 m_generation = 0;
    quint64 m_awaitingGeneration = 0;

    qreal m_scale = 1.0;
    bool m_reducedMotion = false;
};
