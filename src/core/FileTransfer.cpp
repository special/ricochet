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

#include "FileTransfer.h"
#include "ContactUser.h"
#include "UserIdentity.h"
#include "tor/HiddenService.h"
#include "utils/StringUtil.h"
#include "utils/Useful.h"
#include "utils/SecureRNG.h"
#include "protocol/FileTransferChannel.h"
#include "protocol/FileTransferDataChannel.h"
#include "protocol/Connection.h"
#include "protocol/OutboundConnector.h"
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QDebug>

class FileTransferPrivate : public QObject
{
    Q_OBJECT

public:
    FileTransfer * const q;

    ContactUser *contact;
    QString fileName;
    QByteArray transferId;
    QIODevice *localDevice;
    FileTransfer::State state;
    quint64 fileSize;
    quint64 transferredSize;
    static const int RateSamplesCount = 10;
    quint64 rateSamples[RateSamplesCount];
    QElapsedTimer rateLastTime;
    QPointer<Protocol::FileTransferChannel> channel;
    bool isOutbound;
    bool wasAbortedLocally;

    Protocol::OutboundConnector *dataConnector;
    QSharedPointer<Protocol::Connection> dataConnection;

    FileTransferPrivate(FileTransfer *qptr, ContactUser *c, bool out)
        : QObject(qptr)
        , q(qptr)
        , contact(c)
        , localDevice(0)
        , state(FileTransfer::Unknown)
        , fileSize(0)
        , transferredSize(0)
        , rateSamples{0}
        , isOutbound(out)
        , wasAbortedLocally(false)
        , dataConnector(0)
    {
        rateLastTime.invalidate();
    }

    virtual ~FileTransferPrivate()
    {
        stopDataConnection();
        delete localDevice;
        if (!channel.isNull() && channel->isOpened())
            channel->closeChannel();
    }

    void setState(FileTransfer::State state);
    void setChannel(Protocol::FileTransferChannel *channel);
    void sendOffer(const QSharedPointer<Protocol::Connection> &connection);
    void setLocalError(const QString &message) { setError(message, true); }
    void setRemoteError(const QString &message) { setError(message, false); }

    void transferStarted();
    void transferFinished();
    void channelInvalidated();

    void startDataConnection();
    void stopDataConnection();
    void dataConnectionReady();
    void dataChannelReady(Protocol::FileTransferDataChannel *channel);
    void dataConnectionClosed();

    void rateAddSample(quint64 value);

    QDebug debugLog() const
    {
        QByteArray shortId = transferId.toHex();
        shortId.truncate(6);
        return qDebug() << (isOutbound ? "Outbound" : "Inbound") << "file" << shortId.constData() << ":";
    }

    static bool isStateFinal(FileTransfer::State state)
    {
        return state == FileTransfer::Finished ||
               state == FileTransfer::Canceled ||
               state == FileTransfer::Error;
    }

    static const char *stateString(FileTransfer::State state)
    {
        switch (state) {
            case FileTransfer::Canceled: return "canceled";
            case FileTransfer::Error: return "error";
            case FileTransfer::Unknown: return "unknown";
            case FileTransfer::Offer: return "offer";
            case FileTransfer::Active: return "active";
            case FileTransfer::Finished: return "finished";
            default: return "???";
        }
    }

private:
    void setError(const QString &message, bool local);
};

FileTransfer::FileTransfer(ContactUser *contact, bool isOutbound)
    : d(new FileTransferPrivate(this, contact, isOutbound))
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

    if (isStateFinal(state)) {
        BUG() << "File transfer moved from final state" << state << "to" << newState;
    }

    if (newState == FileTransfer::Active && channel.isNull()) {
        BUG() << "File transfer moved to active state, but has no attached protocol channel";
    }

    debugLog() << stateString(state) << "->" << stateString(newState);
    state = newState;
    emit q->stateChanged();

    if (isStateFinal(state) && localDevice)
        localDevice->close();

    if (newState != FileTransfer::Offer && newState != FileTransfer::Active) {
        if (!channel.isNull()) {
            // Channel must be closed if we're no longer active
            channel->closeChannel();
        }
        stopDataConnection();
    }
}

void FileTransferPrivate::setError(const QString &message, bool local)
{
    Q_UNUSED(message);

    wasAbortedLocally = local;
    setState(FileTransfer::Error);

    if (channel)
        channel->closeChannel();
}

