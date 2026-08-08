pragma ComponentBehavior: Bound

import QtQuick

// Питомец: кадры спрайтового листа активного Pet Pack.
//
// Раскладка приходит снаружи — какая анимация в какой строке лежит, знает
// только ядро. Собственной таблицы здесь нет намеренно: с ней второй способ
// отрисовки пришлось бы вживлять прямо сюда
// ([ADR-005](../../docs/adr/0005-pet-pack-sprite-sheet.md)).
//
// Кадр меняется таймером, а не AnimatedSprite. Разница не в удобстве:
// AnimatedSprite перерисовывается каждый кадр экрана независимо от того,
// сменился ли спрайт, и на дисплее 90 Гц это 90 перерисовок в секунду
// вместо четырёх. Здесь сцена грязнится ровно при смене кадра, а между
// сменами не происходит ничего.
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

    // Номер кадра внутри анимации, не столбец листа.
    property int frameIndex: 0

    implicitWidth: root.cellWidth
    implicitHeight: root.cellHeight

    // Смена анимации начинает её с первого кадра: иначе питомец,
    // перешедший в короткое состояние, показал бы его с середины.
    onRowChanged: root.frameIndex = 0
    onStartColumnChanged: root.frameIndex = 0
    onFramesChanged: root.frameIndex = 0

    Item {
        anchors.fill: parent
        // Окно в лист: видно ровно одну ячейку.
        clip: true

        Image {
            id: sheetImage

            source: root.sheet
            // Лист сдвигается целиком — это перенос уже загруженной текстуры,
            // без повторного разбора PNG на каждом кадре.
            x: -(root.startColumn + root.frameIndex) * root.cellWidth
            y: -root.row * root.cellHeight

            // Пиксель-арт: сглаживание превратило бы его в мыло.
            smooth: false
            cache: false
            // Лист кладётся в текстуру как есть: масштабирование целиком
            // исказило бы соседние ячейки по краям.
            fillMode: Image.Pad
            horizontalAlignment: Image.AlignLeft
            verticalAlignment: Image.AlignTop
        }
    }

    Timer {
        // При reduced motion питомец замирает на первом кадре состояния,
        // но остаётся видимым и меняет позу при смене эмоции (§7).
        running: root.animated && root.frames > 1 && root.sheet != ""
        interval: Math.max(20, root.frameDuration)
        repeat: true
        onTriggered: root.frameIndex = (root.frameIndex + 1) % Math.max(1, root.frames)
    }
}
