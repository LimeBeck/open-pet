#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class CoreBridge;

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
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(qreal scale READ scale WRITE setScale NOTIFY scaleChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)

public:
    explicit PetViewModel(CoreBridge *core, QObject *parent = nullptr);

    int emotion() const { return m_emotion; }
    QString emotionName() const;

    QString phrase() const { return m_phrase; }

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
    void pausedChanged();
    void scaleChanged();
    void reducedMotionChanged();

private:
    void applyEmotion(int emotion);

    void showPhrase(const QString &text, int ttlMs);

    CoreBridge *m_core = nullptr;
    int m_emotion = 0;
    QString m_phrase;
    QTimer m_phraseTimer;
    qreal m_scale = 1.0;
    bool m_reducedMotion = false;
};
