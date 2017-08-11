import QtQuick 2.2
import QtQuick.Controls 1.0
import QtQuick.Layouts 1.0

ApplicationWindow {
    id: contactRequestDialog
    width: 350
    height: 200
    minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height
    flags: styleHelper.dialogWindowFlags
    modality: Qt.WindowModal
    title: mainWindow.title

    signal closed
    onVisibleChanged: if (!visible) closed()

    property var request
    property bool hasValidContact: request.address != "" && fields.name.text.length

    function close() {
        visible = false
    }

    function accept() {
        userIdentity.contacts.acceptIncomingRequest(request.address, fields.name.text)
        close()
    }

    function reject() {
        userIdentity.contacts.rejectIncomingRequest(request.address)
        close()
    }

    GridLayout {
        id: infoArea
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            topMargin: 8
            leftMargin: 16
            rightMargin: 16
        }
        columns: 2

        Label {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            horizontalAlignment: Qt.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("Someone new is asking to connect to you")
        }

        Item { height: 1 }

        Rectangle {
            color: palette.mid
            height: 1
            Layout.fillWidth: true
            Layout.columnSpan: 2
        }

        Item { height: 1 }
    }

    ContactRequestFields {
        id: fields
        anchors {
            left: parent.left
            right: parent.right
            top: infoArea.bottom
            bottom: buttonRow.top
            margins: 8
            leftMargin: 16
            rightMargin: 16
        }
        readOnly: true

        Component.onCompleted: {
            contactId.text = request.address
            name.text = request.nickname
            name.readOnly = false
            name.focus = true
            message.text = request.text
        }
    }

    RowLayout {
        id: buttonRow
        anchors {
            right: parent.right
            bottom: parent.bottom
            rightMargin: 16
            bottomMargin: 8
        }

        Button {
            text: qsTr("Reject")
            onClicked: contactRequestDialog.reject()
        }

        Button {
            text: qsTr("Accept")
            enabled: hasValidContact
            onClicked: contactRequestDialog.accept()
        }
    }

    Action {
        shortcut: StandardKey.Close
        onTriggered: contactRequestDialog.close()
    }

    Action {
        shortcut: "Escape"
        onTriggered: contactRequestDialog.close()
    }
}

