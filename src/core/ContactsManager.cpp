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

#include "ContactsManager.h"
#include "ContactIDValidator.h"
#include "ConversationModel.h"
#include "core/BackendRPC.h"
#include <QStringList>
#include <QDateTime>
#include <QDebug>

#ifdef Q_OS_MAC
#include <QtMac>
#endif

ContactsManager *contactsManager = 0;

ContactsManager::ContactsManager(UserIdentity *id)
    : identity(id), contactsPopulated(false), conversationsPopulated(false)
{
    contactsManager = this;
    connect(backend, &BackendRPC::contactEvent, this, &ContactsManager::contactEvent);
    connect(backend, &BackendRPC::conversationEvent, this, &ContactsManager::conversationEvent);
}

void ContactsManager::loadFromBackend()
{
    Q_ASSERT(!contactsPopulated && !conversationsPopulated);
    // Begin MonitorContacts. The full contacts list will be sent in POPULATE events first,
    // followed by a null POPULATE event, followed by a stream of any changes.
    backend->startMonitorContacts();

    // Conversations will be started once contacts have finished populating
}

void ContactsManager::contactEvent(const ricochet::ContactEvent &event)
{
    if (!contactsPopulated) {
        if (event.type() != ricochet::ContactEvent::POPULATE) {
            qDebug() << "Ignoring unexpected contact event type" << event.type() << "during populate";
            return;
        }

        if (event.has_contact()) {
            ContactUser *user = new ContactUser(identity, event.contact(), this);
            connectSignals(user);
            pContacts.append(user);
            emit contactAdded(user);
        } else if (event.has_request()) {
            // XXX incomingRequests.loadRequests
            qDebug() << "XXX Ignoring incoming request in contacts for now";
        } else {
            // End of populate
            qDebug() << "Contacts populated";
            contactsPopulated = true;
            backend->startMonitorConversations();
        }

        return;
    }

    if (event.has_contact()) {
        ContactUser *user = lookupHostname(ContactIDValidator::hostnameFromID(QString::fromStdString(event.contact().address())));
        if (user && user->uniqueID != event.contact().id()) {
            qDebug() << "Ignoring contact event with an address/id mismatch";
            return;
        }

        switch (event.type()) {
            case ricochet::ContactEvent::ADD:
                if (user) {
                    // This can happen under normal circumstances, because addContact will create the contact also.
                    // It's harmless and the contacts are identical.
                    qDebug() << "Ignoring contact add event for existing contact";
                    return;
                }
                user = new ContactUser(identity, event.contact(), this);
                connectSignals(user);
                pContacts.append(user);
                emit contactAdded(user);
                break;

            case ricochet::ContactEvent::UPDATE:
                if (!user) {
                    qDebug() << "Ignoring contact update event for unknown contact";
                    return;
                }
                user->updated(event.contact());
                break;

            case ricochet::ContactEvent::DELETE:
                if (!user) {
                    qDebug() << "Ignoring contact delete event for unknown contact";
                    return;
                }
                // XXX
                break;

            default:
                qDebug() << "Ignoring unknown contact event type" << event.type();
                return;
        }
    } else if (event.has_request()) {
        // XXX
        qDebug() << "Ignoring contact request event for now";
    } else {
        qDebug() << "Ignoring contact event without a subject";
    }
}

void ContactsManager::conversationEvent(const ricochet::ConversationEvent &event)
{
    if (!conversationsPopulated) {
        if (event.type() != ricochet::ConversationEvent::POPULATE) {
            qDebug() << "Ignoring unexpected conversation event type" << event.type() << "during population";
            return;
        }
        if (!event.has_msg()) {
            qDebug() << "Finished populating conversations";
            conversationsPopulated = true;
            return;
        }
    } else if (event.type() == ricochet::ConversationEvent::POPULATE) {
        qDebug() << "Ignoring conversation populate event after population finished";
        return;
    }

    ricochet::Message msg = event.msg();
    if (!event.has_msg() || !msg.has_recipient() || !msg.has_sender() ||
        (msg.sender().isself() && msg.recipient().isself()))
    {
        qDebug() << "Ignoring invalid conversation event";
        return;
    }

    ricochet::Entity remoteEntity = msg.sender().isself() ? msg.recipient() : msg.sender();
    ContactUser *user = lookupHostname(ContactIDValidator::hostnameFromID(QString::fromStdString(remoteEntity.address())));
    if (!user || user->uniqueID != remoteEntity.contactid()) {
        qDebug() << "Ignoring conversation event with unknown remote entity";
        return;
    }

    user->conversation()->handleMessageEvent(event);
}

