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

#include "FileTransferDataChannel.h"
#include "FileTransferChannel.h"
#include "Channel_p.h"
#include "Connection.h"
#include <QPointer>

using namespace Protocol;

namespace Protocol {

class FileTransferDataChannelPrivate : public ChannelPrivate
{
    Q_DECLARE_PUBLIC(FileTransferDataChannel)

public:
    QByteArray transferId;
    qint64 dataPosition;
    qint64 maxDataSize;
    QPointer<QIODevice> localDevice;

    FileTransferDataChannelPrivate(Channel *q, Channel::Direction direction, Connection *conn)
        : ChannelPrivate(q, QStringLiteral("im.ricochet.file-transfer.data"), direction, conn)
        , dataPosition(0)
        , maxDataSize(0)
        , localDevice(0)
    {
    }

    void startSending();
    void sendPacket();
};

}

FileTransferDataChannel::FileTransferDataChannel(Direction dir, Connection *conn)
    : Channel(new FileTransferDataChannelPrivate(this, dir, conn))
{
}

QByteArray FileTransferDataChannel::transferId() const
{
    Q_D(const FileTransferDataChannel);
    return d->transferId;
}

void FileTransferDataChannel::setTransferId(const QByteArray &transferId)
{
    Q_D(FileTransferDataChannel);
    if (transferId.size() != FileTransferChannel::TransferIdSize) {
        BUG() << "FileTransferDataChannel given an invalid transferId of size" << transferId.size();
        return;
    }

    d->transferId = transferId;
}

QIODevice *FileTransferDataChannel::localDevice() const
{
    Q_D(const FileTransferDataChannel);
    return d->localDevice;
}

void FileTransferDataChannel::setLocalDevice(QIODevice *device)
{
    Q_D(FileTransferDataChannel);
    if (!device->isOpen() ||
        (direction() == Inbound && !device->isReadable()) ||
        (direction() == Outbound && !device->isWritable()))
    {
        BUG() << "Device for FileTransferDataChannel isn't open in the correct mode for this transfer";
        return;
    }

    d->localDevice = device;
}

qint64 FileTransferDataChannel::dataPosition() const
{
    Q_D(const FileTransferDataChannel);
    return d->dataPosition;
}

void FileTransferDataChannel::setDataPosition(qint64 position)
{
    Q_D(FileTransferDataChannel);
    d->dataPosition = position;
}

qint64 FileTransferDataChannel::maxDataSize() const
{
    Q_D(const FileTransferDataChannel);
    return d->maxDataSize;
}

void FileTransferDataChannel::setMaxDataSize(qint64 max)
{
    Q_D(FileTransferDataChannel);
    d->maxDataSize = max;
}

bool FileTransferDataChannel::allowInboundChannelRequest(const Data::Control::OpenChannel *request, Data::Control::ChannelResult *result)
{
    Q_D(FileTransferDataChannel);

    // Allow a request from an Unknown channel, but we'll require that the
    // type is changed to FileTransferData before accepting.
    if (connection()->purpose() != Connection::Purpose::KnownContact &&
        connection()->purpose() != Connection::Purpose::FileTransferData &&
        connection()->purpose() != Connection::Purpose::Unknown) {
        qDebug() << "Rejecting request for" << type() << "channel from connection with purpose" << int(connection()->purpose());
        result->set_common_error(Data::Control::ChannelResult::UnauthorizedError);
        return false;
    }

    d->transferId = QByteArray::fromStdString(request->GetExtension(Data::FileTransferData::transfer_id));
    d->dataPosition = request->GetExtension(Data::FileTransferData::start_position);

    if (d->transferId.size() != FileTransferChannel::TransferIdSize) {
        qDebug() << "Rejecting request for" << type() << "channel with invalid transfer id of" << d->transferId.size() << "bytes";
        result->set_common_error(Data::Control::ChannelResult::BadUsageError);
        return false;
    }

    if (d->dataPosition < 0) {
        qDebug() << "Rejecting request for" << type() << "channel with invalid start position of" << d->dataPosition;
        result->set_common_error(Data::Control::ChannelResult::BadUsageError);
        return false;
    }

    // If the transfer ID is recognized, this data channel will be claimed
    // by a FileTransfer instance, and localDevice should be set.
    requestInboundApproval();

    if (d->localDevice.isNull() || !d->localDevice->isOpen()) {
        qDebug() << "Rejecting request for" << type() << "channel with no open local device";
        result->set_common_error(Data::Control::ChannelResult::FailedError);
        return false;
    }

    if (d->dataPosition >= d->maxDataSize) {
        qDebug() << "Rejecting request for" << type() << "channel with position" << d->dataPosition << "exceeding maximum of" << d->maxDataSize;
        result->set_common_error(Data::Control::ChannelResult::BadUsageError);
        return false;
    }

    // If it was Unknown, the purpose should have been changed when approving
    if (connection()->purpose() == Connection::Purpose::Unknown) {
        qDebug() << "Rejecting request for" << type() << "channel on Unknown purpose connection";
        result->set_common_error(Data::Control::ChannelResult::UnauthorizedError);
        return false;
    }

    connect(this, &Channel::channelOpened, d, &FileTransferDataChannelPrivate::startSending, Qt::QueuedConnection);
    return true;
}

