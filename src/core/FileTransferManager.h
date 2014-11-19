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

#ifndef FILETRANSFERMANAGER_H
#define FILETRANSFERMANAGER_H

#include "FileTransfer.h"
#include <QObject>
#include <QUrl>

class ContactUser;
class UserIdentity;
class FileTransferManagerPrivate;

/* FileTransferManager tracks all active file transfers associated with an
 * identity. New outbound transfers are created with the sendFile methods.
 * The manager also takes ownership of inbound file transfer channels, and
 * creates FileTransfer instances for them.
 *
 * FileTransferManager will hold a reference to any FileTransfer that is
 * still viable - meaning it has not finished, and is active or could
 * become active again. Once it's removed, the FileTransfer instance will
 * continue to exist until all other references (e.g. ConversationModel)
 * are released.
 *
 * XXX: An alternate design would be to not hold references at this layer,
 * and require the UI to hold them for as long as it's interested. In that
 * case, if the UI forgot about a transfer, it would always be removed, so
 * there's no chance of "invisible" transfers persisting. With a custom
 * deleter function, this can be definitely-safe.
 */
class FileTransferManager : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(FileTransferManager)

public:
    explicit FileTransferManager(UserIdentity *identity);
    virtual ~FileTransferManager();

    QList<QSharedPointer<FileTransfer>> transfers() const;
    QSharedPointer<FileTransfer> sendFile(ContactUser *user, const QUrl &path);

    /* QML can't handle QSharedPointers, so we need to return the pointer directly.
     * This is still safe - QObject pointers are tracked by the engine and will be
     * null if accessed from javascript after free, like a weak pointer. QML won't
     * hold a reference, but the manager holds one for incomplete transfers anyway.
     */
    Q_INVOKABLE FileTransfer *sendFile2(ContactUser *user, const QUrl &path);

signals:
    void transferAdded(const QSharedPointer<FileTransfer> &transfer);
    void transferRemoved(const QSharedPointer<FileTransfer> &transfer);

private:
    friend class FileTransferManagerPrivate;
    FileTransferManagerPrivate *d;
};

#endif

