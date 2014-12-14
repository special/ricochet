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
#include <QFileInfo>
#include <QDebug>

FileTransferManager *fileTransferManager = 0;

class FileTransferManagerPrivate
{
public:
    FileTransferManager *q;
    QList<FileTransfer*> transfers;

    FileTransferManagerPrivate(FileTransferManager *qq)
        : q(qq)
    {
    }

    void addTransfer(FileTransfer *t);
};

FileTransferManager::FileTransferManager(QObject *parent)
    : QObject(parent), d(new FileTransferManagerPrivate(this))
{
}

FileTransferManager::~FileTransferManager()
{
    delete d;
}

QList<FileTransfer*> FileTransferManager::transfers() const
{
    return d->transfers;
}

FileTransfer *FileTransferManager::findTransfer(ContactUser *user, quint32 identifier)
{
    foreach (FileTransfer *t, d->transfers) {
        if (t->contact() == user && t->identifier() == identifier)
            return t;
    }
    return 0;
}

void FileTransferManagerPrivate::addTransfer(FileTransfer *t)
{
    if (transfers.contains(t))
        return;

    transfers.append(t);
    emit q->transferAdded(t);
}

FileTransfer *FileTransferManager::sendFile(ContactUser *user, const QUrl &path)
{
    if (!user || !path.isLocalFile()) {
        qWarning() << Q_FUNC_INFO << "invalid arguments";
        return 0;
    }

    QFileInfo fi(path.toLocalFile());
    fi.makeAbsolute();
    if (!fi.isFile() || fi.size() < 1) {
        qWarning() << Q_FUNC_INFO << "target file does not exist:" << fi.filePath();
        return 0;
    }

    FileTransfer *transfer = new FileTransfer(user, true, this);
    transfer->setLocalFilePath(fi.filePath());

    d->addTransfer(transfer);
    transfer->start();
    return transfer;
}

bool FileTransferManager::addOfferedTransfer(FileTransfer *transfer)
{
    if (!transfer || transfer->state() != FileTransfer::Unknown || transfer->isOutbound())
        return false;

    if (findTransfer(transfer->contact(), transfer->identifier()))
        return false;

    d->addTransfer(transfer);
    return transfer->initializeOffer();
}