void FileTransferPrivate::setChannel(Protocol::FileTransferChannel *c)
{
    if (channel == c)
        return;

    if (channel) {
        BUG() << "Replacing existing channel" << channel << "with" << c << "on a FileTransfer";
        disconnect(channel.data(), 0, this, 0);
        channel->closeChannel();
    }

    channel = c;
    connect(channel.data(), &Protocol::FileTransferChannel::started, this, &FileTransferPrivate::transferStarted);
    connect(channel.data(), &Protocol::Channel::invalidated, this, &FileTransferPrivate::channelInvalidated);
    connect(channel.data(), &Protocol::FileTransferChannel::finished, this, &FileTransferPrivate::transferFinished);
}

/* Note that this may be called repeatedly, because it's connected
 * to the contact's connectionChanged signal. Ignore any calls in
 * wrong state. */
void FileTransferPrivate::sendOffer(const QSharedPointer<Protocol::Connection> &connection)
{
    if (!connection || state != FileTransfer::Offer || !isOutbound || channel)
        return;

    auto newChannel = new Protocol::FileTransferChannel(Protocol::Channel::Outbound, connection.data());
    newChannel->setFileName(fileName);
    newChannel->setFileSize(fileSize);
    newChannel->setTransferId(transferId);
    setChannel(newChannel);
    if (!newChannel->openChannel())
        setLocalError(QStringLiteral("Internal error"));
}

/* Called for inbound transfers when we have just asked for the transfer
 * to start (and should establish a data channel/connection), or for outbound
 * transfers when the peer has started (and we should expect a data channel
 * or connection). */
void FileTransferPrivate::transferStarted()
{
    if (state != FileTransfer::Offer && state != FileTransfer::Active) {
        BUG() << "Transfer channel reports started, but transfer state is" << state;
        setLocalError(QStringLiteral("Internal error"));
        return;
    }

    debugLog() << "transfer started";
    setState(FileTransfer::Active);
}

void FileTransferPrivate::channelInvalidated()
{
    debugLog() << "transfer channel invalidated";
    channel.clear();

    if (state == FileTransfer::Active) {
        setRemoteError(QStringLiteral("Channel lost"));
    } else if (state == FileTransfer::Offer) {
        wasAbortedLocally = false;
        setState(FileTransfer::Canceled);
    }
}

void FileTransferPrivate::transferFinished()
{
    debugLog() << "transfer finished";
    if (state != FileTransfer::Active) {
        BUG() << "Transfer channel reports finished, but transfer state is" << state;
        setLocalError(QStringLiteral("Internal error"));
        return;
    }

    if (localDevice)
        localDevice->close();
    setState(FileTransfer::Finished);
    if (channel)
        channel->closeChannel();
}

ContactUser *FileTransfer::contact() const
{
    return d->contact;
}

