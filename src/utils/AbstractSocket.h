/* Ricochet - https://ricochet.im/
 * Copyright (C) 2016, John Brooks <john.brooks@dereferenced.net>
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

#ifndef UTILS_ABSTRACTSOCKET_H
#define UTILS_ABSTRACTSOCKET_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QLocalServer>
#include <QLocalSocket>

class AbstractSocket : public QObject
{
    Q_OBJECT

public:
    AbstractSocket(QTcpSocket *socket, QObject *parent = 0);
    AbstractSocket(QLocalSocket *socket, QObject *parent = 0);

    QTcpSocket *tcpSocket() { return m_tcpSocket; }
    const QTcpSocket *tcpSocket() const { return m_tcpSocket; }
    QLocalSocket *localSocket() { return m_localSocket; }
    const QLocalSocket *localSocket() const { return m_localSocket; }

    QIODevice *device() const
    {
        if (m_tcpSocket)
            return m_tcpSocket;
        else
            return m_localSocket;
    }

    QAbstractSocket::SocketState state() const;
    QAbstractSocket::SocketError error() const;
    QString errorString() const;

public slots:
    void abort();
    void disconnectFromHost();

signals:
    void connected();
    void disconnected();
    void errored();

private:
    QTcpSocket *m_tcpSocket;
    QLocalSocket *m_localSocket;
};

class AbstractServer : public QObject
{
    Q_OBJECT

public:
    AbstractServer(QTcpServer *server, QObject *parent = 0);
    AbstractServer(QLocalServer *server, QObject *parent = 0);

    QTcpServer *tcpServer() { return m_tcpServer; }
    const QTcpServer *tcpServer() const { return m_tcpServer; }
    QLocalServer *localServer() { return m_localServer; }
    const QLocalServer *localServer() const { return m_localServer; }

    bool hasPendingConnections() const;
    AbstractSocket *nextPendingConnection();

signals:
    void newConnection();

private:
    QTcpServer *m_tcpServer;
    QLocalServer *m_localServer;
};

#endif
