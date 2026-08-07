#pragma once

#include "corebridge.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QTimer>

// ViewModel питомца: то, что видит QML.
//
// Граница здесь такая же строгая, как у FFI, только с другой стороны:
// QML не знает ни о ядре, ни о событиях рабочего стола — только о текущей
// эмоции и о том, приостановлены ли реакции.
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
    Q_INVOKABLE void handleClick();

    // Пузырь закрывается по клику, не только по таймеру (§FR-6).
    Q_INVOKABLE void dismissPhrase();

signals:
    void emotionChanged();
    void phraseChanged();
    void sheetSourceChanged();
    void pausedChanged();
    void scaleChanged();
    void reducedMotionChanged();

private:
    void applyEmotion(int emotion);

    void showPhrase(const QString &text, int ttlMs);

    CoreBridge *m_core = nullptr;
    int m_emotion = 0;
    QString m_phrase;
    CoreBridge::Animation m_animation;
    // Встроенный питомец лежит в ресурсах, импортированный — на диске.
    // QML это различие не касается: он получает готовый URL.
    QUrl m_sheetSource = QUrl(QStringLiteral("qrc:/qt/qml/OpenPet/Ui/lime.png"));
    QTimer m_phraseTimer;
    qreal m_scale = 1.0;
    bool m_reducedMotion = false;
};
