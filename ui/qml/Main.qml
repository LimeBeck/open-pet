import QtQuick

Window {
    id: root

    // Над питомцем оставлено место под пузырь с репликой. Поля вокруг
    // остаются прозрачными и кликов не ловят — за это отвечает input region
    // (ADR-002), который пересчитывается и при появлении пузыря.
    readonly property int bubbleArea: 76

    width: Math.round(petView.implicitWidth * petModel.scale)
    height: Math.round((petView.implicitHeight + bubbleArea) * petModel.scale)
    visible: false
    color: "transparent"
    title: "open-pet"

    Item {
        id: stage
        anchors.fill: parent
        scale: petModel.scale
        transformOrigin: Item.Center

        Pet {
            id: petView
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom

            // Всё, что знает QML о питомце: где лист и какие кадры показывать.
            // Что именно означает состояние — дело ядра.
            sheet: petModel.sheetSource
            cellWidth: petModel.cellWidth
            cellHeight: petModel.cellHeight
            row: petModel.animationRow
            startColumn: petModel.animationStartColumn
            frames: petModel.animationFrames
            frameDuration: petModel.animationFrameDuration

            // Пауза останавливает реакции, но не замораживает питомца:
            // он остаётся живым, просто перестаёт отзываться (§FR-2).
            animated: !petModel.reducedMotion
        }

        Bubble {
            id: bubble
            text: petModel.phrase
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: petView.top
            // Хвостик заходит на макушку питомца, иначе пузырь висит в воздухе.
            anchors.bottomMargin: -18
        }
    }

    MouseArea {
        id: petMouse
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton

        // Порог отличает клик от перетаскивания. Без него дрожание руки
        // на клике уносило бы питомца с места, а клик переставал работать.
        readonly property int dragThreshold: 6

        property point pressPoint
        property bool dragging: false
        property bool announced: false

        onPressed: (mouse) => {
            pressPoint = Qt.point(mouse.x, mouse.y)
            dragging = false
            announced = false
        }

        onPositionChanged: (mouse) => {
            if (!pressed)
                return

            // Порог считается по координатам окна и только до начала
            // перетаскивания: пока окно стоит на месте, они честные.
            if (!dragging) {
                const dx = mouse.x - pressPoint.x
                const dy = mouse.y - pressPoint.y
                if (Math.abs(dx) + Math.abs(dy) < dragThreshold)
                    return

                dragging = true

                // Реплика — один раз за перетаскивание, а не на каждый
                // пиксель (§FR-2). Заодно здесь запоминается опорная точка
                // курсора.
                announced = true
                petModel.handleDragStart()
                return
            }

            // Дальше окно двигается под курсором, и его координаты врут.
            // Хост считает смещение по глобальной позиции курсора сам.
            petModel.dragTick()
        }

        onReleased: {
            if (dragging)
                petModel.finishDrag()
            dragging = false
        }

        // Клик по пузырю закрывает реплику, клик по питомцу — просит новую
        // (§FR-6, §FR-2).
        onClicked: (mouse) => {
            if (dragging)
                return

            const inBubble = bubble.shown
                && mouse.y < stage.height - petView.implicitHeight * petModel.scale
            if (inBubble)
                petModel.dismissPhrase()
            else
                petModel.handleClick()
        }
    }
}
