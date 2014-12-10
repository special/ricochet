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

#include "IdentityManager.h"
#include "IncomingRequestManager.h"
#include "ContactsManager.h"
#include "OutgoingContactRequest.h"
#include "ContactIDValidator.h"
#include "utils/Useful.h"

#ifdef PROTOCOL_NEW
#include "protocol/Connection.h"
#include "protocol/ContactRequestChannel.h"
#else
#include "protocol/ContactRequestServer.h"
#endif

IncomingRequestManager::IncomingRequestManager(ContactsManager *c)
    : QObject(c), contacts(c)
{
    connect(this, SIGNAL(requestAdded(IncomingContactRequest*)), this, SIGNAL(requestsChanged()));
    connect(this, SIGNAL(requestRemoved(IncomingContactRequest*)), this, SIGNAL(requestsChanged()));

#ifdef PROTOCOL_NEW
    // Attach to any ContactRequestChannel on an incoming connection for this identity
    connect(contacts->identity, &UserIdentity::incomingConnection, this,
        [this](Protocol::Connection *connection) {
            qDebug() << "IncomingRequestManager attaching to connection" << connection;
            connect(connection, &Protocol::Connection::channelCreated, this,
                [this](Protocol::Channel *channel) {
                    Protocol::ContactRequestChannel *req = qobject_cast<Protocol::ContactRequestChannel*>(channel);
                    qDebug() << "IncomingRequestManager attaching to channel" << channel << req << channel->type() << channel->metaObject()->className();
                    if (req)
                        attachRequestChannel(req);
                }
            );
        }
    );
#endif
}

void IncomingRequestManager::loadRequests()
{
    SettingsObject settings(QStringLiteral("contactRequests"));

    foreach (QString host, settings.data().keys())
    {
#ifdef PROTOCOL_NEW
        if (!host.endsWith(QStringLiteral(".onion")))
            host.append(QStringLiteral(".onion"));
#else
        if (host.endsWith(QStringLiteral(".onion")))
            host.chop(QStringLiteral(".onion").size());
#endif

        IncomingContactRequest *request = new IncomingContactRequest(this, host.toLatin1());
        request->load();

        m_requests.append(request);
        emit requestAdded(request);
    }
}

QList<QObject*> IncomingRequestManager::requestObjects() const
{
    QList<QObject*> re;
    re.reserve(m_requests.size());
    foreach (IncomingContactRequest *o, m_requests)
        re.append(o);
    return re;
}

IncomingContactRequest *IncomingRequestManager::requestFromHostname(const QByteArray &hostname)
{
#ifdef PROTOCOL_NEW
    Q_ASSERT(hostname.endsWith(".onion"));
#else
    Q_ASSERT(!hostname.endsWith(".onion"));
#endif

    Q_ASSERT(hostname == hostname.toLower());

    for (QList<IncomingContactRequest*>::ConstIterator it = m_requests.begin(); it != m_requests.end(); ++it)
        if ((*it)->hostname() == hostname)
            return *it;

    return 0;
}

#ifdef PROTOCOL_NEW
void IncomingRequestManager::attachRequestChannel(Protocol::ContactRequestChannel *channel)
{
    if (channel->direction() != Protocol::Channel::Inbound) {
        BUG() << "IncomingRequestManager shouldn't try to attach to an outbound channel";
        return;
    }

    qDebug() << "Attached to ContactRequestChannel";
    connect(channel, &Protocol::ContactRequestChannel::requestReceived, this, &IncomingRequestManager::requestReceived);
}

