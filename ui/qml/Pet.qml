pragma ComponentBehavior: Bound

import QtQuick

// Встроенный питомец: спрайтовый лист 8×11, ячейка 192×208.
//
// Раскладка состояний по строкам листа и расхождение с форматом Pet Pack v1
// описаны в assets/builtin-pet/README.md.
//
// Анимация идёт покадрово из одной текстуры. Непрерывных QML-анимаций здесь
// намеренно нет: сцена становится грязной только при смене кадра, то есть
// 4–8 раз в секунду вместо каждого кадра дисплея. Для бюджета §7 это
// принципиально — 90 перерисовок в секунду на процедурном питомце давали
// 4–6% ядра при цели в 2%.
Item {
    id: root

    // Имя состояния из ядра: idle, happy, curious, sleepy, charging,
    // low_battery, notification, busy.
    property string emotion: "idle"
    property bool animated: true

    readonly property int cellWidth: 192
    readonly property int cellHeight: 208

    implicitWidth: cellWidth
    implicitHeight: cellHeight

    // Строка листа, начальный кадр, число кадров и темп для каждого состояния.
    // Значения совпадают с assets/builtin-pet/manifest.json; при переходе
    // на Pet Pack (M5) манифест станет единственным источником, а эта
    // таблица уйдёт.
    //
    // Строка 5 даёт две анимации: первые шесть кадров — сон, последние два —
    // низкий заряд. Ради этого и нужен startColumn.
    readonly property var animations: ({
        "idle":         { "row": 0, "startColumn": 0, "frames": 6, "duration": 220 },
        "happy":        { "row": 3, "startColumn": 0, "frames": 4, "duration": 150 },
        "curious":      { "row": 9, "startColumn": 0, "frames": 8, "duration": 170 },
        "sleepy":       { "row": 5, "startColumn": 0, "frames": 6, "duration": 420 },
        "charging":     { "row": 4, "startColumn": 0, "frames": 5, "duration": 120 },
        "low_battery":  { "row": 5, "startColumn": 6, "frames": 2, "duration": 700 },
        "notification": { "row": 6, "startColumn": 0, "frames": 6, "duration": 160 },
        "busy":         { "row": 7, "startColumn": 0, "frames": 6, "duration": 200 }
    })

    // Неизвестное состояние откатывается на idle — это fallbackAnimation
    // из §FR-8: отсутствие анимации не должно оставлять пустое окно.
    readonly property var current: root.animations[root.emotion] || root.animations["idle"]

    AnimatedSprite {
        id: sprite
        anchors.fill: parent

        source: "lime.png"
        frameWidth: root.cellWidth
        frameHeight: root.cellHeight
        frameX: root.current.startColumn * root.cellWidth
        frameY: root.current.row * root.cellHeight
        frameCount: root.current.frames
        frameDuration: root.current.duration

        loops: AnimatedSprite.Infinite
        // При reduced motion питомец замирает на первом кадре состояния,
        // но остаётся видимым и продолжает менять позу при смене эмоции (§7).
        running: true
        paused: !root.animated

        // Пиксель-арт: сглаживание превратило бы его в мыло.
        smooth: false

        // Кадры соседних строк не должны просачиваться по краю ячейки.
        interpolate: false
    }
}
