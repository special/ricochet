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

#include "AbstractSocket.h"

AbstractSocket::AbstractSocket(QTcpSocket *socket, QObject *parent)
    : QObject(parent), m_tcpSocket(socket), m_localSocket(0)
{
    Q_ASSERT(m_tcpSocket);
    m_tcpSocket->setParent(this);

    connect(m_tcpSocket, &QAbstractSocket::connected, this, &AbstractSocket::connected);
    connect(m_tcpSocket, &QAbstractSocket::disconnected, this, &AbstractSocket::disconnected);
    connect(m_tcpSocket, (void (QAbstractSocket::*)(QAbstractSocket::SocketError))&QAbstractSocket::error, this, &AbstractSocket::errored);
}

AbstractSocket::AbstractSocket(QLocalSocket *socket, QObject *parent)
    : QObject(parent), m_tcpSocket(0), m_localSocket(socket)
{
    Q_ASSERT(m_localSocket);
    m_localSocket->setParent(this);

    connect(m_localSocket, &QLocalSocket::connected, this, &AbstractSocket::connected);
    connect(m_localSocket, &QLocalSocket::disconnected, this, &AbstractSocket::disconnected);
    connect(m_localSocket, (void (QLocalSocket::*)(QLocalSocket::LocalSocketError))&QLocalSocket::error, this, &AbstractSocket::errored);
}

QAbstractSocket::SocketState AbstractSocket::state() const
{
    if (m_tcpSocket)
        return m_tcpSocket->state();
    else
        // Compatible enums
        return static_cast<QAbstractSocket::SocketState>(m_localSocket->state());
}

QAbstractSocket::SocketError AbstractSocket::error() const
{
    if (m_tcpSocket)
        return m_tcpSocket->error();
    else
        // Compatible enums
        return static_cast<QAbstractSocket::SocketError>(m_localSocket->error());
}

QString AbstractSocket::errorString() const
{
    if (m_tcpSocket)
        return m_tcpSocket->errorString();
    else
        return m_localSocket->errorString();
}

void AbstractSocket::abort()
{
    if (m_tcpSocket)
        m_tcpSocket->abort();
    else
        m_localSocket->abort();
}

void AbstractSocket::disconnectFromHost()
{
    if (m_tcpSocket)
        m_tcpSocket->disconnectFromHost();
    else
        m_localSocket->disconnectFromServer();
}

AbstractServer::AbstractServer(QTcpServer *server, QObject *parent)
    : QObject(parent), m_tcpServer(server), m_localServer(0)
{
    Q_ASSERT(m_tcpServer);
    m_tcpServer->setParent(this);

    connect(m_tcpServer, &QTcpServer::newConnection, this, &AbstractServer::newConnection);
}

AbstractServer::AbstractServer(QLocalServer *server, QObject *parent)
    : QObject(parent), m_tcpServer(0), m_localServer(server)
{
    Q_ASSERT(m_localServer);
    m_localServer->setParent(this);

    connect(m_localServer, &QLocalServer::newConnection, this, &AbstractServer::newConnection);
}

bool AbstractServer::hasPendingConnections() const
{
    if (m_tcpServer)
        return m_tcpServer->hasPendingConnections();
    else
        return m_localServer->hasPendingConnections();
}

AbstractSocket *AbstractServer::nextPendingConnection()
{
    if (m_tcpServer) {
        QTcpSocket *s = m_tcpServer->nextPendingConnection();
        return s ? (new AbstractSocket(s)) : 0;
    } else {
        QLocalSocket *s = m_localServer->nextPendingConnection();
        return s ? (new AbstractSocket(s)) : 0;
    }
}

