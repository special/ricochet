/* Torsion - http://torsionim.org/
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
#include <QFile>
#include <QFileInfo>
#include <QDebug>

class FileTransferPrivate
{
public:
    FileTransfer * const q;

    ContactUser *contact;
    bool isOutgoing;
    quint32 identifier;
    QString fileName;
    QIODevice *localDevice;
    FileTransfer::State state;
    QString errorMessage;
    quint64 fileSize;
    quint64 transferredSize;
    quint64 transferRate;

    FileTransferPrivate(FileTransfer *qptr, ContactUser *c, bool out)
        : q(qptr)
        , contact(c)
        , isOutgoing(out)
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
    void setError(const QString &errorMessage);
};

FileTransfer::FileTransfer(ContactUser *contact, bool isOutgoing, QObject *parent)
    : QObject(parent), d(new FileTransferPrivate(this, contact, isOutgoing))
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

    state = newState;
    emit q->stateChanged();
}

void FileTransferPrivate::setError(const QString &message)
{
    errorMessage = message;
    emit q->errorMessageChanged();
    setState(FileTransfer::Failed);
}

ContactUser *FileTransfer::contact() const
{
    return d->contact;
}

bool FileTransfer::isOutgoing() const
{
    return d->isOutgoing;
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

// XXX ownership
void FileTransfer::setLocalDevice(QIODevice *device)
{
    if (d->localDevice == device)
        return;

    // XXX When is it valid to change local device?
    delete d->localDevice;
    d->localDevice = device;

    if (d->localDevice && isOutgoing()) {
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
    if (!fileUrl.isLocalFile())
        return;
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

QString FileTransfer::errorMessage() const
{
    return d->errorMessage;
}

quint64 FileTransfer::transferredSize() const
{
    return d->transferredSize;
}

quint64 FileTransfer::transferRate() const
{
    return d->transferRate;
}

void FileTransfer::sendOffer()
{
    if (!d->isOutgoing || d->state > Offered) {
        Q_ASSERT_X(false, "FileTransfer::sendOffer", "Transfer is not in a valid state to offer");
        d->setError(QStringLiteral("Transfer is not in a valid state to offer"));
        return;
    }

    if (!d->localDevice) {
        d->setError(QStringLiteral("No local file for transfer"));
        return;
    }

    if (d->fileName.isEmpty() || d->fileSize < 1) {
        d->setError(QStringLiteral("Transfer does not have valid file name and size"));
        return;
    }

    if (!d->identifier) {
        // XXX test for collision
        while (!d->identifier)
            d->identifier = SecureRNG::randomInt(UINT_MAX);
    }

    qWarning() << "XXX Should offer transfer" << d->identifier << d->fileName << d->fileSize;
    d->setState(FileTransfer::Offered);
}

void FileTransfer::cancel()
{
    if (d->state == Cancelled)
        return;

    // XXX what else needs to happen here?
    qWarning() << "XXX Should send cancel" << d->identifier;
    d->setState(Cancelled);
}

void FileTransfer::start()
{
    if (d->isOutgoing || (d->state != Offered && d->state != Failed)) {
        Q_ASSERT_X(false, "FileTransfer::start", "Transfer is not in a valid state to start");
        d->setError(QStringLiteral("Transfer is not in a valid state to start"));
        return;
    }

    if (!d->localDevice) {
        d->setError(QStringLiteral("No local file for transfer"));
        return;
    }

    qWarning() << "XXX Should start transfer" << d->identifier << d->transferredSize;
    d->setState(Connecting);
}

void FileTransfer::setState(State newState)
{
    d->setState(newState);
}

void FileTransfer::peerFinished()
{
    if (!isOutgoing())
        return;

    // XXX what do we need to do other than state?
    d->setState(Finished);
}

void FileTransfer::peerCancel()
{
    // XXX what do we need to do other than state?
    d->setState(Cancelled);
}