bool FileTransferDataChannel::allowOutboundChannelRequest(Data::Control::OpenChannel *request)
{
    Q_D(FileTransferDataChannel);
    if (connection()->purpose() != Connection::Purpose::KnownContact &&
        connection()->purpose() != Connection::Purpose::FileTransferData) {
        BUG() << "Rejecting outbound request for" << type() << "channel for connection with unexpected purpose" << int(connection()->purpose());
        return false;
    }

    if (d->transferId.size() != FileTransferChannel::TransferIdSize) {
        BUG() << "Rejecting outbound request for" << type() << "channel with invalid transfer id of" << d->transferId.size() << "bytes";
        return false;
    }

    if (d->localDevice.isNull() || !d->localDevice->isOpen()) {
        BUG() << "Rejecting outbound request for" << type() << "channel without opened local device";
        return false;
    }

    request->SetExtension(Data::FileTransferData::transfer_id, d->transferId.toStdString());
    request->SetExtension(Data::FileTransferData::start_position, d->dataPosition);
    return true;
}

void FileTransferDataChannel::receivePacket(const QByteArray &packetData)
{
    Q_D(FileTransferDataChannel);
    if (direction() != Channel::Outbound) {
        qDebug() << "Received unexpected packet on an inbound" << type();
        closeChannel();
        return;
    }

    Data::FileTransferData::Packet packet;
    if (!packet.ParseFromArray(packetData.constData(), packetData.size())) {
        closeChannel();
        return;
    }

    if (d->localDevice.isNull() || !d->localDevice->isOpen()) {
        BUG() << "Received data for an inbound transfer, but local device has disappeared";
        closeChannel();
        return;
    }

    if (packet.has_data_position() && packet.data_position() != d->dataPosition) {
        qDebug() << "Received file transfer data packet for position"
                 << packet.data_position() << "when expecting" << d->dataPosition;
        closeChannel();
        return;
    }

    std::string data = packet.data();
    if (data.size() == 0) {
        qDebug() << "Received empty file data message";
        closeChannel();
        return;
    }

    if (data.size() > 65535) {
        // This is larger than the protocol framing should allow
        BUG() << "Impossibly large file transfer data packet";
        closeChannel();
        return;
    }

    qDebug() << this << "Received data packet of" << data.size() << "bytes";

    if (d->dataPosition + int(data.size()) > d->maxDataSize ||
        d->dataPosition + int(data.size()) < d->dataPosition) // Overflow check
    {
        qDebug() << "Received too much data for file transfer; we expected no more"
                 << "than" << d->maxDataSize << "bytes, but this packet reaches"
                 << d->dataPosition + data.size();
        closeChannel();
        return;
    }

    // It's safe to assume that write() will be all-or-nothing due to the
    // buffering behavior in QIODevice.
    if (!d->localDevice->seek(d->dataPosition) ||
        d->localDevice->write(data.data(), data.size()) != qint64(data.size()))
    {
        qWarning() << "Write of file transfer failed:" << d->localDevice->errorString();
        closeChannel();
        return;
    }

    d->dataPosition += data.size();
    emit dataTransferred(data.size());

    if (packet.last_packet()) {
        if (d->dataPosition < d->maxDataSize) {
            qDebug() << "File transfer data channel is finished, but we didn't get"
                     << "as much as expected. We have" << d->dataPosition << "bytes,"
                     << "and the maximum was" << d->maxDataSize;
        }
        emit finished();
        qDebug() << "Closing file transfer data receive channel after the last packet";
        closeChannel();
    }
}

void FileTransferDataChannelPrivate::startSending()
{
    Q_Q(FileTransferDataChannel);

    connect(connection, &Connection::dataWritten, this,
        [this]() {
            if (connection->bytesToWrite() < FileTransferDataChannel::WriteBufferSize)
                sendPacket();
        }
    );

    connect(q, &Channel::invalidated, this,
        [this]() {
            disconnect(connection, &Connection::dataWritten, this, 0);
        }
    );

    connect(q, &FileTransferDataChannel::finished, this,
        [this]() {
            disconnect(connection, &Connection::dataWritten, this, 0);
        }
    );

    // Send the first packet
    sendPacket();
}

void FileTransferDataChannelPrivate::sendPacket()
{
    Q_Q(FileTransferDataChannel);
    // Inbound channels are for sending files
    if (q->direction() != Channel::Inbound) {
        BUG() << "Sending data on an outbound file transfer channel (for an inbound file)";
        return;
    }

    if (!q->isOpened()) {
        BUG() << "File transfer data channel is already closed";
        return;
    }

    if (localDevice.isNull() || !localDevice->isOpen()) {
        BUG() << "Trying to send data for outbound transfer, but local device has disappeared";
        q->closeChannel();
        return;
    }

    if (!localDevice->seek(dataPosition)) {
        qDebug() << "Seek error while sending file:" << localDevice->errorString();
        q->closeChannel();
        return;
    }

    QByteArray data = localDevice->read(FileTransferDataChannel::DataPacketSize);
    if (data.isEmpty()) {
        qDebug() << "Read error while sending file:" << localDevice->errorString();
        q->closeChannel();
        return;
    }

    if (dataPosition + data.size() > maxDataSize) {
        qDebug() << "Read more data than expected from local file for transfer; truncating";
        data.truncate(maxDataSize - dataPosition);
        Q_ASSERT(dataPosition + data.size() == maxDataSize);
    }

    Data::FileTransferData::Packet packet;
    packet.set_data(data.toStdString());
    packet.set_data_position(dataPosition);
    if (localDevice->atEnd() || dataPosition + data.size() == maxDataSize)
        packet.set_last_packet(true);
    if (!q->sendMessage(packet)) {
        qDebug() << "Write error while sending file";
        q->closeChannel();
        return;
    }

    dataPosition += data.size();
    emit q->dataTransferred(data.size());

    if (packet.last_packet()) {
        // XXX this wait needs a timeout
        qDebug() << "File transfer data channel sent last packet; waiting for peer to acknowledge";
        emit q->finished();
    }
}
