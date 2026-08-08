import QtQuick
import QtQuick.Window

Window {
    id: root

    // Над питомцем оставлено место под пузырь с репликой. Поля вокруг
    // остаются прозрачными и кликов не ловят — за это отвечает input region
    // (ADR-002), который пересчитывается и при появлении пузыря.
    readonly property int bubbleArea: 76

    // Размер окна в покое. Во время перетаскивания окно растянуто на экран,
    // и эти числа остаются мерой самой рамки питомца.
    // Резерв под траекторию. Считается один раз по всему пакету: растягивать
    // поверхность на каждое движение дорого, а движение случается часто
    // (ADR-009, вариант D отвергнут именно поэтому).
    readonly property int restWidth: Math.round(
        (petView.implicitWidth + petModel.motionLeft + petModel.motionRight) * petModel.scale)
    readonly property int restHeight: Math.round(
        (petView.implicitHeight + bubbleArea + petModel.motionTop + petModel.motionBottom)
        * petModel.scale)

    width: restWidth
    height: restHeight
    visible: false
    color: "transparent"
    title: "open-pet"

    // Рамка размером с окно в покое. Двигается именно она, а не питомец:
    // между ними разница в место под пузырь, и если считать положение
    // по питомцу, а ставить отступы по окну, питомец после отпускания
    // проседает ровно на эту разницу.
    Item {
        id: frame

        width: root.restWidth
        height: root.restHeight

        // Координаты держатся, пока окно ещё растянуто: если сбросить их
        // сразу, на кадр-другой питомец покажется в левом верхнем углу
        // растянутого окна.
        property point dragPos: Qt.point(0, 0)

        // Питомец не уходит за край экрана. Захват мыши продолжает слать
        // координаты и после того, как курсор покинул окно, поэтому без
        // ограничения рамка уезжала на тысячи пикселей: замер показал
        // старт 1728 и конец 3200 при ширине экрана 1920. На отпускании
        // отступ обрезался в ноль, и питомец прилипал к краю.
        //
        // Перетащить его на другой монитор всё равно нельзя — поверхность
        // растянута на один экран. Для переезда есть пункт трея (ADR-002).
        function place(nx, ny) {
            dragPos = Qt.point(Math.max(0, Math.min(nx, root.width - width)),
                               Math.max(0, Math.min(ny, root.height - height)))
        }
        readonly property bool floating: petMouse.dragging || root.width > root.restWidth
        x: floating ? dragPos.x : 0
        y: floating ? dragPos.y : 0

    Item {
        id: stage
        anchors.fill: parent

        scale: petModel.scale
        transformOrigin: Item.Center

        // Слой движения: смещает уже выбранный визуал, не трогая позу.
        Motion {
            id: motion
            keyframes: petModel.motionKeyframes
            durationMs: petModel.motionDurationMs
            loops: petModel.motionLoops
            // reduced motion выключает процедурное движение целиком (§7),
            // но спрайтовая анимация сохраняет своё прежнее поведение.
            motionEnabled: !petModel.reducedMotion
            pixelRatio: Screen.devicePixelRatio

            // Маска едет следом за визуалом. Смещение переводится
            // в координаты окна: внутри сцены оно в единицах до масштаба.
            onOffsetXChanged: petModel.setMotionOffset(
                Math.round(offsetX * petModel.scale), Math.round(offsetY * petModel.scale))
            onOffsetYChanged: petModel.setMotionOffset(
                Math.round(offsetX * petModel.scale), Math.round(offsetY * petModel.scale))
        }

        Pet {
            id: petView

            // Пока идёт движение, кадры ведёт оно же: иначе поза и высота
            // расходятся, и питомец приземляется, будучи в воздухе.
            syncProgress: motion.progress

            // Смещение от слоя движения.
            transform: Translate {
                x: motion.offsetX
                y: motion.offsetY
            }

            // Во время перетаскивания положение задаётся вручную: якоря
            // прижали бы питомца к краю растянутого на экран окна.
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
    }

    MouseArea {
        id: petMouse
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton

        // Порог отличает клик от перетаскивания. Без него дрожание руки
        // на клике уносило бы питомца с места, а клик переставал работать.
        readonly property int dragThreshold: 6

        property point pressPoint
        property point lastPoint
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

            if (!dragging) {
                const movedX = mouse.x - pressPoint.x
                const movedY = mouse.y - pressPoint.y
                if (Math.abs(movedX) + Math.abs(movedY) < dragThreshold)
                    return

                // Окно растягивается на экран и дальше не двигается.
                // Пока оно двигалось, смещение приходилось считать в его же
                // системе отсчёта, которую композитор обновляет асинхронно —
                // петля неустойчива по устройству (ADR-002).
                const spot = petModel.beginDrag()
                frame.place(spot.x, spot.y)

                dragging = true
                announced = true
                petModel.handleDragStart()
                return
            }

            // Система отсчёта теперь неподвижна, поэтому дельта честная.
            frame.place(frame.dragPos.x + mouse.x - pressPoint.x,
                        frame.dragPos.y + mouse.y - pressPoint.y)
            pressPoint = Qt.point(mouse.x, mouse.y)
        }

        // Захват мыши можно потерять и без отпускания — тогда onReleased
        // не придёт вовсе. Без этого обработчика питомец остался бы
        // в растянутом окне, а следующее перетаскивание не началось бы.
        onCanceled: {
            if (dragging) {
                petModel.endDrag(Math.round(frame.dragPos.x), Math.round(frame.dragPos.y))
                petModel.finishDrag()
            }
            dragging = false
        }

        onReleased: {
            if (dragging) {
                petModel.endDrag(Math.round(frame.dragPos.x), Math.round(frame.dragPos.y))
                petModel.finishDrag()
            }
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
