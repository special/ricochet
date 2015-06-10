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

#ifndef PROTOCOL_FILETRANSFERDATACHANNEL_H
#define PROTOCOL_FILETRANSFERDATACHANNEL_H

#include "Channel.h"
#include "FileTransferDataChannel.pb.h"

class QIODevice;

namespace Protocol
{

class FileTransferDataChannelPrivate;

/* Implements im.ricochet.file-transfer.data.
 *
 * The data channel, identified by a transferId matching an existing file transfer
 * offer, sends or receives data between the network and a local file.
 *
 * An outbound data channel is the recipient, and an inbound channel is the sender.
 *
 * The sender-side data channel will constantly transmit packets of data read from
 * the localDevice, starting at the dataPosition, until end of file is reached. The
 * last packet has a flag set to indicate that the sender believes the transmission
 * to be finished.
 *
 * The recipient-side writes received data to the localDevice.
 *
 * When a packet arrives with the last_packet flag set, the channel will be closed.
 * The dataTransferred signal is emitted for any inbound or outbound packet, and both
 * sides emit the finished signal when they believe the transfer to be completed.
 * If the dataPosition exceeds maxDataSize, the channel will be closed.
 *
 * Because of many layers of socket buffering, it's likely that the sender thinks
 * it is much further ahead than the recipient will. Also as a result of this, the
 * connection used for this channel is likely to have extremely high latency.
 *
 * This channel can be used on an Unknown-purpose connection, which will be changed
 * to the FileTransferData purpose, or on a KnownContact connection. It should be
 * used on a separate connection except for very small files.
 */
class FileTransferDataChannel : public Channel
{
    Q_OBJECT
    Q_DISABLE_COPY(FileTransferDataChannel)
    Q_DECLARE_PRIVATE(FileTransferDataChannel)

public:
    static const int DataPacketSize = 10240; // XXX why?
    static const int WriteBufferSize = DataPacketSize * 4;

    explicit FileTransferDataChannel(Direction direction, Connection *connection);

    QByteArray transferId() const;
    void setTransferId(const QByteArray &transferId);
    QIODevice *localDevice() const;
    void setLocalDevice(QIODevice *device);
    qint64 dataPosition() const;
    void setDataPosition(qint64 dataPosition);
    qint64 maxDataSize() const;
    void setMaxDataSize(qint64 maxDataSize);

signals:
    void dataTransferred(quint64 bytes);

    /* For outbound files, emitted when the last packet of the file is sent.
     * For inbound files, emitted when a packet marked as the end is received.
     * In either case, the channel will be closed afterwards.
     */
    void finished();

protected:
    virtual bool allowInboundChannelRequest(const Data::Control::OpenChannel *request, Data::Control::ChannelResult *result);
    virtual bool allowOutboundChannelRequest(Data::Control::OpenChannel *request);
    virtual void receivePacket(const QByteArray &packet);
};

}

#endif
