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

#ifndef TORSOCKET_H
#define TORSOCKET_H

#include <QObject>
#include <QTimer>
#include <QAbstractSocket>
#include <QSharedPointer>
#include "utils/AbstractSocket.h"

namespace Tor {

/* Wrapper around a socket, which makes connections over the SOCKS proxy
 * from a TorControl instance, automatically attempts reconnections, and
 * reacts to Tor's connectivity state.
 *
 * Once a connection is established, the socket can be retrieved with the
 * socket() method and used normally. When the connection is lost, that
 * socket is discarded, and TorSocket will attempt to reconnect. When a
 * new connection is established, it will have a new socket instance.
 *
 * The caller is responsible for resetting the attempt counter if a
 * connection was successful and reconnection will be used again on
 * this instance of TorSocket.
 */
class TorSocket : public QObject
{
    Q_OBJECT

public:
    explicit TorSocket(QObject *parent = 0);
    virtual ~TorSocket();

    bool reconnectEnabled() const { return m_reconnectEnabled; }
    void setReconnectEnabled(bool enabled);
    int maxAttemptInterval() { return m_maxInterval; }
    void setMaxAttemptInterval(int interval);
    void resetAttempts();

    const QSharedPointer<AbstractSocket> &socket() const { return m_socket; };

    virtual void connectToHost(const QString &hostName, quint16 port);
    virtual void connectToHost(const QHostAddress &address, quint16 port);

    QString hostName() const { return m_host; }
    quint16 port() const { return m_port; }

signals:
    void socketChanged();
    void connected();

protected:
    virtual int reconnectInterval();

private slots:
    void reconnect();
    void connectivityChanged();
    void onFailed();
    void sendSocksRequest();
    void handleSocksResponse();

private:
    QSharedPointer<AbstractSocket> m_socket;
    QString m_host;
    quint16 m_port;
    QTimer m_connectTimer;
    bool m_reconnectEnabled;
    int m_maxInterval;
    int m_connectAttempts;
};

}

#endif
