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

#include "FileTransfer.h"
#include "utils/SecureRNG.h"
#include "utils/StringUtil.h"
#include "utils/Useful.h"
#include <QFile>
#include <QFileInfo>
#include <QDebug>

class FileTransferPrivate
{
public:
    FileTransfer * const q;

    ContactUser *contact;
    bool isOutbound;
    quint32 identifier;
    QString fileName;
    QIODevice *localDevice;
    FileTransfer::State state;
    quint64 fileSize;
    quint64 transferredSize;
    quint64 transferRate;

    FileTransferPrivate(FileTransfer *qptr, ContactUser *c, bool out)
        : q(qptr)
        , contact(c)
        , isOutbound(out)
        , identifier(0)
        , localDevice(0)
        , state(FileTransfer::Unknown)
        , fileSize(0)
        , transferredSize(0)
        , transferRate(0)
    {
    }

    ~FileTransferPrivate()
    {
        delete localDevice;
    }

    void setState(FileTransfer::State state);
};

FileTransfer::FileTransfer(ContactUser *contact, bool isOutbound, QObject *parent)
    : QObject(parent), d(new FileTransferPrivate(this, contact, isOutbound))
{
}

FileTransfer::~FileTransfer()
{
    delete d;
}

void FileTransferPrivate::setState(FileTransfer::State newState)
{
    if (state == newState)
        return;

    qDebug() << this << "State" << state << "->" << newState;
    state = newState;
    emit q->stateChanged();
}

ContactUser *FileTransfer::contact() const
{
    return d->contact;
}

bool FileTransfer::isOutbound() const
{
    return d->isOutbound;
}

quint32 FileTransfer::identifier() const
{
    return d->identifier;
}

QString FileTransfer::fileName() const
{
    return d->fileName;
}

void FileTransfer::setFileName(const QString &input)
{
    QString fileName = sanitizedFileName(input);
    if (d->fileName == fileName)
        return;

    d->fileName = fileName;
    emit fileNameChanged();
}

quint64 FileTransfer::fileSize() const
{
    return d->fileSize;
}

void FileTransfer::setFileSize(quint64 size)
{
    if (d->fileSize == size)
        return;

    d->fileSize = size;
    emit fileSizeChanged();
}

QIODevice *FileTransfer::localDevice() const
{
    return d->localDevice;
}

void FileTransfer::setLocalDevice(QIODevice *device)
{
    if (d->localDevice == device)
        return;

    if (d->state == Active) {
        BUG() << "Cannot change local device of file transfer in active state";
        return;
    }

    delete d->localDevice;
    d->localDevice = device;
    if (device)
        device->setParent(this);

    if (d->localDevice && isOutbound()) {
        d->fileSize = d->localDevice->size();
        emit fileSizeChanged();
    }

    QFile *file = qobject_cast<QFile*>(d->localDevice);
    if (d->fileName.isEmpty() && file)
        setFileName(QFileInfo(*file).fileName());

    emit localDeviceChanged();
}

QString FileTransfer::localFilePath() const
{
    QFile *file = qobject_cast<QFile*>(d->localDevice);
    if (file)
        return QFileInfo(*file).absoluteFilePath();
    return QString();
}

void FileTransfer::setLocalFilePath(const QString &filePath)
{
    QFileInfo fi(filePath);
    if (localFilePath() == fi.absoluteFilePath())
        return;

    QFile *file = new QFile(fi.absoluteFilePath(), this);
    setLocalDevice(file);
}

QUrl FileTransfer::localFileUrl() const
{
    QString path = localFilePath();
    return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
}

void FileTransfer::setLocalFileUrl(const QUrl &fileUrl)
{
    if (!fileUrl.isLocalFile()) {
        BUG() << "Cannot set transfer localFileUrl to a non-file URL";
        return;
    }
    setLocalFilePath(fileUrl.toLocalFile());
}

bool FileTransfer::hasLocalFile() const
{
    return !localFilePath().isEmpty();
}

FileTransfer::State FileTransfer::state() const
{
    return d->state;
}

quint64 FileTransfer::transferredSize() const
{
    return d->transferredSize;
}

quint64 FileTransfer::transferRate() const
{
    return d->transferRate;
}

bool FileTransfer::initializeOffer()
{
    if (d->isOutbound) {
        BUG() << "Tried to initialize an offer on an outbound file transfer";
        return false;
    }

    if (d->state != Unknown)
        return false;

    // XXX Do we need separate states for "initialized but not started" and "offering"?
    d->setState(Offer);
    return true;
}

void FileTransfer::start()
{
    if (!d->localDevice) {
        BUG() << "Tried to start a" << (isOutbound() ? "outbound" : "inbound") << "file transfer without a local device";
        return;
   }

    if (d->fileName.isEmpty() || d->fileSize < 1) {
        BUG() << "Tried to start a" << (isOutbound() ? "outbound" : "inbound") << "file transfer without a filename and size";
        return;
    }

    if (d->isOutbound) {
        if (d->state == Finished) {
            BUG() << "Tried to start an outbound file transfer that is already finished";
            return;
        }

        if (!d->identifier) {
            // XXX test for collision
            while (!d->identifier)
                d->identifier = SecureRNG::randomInt(UINT_MAX);
        }

        qWarning() << "XXX Should offer transfer" << d->identifier << d->fileName << d->fileSize;
        d->setState(Offer);
    } else {
        if (d->state != Offer) {
            BUG() << "Tried to start an inbound file transfer in non-Offer state" << d->state;
            return;
        }

        qWarning() << "XXX Should start transfer" << d->identifier << d->transferredSize;
        d->setState(Active);
    }
}

void FileTransfer::cancel()
{
    if (d->state == Canceled)
        return;

    qWarning() << "XXX Should send cancel" << d->identifier;
    d->setState(Canceled);
}

