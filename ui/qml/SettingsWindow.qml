import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// Окно настроек (§4.1, §9).
//
// Обычное окно, а не layer-shell поверхность: у настроек нет причин
// висеть поверх всего.
Window {
    id: root

    property var model: settingsModel

    // Фон берётся из системной палитры. Без этого окно получает светлый фон
    // по умолчанию, а текст — цвет из тёмной системной темы, и подписи
    // становятся нечитаемыми.
    color: palette.window

    width: 560
    height: 680
    minimumWidth: 460
    minimumHeight: 480
    title: qsTr("open-pet — настройки")

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: root.width - 32
            x: 16
            y: 16
            spacing: 18

            // --- Внешний вид ---
            Label { text: qsTr("Внешний вид"); font.bold: true }

            GridLayout {
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Layout.fillWidth: true

                Label { text: qsTr("Угол экрана") }
                ComboBox {
                    Layout.fillWidth: true
                    model: ["bottom-right", "bottom-left", "top-right", "top-left"]
                    currentIndex: Math.max(0, model.indexOf(root.model.corner))
                    onActivated: root.model.corner = model[currentIndex]
                }

                Label { text: qsTr("Отступ справа") }
                SpinBox {
                    Layout.fillWidth: true
                    from: 0; to: 4000
                    value: root.model.marginRight
                    onValueModified: root.model.marginRight = value
                }

                Label { text: qsTr("Отступ снизу") }
                SpinBox {
                    Layout.fillWidth: true
                    from: 0; to: 4000
                    value: root.model.marginBottom
                    onValueModified: root.model.marginBottom = value
                }

                Label { text: qsTr("Масштаб") }
                RowLayout {
                    Layout.fillWidth: true
                    Slider {
                        Layout.fillWidth: true
                        from: 0.75; to: 2.0; stepSize: 0.05
                        value: root.model.scale
                        onMoved: root.model.scale = value
                    }
                    Label { text: Math.round(root.model.scale * 100) + "%" }
                }
            }

            CheckBox {
                text: qsTr("Уменьшить движение")
                checked: root.model.reducedMotion
                onToggled: root.model.reducedMotion = checked
            }

            MenuSeparator { Layout.fillWidth: true }

            // --- Поведение ---
            Label { text: qsTr("Поведение"); font.bold: true }

            CheckBox {
                text: qsTr("Пауза реакций")
                checked: root.model.paused
                onToggled: root.model.paused = checked
            }

            CheckBox {
                text: qsTr("Запускать при входе в систему")
                checked: root.model.autostart
                onToggled: root.model.autostart = checked
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Засыпать после простоя, с") }
                SpinBox {
                    from: 5; to: 3600; stepSize: 5
                    value: root.model.idleSeconds
                    onValueModified: root.model.idleSeconds = value
                }
            }

            MenuSeparator { Layout.fillWidth: true }

            // --- Источники событий (§9) ---
            Label { text: qsTr("Источники событий"); font.bold: true }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.75
                text: qsTr("Выключенный источник не подписывается на события вовсе — "
                           + "приложение их не получает, а не отбрасывает после получения.")
            }

            ColumnLayout {
                spacing: 2
                CheckBox {
                    text: qsTr("Простой и возвращение")
                    checked: root.model.sourceIdle
                    onToggled: root.model.sourceIdle = checked
                }
                CheckBox {
                    text: qsTr("Питание и заряд")
                    checked: root.model.sourcePower
                    onToggled: root.model.sourcePower = checked
                }
                CheckBox {
                    text: qsTr("Сон и блокировка экрана")
                    checked: root.model.sourceSession
                    onToggled: root.model.sourceSession = checked
                }
                CheckBox {
                    text: qsTr("Воспроизведение медиа")
                    checked: root.model.sourceMedia
                    onToggled: root.model.sourceMedia = checked
                }
                CheckBox {
                    text: qsTr("Уведомления (видно закрытие, а не появление)")
                    checked: root.model.sourceNotification
                    onToggled: root.model.sourceNotification = checked
                }
                CheckBox {
                    text: qsTr("Активное приложение (нужен KWin-скрипт)")
                    checked: root.model.sourceActiveApp
                    onToggled: root.model.sourceActiveApp = checked
                }
            }

            MenuSeparator { Layout.fillWidth: true }

            // --- LLM ---
            Label { text: qsTr("Реплики через LLM"); font.bold: true }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.75
                text: qsTr("Пока провайдер не выбран, приложение не делает ни одного "
                           + "сетевого запроса. При ошибке или таймауте показывается "
                           + "локальный шаблон.")
            }

            GridLayout {
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Layout.fillWidth: true

                Label { text: qsTr("Провайдер") }
                ComboBox {
                    id: providerBox
                    Layout.fillWidth: true
                    model: [qsTr("Выключено"), "Ollama", qsTr("OpenAI-совместимый"), "Vertex AI"]
                    currentIndex: root.model.llmKind
                    onActivated: root.model.llmKind = currentIndex
                }

                Label { text: qsTr("Адрес"); visible: root.model.llmKind > 0 }
                TextField {
                    Layout.fillWidth: true
                    visible: root.model.llmKind > 0
                    placeholderText: "http://127.0.0.1:11434"
                    text: root.model.llmBaseUrl
                    onEditingFinished: root.model.llmBaseUrl = text
                }

                Label { text: qsTr("Модель"); visible: root.model.llmKind > 0 }
                TextField {
                    Layout.fillWidth: true
                    visible: root.model.llmKind > 0
                    text: root.model.llmModel
                    onEditingFinished: root.model.llmModel = text
                }

                Label { text: qsTr("Таймаут, мс"); visible: root.model.llmKind > 0 }
                SpinBox {
                    visible: root.model.llmKind > 0
                    from: 500; to: 60000; stepSize: 500
                    value: root.model.llmTimeoutMs
                    onValueModified: root.model.llmTimeoutMs = value
                }

                Label { text: qsTr("Ключ API"); visible: root.model.llmKind === 2 }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.model.llmKind === 2

                    TextField {
                        id: keyField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        enabled: root.model.secretStorageAvailable
                        placeholderText: root.model.hasApiKey
                            ? qsTr("сохранён в KWallet")
                            : qsTr("не сохранён")
                    }
                    Button {
                        text: qsTr("Сохранить")
                        enabled: root.model.secretStorageAvailable && keyField.text.length > 0
                        onClicked: {
                            root.model.storeApiKey(keyField.text)
                            keyField.text = ""
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: root.model.llmKind === 2 && !root.model.secretStorageAvailable
                color: "#c0392b"
                text: qsTr("KWallet недоступен, поэтому ключ сохранить некуда. "
                           + "Записывать его в обычный файл настроек приложение не будет.")
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.model.llmKind > 0

                Button {
                    text: qsTr("Проверить связь")
                    onClicked: root.model.checkConnection()
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: root.model.healthStatus
                }
            }

            // §9: точный payload до включения сетевого провайдера.
            Button {
                text: qsTr("Показать, что уйдёт провайдеру")
                visible: root.model.llmKind > 0
                onClicked: {
                    payloadText.text = root.model.payloadPreview()
                    payloadBox.visible = true
                }
            }

            Frame {
                id: payloadBox
                Layout.fillWidth: true
                visible: false

                ColumnLayout {
                    anchors.fill: parent
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Это настоящее тело запроса, а не образец. "
                                   + "Ни идентификатора приложения, ни текста уведомления, "
                                   + "ни истории здесь нет и быть не может.")
                        opacity: 0.75
                    }
                    TextArea {
                        id: payloadText
                        Layout.fillWidth: true
                        readOnly: true
                        wrapMode: TextEdit.Wrap
                        font.family: "monospace"
                        font.pixelSize: 11
                    }
                }
            }

            MenuSeparator { Layout.fillWidth: true }

            // --- Питомец (§US-07) ---
            Label { text: qsTr("Питомец"); font.bold: true }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.75
                text: qsTr("Пакет проверяется до установки. Негодный отклоняется "
                           + "со списком причин, а текущий питомец остаётся на месте.")
            }

            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: qsTr("Импортировать…")
                    onClicked: packDialog.open()
                }
                Button {
                    text: qsTr("Вернуть встроенного")
                    onClicked: root.model.resetPackToBuiltin()
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("активен: %1").arg(root.model.activePackId)
                    elide: Text.ElideRight
                    opacity: 0.75
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: text.length > 0
                text: root.model.packStatus
                font.family: "monospace"
                font.pixelSize: 11
            }

            FileDialog {
                id: packDialog
                title: qsTr("Выберите Pet Pack")
                nameFilters: [qsTr("Pet Pack (*.zip *.petpack)"), qsTr("Все файлы (*)")]
                onAccepted: root.model.importPack(selectedFile)
            }

            MenuSeparator { Layout.fillWidth: true }

            // --- Прокси ---
            Label { text: qsTr("Прокси"); font.bold: true }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.75
                text: qsTr("Действует только на запросы к провайдеру: других сетевых "
                           + "обращений приложение не делает.")
            }

            GridLayout {
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Layout.fillWidth: true

                Label { text: qsTr("Режим") }
                ComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("Системный"), qsTr("Без прокси"), qsTr("Указать вручную")]
                    currentIndex: root.model.proxyMode
                    onActivated: root.model.proxyMode = currentIndex
                }

                Label { text: qsTr("Адрес"); visible: root.model.proxyMode === 2 }
                TextField {
                    Layout.fillWidth: true
                    visible: root.model.proxyMode === 2
                    placeholderText: "proxy.example.com"
                    text: root.model.proxyHost
                    onEditingFinished: root.model.proxyHost = text
                }

                Label { text: qsTr("Порт"); visible: root.model.proxyMode === 2 }
                SpinBox {
                    visible: root.model.proxyMode === 2
                    from: 0; to: 65535
                    value: root.model.proxyPort
                    onValueModified: root.model.proxyPort = value
                }

                Label { text: qsTr("Пользователь"); visible: root.model.proxyMode === 2 }
                TextField {
                    Layout.fillWidth: true
                    visible: root.model.proxyMode === 2
                    text: root.model.proxyUser
                    onEditingFinished: root.model.proxyUser = text
                }

                Label { text: qsTr("Пароль"); visible: root.model.proxyMode === 2 }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.model.proxyMode === 2

                    TextField {
                        id: proxyPasswordField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        enabled: root.model.secretStorageAvailable
                        placeholderText: root.model.secretStorageAvailable
                            ? qsTr("хранится в KWallet")
                            : qsTr("KWallet недоступен")
                    }
                    Button {
                        text: qsTr("Сохранить")
                        enabled: root.model.secretStorageAvailable
                                 && proxyPasswordField.text.length > 0
                        onClicked: {
                            root.model.storeProxyPassword(proxyPasswordField.text)
                            proxyPasswordField.text = ""
                        }
                    }
                }
            }

            CheckBox {
                text: qsTr("Локальные адреса мимо прокси")
                checked: root.model.proxyBypassLocal
                onToggled: root.model.proxyBypassLocal = checked
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.75
                visible: root.model.proxyBypassLocal
                text: qsTr("Ollama на 127.0.0.1 — приватный сценарий: заворачивать её "
                           + "во внешний прокси значит отправлять наружу то, "
                           + "что должно остаться дома.")
            }

            MenuSeparator { Layout.fillWidth: true }

            // --- Локальные данные (§9) ---
            Label { text: qsTr("Локальные данные"); font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("Сбросить настройки и историю")
                    onClicked: root.model.resetLocalData()
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    opacity: 0.75
                    text: qsTr("Импортированные питомцы не удаляются.")
                }
            }

            MenuSeparator { Layout.fillWidth: true }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: text.length > 0
                text: root.model.restartNotice
                opacity: 0.8
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Закрыть")
                    onClicked: root.close()
                }
                Button {
                    text: qsTr("Применить")
                    highlighted: true
                    onClicked: root.model.apply()
                }
            }

            Item { Layout.preferredHeight: 8 }
        }
    }
}
