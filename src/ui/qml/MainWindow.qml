import QtQuick 2.2
import QtQuick.Window 2.0
import QtQuick.Controls 1.0
import QtQuick.Layouts 1.0
import im.ricochet 1.0

ApplicationWindow {
    id: window
    title: "Ricochet"
    visibility: Window.AutomaticVisibility

    width: 650
    height: 400
    minimumHeight: 400
    minimumWidth: 650
    maximumWidth: (1 << 24) - 1

    // OS X Menu
    Loader {
        active: Qt.platform.os == 'osx'
        sourceComponent: MenuBar {
            Menu {
                title: "Ricochet"
                MenuItem {
                    text: qsTranslate("QCocoaMenuItem", "Preference")
                    onTriggered: toolBar.preferences.trigger()
                }
            }
        }
    }

    Connections {
        target: userIdentity.contacts
        onUnreadCountChanged: {
            if (unreadCount > 0) {
                if (audioNotifications !== null)
                    audioNotifications.message.play()
                // On OS X, avoid bouncing the dock icon forever
                window.alert(Qt.platform.os == "osx" ? 1000 : 0)
            }
        }
        onContactStatusChanged: {
            if (status === ContactUser.Online && audioNotifications !== null) {
                audioNotifications.contactOnline.play()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            spacing: 0
            Layout.preferredWidth: 220
            Layout.fillWidth: false

            MainToolBar {
                id: toolBar
                // Needed to allow bubble to appear over contact list
                z: 3
            }

            Item {
                Layout.fillHeight: true
                Layout.fillWidth: true

                ContactList {
                    id: contactList
                    anchors.fill: parent
                    opacity: offlineLoader.item !== null ? (1 - offlineLoader.item.opacity) : 1

                    onContactActivated: {
                        if (contact.status === ContactUser.RequestPending || contact.status === ContactUser.RequestRejected) {
                            actions.openPreferences()
                        }
                    }
                }

                Loader {
                    id: offlineLoader
                    active: torControl.torStatus !== TorControl.TorReady || (item !== null && item.visible)
                    anchors.fill: parent
                    source: Qt.resolvedUrl("OfflineStateItem.qml")
                }
            }
        }

        Rectangle {
            width: 1
            Layout.fillHeight: true
            color: Qt.darker(palette.window, 1.5)
        }

        PageView {
            id: chatView
            Layout.fillWidth: true
            Layout.fillHeight: true

            property QtObject currentContact: (visible && width > 0) ? contactList.selectedContact : null
            onCurrentContactChanged: {
                if (currentContact !== null) {
                    show(currentContact.uniqueID, Qt.resolvedUrl("ChatPage.qml"),
                         { 'contact': currentContact });
                } else {
                    currentKey = ""
                }
            }
        }
    }

    property bool inactive: true
    onActiveFocusItemChanged: {
        // Focus current page when window regains focus
        if (activeFocusItem !== null && inactive) {
            inactive = false
            retakeFocus.start()
        } else if (activeFocusItem === null) {
            inactive = true
        }
    }

    Timer {
        id: retakeFocus
        interval: 1
        onTriggered: {
            if (chatView.currentPage !== null)
                chatView.currentPage.forceActiveFocus()
        }
    }

    Action {
        shortcut: StandardKey.Close
        onTriggered: window.close()
    }
}

