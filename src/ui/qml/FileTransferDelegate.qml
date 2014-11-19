import QtQuick 2.0
import QtQuick.Controls 1.0
import QtQuick.Layouts 1.0
import QtQuick.Dialogs 1.1
import im.ricochet 1.0

MouseArea {
    id: fileTransferDelegate
    height: incomingFile.height
    width: Math.min(Math.max(incomingFile.implicitWidth, fileNameMetrics.width), background.__maxWidth)

    property QtObject transfer: model.fileTransfer

    Label {
        id: fileNameMetrics
        visible: false
        text: transfer.fileName
    }

    Component {
        id: fileDialogComponent

        FileDialog {
            title: qsTr("Save file")
            folder: "file:../" + transfer.fileName
            selectExisting: false
            Component.onCompleted: visible = true
        }
    }

    onDoubleClicked: {
        incomingFile.running = !incomingFile.running
        return
        if (!transfer.isOutgoing && (transfer.state == FileTransfer.Offered || transfer.state == FileTransfer.Failed)) {
            if (!transfer.hasLocalFile) {
                saveAs()
                return
            }

            transfer.start()
            incomingFile.running = true
        } else if (transfer.state != FileTransfer.Cancelled) {
            transfer.cancel()
            incomingFile.running = false
        }
    }

    GridLayout {
        id: incomingFile
        width: parent.width
        columns: 3
        columnSpacing: incomingFile.running ? 20 : 6

        property bool running

        Label {
            id: filename
            Layout.columnSpan: incomingFile.running ? 3 : 1
            Layout.fillWidth: true
            text: transfer.fileName
            wrapMode: Text.WrapAnywhere
        }

        Label {
            id: eta
            font.pointSize: styleHelper.pointSize - 2
            Layout.alignment: Qt.AlignLeft
            color: Qt.darker(background.color, 2.0)
            visible: incomingFile.running

            text: "01:29"
        }

        Label {
            id: speed
            font.pointSize: styleHelper.pointSize - 2
            Layout.alignment: Qt.AlignHCenter
            horizontalAlignment: Text.AlignHCenter
            color: Qt.darker(background.color, 2.0)
            visible: incomingFile.running

            text: "21.12 KB/s"
        }

        Label {
            id: filesize
            font.pointSize: styleHelper.pointSize - 2
            Layout.alignment: Qt.AlignRight
            color: Qt.darker(background.color, 2.0)

            property double actual: 0
            property double total: 10.11
            text: total + " MB"
                /*if (!incomingFile.running)
                    return total + " MB - double click to download"
                return actual.toFixed(2) + " of " + total + " MB (21.12 KB/sec)"
            }*/

            Timer {
                running: incomingFile.running
                repeat: true
                interval: 100
                onTriggered: {
                    filesize.actual += 0.02112
                    if (filesize.actual >= filesize.total) {
                        filesize.actual = filesize.total
                        incomingFile.running = false
                    }
                }
            }
        }

        Rectangle {
            id: progress
            Layout.fillWidth: true
            Layout.columnSpan: 3
            height: 5
            color: Qt.darker(background.color, 1.1)
            visible: incomingFile.running
            Rectangle {
                color: Qt.darker("#76c7ff", 1.3)
                height: parent.height
                width: parent.width * (filesize.actual / filesize.total)
            }
        }

        Button {
            visible: !incomingFile.running
            text: transfer.isOutgoing ? "Cancel" : "Save"
            onClicked: incomingFile.running = !incomingFile.running
        }
    }

    function showContextMenu() {
        var object = contextMenu.createObject(delegate, { })
        object.visibleChanged.connect(function() { if (!object.visible) object.destroy() })
        object.popup()
    }

    function saveAs() {
        var fileDialog = fileDialogComponent.createObject(fileTransferDelegate, { })
        fileDialog.accepted.connect(function() {
            transfer.localFileUrl = fileDialog.fileUrl
            transfer.start()
            incomingFile.running = true
        })
    }

    function openExternally() {
        // XXX should this warn on downloads?
        Qt.openUrlExternally(transfer.localFileUrl)
    }

    Component {
        id: contextMenu

        Menu {
            MenuItem {
                text: qsTr("Save as...")
                visible: !transfer.isOutgoing && transfer.state == FileTransfer.Offered && !transfer.hasLocalFile
                onTriggered: fileTransferDelegate.saveAs()
            }
            MenuItem {
                text: qsTr("Retry")
                visible: !transfer.isOutgoing && transfer.state == FileTransfer.Failed && transfer.hasLocalFile
                onTriggered: transfer.start()
            }
            MenuItem {
                text: qsTr("Open")
                visible: transfer.isOutgoing || transfer.state == FileTransfer.Finished
                onTriggered: fileTransferDelegate.openExternally()
            }
            MenuItem {
                text: qsTr("Cancel")
                visible: transfer.state !== FileTransfer.Cancelled
                onTriggered: transfer.cancel()
            }
        }
    }
}