bool FileTransfer::isOutbound() const
{
    return d->isOutbound;
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

    static const int max = Protocol::FileTransferChannel::FilenameMaxCharacters;

    // Truncate too-long filenames before the extension
    if (fileName.size() > max) {
        QString extension;
        int extp = fileName.lastIndexOf(QLatin1Char('.'));
        if (extp >= 0) {
            extension = fileName.mid(extp);
            extension.truncate(max);
        }
        fileName.truncate(max - extension.size());
        fileName.append(extension);
        Q_ASSERT(fileName.size() <= max);
    }

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

QByteArray FileTransfer::transferId() const
{
    return d->transferId;
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

bool FileTransfer::setLocalFilePath(const QString &filePath)
{
    QFileInfo fi(filePath);
    if (localFilePath() == fi.absoluteFilePath())
        return true;

    QFile *file = new QFile(fi.absoluteFilePath(), this);
    if (!file->open(isOutbound() ? QIODevice::ReadOnly : QIODevice::ReadWrite)) {
        qDebug() << "Failed opening local file" << file->fileName() << "for transfer:" << file->errorString();
        delete file;
        setLocalDevice(0);
        return false;
    }

    setLocalDevice(file);
    return true;
}

QUrl FileTransfer::localFileUrl() const
{
    QString path = localFilePath();
    return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
}

bool FileTransfer::setLocalFileUrl(const QUrl &fileUrl)
{
    if (!fileUrl.isLocalFile()) {
        BUG() << "Cannot set transfer localFileUrl to a non-file URL";
        setLocalDevice(0);
        return false;
    }
    return setLocalFilePath(fileUrl.toLocalFile());
}

bool FileTransfer::hasLocalFile() const
{
    return !localFilePath().isEmpty();
}

FileTransfer::State FileTransfer::state() const
{
    return d->state;
}

bool FileTransfer::isStateFinal() const
{
    return FileTransferPrivate::isStateFinal(d->state);
}

bool FileTransfer::wasAbortedLocally() const
{
    return d->wasAbortedLocally;
}

quint64 FileTransfer::transferredSize() const
{
    return d->transferredSize;
}

quint64 FileTransfer::transferRate() const
{
    if (!d->rateLastTime.isValid())
        return 0;

    /* rateSamples holds one sample per second over RateSamplesCount seconds,
     * with the time of the last sample being held in rateLastTime. Skip the
     * oldest according to the time since the last sample to get the past 10 real
     * seconds of data. If msecsSinceLast is under 1000, ignore the last sample
     * (as it's an incomplete interval).
     */
    qint64 msecsSinceLast = d->rateLastTime.elapsed();
    quint64 re = 0;
    int samples = d->RateSamplesCount - (msecsSinceLast < 1000 ? 1 : 0);
    for (int i = msecsSinceLast / 1000; i < samples; i++)
        re += d->rateSamples[i];
    return re / samples;
}

void FileTransferPrivate::rateAddSample(quint64 value)
{
    qint64 msecsSinceLast = rateLastTime.isValid() ? rateLastTime.elapsed() : 0;
    if (!rateLastTime.isValid() || msecsSinceLast >= 1000)
        rateLastTime.restart();
    int rotate = msecsSinceLast / 1000;
    if (rotate > RateSamplesCount)
        rotate = RateSamplesCount;

    // Move samples forward by 'rotate' positions and fill the end with 0
    for (int i = 0, j = rotate; i != j && j < RateSamplesCount; i++, j++)
        rateSamples[i] = rateSamples[j];
    for (int i = RateSamplesCount - rotate; i < RateSamplesCount; i++)
        rateSamples[i] = 0;

    // Update the last sample
    rateSamples[RateSamplesCount-1] += value;
}

bool FileTransfer::setInboundChannel(Protocol::FileTransferChannel *channel)
{
    if (d->isOutbound) {
        BUG() << "Tried to initialize an offer on an outbound file transfer";
        return false;
    }

    if (d->state != Unknown || !d->channel.isNull()) {
        BUG() << "Tried to set an inbound channel on a file transfer in state" << d->state;
        return false;
    }

    if (!channel || channel->direction() != Protocol::Channel::Inbound || !channel->isOpened()) {
        BUG() << "Tried to initialize an offer with a channel in an invalid state";
        return false;
    }

    setFileName(channel->fileName());
    d->fileSize = channel->fileSize();
    d->transferId = channel->transferId();

    if (d->fileName.isEmpty() || d->fileSize < 1 || d->transferId.isEmpty()) {
        // These should've been filtered out by FileTransferChannel
        BUG() << "Received an inbound file transfer offer without a valid name and size";
        d->setLocalError(QStringLiteral("Invalid file offer"));
        return false;
    }

    d->setChannel(channel);

    d->debugLog() << "Inbound channel offers" << d->fileName << "of" << d->fileSize << "bytes";
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
        if (d->state != Unknown) {
            BUG() << "Tried to start an outbound file transfer in non-Unknown state" << d->state;
            return;
        }

        d->transferId = SecureRNG::random(Protocol::FileTransferChannel::TransferIdSize);
        d->setState(Offer);

        if (d->channel)
            BUG() << "Just started an outbound offer, but somehow it already has a channel";

        if (!contact()) {
            BUG() << "Can't start an outbound file transfer without a contact instance";
            return;
        }

        connect(contact(), &ContactUser::connectionChanged, d, &FileTransferPrivate::sendOffer);
        if (contact()->isConnected())
            d->sendOffer(contact()->connection());
    } else {
        if (d->state != Offer) {
            BUG() << "Tried to start an inbound file transfer in non-Offer state" << d->state;
            return;
        }

        if (d->channel.isNull()) {
            qDebug() << "Tried to start an inbound file transfer with no channel";
            d->setState(Error);
            return;
        }

        d->setState(Active);
        d->channel->start();
    }

    if (!isOutbound())
        d->startDataConnection();
}

void FileTransfer::cancel()
{
    if (d->state == Canceled)
        return;

    d->debugLog() << "Canceling file transfer by local user action";

    if (d->channel)
        d->channel->cancel();

    d->wasAbortedLocally = true;
    d->setState(Canceled);
}

void FileTransferPrivate::startDataConnection()
{
    if (isOutbound) {
        BUG() << "Cannot build a data connection for an outbound transfer";
        return;
    }

    using namespace Protocol;

    if (dataConnector)
        return;

    debugLog() << "Starting outbound data connection";

    dataConnector = new OutboundConnector(this);
    connect(dataConnector, &OutboundConnector::statusChanged, this,
        [this]() {
            if (!dataConnector)
                return;
            qDebug() << "File transfer data connection status:" << dataConnector->status();

            if (dataConnector->status() == OutboundConnector::Ready) {
                if (dataConnection) {
                    BUG() << "Transfer already has a data connection assigned";
                    dataConnector->takeConnection()->close();
                    return;
                }

                debugLog() << "Outbound data connection is ready";
                dataConnection = dataConnector->takeConnection();
                dataConnectionReady();
            } else if (dataConnector->status() == OutboundConnector::Error) {
                debugLog() << "Outbound data connection error:" << dataConnector->errorMessage();
                setLocalError(dataConnector->errorMessage());
            }
        }
    );

    dataConnector->connectToHost(contact->hostname(), contact->port());
}

void FileTransferPrivate::stopDataConnection()
{
    if (dataConnection) {
        debugLog() << "Disconnecting data connection";
        disconnect(dataConnection.data(), &Protocol::Connection::closed, this, &FileTransferPrivate::dataConnectionClosed);
        dataConnection->close();
        dataConnection.clear();
    }

    if (dataConnector) {
        debugLog() << "Aborting outbound data connection attempt";
        disconnect(dataConnector, 0, this, 0);
        dataConnector->abort();
        dataConnector->deleteLater();
        dataConnector = 0;
    }
}

void FileTransferPrivate::dataConnectionClosed()
{
    debugLog() << "Data connection closed while in state" << stateString(state);
    dataConnection.clear();

    if (state == FileTransfer::Active)
        setRemoteError(QStringLiteral("Connection lost"));
}

void FileTransferPrivate::dataConnectionReady()
{
    using namespace Protocol;
    if (isOutbound) {
        BUG() << "Data connection ready on an outbound file transfer (which should have an incoming connection)";
        dataConnection->close();
        return;
    }

    if (!dataConnection->setPurpose(Connection::Purpose::FileTransferData)) {
        setLocalError(QStringLiteral("Internal error"));
        return;
    }
    connect(dataConnection.data(), &Connection::closed, this, &FileTransferPrivate::dataConnectionClosed);

    FileTransferDataChannel *dataChannel = new FileTransferDataChannel(Channel::Outbound, dataConnection.data());
    dataChannel->setTransferId(transferId);
    dataChannel->setLocalDevice(localDevice);
    dataChannel->setMaxDataSize(fileSize);

    connect(dataChannel, &Channel::channelOpened, this,
        [this,dataChannel]() {
            dataChannelReady(dataChannel);
        }
    );

    // XXX close signals, etc
    if (!dataChannel->openChannel()) {
        // XXX error
    }
}

bool FileTransfer::setDataChannel(Protocol::FileTransferDataChannel *channel)
{
    using namespace Protocol;

    if (channel->direction() != Channel::Inbound || !isOutbound()) {
        qDebug() << "Rejecting file transfer data channel for invalid direction";
        return false;
    }

    if (channel->transferId() != transferId()) {
        BUG() << "Called setDataChannel with mismatching transfer id";
        return false;
    }

    if (channel->localDevice()) {
        BUG() << "Called setDataChannel for an already-claimed channel";
        return false;
    }

    if (d->dataConnection) {
        BUG() << "Called setDataChannel with an existing data connection";
        return false;
    }

    // Claim the connection, if it's not already KnownContact
    Connection *conn = channel->connection();
    if (conn->purpose() == Connection::Purpose::Unknown) {
        if (!conn->setPurpose(Connection::Purpose::FileTransferData)) {
            conn->close();
            return false;
        }
        d->dataConnection = contact()->identity->takeIncomingConnection(conn);
        if (!d->dataConnection) {
            BUG() << "Connection with an unknown purpose wasn't available to claim for an inbound file transfer data channel";
            conn->close();
            return false;
        }
        connect(conn, &Connection::closed, d, &FileTransferPrivate::dataConnectionClosed);
        connect(channel, &Channel::invalidated, conn, &Connection::close);
    }

    // XXX sanity checks
    // XXX start position?
    channel->setLocalDevice(d->localDevice);
    channel->setMaxDataSize(d->fileSize);
    // XXX any reason to defer until actually open?
    d->dataChannelReady(channel);
    return true;
}

void FileTransferPrivate::dataChannelReady(Protocol::FileTransferDataChannel *channel)
{
    using namespace Protocol;

    debugLog() << "Data channel ready";
    // XXX sanity check
    connect(channel, &FileTransferDataChannel::dataTransferred, this,
        [this](quint64 bytes) {
            transferredSize += bytes;
            rateAddSample(bytes);
            emit q->transferredSizeChanged();
        }
    );
    // XXX other signals?
}

#include "FileTransfer.moc"
