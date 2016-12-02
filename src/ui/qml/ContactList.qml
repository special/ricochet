import QtQuick 2.0
import QtQuick.Controls 1.0
import QtQuick.Layouts 1.0
import im.ricochet 1.0
import "style.js" as Style

ScrollView {
    id: scroll

    data: [
        Rectangle {
            anchors.fill: scroll
            z: -1
            color: Style.lightGrey
        },
        ContactsModel {
            id: contactsModel
            identity: userIdentity
        }
    ]

    property QtObject selectedContact
    property ListView view: contactListView

    // Emitted for double click on a contact
    signal contactActivated(ContactUser contact, Item actions)

    onSelectedContactChanged: {
        if (selectedContact !== contactsModel.contact(contactListView.currentIndex)) {
            contactListView.currentIndex = contactsModel.rowOfContact(selectedContact)
        }
    }

    ListView {
        id: contactListView
        model: contactsModel
        currentIndex: -1

        signal contactActivated(ContactUser contact, Item actions)
        onContactActivated: scroll.contactActivated(contact, actions)

        onCurrentIndexChanged: {
            // Not using a binding to allow writes to selectedContact
            scroll.selectedContact = contactsModel.contact(contactListView.currentIndex)
        }

        data: [
            MouseArea {
                anchors.fill: parent
                z: -100
                onClicked: contactListView.currentIndex = -1
            }
        ]

        header: Column {
            width: parent.width
            spacing: 14

            Item { width: 1; height: 1 }

            Row {
                x: 40 - presenceIcon.width - spacing
                spacing: 8

                PresenceIcon {
                    id: presenceIcon
                    anchors.verticalCenter: presenceLabel.verticalCenter
                    anchors.verticalCenterOffset: 1
                    status: 0
                }

                Label {
                    id: presenceLabel
                    font.pixelSize: 16
                    font.bold: true
                    font.capitalization: Font.SmallCaps
                    textFormat: Text.PlainText
                    color: Style.almostBlack
                    text: qsTr("Online").toLowerCase()
                }
            }

            Column {
                spacing: 2

                Label {
                    id: contactIdLabel
                    x: 40
                    textFormat: Text.PlainText
                    font.pointSize: styleHelper.pointSize
                    color: Style.almostBlack
                    text: userIdentity.contactID
                }

                Label {
                    x: 40
                    textFormat: Text.PlainText
                    font.pointSize: styleHelper.pointSize - 2
                    color: Style.primaryBlue
                    text: qsTr("Copy My Address")
                }
            }

            Rectangle {
                x: 40
                width: contactIdLabel.width
                height: 1
                color: Qt.darker(Style.lightGrey, 1.2)
            }
        }

        section.property: "status"
        section.delegate: Item {
            x: 40
            height: label.height + 12

            Label {
                id: label
                y: 6
                font.pixelSize: 16
                font.bold: true
                font.capitalization: Font.SmallCaps
                textFormat: Text.PlainText
                color: Style.almostBlack

                text: {
                    // Translation strings are uppercase for legacy reasons, and because they
                    // should correctly be capitalized. We go lowercase only because it looks
                    // nicer when using SmallCaps, and that's a display detail.
                    switch (parseInt(section)) {
                        case ContactUser.Online: return qsTr("Online").toLowerCase()
                        case ContactUser.Offline: return qsTr("Offline").toLowerCase()
                        case ContactUser.RequestPending: return qsTr("Requests").toLowerCase()
                        case ContactUser.RequestRejected: return qsTr("Rejected").toLowerCase()
                        case ContactUser.Outdated: return qsTr("Outdated").toLowerCase()
                    }
                }
            }
        }

        delegate: ContactListDelegate { }
    }
}
