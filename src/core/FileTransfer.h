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
    Q_PROPERTY(bool isOutgoing READ isOutgoing CONSTANT)
    Q_PROPERTY(QString fileName READ fileName WRITE setFileName NOTIFY fileNameChanged)
    Q_PROPERTY(qint64 fileSize READ fileSize NOTIFY fileSizeChanged)
    Q_PROPERTY(QUrl localFileUrl READ localFileUrl WRITE setLocalFileUrl NOTIFY localDeviceChanged)
    Q_PROPERTY(bool hasLocalFile READ hasLocalFile NOTIFY localDeviceChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(qint64 transferredSize READ transferredSize NOTIFY transferredSizeChanged)
    Q_PROPERTY(qint64 transferRate READ transferRate)

public:
    enum State
    {
        Cancelled = -2,
        Failed = -1,
        Unknown,
        Offered,
        Connecting,
        Transferring,
        Finished
    };

    explicit FileTransfer(ContactUser *contact, bool isOutgoing, QObject *parent);
    virtual ~FileTransfer();

    ContactUser *contact() const;
    bool isOutgoing() const;

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
    QString errorMessage() const;

    quint64 transferredSize() const;
    quint64 transferRate() const;

    // Protocol API
    void peerFinished();
    void peerCancel();

public slots:
    /* Offer an outgoing transfer to the contact
     *
     * Only valid for outgoing transfers in a state below Connecting.
     * Must have a valid fileName, fileSize, and localDevice.
     */
    void sendOffer();

    /* Cancel the transfer from either end
     *
     * Any ongoing activity will stop and the contact will be told to cancel.
     * A cancelled transfer can be resumed only by the sending peer, by sending a new offer.
     */
    void cancel();

    /* Accept a transfer offer and begin negotiating connection
     *
     * Only valid for incoming transfers in the Offered and Failed states.
     * Must have a valid localDevice.
     */
    void start();

signals:
    void fileNameChanged();
    void fileSizeChanged();
    void localDeviceChanged();
    void stateChanged();
    void errorMessageChanged();
    void transferredSizeChanged();

private:
    FileTransferPrivate *d;

    friend class FileTransferManager;

    void setState(State newState);
};

#endif
