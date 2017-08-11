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

#ifndef CONTACTSMANAGER_H
#define CONTACTSMANAGER_H

#include <QObject>
#include <QList>
#include "ContactUser.h"
#include "rpc/contact.pb.h"
#include "rpc/conversation.pb.h"

class UserIdentity;

class ContactsManager : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(ContactsManager)

    Q_PROPERTY(QList<QVariantMap> incomingRequests READ incomingRequestsQt NOTIFY incomingRequestsChanged)
    Q_PROPERTY(int globalUnreadCount READ globalUnreadCount NOTIFY unreadCountChanged)

public:
    UserIdentity * const identity;

    explicit ContactsManager(UserIdentity *identity);

    const QList<ContactUser*> &contacts() const { return pContacts; }
    ContactUser *lookupHostname(const QString &hostname) const;
    ContactUser *lookupNickname(const QString &nickname) const;
    ContactUser *lookupUniqueID(int uniqueID) const;

    /* Create a new user and a contact request for that user. Use this instead of addContact.
     * Note that contactID should be an ricochet: ID. */
    Q_INVOKABLE ContactUser *createContactRequest(const QString &contactID, const QString &nickname,
                                                  const QString &myNickname, const QString &message);

    /* addContact will add the contact, but does not create a request. Use createContactRequest */
    ContactUser *addContact(const QString &nickname);

    static QString hostnameFromID(const QString &ID);

    void loadFromBackend();

    int globalUnreadCount() const;

    const QList<ricochet::ContactRequest> &incomingRequests() const { return m_incomingRequests; }
    QList<QVariantMap> incomingRequestsQt() const;

    Q_INVOKABLE void acceptIncomingRequest(const QString &address, const QString &nickname);
    Q_INVOKABLE void rejectIncomingRequest(const QString &address);

signals:
    void contactAdded(ContactUser *user);
    void unreadCountChanged(ContactUser *user, int unreadCount);
    void contactStatusChanged(ContactUser* user, int status);

    void incomingRequest(const QVariantMap &request);
    void incomingRequestUpdated(const QVariantMap &request);
    void incomingRequestDeleted(const QVariantMap &request);
    void incomingRequestsChanged();

private slots:
    void contactDeleted(ContactUser *user);
    void onUnreadCountChanged();

private:
    QList<ContactUser*> pContacts;
    QList<ricochet::ContactRequest> m_incomingRequests;
    bool contactsPopulated;
    bool conversationsPopulated;

    void connectSignals(ContactUser *user);
    void contactEvent(const ricochet::ContactEvent &event);
    void conversationEvent(const ricochet::ConversationEvent &event);

    QVariantMap requestData(const ricochet::ContactRequest &request) const;
};

#endif // CONTACTSMANAGER_H
