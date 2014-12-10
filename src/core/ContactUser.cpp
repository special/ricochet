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

#include "ContactUser.h"
#include "UserIdentity.h"
#include "ContactsManager.h"
#include "utils/SecureRNG.h"
#include "core/ContactIDValidator.h"
#include "core/OutgoingContactRequest.h"
#include "core/ConversationModel.h"
#include "tor/HiddenService.h"
#include <QtDebug>
#include <QDateTime>

#ifdef PROTOCOL_NEW
#include "protocol/OutboundConnector.h"
#include "utils/Useful.h"
#else
#include "protocol/GetSecretCommand.h"
#include "protocol/ChatMessageCommand.h"
#include "protocol/ProtocolConstants.h"
#include "protocol/OutgoingContactSocket.h"
#endif

ContactUser::ContactUser(UserIdentity *ident, int id, QObject *parent)
    : QObject(parent)
    , identity(ident)
    , uniqueID(id)
#ifdef PROTOCOL_NEW
    , m_connection(0)
#endif
    , m_outgoingSocket(0)
    , m_lastReceivedChatID(0)
    , m_contactRequest(0)
    , m_settings(0)
    , m_conversation(0)
{
    Q_ASSERT(uniqueID >= 0);

    m_settings = new SettingsObject(QStringLiteral("contacts.%1").arg(uniqueID));
    connect(m_settings, &SettingsObject::modified, this, &ContactUser::onSettingsModified);

    m_conversation = new ConversationModel(this);
    m_conversation->setContact(this);

#ifndef PROTOCOL_NEW
    m_conn = new ProtocolSocket(this);
    connect(m_conn, SIGNAL(connected()), this, SLOT(onConnected()));
    connect(m_conn, SIGNAL(disconnected()), this, SLOT(onDisconnected()));
#endif

    loadContactRequest();
    updateStatus();
}

void ContactUser::loadContactRequest()
{
    if (m_contactRequest)
        return;

    if (m_settings->read("request.status") != QJsonValue::Undefined) {
        m_contactRequest = new OutgoingContactRequest(this);
        connect(m_contactRequest, SIGNAL(statusChanged(int,int)), SLOT(updateStatus()));
        connect(m_contactRequest, SIGNAL(removed()), SLOT(requestRemoved()));
        updateStatus();
    }
}

ContactUser *ContactUser::addNewContact(UserIdentity *identity, int id)
{
    ContactUser *user = new ContactUser(identity, id);
    user->settings()->write("whenCreated", QDateTime::currentDateTime());

#ifndef PROTOCOL_NEW
    /* Generate the local secret and set it */
    user->settings()->write("localSecret", Base64Encode(SecureRNG::random(16)));
#endif

    return user;
}

void ContactUser::updateStatus()
{
    Status newStatus;
    if (m_contactRequest) {
        if (m_contactRequest->status() == OutgoingContactRequest::Error ||
            m_contactRequest->status() == OutgoingContactRequest::Rejected)
        {
            newStatus = RequestRejected;
        } else {
            newStatus = RequestPending;
        }
    } else {
#ifdef PROTOCOL_NEW
        newStatus = m_connection && m_connection->isConnected() ? Online : Offline;
#else
        newStatus = m_conn->isConnected() ? Online : Offline;
#endif
    }

    if (newStatus == m_status)
        return;

    m_status = newStatus;
    emit statusChanged();

#ifdef PROTOCOL_NEW
    updateOutgoingSocket();
#else
    if (m_status == Offline)
        updateOutgoingSocket();
#endif
}

void ContactUser::onSettingsModified(const QString &key, const QJsonValue &value)
{
    Q_UNUSED(value);
    if (key == QLatin1String("nickname"))
        emit nicknameChanged();
}

void ContactUser::updateOutgoingSocket()
{
    if (m_status != Offline
#ifdef PROTOCOL_NEW
        && m_status != RequestPending
#endif
        )
    {
        if (m_outgoingSocket) {
            m_outgoingSocket->disconnect(this);
#ifdef PROTOCOL_NEW
            m_outgoingSocket->abort();
#endif
            m_outgoingSocket->deleteLater();
            m_outgoingSocket = 0;
        }
        return;
    }

    // Refuse to make outgoing connections to the local hostname
    if (hostname() == identity->hostname())
        return;

#ifdef PROTOCOL_NEW
    if (m_outgoingSocket && m_outgoingSocket->status() == Protocol::OutboundConnector::Ready) {
        BUG() << "Called updateOutgoingSocket with an existing socket in Ready. This should've been deleted.";
        m_outgoingSocket->disconnect(this);
        m_outgoingSocket->deleteLater();
        m_outgoingSocket = 0;
    }

    if (!m_outgoingSocket) {
        m_outgoingSocket = new Protocol::OutboundConnector(this);
        m_outgoingSocket->setAuthPrivateKey(identity->hiddenService()->cryptoKey());
        connect(m_outgoingSocket, &Protocol::OutboundConnector::ready, this,
            [this]() {
                assignConnection(m_outgoingSocket->takeConnection(this));
            }
        );
    }

#else
    QByteArray secret = m_settings->read<Base64Encode>("remoteSecret");
    if (secret.isEmpty() || hostname().isEmpty() || !port())
        return;

    if (!m_outgoingSocket) {
        qDebug() << "Creating outgoing connection socket for contact";
        m_outgoingSocket = new OutgoingContactSocket(this);
        // TODO: Need proper UI and handling for authenticationFailed and versionNegotiationFailed
        connect(m_outgoingSocket, SIGNAL(socketReady(QTcpSocket*)), SLOT(incomingProtocolSocket(QTcpSocket*)));
    }

    m_outgoingSocket->setAuthentication(Protocol::PurposePrimary, secret);
#endif

    m_outgoingSocket->connectToHost(hostname(), port());
}

