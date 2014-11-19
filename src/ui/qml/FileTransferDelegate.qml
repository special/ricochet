import QtQuick 2.0
import QtQuick.Controls 1.0
import QtQuick.Layouts 1.0
import QtQuick.Dialogs 1.2
import im.ricochet 1.0

MouseArea {
    id: fileTransferDelegate
    height: infoColumn.height + 12
    implicitWidth: Math.max(infoMetrics.width, fileNameMetrics.width) + 18 + icon.width
    clip: true

    property QtObject transfer: model.fileTransfer

    Label {
        id: fileNameMetrics
        visible: false
        text: transfer.fileName
        font.pointSize: styleHelper.pointSize
    }

    Label {
        id: infoMetrics
        visible: false
        // XXX i18n
        text: "xxXX \u2014 xx:xx remaining \u2014 xxxxXX/x"
        font.pointSize: styleHelper.pointSize - 1
    }

    Component {
        id: fileDialogComponent

        FileDialog {
            title: qsTr("Save file")
            // Transfer filename is _always_ sanitized
            folder: "file:../" + transfer.fileName
            selectExisting: false
            Component.onCompleted: visible = true
        }
    }

    Timer {
        id: updateTick
        interval: 333
        repeat: true
        triggeredOnStart: true
        running: infoColumn.running && infoColumn.visible

        onTriggered: {
            infoColumn.transferRate = transfer.transferRate
            infoColumn.transferredSize = transfer.transferredSize
        }

        onRunningChanged: {
            if (!running) {
                infoColumn.transferRate = 0
                infoColumn.transferredSize = transfer.transferredSize
            }
        }
    }

    Rectangle {
        x: 2 + (parent.width - 4) * (containsMouse ? 1 : infoColumn.transferredSize / transfer.fileSize)
        y: 2
        height: parent.height - 4
        width: parent.width - x - 2
        color: Qt.lighter(background.color, 1.15)
    }

    Label {
        id: icon
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
            margins: 6
        }

        font.pixelSize: height
        font.family: iconFont.name
        verticalAlignment: Text.AlignVCenter
        text: "\ue800"
    }

    ColumnLayout {
        id: infoColumn
        anchors {
            left: icon.right
            top: parent.top
            right: parent.right
            margins: 6
        }
        spacing: 4

        property bool running: transfer.state === FileTransfer.Active
        property int transferRate: 0
        property int transferredSize: 0

        Label {
            id: filename
            Layout.fillWidth: true
            font.pointSize: styleHelper.pointSize
            elide: Text.ElideMiddle
            text: transfer.fileName
            textFormat: Text.PlainText
        }

        Label {
            id: info
            Layout.fillWidth: true
            font.pointSize: styleHelper.pointSize - 1
            color: Qt.darker(background.color, 2.0)

            text: {
                var eta = "5:00 remaining"
                var size = formatFileSize(transfer.fileSize)
                // XXX i18n
                var rate = formatFileSize(infoColumn.transferRate) + "/s"
                var sep = " \u2014 "

                switch (transfer.state) {
                    case FileTransfer.Canceled:
                        return size + sep + qsTr("Canceled")
                    case FileTransfer.Error:
                        return size + sep + qsTr("Error") // XXX More details?
                    case FileTransfer.Offer:
                        if (transfer.isOutbound)
                            return size + sep + qsTr("Waiting")
                        else
                            return size + sep + qsTr("Click to save")
                    case FileTransfer.Active:
                        // \u009c separates alternate strings. First fitting choice is used.
                        return size + sep + eta + sep + rate + "\u009c" +
                               size + sep + eta + "\u009c" +
                               eta
                    case FileTransfer.Finished:
                        return size + sep + qsTr("Click to open")
                    default:
                        console.log("Unknown file transfer state " + transfer.state + " in UI handler")
                        return ""
                }
            }
            elide: Text.ElideRight
        }
    }

    function showContextMenu() {
        var object = contextMenu.createObject(delegate, { })
        object.visibleChanged.connect(function() { if (!object.visible) object.destroy() })
        object.popup()
    }

    hoverEnabled: transfer.state == FileTransfer.Finished || (!transfer.isOutbound && (transfer.state == FileTransfer.Offer || transfer.state == FileTransfer.Error))
    onClicked: {
        if (transfer.state == FileTransfer.Finished) {
            openExternally()
        } else if (!transfer.isOutbound && (transfer.state == FileTransfer.Offer || transfer.state == FileTransfer.Error)) {
            // XXX is this possible from error state?
            if (!transfer.hasLocalFile)
                saveAs()
            else
                transfer.start()
        }
    }

    function saveAs() {
        var fileDialog = fileDialogComponent.createObject(fileTransferDelegate, { })
        fileDialog.accepted.connect(function() {
            transfer.localFileUrl = fileDialog.fileUrl
            transfer.start()
        })
    }

    function openExternally() {
        // XXX should this warn on downloads?
        Qt.openUrlExternally(transfer.localFileUrl)
    }

    function formatFileSize(sz) {
        var suffixes = [ "B", "KB", "MB", "GB", "TB" ]
        var i = 0
        for (; sz >= 1024 && i < suffixes.length; i++)
            sz /= 1024
        return sz.toFixed(0) + " " + suffixes[i]
    }

    Component {
        id: contextMenu

        Menu {
            MenuItem {
                text: qsTr("Save as...")
                visible: !transfer.isOutbound && transfer.state == FileTransfer.Offer && !transfer.hasLocalFile
                onTriggered: fileTransferDelegate.saveAs()
            }
            MenuItem {
                text: qsTr("Retry")
                visible: !transfer.isOutbound && transfer.state == FileTransfer.Failed && transfer.hasLocalFile
                onTriggered: transfer.start()
            }
            MenuItem {
                text: qsTr("Open")
                visible: transfer.isOutbound || transfer.state == FileTransfer.Finished
                onTriggered: fileTransferDelegate.openExternally()
            }
            MenuItem {
                text: qsTr("Cancel")
                visible: transfer.state !== FileTransfer.Canceled
                onTriggered: transfer.cancel()
            }
        }
    }
}

