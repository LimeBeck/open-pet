pragma ComponentBehavior: Bound

import QtQuick

// Процедурный слой движения ([ADR-009](../../docs/adr/0009-procedural-motion-layer.md)).
//
// Отвечает только за смещение уже выбранного визуала. Позу меняет спрайтовый
// таймер со своим темпом — это два независимых контура, и смешивать их
// значит либо тратить кадры впустую, либо получать рваное движение.
//
// Непрерывное обновление включается только пока движение идёт. Как только оно
// закончилось, сцена снова грязнится лишь при смене спрайтового кадра, и
// достигнутые 0.30% CPU в покое сохраняются.
Item {
    id: root

    // Точки от ядра: доли по возрастанию от 0.0 до 1.0, смещения в пределах
    // лимита. Проверка уже сделана в ядре, здесь ничего не перепроверяется.
    property var keyframes: []
    property int durationMs: 0
    property bool loops: false

    // Отключает движение целиком (§7): самопроизвольное перемещение — первое,
    // что мешает при reduced motion.
    property bool motionEnabled: true

    // Плотность пикселей экрана. Нужна, чтобы промежуточное положение
    // не оказалось между физическими пикселями и не размыло пиксель-арт.
    property real pixelRatio: 1.0

    readonly property bool active: motionEnabled && durationMs > 0 && keyframes.length >= 2

    // Результат: смещение, уже привязанное к сетке физических пикселей.
    readonly property real offsetX: active ? snap(rawX) : 0
    readonly property real offsetY: active ? snap(rawY) : 0

    property real rawX: 0
    property real rawY: 0
    property real elapsed: 0

    // Доля пройденного, 0..1. По ней спрайт синхронизируется с траекторией:
    // поза и высота — две половины одного движения, и разводить их
    // по независимым часам значит гарантировать расхождение.
    readonly property real progress: active && durationMs > 0
        ? Math.min(1, elapsed / durationMs)
        : -1

    function snap(value) {
        // Привязка к физическим пикселям: движение остаётся плавным по времени,
        // но текстура не попадает между пикселями при дробном масштабе.
        const ratio = root.pixelRatio > 0 ? root.pixelRatio : 1.0
        return Math.round(value * ratio) / ratio
    }

    function ease(kind, t) {
        switch (kind) {
        case 1: return t * t                        // in-quad
        case 2: return t * (2 - t)                  // out-quad
        case 3: return t < 0.5 ? 2 * t * t          // in-out-quad
                               : -1 + (4 - 2 * t) * t
        default: return t                           // linear
        }
    }

    function sample(progress) {
        const frames = root.keyframes
        // Отрезок ищется по возрастанию доли — ядро гарантирует этот порядок.
        for (let i = 0; i < frames.length - 1; ++i) {
            const from = frames[i]
            const to = frames[i + 1]
            if (progress > to.at)
                continue

            const span = to.at - from.at
            // Совпадающие доли ядро не пропускает, но деление на ноль
            // здесь было бы слишком дорогой ошибкой, чтобы на это полагаться.
            const local = span > 0 ? (progress - from.at) / span : 1
            const eased = root.ease(from.easing, local)

            root.rawX = from.x + (to.x - from.x) * eased
            root.rawY = from.y + (to.y - from.y) * eased
            return
        }

        const last = frames[frames.length - 1]
        root.rawX = last.x
        root.rawY = last.y
    }

    // Смена анимации обрывает движение и начинает новое от нейтральной точки:
    // перенос остаточной скорости между состояниями сделал бы поведение
    // непредсказуемым (ADR-009).
    onKeyframesChanged: {
        elapsed = 0
        rawX = 0
        rawY = 0
        if (active)
            sample(0)
    }

    FrameAnimation {
        // Единственное место, где приложение просит кадры непрерывно.
        running: root.active
        onTriggered: {
            root.elapsed += frameTime * 1000

            if (root.elapsed >= root.durationMs) {
                if (!root.loops) {
                    // Движение кончилось: смещение обнуляется, непрерывные
                    // кадры прекращаются.
                    root.elapsed = 0
                    root.rawX = 0
                    root.rawY = 0
                    root.durationMs = 0
                    return
                }
                root.elapsed -= root.durationMs
            }

            root.sample(root.elapsed / root.durationMs)
        }
    }
}
