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

#include "ConversationModel.h"
#include "core/BackendRPC.h"
#include <QDebug>
#include <QDateTime>

ConversationModel::ConversationModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_contact(0)
    , m_unreadCount(0)
{
}

void ConversationModel::setContact(ContactUser *contact)
{
    if (contact == m_contact)
        return;

    beginResetModel();
    messages.clear();

    m_contact = contact;
    connect(m_contact, &ContactUser::statusChanged,
            this, &ConversationModel::onContactStatusChanged);

    endResetModel();
    emit contactChanged();
}

void ConversationModel::handleMessageEvent(const ricochet::ConversationEvent &event)
{
    // Can assume that the message is already basically valid, but be paranoid on this one
    ricochet::Message msg = event.msg();
    ricochet::Entity remoteEntity = msg.sender().isself() ? msg.recipient() : msg.sender();
    Q_ASSERT(QString::fromStdString(remoteEntity.address()) == m_contact->address());

    if (event.type() == ricochet::ConversationEvent::UPDATE)
    {
        int i = indexOfIdentifier(msg.identifier(), msg.sender().isself());
        if (i < 0) {
            qDebug() << "Ignoring message update for a message that isn't in this conversation model";
            return;
        }
        if (messages[i].status() == ricochet::Message::UNREAD && msg.status() != ricochet::Message::UNREAD) {
            m_unreadCount--;
            emit unreadCountChanged();
        }
        messages[i] = msg;
        emit dataChanged(index(i, 0), index(i, 0));
        return;
    }

    // New messages (either send, receive, or populate)
    int row = 0;
    if (event.type() == ricochet::ConversationEvent::RECEIVE) {
        // To preserve conversation flow despite potentially high latency, incoming messages
        // are positioned above the last unacknowledged messages to the peer. We assume that
        // the peer hadn't seen any unacknowledged message when this message was sent.
        for (int i = 0; i < messages.size() && i < 5; i++) {
            if (messages[i].status() != ricochet::Message::QUEUED &&
                messages[i].status() != ricochet::Message::SENDING) {
                row = i;
                break;
            }
        }
    }

    beginInsertRows(QModelIndex(), row, row);
    messages.insert(row, msg);
    endInsertRows();
    prune();

    if (msg.status() == ricochet::Message::UNREAD) {
        m_unreadCount++;
        emit unreadCountChanged();
    }
}

void ConversationModel::sendMessage(const QString &text)
{
    ricochet::Message msg;
    msg.mutable_sender()->set_isself(true);
    msg.mutable_recipient()->set_address(m_contact->address().toStdString());
    msg.set_text(text.toStdString());

    if (!backend->sendMessage(msg)) {
        // We should probably insert this message into the conversation as an error here, but
        // more thought is needed on how to handle these failures.
        qDebug() << "Sending conversation message failed";
        return;
    }

    // msg is now updated to be the full message object, but we can just wait for the
    // event to come in via handleMessageEvent also.
}

// XXX remote
void ConversationModel::clear()
{
    if (messages.isEmpty())
        return;

    beginRemoveRows(QModelIndex(), 0, messages.size()-1);
    messages.clear();
    endRemoveRows();

    resetUnreadCount();
}

// XXX remote
void ConversationModel::resetUnreadCount()
{
    if (m_unreadCount == 0)
        return;
    m_unreadCount = 0;
    emit unreadCountChanged();
}

void ConversationModel::onContactStatusChanged()
{
    // Update in case section has changed
    emit dataChanged(index(0, 0), index(rowCount()-1, 0), QVector<int>() << SectionRole);
}

QHash<int,QByteArray> ConversationModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[Qt::DisplayRole] = "text";
    roles[TimestampRole] = "timestamp";
    roles[IsOutgoingRole] = "isOutgoing";
    roles[StatusRole] = "status";
    roles[SectionRole] = "section";
    roles[TimespanRole] = "timespan";
    return roles;
}

int ConversationModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return messages.size();
}

QVariant ConversationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= messages.size())
        return QVariant();

    const ricochet::Message &message = messages[index.row()];

    switch (role) {
        case Qt::DisplayRole: return QString::fromStdString(message.text());
        case TimestampRole: return QDateTime::fromTime_t(message.timestamp());
        case IsOutgoingRole: return message.sender().isself();
        case StatusRole: return message.status();

        case SectionRole: {
            if (m_contact->status() == ContactUser::Online || message.status() != ricochet::Message::QUEUED)
                return QString();
            if (index.row() < messages.size() - 1) {
                const ricochet::Message &previous = messages[index.row()+1];
                if (previous.status() == ricochet::Message::QUEUED)
                    return QString();
            }
            for (int i = 0; i <= index.row(); i++) {
                if (messages[i].status() != ricochet::Message::QUEUED)
                    return QString();
            }
            return QStringLiteral("offline");
        }
        case TimespanRole: {
            if (index.row() < messages.size() - 1)
                return message.timestamp() - messages[index.row() + 1].timestamp();
            else
                return -1;
        }
    }

    return QVariant();
}

int ConversationModel::indexOfIdentifier(uint64_t identifier, bool isOutgoing) const
{
    for (int i = 0; i < messages.size(); i++) {
        if (messages[i].identifier() == identifier && messages[i].sender().isself() == isOutgoing)
            return i;
    }
    return -1;
}

void ConversationModel::prune()
{
    const int history_limit = 1000;
    if (messages.size() > history_limit) {
        beginRemoveRows(QModelIndex(), history_limit, messages.size()-1);
        while (messages.size() > history_limit) {
            messages.removeLast();
        }
        endRemoveRows();
    }
}
