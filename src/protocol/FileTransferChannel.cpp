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

using namespace Protocol;

FileTransferChannel::FileTransferChannel(Direction direction, Connection *connection)
    : Channel(QStringLiteral("im.ricochet.file-transfer"), direction, connection)
    , filesize(0)
{
}

bool FileTransferChannel::allowInboundChannelRequest(const Data::Control::OpenChannel *request, Data::Control::ChannelResult *result)
{
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

    filesize = offer.file_size();
    if (filesize == 0) {
        qDebug() << "Rejecting request for" << type() << "of empty file";
        return false;
    }

    QString rawFilename = QString::fromStdString(offer.file_name());
    if (rawFilename.size() > FilenameMaxCharacters) {
        qDebug() << "Rejecting request for" << type() << "with excessive filename of" << rawFilename.size() << "characters";
        return false;
    }
    filename = sanitizedFileName(rawFilename);
    if (filename.isEmpty()) {
        qDebug() << "Rejecting request for" << type() << "with empty filename";
        return false;
    }

    return true;
}

bool FileTransferChannel::allowOutboundChannelRequest(Data::Control::OpenChannel *request)
{
    Q_UNUSED(request);

    if (connection()->purpose() != Connection::Purpose::KnownContact) {
        BUG() << "Rejecting outbound request for" << type() << "channel for connection with unexpected purpose" << int(connection()->purpose());
        return false;
    }

    return true;
}

void FileTransferChannel::receivePacket(const QByteArray &packet)
{
    Data::FileTransfer::Packet message;
    if (!message.ParseFromArray(packet.constData(), packet.size())) {
        closeChannel();
        return;
    }

    if (message.has_file_data()) {
    } else if (message.has_transfer_start()) {
    } else if (message.has_transfer_cancel()) {
    } else {
        qWarning() << "Unrecognized message on" << type();
        closeChannel();
    }
}

