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
public:
    QString filename;
    quint64 filesize;
    QPointer<QIODevice> localDevice;

    FileTransferChannelPrivate(Channel *q, Channel::Direction direction, Connection *conn)
        : ChannelPrivate(q, QStringLiteral("im.ricochet.file-transfer"), direction, conn)
        , filesize(0)
    {
    }
};

}

FileTransferChannel::FileTransferChannel(Direction dir, Connection *conn)
    : Channel(new FileTransferChannelPrivate(this, dir, conn))
{
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
    if (d->filesize == 0) {
        qDebug() << "Rejecting request for" << type() << "of empty file";
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

    return true;
}

bool FileTransferChannel::allowOutboundChannelRequest(Data::Control::OpenChannel *request)
{
    Q_UNUSED(request);
    Q_D(FileTransferChannel);

    if (connection()->purpose() != Connection::Purpose::KnownContact) {
        BUG() << "Rejecting outbound request for" << type() << "channel for connection with unexpected purpose" << int(connection()->purpose());
        return false;
    }

    if (d->filesize == 0 || d->filename.isEmpty()) {
        BUG() << "Rejecting outbound request for" << type() << "channel without file data";
        return false;
    }

    if (d->localDevice.isNull() || !d->localDevice->isOpen()) {
        BUG() << "Rejecting outbound request for" << type() << "channel without opened local device";
        return false;
    }

    return true;
}

void FileTransferChannel::cancel()
{
}

void FileTransferChannel::receivePacket(const QByteArray &packet)
{
    Data::FileTransfer::Packet message;
    if (!message.ParseFromArray(packet.constData(), packet.size())) {
        closeChannel();
        return;
    }

    if (message.has_file_data()) {
        handleFileData(message.file_data());
    } else if (message.has_transfer_start()) {
        handleTransferStart(message.transfer_start());
    } else if (message.has_transfer_cancel()) {
        handleTransferCancel(message.transfer_cancel());
    } else {
        qWarning() << "Unrecognized message on" << type();
        closeChannel();
    }
}

void FileTransferChannel::start()
{
    Q_D(FileTransferChannel);
    if (direction() != Inbound) {
        BUG() << "Cannot start an outbound file transfer channel";
        return;
    }

    if (d->localDevice.isNull() || !d->localDevice->isOpen()) {
        BUG() << "Cannot start an inbound file transfer without a local device";
        return;
    }

    QScopedPointer<Data::FileTransfer::TransferStart> message(new Data::FileTransfer::TransferStart);
    Data::FileTransfer::Packet packet;
    packet.set_allocated_transfer_start(message.take());
    sendMessage(packet);
}

void FileTransferChannel::handleTransferStart(const Data::FileTransfer::TransferStart &message)
{
    if (direction() != Outbound) {
        closeChannel();
        return;
    }

    // XXX check state

    if (d->localDevice.isNull() || !d->localDevice.isOpen()) {
        BUG() << "Received transfer start for outbound transfer, but local device has disappeared";
        // XXX error, not user cancel
        cancel();
        return;
    }

    emit started();
}

void FileTransferChannel::handleTransferCancel(const Data::FileTransfer::TransferCancel &message)
{
}

void FileTransferChannel::handleFileData(const Data::FileTransfer::FileData &message)
{
}