ContactUser *ContactsManager::addContact(const QString &nickname)
{
    qFatal("dead function");
    return nullptr;
}

void ContactsManager::connectSignals(ContactUser *user)
{
    connect(user, SIGNAL(contactDeleted(ContactUser*)), SLOT(contactDeleted(ContactUser*)));
    connect(user->conversation(), &ConversationModel::unreadCountChanged, this, &ContactsManager::onUnreadCountChanged);
    connect(user, &ContactUser::statusChanged, [this,user]() { emit contactStatusChanged(user, user->status()); });
}

ContactUser *ContactsManager::createContactRequest(const QString &contactid, const QString &nickname,
                                                   const QString &myNickname, const QString &message)
{
    ricochet::ContactRequest request;
    request.set_direction(ricochet::ContactRequest::OUTBOUND);
    request.set_address(contactid.toStdString());
    request.set_nickname(nickname.toStdString());
    request.set_fromnickname(myNickname.toStdString());
    request.set_text(message.toStdString());
    request.set_whencreated(QDateTime::currentDateTime().toString(Qt::ISODate).toStdString());

    ricochet::Contact contactData;
    if (!backend->addContactRequest(request, contactData)) {
        qDebug() << "Add contact request RPC failed";
        return nullptr;
    }

    // Check for a matching contact, in case the add event was somehow handled already
    ContactUser *user = lookupHostname(ContactIDValidator::hostnameFromID(QString::fromStdString(contactData.address())));
    Q_ASSERT(!user || user->uniqueID == contactData.id());

    // Create the contact now. We'll also receive it as an ADD event, but that can be safely ignored.
    if (!user) {
        user = new ContactUser(identity, contactData, this);
        connectSignals(user);
        pContacts.append(user);
        emit contactAdded(user);
    }

    return user;
}

void ContactsManager::contactDeleted(ContactUser *user)
{
    pContacts.removeOne(user);
}

ContactUser *ContactsManager::lookupHostname(const QString &hostname) const
{
    QString ohost = ContactIDValidator::hostnameFromID(hostname);
    if (ohost.isNull())
        ohost = hostname;

    if (!ohost.endsWith(QLatin1String(".onion")))
        ohost.append(QLatin1String(".onion"));

    for (QList<ContactUser*>::ConstIterator it = pContacts.begin(); it != pContacts.end(); ++it)
    {
        if (ohost.compare((*it)->hostname(), Qt::CaseInsensitive) == 0)
            return *it;
    }

    return 0;
}

ContactUser *ContactsManager::lookupNickname(const QString &nickname) const
{
    for (QList<ContactUser*>::ConstIterator it = pContacts.begin(); it != pContacts.end(); ++it)
    {
        if (QString::compare(nickname, (*it)->nickname(), Qt::CaseInsensitive) == 0)
            return *it;
    }

    return 0;
}

ContactUser *ContactsManager::lookupUniqueID(int uniqueID) const
{
    for (QList<ContactUser*>::ConstIterator it = pContacts.begin(); it != pContacts.end(); ++it)
    {
        if ((*it)->uniqueID == uniqueID)
            return *it;
    }

    return 0;
}

void ContactsManager::onUnreadCountChanged()
{
    ConversationModel *model = qobject_cast<ConversationModel*>(sender());
    Q_ASSERT(model);
    if (!model)
        return;
    ContactUser *user = model->contact();

    emit unreadCountChanged(user, model->unreadCount());

#ifdef Q_OS_MAC
    int unread = globalUnreadCount();
    QtMac::setBadgeLabelText(unread == 0 ? QString() : QString::number(unread));
#endif
}

int ContactsManager::globalUnreadCount() const
{
    int re = 0;
    foreach (ContactUser *u, pContacts) {
        if (u->conversation())
            re += u->conversation()->unreadCount();
    }
    return re;
}

