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

#include "FileTransferManager.h"
#include "FileTransfer.h"
#include "ContactUser.h"
#include "UserIdentity.h"
#include "protocol/FileTransferChannel.h"
#include "protocol/FileTransferDataChannel.h"
#include "protocol/Connection.h"
#include "utils/Useful.h"
#include <QFileInfo>
#include <QQmlEngine>
#include <QDebug>

class FileTransferManagerPrivate
{
public:
    FileTransferManager *q;
    QList<QSharedPointer<FileTransfer>> transfers;
    UserIdentity *identity;

    FileTransferManagerPrivate(FileTransferManager *qq)
        : q(qq)
        , identity(0)
    {
    }

    void addTransfer(const QSharedPointer<FileTransfer> &t);
    void removeTransfer(const QSharedPointer<FileTransfer> &t);
    void removeTransfer(FileTransfer *t);
};

FileTransferManager::FileTransferManager(UserIdentity *identity)
    : QObject(identity), d(new FileTransferManagerPrivate(this))
{
    d->identity = identity;

    auto attachChannel = [this](Protocol::Channel *channel) {
        if (Protocol::FileTransferChannel *transferChannel = qobject_cast<Protocol::FileTransferChannel*>(channel)) {
            if (transferChannel->direction() == Protocol::Channel::Inbound) {
                ContactUser *user = ContactUser::userFromConnection(d->identity, channel->connection());
                if (!user) {
                    BUG() << "Called to handle an inbound FileTransferChannel on a connection of purpose" << int(channel->connection()->purpose()) << "without an attached contact";
                    return;
                }

                qDebug() << "Creating file transfer for inbound channel" << transferChannel << transferChannel->fileName() << transferChannel->fileSize();
                QSharedPointer<FileTransfer> transfer(new FileTransfer(user, false), &QObject::deleteLater);
                if (!transfer->setInboundChannel(transferChannel)) {
                    qWarning() << "Failed creating transfer from inbound transfer channel; destroying channel";
                    transfer.clear();
                    transferChannel->closeChannel();
                    return;
                }

                d->addTransfer(transfer);
            }
        }
    };

    auto approveDataChannel = [this](Protocol::Channel *channel) {
        if (Protocol::FileTransferDataChannel *dataChannel = qobject_cast<Protocol::FileTransferDataChannel*>(channel)) {
            if (dataChannel->transferId().isEmpty()) {
                BUG() << "Cannot approve a FileTransferDataChannel with no transfer id";
                return;
            }

            foreach (const QSharedPointer<FileTransfer> &transfer, d->transfers) {
                if (transfer->transferId() == dataChannel->transferId()) {
                    if (!transfer->setDataChannel(dataChannel)) {
                        qWarning() << "Failed setting data channel for transfer";
                    }
                    break;
                }
            }
        }
    };

    auto attachConnection = [=](const QWeakPointer<Protocol::Connection> &connection) {
        if (connection) {
            connect(connection.data(), &Protocol::Connection::channelRequestingInboundApproval, this, approveDataChannel, Qt::UniqueConnection);
            connect(connection.data(), &Protocol::Connection::channelOpened, this, attachChannel);
            foreach (auto channel, connection.data()->findChannels<Protocol::FileTransferChannel>())
                attachChannel(channel);
        }
    };

    auto attachContact = [this,attachConnection](ContactUser *user) {
        connect(user, &ContactUser::connectionChanged, this, attachConnection);
        if (user->connection())
            attachConnection(user->connection());
    };

    connect(&identity->contacts, &ContactsManager::contactAdded, this, attachContact);
    foreach (ContactUser *user, identity->contacts.contacts())
        attachContact(user);

    connect(identity, &UserIdentity::incomingConnection, this,
        [=](Protocol::Connection *connection) {
            connect(connection, &Protocol::Connection::channelRequestingInboundApproval, this, approveDataChannel);
        }
    );
}

FileTransferManager::~FileTransferManager()
{
    delete d;
}

QList<QSharedPointer<FileTransfer>> FileTransferManager::transfers() const
{
    return d->transfers;
}

void FileTransferManagerPrivate::addTransfer(const QSharedPointer<FileTransfer> &t)
{
    if (transfers.contains(t))
        return;

    FileTransfer *ft = t.data();
    QObject::connect(ft, &FileTransfer::stateChanged, q,
        [this,ft]() {
            if (ft->isStateFinal())
                removeTransfer(ft);
        }
    );

    transfers.append(t);
    emit q->transferAdded(t);
}

void FileTransferManagerPrivate::removeTransfer(const QSharedPointer<FileTransfer> &t)
{
    if (transfers.removeOne(t))
        emit q->transferRemoved(t);
}

void FileTransferManagerPrivate::removeTransfer(FileTransfer *t)
{
    for (auto it = transfers.begin(), end = transfers.end(); it != end; it++) {
        if (it->data() == t) {
            QSharedPointer<FileTransfer> ft = *it;
            transfers.erase(it);
            emit q->transferRemoved(ft);
            return;
        }
    }
}

QSharedPointer<FileTransfer> FileTransferManager::sendFile(ContactUser *user, const QUrl &path)
{
    QSharedPointer<FileTransfer> transfer;
    if (!user || !path.isLocalFile()) {
        qWarning() << Q_FUNC_INFO << "invalid arguments";
        return transfer;
    }

    QFileInfo fi(path.toLocalFile());
    fi.makeAbsolute();
    if (!fi.isFile() || fi.size() < 1) {
        qWarning() << Q_FUNC_INFO << "target file does not exist:" << fi.filePath();
        return transfer;
    }

    transfer = QSharedPointer<FileTransfer>(new FileTransfer(user, true), &QObject::deleteLater);
    transfer->setLocalFilePath(fi.filePath());

    d->addTransfer(transfer);
    transfer->start();
    return transfer;
}

FileTransfer *FileTransferManager::sendFile2(ContactUser *user, const QUrl &path)
{
    FileTransfer *t = sendFile(user, path).data();
    // Prevent QML from taking ownership of this object
    QQmlEngine::setObjectOwnership(t, QQmlEngine::CppOwnership);
    return t;
}
