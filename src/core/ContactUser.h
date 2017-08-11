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

#ifndef CONTACTUSER_H
#define CONTACTUSER_H

#include <QObject>
#include <QHash>
#include <QMetaType>
#include <QVariant>
#include <QSharedPointer>
#include "protocol/Connection.h"
#include "rpc/contact.pb.h"

class UserIdentity;
class ConversationModel;

namespace Protocol
{
    class OutboundConnector;
}

/* Represents a user on the contact list.
 * All persistent uses of a ContactUser instance must either connect to the
 * contactDeleted() signal, or use a QWeakPointer to track deletion. A ContactUser
 * can be removed at essentially any time. */

// XXX QML uses of contact.settings, contact.contactRequest

class ContactUser : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(ContactUser)

    Q_PROPERTY(int uniqueID READ getUniqueID CONSTANT)
    Q_PROPERTY(UserIdentity* identity READ getIdentity CONSTANT)
    Q_PROPERTY(QString nickname READ nickname WRITE setNickname NOTIFY nicknameChanged)
    Q_PROPERTY(QString contactID READ contactID CONSTANT)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(ConversationModel *conversation READ conversation CONSTANT)

    friend class ContactsManager;

public:
    enum Status
    {
        Unknown = ricochet::Contact::UNKNOWN,
        Offline = ricochet::Contact::OFFLINE,
        Online = ricochet::Contact::ONLINE,
        RequestPending = ricochet::Contact::REQUEST,
        RequestRejected = ricochet::Contact::REJECTED
    };
    Q_ENUM(Status)

    UserIdentity * const identity;
    const int uniqueID;

    explicit ContactUser(UserIdentity *identity, const ricochet::Contact &data, QObject *parent = 0);
    virtual ~ContactUser();

    QSharedPointer<Protocol::Connection> connection() { return nullptr; }
    bool isConnected() const { return status() == Online; }

    ConversationModel *conversation() { return m_conversation; }

    UserIdentity *getIdentity() const { return identity; }
    int getUniqueID() const { return uniqueID; }

    QString nickname() const;
    /* Hostname is in the onion hostname format, i.e. it ends with .onion */
    QString hostname() const;
    quint16 port() const;
    /* Contact ID in the ricochet: format */
    QString contactID() const;

    Status status() const;

    Q_INVOKABLE void deleteContact();

public slots:
    void setNickname(const QString &nickname);
    void setHostname(const QString &hostname);

signals:
    void statusChanged();
    void connected();
    void disconnected();

    void nicknameChanged();
    void contactDeleted(ContactUser *user);

private:
    ricochet::Contact m_data;
    QSharedPointer<Protocol::Connection> m_connection;

    quint16 m_lastReceivedChatID;
    ConversationModel *m_conversation;

    void updated(const ricochet::Contact &data);
};

Q_DECLARE_METATYPE(ContactUser*)

#endif // CONTACTUSER_H
