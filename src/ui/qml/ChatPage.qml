import QtQuick 2.0
import QtQuick.Controls 1.0
import QtQuick.Controls.Styles 1.3
import QtQuick.Layouts 1.0
import im.ricochet 1.0
import "style.js" as Style

FocusScope {
    id: chatPage

    property var contact
    property TextArea textField: textInput
    property var conversationModel: (contact !== null) ? contact.conversation : null

    function forceActiveFocus() {
        textField.forceActiveFocus()
    }

    onVisibleChanged: if (visible) forceActiveFocus()

    property bool active: visible && activeFocusItem !== null
    onActiveChanged: {
        if (active)
            conversationModel.resetUnreadCount()
    }

    Connections {
        target: conversationModel
        onUnreadCountChanged: if (active) conversationModel.resetUnreadCount()
    }

    Rectangle {
        anchors.fill: parent
        color: Style.chatAreaBackground
    }

    RowLayout {
        id: infoBar
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            margins: 15
        }
        spacing: 8

        PresenceIcon {
            status: contact.status
        }

        Label {
            text: contact.nickname
            textFormat: Text.PlainText
            font.pointSize: styleHelper.pointSize
        }

        Item {
            Layout.fillWidth: true
            height: 1
        }
    }

    ChatMessageArea {
        anchors {
            top: infoBar.bottom
            topMargin: 2
            left: parent.left
            right: parent.right
            bottom: inputArea.top
        }
        model: conversationModel
    }

    Item {
        id: inputArea
        anchors {
            left: parent.left
            leftMargin: 16
            right: parent.right
            rightMargin: 16
            bottom: parent.bottom
        }
        height: Math.max(Style.inputAreaHeight, statusLayout.height + 8)

        // XXX Is this RowLayout actually necessary? Looks like it's used for the auto-sizing textarea..
        RowLayout {
            id: statusLayout
            width: inputArea.width
            anchors.verticalCenter: inputArea.verticalCenter

            TextArea {
                id: textInput
                Layout.fillWidth: true
                // This ridiculous incantation enables an automatically sized TextArea
                Layout.preferredHeight: mapFromItem(flickableItem, 0, 0).y * 2 +
                                        Math.max(styleHelper.textHeight + 2*edit.textMargin, flickableItem.contentHeight) + 1
                Layout.maximumHeight: (styleHelper.textHeight * 2) + (2 * edit.textMargin) + 5
                textMargin: 8
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                font.pointSize: styleHelper.pointSize
                focus: true

                property TextEdit edit

                Component.onCompleted: {
                    var objects = contentItem.contentItem.children
                    for (var i = 0; i < objects.length; i++) {
                        if (objects[i].hasOwnProperty('textDocument')) {
                            edit = objects[i]
                            break
                        }
                    }

                    edit.Keys.pressed.connect(keyHandler)
                }

                function keyHandler(event) {
                    switch (event.key) {
                        case Qt.Key_Enter:
                        case Qt.Key_Return:
                            if (event.modifiers & Qt.ShiftModifier || event.modifiers & Qt.AltModifier) {
                                textInput.insert(textInput.cursorPosition, "\n")
                            } else {
                                send()
                            }
                            event.accepted = true
                            break
                        default:
                            event.accepted = false
                    }
                }

                function send() {
                    if (textInput.length > 2000)
                        textInput.remove(2000, textInput.length)
                    conversationModel.sendMessage(textInput.text)
                    textInput.remove(0, textInput.length)
                }

                onLengthChanged: {
                    if (textInput.length > 2000)
                        textInput.remove(2000, textInput.length)
                }

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border {
                        color: Qt.darker(Style.lightGrey, 1.1)
                        width: 1
                    }
                    radius: 16
                }

                style: TextAreaStyle {
                    textColor: Style.almostBlack
                    backgroundColor: Style.chatAreaBackground
                    frame: Item {}
                    corner: Item {}
                }
            }
        }
    }
}

