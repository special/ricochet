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

#ifndef FILETRANSFER_H
#define FILETRANSFER_H

#include <QObject>
#include <QUrl>

class ContactUser;
class QIODevice;

class FileTransferPrivate;
class FileTransfer : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(FileTransfer)
    Q_ENUMS(State)

    Q_PROPERTY(ContactUser* contact READ contact CONSTANT)
    Q_PROPERTY(bool isOutbound READ isOutbound CONSTANT)
    Q_PROPERTY(QString fileName READ fileName WRITE setFileName NOTIFY fileNameChanged)
    Q_PROPERTY(qint64 fileSize READ fileSize NOTIFY fileSizeChanged)
    Q_PROPERTY(QUrl localFileUrl READ localFileUrl WRITE setLocalFileUrl NOTIFY localDeviceChanged)
    Q_PROPERTY(bool hasLocalFile READ hasLocalFile NOTIFY localDeviceChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(qint64 transferredSize READ transferredSize NOTIFY transferredSizeChanged)
    Q_PROPERTY(qint64 transferRate READ transferRate)

public:
    enum State
    {
        Canceled = -2,
        Error = -1,
        Unknown,
        Offer,
        Active,
        Finished
    };

    explicit FileTransfer(ContactUser *contact, bool isOutbound, QObject *parent);
    virtual ~FileTransfer();

    ContactUser *contact() const;
    bool isOutbound() const;

    /* Transmitter-assigned identifier for this transfer
     *
     * This is unique per ContactUser only.
     */
    quint32 identifier() const;

    QString fileName() const;
    void setFileName(const QString &fileName);
    quint64 fileSize() const;
    void setFileSize(quint64 fileSize);

    QIODevice *localDevice() const;
    void setLocalDevice(QIODevice *device);
    QString localFilePath() const;
    void setLocalFilePath(const QString &filePath);
    QUrl localFileUrl() const;
    void setLocalFileUrl(const QUrl &fileUrl);
    bool hasLocalFile() const;

    State state() const;

    quint64 transferredSize() const;
    quint64 transferRate() const;

public slots:
    /* Activate a transfer, either by offering it to the peer or accepting an offer
     *
     * For outbound transfers, this function may be used from any state other than
     * Finished to send an offer to the peer. The transfer will be moved to the Offer
     * state, and automatically to Active if/when the peer accepts.
     *
     * For inbound transfers, this function may be used only in the Offer state.
     *
     * localDevice must be set before calling this function.
     */
    void start();

    /* Initialize an incoming offer
     *
     * This function should be called on a new transfer after initializing all
     * properties for a new incoming offer.
     */
    bool initializeOffer();

    /* Cancel the transfer
     *
     * May be used by either side to cancel a transfer in the Offer, Active, or Error
     * states. The sending peer must explicitly send a new offer for the file to restart
     * the transfer.
     */
    void cancel();

signals:
    void fileNameChanged();
    void fileSizeChanged();
    void localDeviceChanged();
    void stateChanged();
    void transferredSizeChanged();

private:
    FileTransferPrivate *d;

    friend class FileTransferManager;
};

#endif
