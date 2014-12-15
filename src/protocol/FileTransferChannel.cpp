/* Ricochet - https://ricochet.im/
 * Copyright (C) 2015, John Brooks <john.brooks@dereferenced.net>
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

#include "FileTransferChannel.h"
#include "Channel_p.h"
#include "Connection.h"
#include "utils/Useful.h"
#include "utils/StringUtil.h"
#include <QIODevice>

using namespace Protocol;

namespace Protocol {

class FileTransferChannelPrivate : public ChannelPrivate
{
    Q_DECLARE_PUBLIC(FileTransferChannel)

public:
    QString filename;
    quint64 filesize;
    QByteArray transferId;
    bool started;
    bool finished;

    FileTransferChannelPrivate(Channel *q, Channel::Direction direction, Connection *conn)
        : ChannelPrivate(q, QStringLiteral("im.ricochet.file-transfer"), direction, conn)
        , filesize(0)
        , started(false)
        , finished(false)
    {
    }
};

}

FileTransferChannel::FileTransferChannel(Direction dir, Connection *conn)
    : Channel(new FileTransferChannelPrivate(this, dir, conn))
{
}

QString FileTransferChannel::fileName() const
{
    Q_D(const FileTransferChannel);
    return d->filename;
}

void FileTransferChannel::setFileName(const QString &name)
{
    Q_D(FileTransferChannel);
    if (direction() != Outbound) {
        BUG() << "Setting filename on an inbound file transfer channel doesn't make sense";
        return;
    }

    if (name.size() > FilenameMaxCharacters) {
        BUG() << "Filename is too long for transfer channel";
        return;
    }

    d->filename = name;
}

quint64 FileTransferChannel::fileSize() const
{
    Q_D(const FileTransferChannel);
    return d->filesize;
}

void FileTransferChannel::setFileSize(quint64 size)
{
    Q_D(FileTransferChannel);
    d->filesize = size;
}

QByteArray FileTransferChannel::transferId() const
{
    Q_D(const FileTransferChannel);
    return d->transferId;
}

void FileTransferChannel::setTransferId(const QByteArray &transferId)
{
    Q_D(FileTransferChannel);
    if (transferId.size() != TransferIdSize) {
        BUG() << "File transfer id is invalid size" << transferId.size();
        return;
    }

    d->transferId = transferId;
}

bool FileTransferChannel::allowInboundChannelRequest(const Data::Control::OpenChannel *request, Data::Control::ChannelResult *result)
{
    Q_D(FileTransferChannel);
    if (connection()->purpose() != Connection::Purpose::KnownContact) {
        qDebug() << "Rejecting request for" << type() << "channel from connection with purpose" << int(connection()->purpose());
        result->set_common_error(Data::Control::ChannelResult::UnauthorizedError);
        return false;
    }

    if (!request->HasExtension(Data::FileTransfer::file_offer)) {
        qDebug() << "Rejecting request for" << type() << "channel with no FileOffer";
        return false;
    }

    Data::FileTransfer::FileOffer offer = request->GetExtension(Data::FileTransfer::file_offer);

    d->filesize = offer.file_size();
    if (d->filesize == 0 || d->filesize > MaxFileSize) {
        qDebug() << "Rejecting request for" << type() << "of file with invalid size" << d->filesize;
        return false;
    }

    QString rawFilename = QString::fromStdString(offer.file_name());
    if (rawFilename.size() > FilenameMaxCharacters) {
        qDebug() << "Rejecting request for" << type() << "with excessive filename of" << rawFilename.size() << "characters";
        return false;
    }
    d->filename = sanitizedFileName(rawFilename);
    if (d->filename.isEmpty()) {
        qDebug() << "Rejecting request for" << type() << "with empty filename";
        return false;
    }

    d->transferId = QByteArray::fromStdString(offer.transfer_id());
    if (d->transferId.size() != TransferIdSize) {
        qDebug() << "Rejecting request for" << type() << "with invalid transfer id size of" << TransferIdSize;
        return false;
    }

    return true;
}

bool FileTransferChannel::allowOutboundChannelRequest(Data::Control::OpenChannel *request)
{
    Q_D(FileTransferChannel);

    if (connection()->purpose() != Connection::Purpose::KnownContact) {
        BUG() << "Rejecting outbound request for" << type() << "channel for connection with unexpected purpose" << int(connection()->purpose());
        return false;
    }

    if (d->filesize == 0 || d->filename.isEmpty()) {
        BUG() << "Rejecting outbound request for" << type() << "channel without file data";
        return false;
    }

    if (d->transferId.size() != TransferIdSize) {
        BUG() << "Rejecting outbound request for" << type() << "channel without transfer id";
        return false;
    }

    QScopedPointer<Data::FileTransfer::FileOffer> offer(new Data::FileTransfer::FileOffer);
    offer->set_file_name(d->filename.toStdString());
    offer->set_file_size(d->filesize);
    offer->set_transfer_id(d->transferId);
    request->SetAllocatedExtension(Data::FileTransfer::file_offer, offer.take());
    return true;
}

void FileTransferChannel::cancel()
{
    QScopedPointer<Data::FileTransfer::TransferCancel> cancel(new Data::FileTransfer::TransferCancel);
    cancel->set_by_user(true);

    Data::FileTransfer::Packet packet;
    packet.set_allocated_cancel(cancel.take());
    sendMessage(packet);
    closeChannel();
}

void FileTransferChannel::receivePacket(const QByteArray &packet)
{
    Data::FileTransfer::Packet message;
    if (!message.ParseFromArray(packet.constData(), packet.size())) {
        closeChannel();
        return;
    }

    if (message.has_cancel()) {
        handleTransferCancel(message.cancel());
        return;
    } else if (direction() == Outbound) {
        if (message.has_start()) {
            handleTransferStart(message.start());
            return;
        } else if (message.has_finished()) {
            handleTransferFinished(message.finished());
            return;
        }
    }

    qWarning() << "Unrecognized message on" << type();
    closeChannel();
}

void FileTransferChannel::start()
{
    Q_D(FileTransferChannel);
    if (direction() != Inbound) {
        BUG() << "Cannot start an outbound file transfer channel";
        return;
    }

    if (d->started) {
        BUG() << "Tried to start a file transfer channel repeatedly";
        return;
    }

    QScopedPointer<Data::FileTransfer::TransferStart> message(new Data::FileTransfer::TransferStart);
    Data::FileTransfer::Packet packet;
    packet.set_allocated_start(message.take());
    sendMessage(packet);

    d->started = true;
    emit started();
}

void FileTransferChannel::handleTransferStart(const Data::FileTransfer::TransferStart &message)
{
    Q_UNUSED(message);
    Q_D(FileTransferChannel);
    if (d->started) {
        qDebug() << "Peer tried to repeatedly start a file transfer channel";
        closeChannel();
        return;
    }

    qDebug() << this << "Received transfer start";
    d->started = true;
    emit started();
}

void FileTransferChannel::handleTransferCancel(const Data::FileTransfer::TransferCancel &message)
{
    Q_UNUSED(message);
    // XXX What else do we need to do here?
    qDebug() << "File transfer is canceled by the peer";
    emit canceled();
    closeChannel();
}

void FileTransferChannel::handleTransferFinished(const Data::FileTransfer::TransferFinished &message)
{
    Q_D(FileTransferChannel);
    Q_UNUSED(message);
    // XXX Anything else?
    qDebug() << "File transfer has finished";
    d->finished = true;
    emit finished();
    closeChannel();
}
