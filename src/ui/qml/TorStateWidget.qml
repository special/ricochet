import QtQuick 2.0
import QtQuick.Controls 1.0
import QtQuick.Layouts 1.0
import im.ricochet 1.0

Label {
    text: {
        if (networkManager.controlStatus === NetworkManager.ControlError)
            return qsTr("Connection failed")
        if (networkManager.controlStatus < NetworkManager.ControlConnected) {
            //: \u2026 is ellipsis
            return qsTr("Connecting\u2026")
        }

        if (networkManager.connectionStatus === NetworkManager.ConnectionUnknown ||
            networkManager.connectionStatus === NetworkManager.ConnectionOffline)
        {
            var bootstrap = networkManager.bootstrapStatus
            if (bootstrap['recommendation'] === 'warn')
                return qsTr("Connection failed")
            else if (bootstrap['progress'] === undefined)
                return qsTr("Connecting\u2026")
            else {
                //: %1 is progress percentage, e.g. 100
                return qsTr("Connecting\u2026 (%1%)").arg(bootstrap['progress'])
            }
        }

        if (networkManager.connectionStatus === NetworkManager.ConnectionReady) {
            // Indicates whether we've verified that the hidden services is connectable
            if (userIdentity.isOnline)
                return qsTr("Online")
            else
                return qsTr("Connected")
        }
    }
}
