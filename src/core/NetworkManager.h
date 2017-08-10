/* Ricochet - https://ricochet.im/
 * Copyright (C) 2014, John Brooks <john.brooks@dereferenced.net>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 *    * Neither the names of the copyright owners nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include "rpc/network.pb.h"
#include <QObject>
#include <QVariantMap>

class NetworkManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(ControlStatus controlStatus READ controlStatus NOTIFY networkStatusChanged)
    Q_PROPERTY(QString controlError READ controlError NOTIFY networkStatusChanged)
    Q_PROPERTY(ConnectionStatus connectionStatus READ connectionStatus NOTIFY networkStatusChanged)
    Q_PROPERTY(QVariantMap bootstrapStatus READ bootstrapStatus NOTIFY networkStatusChanged)
    Q_PROPERTY(QString torVersion READ torVersion NOTIFY networkStatusChanged)

public:
    enum ControlStatus
    {
        ControlStopped = ricochet::TorControlStatus_Status_STOPPED,
        ControlError = ricochet::TorControlStatus_Status_ERROR,
        ControlConnecting = ricochet::TorControlStatus_Status_CONNECTING,
        ControlConnected = ricochet::TorControlStatus_Status_CONNECTED
    };
    Q_ENUM(ControlStatus)

    enum ConnectionStatus
    {
        ConnectionUnknown = ricochet::TorConnectionStatus_Status_UNKNOWN,
        ConnectionOffline = ricochet::TorConnectionStatus_Status_OFFLINE,
        ConnectionBootstrapping = ricochet::TorConnectionStatus_Status_BOOTSTRAPPING,
        ConnectionReady = ricochet::TorConnectionStatus_Status_READY
    };
    Q_ENUM(ConnectionStatus)

    explicit NetworkManager(QObject *parent = 0);
    static NetworkManager *instance();

    ControlStatus controlStatus() const;
    QString controlError() const;
    ConnectionStatus connectionStatus() const;
    QVariantMap bootstrapStatus() const;
    QString torVersion() const;

signals:
    void networkStatusChanged();

private slots:
    void onNetworkStatusChanged(const ricochet::NetworkStatus &status);

private:
    ricochet::NetworkStatus m_status;
};

#endif
