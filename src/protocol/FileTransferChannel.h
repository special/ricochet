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

#ifndef PROTOCOL_FILETRANSFERCHANNEL_H
#define PROTOCOL_FILETRANSFERCHANNEL_H

#include "Channel.h"
#include "FileTransferChannel.pb.h"
#include <QPointer>

class QIODevice;

namespace Protocol
{

class FileTransferChannelPrivate;

class FileTransferChannel : public Channel
{
    Q_OBJECT
    Q_DISABLE_COPY(FileTransferChannel)
    Q_DECLARE_PRIVATE(FileTransferChannel)

public:
    static const int FilenameMaxCharacters = 500;

    explicit FileTransferChannel(Direction direction, Connection *connection);

    QString filename() const;
    void setFilename(const QString &filename);

    quint64 filesize() const;
    void setFilesize(quint64 size);

    QIODevice *localDevice();
    void setLocalDevice(QIODevice *device);

public slots:
    void start();
    void cancel();

signals:
    void started();
    void canceled();

protected:
    virtual bool allowInboundChannelRequest(const Data::Control::OpenChannel *request, Data::Control::ChannelResult *result);
    virtual bool allowOutboundChannelRequest(Data::Control::OpenChannel *request);
    virtual void receivePacket(const QByteArray &packet);

    void handleFileData(const Data::FileTransfer::FileData &message);
    void handleTransferStart(const Data::FileTransfer::TransferStart &message);
    void handleTransferCancel(const Data::FileTransfer::TransferCancel &message);
};

}

#endif
