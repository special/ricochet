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
#include "utils/Useful.h"
#include "core/ContactIDValidator.h"
#include "core/OutgoingContactRequest.h"
#include "core/ConversationModel.h"
#include "tor/HiddenService.h"
#include "protocol/OutboundConnector.h"
#include <QtDebug>
#include <QDateTime>
#include <QtEndian>

ContactUser::ContactUser(UserIdentity *ident, const ricochet::Contact &data, QObject *parent)
    : QObject(parent)
    , identity(ident)
    , uniqueID(data.id())
    , m_data(data)
    , m_connection(0)
    , m_lastReceivedChatID(0)
    , m_conversation(0)
{
    Q_ASSERT(uniqueID >= 0);

    m_conversation = new ConversationModel(this);
    m_conversation->setContact(this);
}

ContactUser::~ContactUser()
{
}

void ContactUser::updated(const ricochet::Contact &data)
{
    Q_ASSERT(data.id() == m_data.id());
    Q_ASSERT(data.address() == m_data.address());

    ricochet::Contact oldData = m_data;
    m_data = data;

    if (data.nickname() != oldData.nickname())
        emit nicknameChanged();
    if (data.status() != oldData.status())
        emit statusChanged();

    if (data.status() == ricochet::Contact::ONLINE && oldData.status() != ricochet::Contact::ONLINE)
        emit connected();
    if (oldData.status() == ricochet::Contact::ONLINE && data.status() != ricochet::Contact::ONLINE)
        emit disconnected();
}

QString ContactUser::nickname() const
{
    return QString::fromStdString(m_data.nickname());
}

void ContactUser::setNickname(const QString &nickname)
{
    // XXX
}

QString ContactUser::hostname() const
{
    return ContactIDValidator::hostnameFromID(contactID());
}

// XXX
quint16 ContactUser::port() const
{
    return 9878;
}

QString ContactUser::contactID() const
{
    return QString::fromStdString(m_data.address());
}

ContactUser::Status ContactUser::status() const
{
    return static_cast<Status>(m_data.status());
}

void ContactUser::setHostname(const QString &hostname)
{
    // XXX
    // Also, what
}

void ContactUser::deleteContact()
{
    /* Anything that uses ContactUser is required to either respond to the contactDeleted signal
     * synchronously, or make use of QWeakPointer. */

    // XXX
#if 0
    qDebug() << "Deleting contact" << uniqueID;

    emit contactDeleted(this);

    m_settings->undefine();
    deleteLater();
#endif
}

