pragma ComponentBehavior: Bound

import QtQuick

// Питомец: кадры спрайтового листа активного Pet Pack.
//
// Раскладка приходит снаружи — какая анимация в какой строке лежит, знает
// только ядро. Собственной таблицы здесь нет намеренно: с ней второй способ
// отрисовки пришлось бы вживлять прямо сюда
// ([ADR-005](../../docs/adr/0005-pet-pack-sprite-sheet.md)).
//
// Непрерывных QML-анимаций тоже нет: сцена становится грязной только при
// смене кадра. Для бюджета §7 это принципиально.
Item {
    id: root

    property url sheet: ""
    property int cellWidth: 192
    property int cellHeight: 208
    property int row: 0
    property int startColumn: 0
    property int frames: 1
    property int frameDuration: 200
    property bool animated: true

    implicitWidth: root.cellWidth
    implicitHeight: root.cellHeight

    AnimatedSprite {
        id: sprite
        anchors.fill: parent

        source: root.sheet
        frameWidth: root.cellWidth
        frameHeight: root.cellHeight
        frameX: root.startColumn * root.cellWidth
        frameY: root.row * root.cellHeight
        frameCount: Math.max(1, root.frames)
        frameDuration: Math.max(20, root.frameDuration)

        loops: AnimatedSprite.Infinite
        // При reduced motion питомец замирает на первом кадре состояния,
        // но остаётся видимым и меняет позу при смене эмоции (§7).
        running: true
        paused: !root.animated

        // Пиксель-арт: сглаживание превратило бы его в мыло.
        smooth: false
        // Кадры соседних строк не должны просачиваться по краю ячейки.
        interpolate: false
    }
}