void IncomingRequestManager::requestReceived()
{
    qDebug() << "requestReceived";
    Protocol::ContactRequestChannel *channel = qobject_cast<Protocol::ContactRequestChannel*>(sender());
    if (!channel) {
        BUG() << "Called without a valid sender";
        return;
    }

    using namespace Protocol::Data::ContactRequest;

    QString hostname = channel->connection()->authenticatedIdentity(Protocol::Connection::HiddenServiceAuth);
    if (hostname.isEmpty() || !hostname.endsWith(QStringLiteral(".onion"))) {
        BUG() << "Incoming contact request received but connection isn't authenticated";
        channel->setResponseStatus(Response::Error, QStringLiteral("internal error"));
        return;
    }

    if (isHostnameRejected(hostname.toLatin1())) {
        qDebug() << "Rejecting contact request due to a blacklist match for" << hostname;
        channel->setResponseStatus(Response::Rejected);
        return;
    }

    if (identityManager->lookupHostname(hostname)) {
        qDebug() << "Rejecting contact request from a local identity (which shouldn't have been allowed)";
        channel->setResponseStatus(Response::Error, QStringLiteral("local identity"));
        return;
    }

    IncomingContactRequest *request = requestFromHostname(hostname.toLatin1());
    bool newRequest = false;

    if (request) {
        // Update the existing request
        request->setConnection(channel->connection());
        request->setNickname(channel->nickname());
        request->setMessage(channel->message());
        request->renew();
    } else {
        newRequest = true;
        request = new IncomingContactRequest(this, hostname.toLatin1());
        request->setConnection(channel->connection());
        request->setNickname(channel->nickname());
        request->setMessage(channel->message());
    }

    // Check if this request matches any existing users, including any outgoing requests
    ContactUser *existingUser = contacts->lookupHostname(hostname);
    if (existingUser) {
        // Implicitly accept a matching outgoing request
        if (existingUser->contactRequest())
            existingUser->contactRequest()->accept();

        // Implicitly accept this request
        // XXX Test this case to make sure response status gets handled correctly
        request->accept(existingUser);
        return;
    }

    qDebug() << "Recording" << (newRequest ? "new" : "existing") << "incoming contact request from" << hostname;
    channel->setResponseStatus(Response::Pending);

    request->save();
    if (newRequest) {
        m_requests.append(request);
        emit requestAdded(request);
    }
}
#else
void IncomingRequestManager::addRequest(const QByteArray &hostname, const QByteArray &connSecret, ContactRequestServer *connection,
                                        const QString &nickname, const QString &message)
{
    if (isHostnameRejected(hostname))
    {
        qDebug() << "Rejecting contact request due to a blacklist match for" << hostname;

        if (connection)
            connection->sendRejection();

        return;
    }

    if (identityManager->lookupHostname(QString::fromLatin1(hostname)))
    {
        qDebug() << "Rejecting contact request from a local identity (?)";
        if (connection)
            connection->sendRejection();
        return;
    }

    IncomingContactRequest *request = requestFromHostname(hostname);
    bool newRequest = false;

    if (request)
    {
        /* Update the existing request */
        request->setConnection(connection);
        request->setRemoteSecret(connSecret);
        request->setNickname(nickname);
        request->setMessage(message);
        request->renew();
    }
    else
    {
        /* Create a new request */
        newRequest = true;

        request = new IncomingContactRequest(this, hostname, connection);
        request->setRemoteSecret(connSecret);
        request->setNickname(nickname);
        request->setMessage(message);
    }

    /* Check if this request matches any existing users, including any outgoing requests. */
    ContactUser *existingUser = contacts->lookupHostname(QString::fromLatin1(hostname));
    if (existingUser)
    {
        /* If the existing user is an outgoing contact request, that is considered accepted */
        if (existingUser->contactRequest())
            existingUser->contactRequest()->accept();

        /* This request is automatically accepted */
        request->accept(existingUser);
        return;
    }

    request->save();
    if (newRequest)
    {
        m_requests.append(request);
        emit requestAdded(request);
    }
}
#endif

void IncomingRequestManager::removeRequest(IncomingContactRequest *request)
{
    if (m_requests.removeOne(request))
        emit requestRemoved(request);

    request->deleteLater();
}

void IncomingRequestManager::addRejectedHost(const QByteArray &hostname)
{
    SettingsObject *settings = contacts->identity->settings();
    QJsonArray blacklist = settings->read<QJsonArray>("hostnameBlacklist");
    if (!blacklist.contains(QString::fromLatin1(hostname))) {
        blacklist.append(QString::fromLatin1(hostname));
        settings->write("hostnameBlacklist", blacklist);
    }
}

bool IncomingRequestManager::isHostnameRejected(const QByteArray &hostname) const
{
    QJsonArray blacklist = contacts->identity->settings()->read<QJsonArray>("hostnameBlacklist");
    return blacklist.contains(QString::fromLatin1(hostname));
}

IncomingContactRequest::IncomingContactRequest(IncomingRequestManager *m, const QByteArray &h
#ifndef PROTOCOL_NEW
                                               , ContactRequestServer *c
#endif
                                              )
    : QObject(m)
    , manager(m)
#ifndef PROTOCOL_NEW
    , connection(c)
#endif
    , m_hostname(h)
{
    Q_ASSERT(manager);
#ifdef PROTOCOL_NEW
    Q_ASSERT(m_hostname.endsWith(".onion"));
#else
    Q_ASSERT(m_hostname.size() == 16);
#endif

    qDebug() << "Created contact request from" << m_hostname << (connection ? "with" : "without") << "connection";
}

QString IncomingContactRequest::settingsKey() const
{
    QString key = QString(QLatin1String(m_hostname));
#ifdef PROTOCOL_NEW
    key.chop(QStringLiteral(".onion").size());
#endif
    return QStringLiteral("contactRequests.%1").arg(key);
}

void IncomingContactRequest::load()
{
    SettingsObject settings(settingsKey());

#ifndef PROTOCOL_NEW
    setRemoteSecret(settings.read<Base64Encode>("remoteSecret"));
#endif
    setNickname(settings.read("nickname").toString());
    setMessage(settings.read("message").toString());

    m_requestDate = settings.read<QDateTime>("requestDate");
    m_lastRequestDate = settings.read<QDateTime>("lastRequestDate");
}