void ContactUser::onConnected()
{
    m_settings->write("lastConnected", QDateTime::currentDateTime());

#ifndef PROTOCOL_NEW
    if (m_contactRequest) {
        qDebug() << "Implicitly accepting outgoing contact request for" << uniqueID << "from primary connection";

        m_contactRequest->accept();
        updateStatus();
        Q_ASSERT(status() != RequestPending);
    }
#endif

#ifndef PROTOCOL_NEW
    if (m_settings->read("remoteSecret") == QJsonValue::Undefined)
    {
        qDebug() << "Requesting remote secret from user" << uniqueID;
        GetSecretCommand *command = new GetSecretCommand(this);
        command->send(conn());
    }
#endif

    updateStatus();
    if (isConnected())
        emit connected();
}

void ContactUser::onDisconnected()
{
    qDebug() << "Contact" << uniqueID << "disconnected";
    m_settings->write("lastConnected", QDateTime::currentDateTime());

#ifdef PROTOCOL_NEW
    if (m_connection) {
        if (m_connection->isConnected()) {
            BUG() << "onDisconnected called, but connection is still connected";
            return;
        }

        m_connection->deleteLater();
        m_connection = 0;
    } else
        BUG() << "onDisconnected called without a connection";
#endif

    updateStatus();
    emit disconnected();
}

SettingsObject *ContactUser::settings()
{
    return m_settings;
}

QString ContactUser::nickname() const
{
    return m_settings->read("nickname").toString();
}

void ContactUser::setNickname(const QString &nickname)
{
    m_settings->write("nickname", nickname);
}

QString ContactUser::hostname() const
{
    return m_settings->read("hostname").toString();
}

quint16 ContactUser::port() const
{
    return m_settings->read("port", 9878).toInt();
}

QString ContactUser::contactID() const
{
    return ContactIDValidator::idFromHostname(hostname());
}

void ContactUser::setHostname(const QString &hostname)
{
    QString fh = hostname;

    if (!hostname.endsWith(QLatin1String(".onion")))
        fh.append(QLatin1String(".onion"));

    m_settings->write("hostname", fh);
    updateOutgoingSocket();
}

void ContactUser::deleteContact()
{
    /* Anything that uses ContactUser is required to either respond to the contactDeleted signal
     * synchronously, or make use of QWeakPointer. */

    qDebug() << "Deleting contact" << uniqueID;

    if (m_contactRequest) {
        qDebug() << "Cancelling request associated with contact to be deleted";
        m_contactRequest->cancel();
        m_contactRequest->deleteLater();
    }

    emit contactDeleted(this);

#ifndef PROTOCOL_NEW
    m_conn->disconnect();
    delete m_conn;
    m_conn = 0;
#endif

    m_settings->undefine();
    deleteLater();
}

void ContactUser::requestRemoved()
{
    if (m_contactRequest) {
        m_contactRequest->deleteLater();
        m_contactRequest = 0;
        updateStatus();
    }
}

#ifndef PROTOCOL_NEW
static bool isOutgoing(QTcpSocket *socket)
{
    return socket->inherits("Tor::TorSocket");
}

