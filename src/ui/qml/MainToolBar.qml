import QtQuick 2.0
import QtQuick.Controls 1.0
import QtQuick.Layouts 1.0
import QtQuick.Controls.Styles 1.0
import im.ricochet 1.0
import "style.js" as Style

Rectangle {
    Layout.fillWidth: true
    Layout.preferredHeight: 70
    color: Qt.lighter(Style.lightGrey, 1.02)

    property Action addContact: addContactAction
    property Action preferences: preferencesAction

    data: [
        Action {
            id: addContactAction
            text: qsTr("Add Contact")
            onTriggered: {
                var object = createDialog("AddContactDialog.qml", { }, window)
                object.visible = true
            }
        },

        Action {
            id: preferencesAction
            text: qsTr("Preferences")
            onTriggered: root.openPreferences()
        }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        MouseArea {
            Layout.fillWidth: true
            Layout.fillHeight: true

            onClicked: addContactAction.trigger()

            Text {
                id: addContactIcon
                anchors {
                    centerIn: parent
                    verticalCenterOffset: -6
                }
                font.family: iconFont.name
                renderType: Text.QtRendering
                text: "\ue810"
                font.pixelSize: 16
                horizontalAlignment: Text.AlignHCenter
                color: Style.primaryBlue
            }

            Label {
                anchors {
                    top: addContactIcon.bottom
                    topMargin: 4
                    horizontalCenter: addContactIcon.horizontalCenter
                }
                text: qsTr("Add Contact")
                color: Style.primaryBlue
                font.pixelSize: 12
            }

            Loader {
                id: emptyState
                active: contactList.view.count == 0
                sourceComponent: Bubble {
                    target: addContactButton
                    maximumWidth: toolBarLayout.width
                    text: qsTr("Click to add contacts")
                }
            }
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            width: 1
            color: Qt.darker(Style.lightGrey, 1.1)
        }

        MouseArea {
            Layout.fillWidth: true
            Layout.fillHeight: true

            onClicked: preferencesAction.trigger()

            Text {
                id: preferencesIcon
                anchors {
                    centerIn: parent
                    verticalCenterOffset: -6
                }
                font.family: iconFont.name
                renderType: Text.QtRendering
                text: "\ue803"
                font.pixelSize: 16
                horizontalAlignment: Text.AlignHCenter
                color: Style.primaryBlue
            }

            Label {
                anchors {
                    top: preferencesIcon.bottom
                    topMargin: 4
                    horizontalCenter: preferencesIcon.horizontalCenter
                }
                text: qsTr("Settings")
                color: Style.primaryBlue
                font.pixelSize: 12
            }
        }
    }
}