void IncomingContactRequest::save()
{
    SettingsObject settings(settingsKey());

#ifndef PROTOCOL_NEW
    settings.write("remoteSecret", Base64Encode(remoteSecret()));
#endif
    settings.write("nickname", nickname());
    settings.write("message", message());

    if (m_requestDate.isNull())
        m_requestDate = m_lastRequestDate = QDateTime::currentDateTime();

    settings.write("requestDate", m_requestDate);
    settings.write("lastRequestDate", m_lastRequestDate);
}

void IncomingContactRequest::renew()
{
    m_lastRequestDate = QDateTime::currentDateTime();
}

void IncomingContactRequest::removeRequest()
{
    SettingsObject(settingsKey()).undefine();
}

QString IncomingContactRequest::contactId() const
{
    return ContactIDValidator::idFromHostname(hostname());
}

void IncomingContactRequest::setRemoteSecret(const QByteArray &remoteSecret)
{
    Q_ASSERT(remoteSecret.size() == 16);
    m_remoteSecret = remoteSecret;
}

void IncomingContactRequest::setMessage(const QString &message)
{
    m_message = message;
}

void IncomingContactRequest::setNickname(const QString &nickname)
{
    m_nickname = nickname;
    emit nicknameChanged();
}

#ifdef PROTOCOL_NEW
void IncomingContactRequest::setConnection(Protocol::Connection *c)
{
    if (c == connection)
        return;

    if (connection) {
        qDebug() << "Replacing connection on an IncomingContactRequest. Old connection is" << connection->age() << "seconds old.";
        connection->close();
    }

    auto channel = c->findChannel<Protocol::ContactRequestChannel>();
    if (!channel) {
        BUG() << "Assigned connection to IncomingContactRequest without an open ContactRequestChannel";
        c->close();
        return;
    }

    // When the channel is closed, also close the connection
    connect(channel, &Protocol::Channel::invalidated, this,
        [this,c]() {
            // XXX Make sure this doesn't happen on accept
            if (connection == c) {
                qDebug() << "Closing connection attached to an IncomingContactRequest because ContactRequestChannel was closed";
                connection->close();
                // XXX How is connection cleared on close?
            }
        }
    );

    qDebug() << "Assigning connection to IncomingContactRequest from" << m_hostname;
    if (!c->setPurpose(Protocol::Connection::Purpose::InboundRequest)) {
        qDebug() << "Setting purpose on incoming contact request connection failed; killing connection";
        c->close();
        return;
    }

    connection = c;
    connection->setParent(this);
    emit hasActiveConnectionChanged();
}
#else
void IncomingContactRequest::setConnection(ContactRequestServer *c)
{
    if (connection)
    {
        /* New connections replace old ones.. but this should honestly never
         * happen, because the redeliver timeout is far longer. */
        connection.data()->close();
    }

    qDebug() << "Setting new connection for an existing contact request from" << m_hostname;
    connection = c;
    emit hasActiveConnectionChanged();
}
#endif

void IncomingContactRequest::accept(ContactUser *user)
{
    qDebug() << "Accepting contact request from" << m_hostname;

    // Create the contact if necessary
    if (!user) {
        Q_ASSERT(!nickname().isEmpty());
        user = manager->contacts->addContact(nickname());
        user->setHostname(QString::fromLatin1(m_hostname));
    }

#ifdef PROTOCOL_NEW
    using namespace Protocol::Data::ContactRequest;

    // If we have a connection, send the response and pass it to ContactUser
    if (connection) {
        auto channel = connection->findChannel<Protocol::ContactRequestChannel>();
        if (channel) {
            // Channel will close after sending a final response
            channel->setResponseStatus(Response::Accepted);
            user->assignConnection(connection.data());

            if (connection->parent() != user) {
                BUG() << "ContactUser didn't claim connection from incoming contact request";
                connection->close();
                // XXX We really shouldn't be putting Connection::deleteLater everywhere; clarify this behavior.
                connection->deleteLater();
            }
        } else {
            connection->close();
            connection->deleteLater();
        }
        connection.clear();
    }
#else
    user->settings()->write("remoteSecret", Base64Encode(remoteSecret()));

    if (connection) {
        connection.data()->sendAccept(user);
        connection = (ContactRequestServer*)0;
    }
#endif

    // Remove the request
    removeRequest();
    manager->removeRequest(this);

    user->updateStatus();
}

void IncomingContactRequest::reject()
{
    qDebug() << "Rejecting contact request from" << m_hostname;

#ifdef PROTOCOL_NEW
    using namespace Protocol::Data::ContactRequest;

    if (connection) {
        auto channel = connection->findChannel<Protocol::ContactRequestChannel>();
        if (channel)
            channel->setResponseStatus(Response::Rejected);
        connection->close();
        connection.clear();
    }
#else
    // Send a rejection if there is an active connection
    if (connection)
        connection.data()->sendRejection();
#endif

    // Remove the request from the config
    removeRequest();
    // Blacklist the host to prevent repeat requests
    manager->addRejectedHost(m_hostname);
    // Remove the request from the manager
    manager->removeRequest(this);

    // Object is now scheduled for deletion by the manager
}