void ContactUser::incomingProtocolSocket(QTcpSocket *socket)
{
    /* Per protocol.txt, to resolve connection races:
     *
     * - If the remote peer establishes a command connection prior to
     *   sending authentication data for an outgoing attempt, the outgoing
     *   attempt is aborted and the peer's connection is used.
     *
     * - If the existing connection is more than 30 seconds old, measured
     *   from the time authentication is sent, it is replaced and closed.
     *
     * - Otherwise, both peers close the connection for which the server's
     *   onion-formatted hostname is considered less by a strcmp function.
     */

    if (!isOutgoing(socket) && m_outgoingSocket && !m_outgoingSocket->isAuthenticationPending())
    {
        // Abort connection attempt and use this socket
        m_outgoingSocket->disconnect();
        m_outgoingSocket->deleteLater();
        m_outgoingSocket = 0;
    }

    if (conn()->isConnected() && isOutgoing(conn()->socket()) != isOutgoing(socket) &&
        conn()->connectedDuration() < 30)
    {
        // Fall back to comparing onion hostnames to decide which connection to keep
        bool keepOutgoing = QString::compare(hostname(), identity->hostname()) < 0;
        if (isOutgoing(socket) != keepOutgoing) {
            qDebug() << "Discarding new protocol connection because existing one is too recent";
            socket->setParent(this);
            socket->abort();
            socket->deleteLater();
        } else {
            qDebug() << "Replacing existing (but recent) protocol connection";
            conn()->setSocket(socket);
        }
    } else {
        conn()->setSocket(socket);
    }

    if (isOutgoing(socket) && m_outgoingSocket) {
        m_outgoingSocket->deleteLater();
        m_outgoingSocket = 0;
    }
}
#else
void ContactUser::assignConnection(Protocol::Connection *connection)
{
    if (connection == m_connection) {
        BUG() << "Connection is already assigned to this ContactUser";
        return;
    }

    if (qobject_cast<ContactUser*>(connection->parent()) && connection->parent() != this) {
        BUG() << "Connection is already owned by another ContactUser";
        connection->close();
        return;
    }

    connection->setParent(this);
    bool isOutbound = connection->direction() == Protocol::Connection::ClientSide;

    if (!connection->isConnected()) {
        BUG() << "Connection assigned to contact but isn't connected; discarding";
        connection->close();
        connection->deleteLater();
        return;
    }

    if (!connection->hasAuthenticatedAs(Protocol::Connection::HiddenServiceAuth, hostname())) {
        BUG() << "Connection assigned to contact without matching authentication";
        connection->close();
        connection->deleteLater();
        return;
    }

    if (m_connection && !m_connection->isConnected()) {
        qDebug() << "Replacing dead connection with new connection";
        clearConnection();
    }

    /* To resolve a race if two contacts try to connect at the same time:
     *
     * If the existing connection is in the same direction as the new one,
     * always use the new one.
     */
    if (m_connection && connection->direction() == m_connection->direction()) {
        qDebug() << "Replacing existing connection with contact because the new one goes the same direction";
        clearConnection();
    }

    /* If the existing connection is more than 30 seconds old, measured from
     * when it was successfully established, it's replaced with the new one.
     */
    if (m_connection && m_connection->age() > 30) {
        qDebug() << "Replacing existing connection with contact because it's more than 30 seconds old";
        clearConnection();
    }

    /* Otherwise, close the connection for which the server's onion-formatted
     * hostname compares less with a strcmp function
     */
    bool preferOutbound = QString::compare(hostname(), identity->hostname()) < 0;
    if (m_connection) {
        if (isOutbound == preferOutbound) {
            // New connection wins
            clearConnection();
        } else {
            // Old connection wins
            qDebug() << "Closing new connection with contact because the old connection won comparison";
            connection->close();
            connection->deleteLater();
            return;
        }
    }

     /* If this connection is inbound and we have an outgoing connection attempt,
      * use the inbound connection if we haven't sent authentication yet, or if
      * we would lose the strcmp comparison above.
      */
    if (!isOutbound && m_outgoingSocket) {
        if (m_outgoingSocket->status() != Protocol::OutboundConnector::Authenticating || !preferOutbound) {
            // Inbound connection wins; outbound connection attempt will abort when status changes
            qDebug() << "Aborting outbound connection attempt because we got an inbound connection instead";
        } else {
            // Outbound attempt wins
            qDebug() << "Closing inbound connection with contact because the pending outbound connection won comparison";
            connection->close();
            connection->deleteLater();
            return;
        }
    }

    if (m_connection) {
        BUG() << "After resolving connection races, ContactUser still has two connections";
        connection->close();
        connection->deleteLater();
        return;
    }

    qDebug() << "Assigned" << (isOutbound ? "outbound" : "inbound") << "connection to contact" << uniqueID;

    if (m_contactRequest && isOutbound) {
        // XXX try deliver request
    } else {
        if (m_contactRequest && !isOutbound) {
            qDebug() << "Implicitly accepting outgoing contact request for" << uniqueID << "due to incoming connection";
            m_contactRequest->accept();
        }

        if (!connection->setPurpose(Protocol::Connection::Purpose::KnownContact)) {
            qWarning() << "BUG: Failed setting connection purpose";
            connection->close();
            connection->deleteLater();
            return;
        }
    }

    m_connection = connection;

    /* Use a queued connection to onDisconnected, because it clears m_connection.
     * If we cleared that immediately, it would be possible for the value to change
     * effectively any time we call into protocol code, which would be dangerous.
     */
    connect(m_connection, &Protocol::Connection::closed, this, &ContactUser::onDisconnected, Qt::QueuedConnection);
    onConnected();
}

void ContactUser::clearConnection()
{
    if (!m_connection)
        return;

    disconnect(m_connection, 0, this, 0);
    if (m_connection->isConnected()) {
        connect(m_connection, &Protocol::Connection::closed, m_connection, &QObject::deleteLater);
        m_connection->close();
    } else
        m_connection->deleteLater();

    m_connection = 0;
}

#endif
