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

#include "TorManager.h"
#include "TorProcess.h"
#include "TorControl.h"
#include "GetConfCommand.h"
#include "utils/Settings.h"
#include "utils/SecureRNG.h"
#include <QFile>
#include <QDir>
#include <QCoreApplication>

using namespace Tor;

namespace Tor
{

class TorManagerPrivate : public QObject
{
    Q_OBJECT

public:
    TorManager *q;
    TorProcess *process;
    TorControl *control;
    QString dataDir;
    QStringList logMessages;
    QString errorMessage;
    bool configNeeded;

    explicit TorManagerPrivate(TorManager *parent = 0);

    QString torExecutablePath() const;
    bool createDataDir(const QString &path);
    bool createDefaultTorrc(const QString &path);

    void setError(const QString &errorMessage);

    bool isSystemTorConfigured() const;

public slots:
    void processStateChanged(int state);
    void processErrorChanged(const QString &errorMessage);
    void processLogMessage(const QString &message);
    void controlStatusChanged(int status);
    void getConfFinished();
};

}

TorManager::TorManager(QObject *parent)
    : QObject(parent), d(new TorManagerPrivate(this))
{
}

TorManagerPrivate::TorManagerPrivate(TorManager *parent)
    : QObject(parent)
    , q(parent)
    , process(0)
    , control(new TorControl(this))
    , configNeeded(false)
{
    connect(control, SIGNAL(statusChanged(int,int)), SLOT(controlStatusChanged(int)));
}

TorManager *TorManager::instance()
{
    static TorManager *p = 0;
    if (!p)
        p = new TorManager(qApp);
    return p;
}

TorControl *TorManager::control()
{
    return d->control;
}

TorProcess *TorManager::process()
{
    return d->process;
}

QString TorManager::dataDirectory() const
{
    return d->dataDir;
}

void TorManager::setDataDirectory(const QString &path)
{
    d->dataDir = QDir::fromNativeSeparators(path);
    if (!d->dataDir.isEmpty() && !d->dataDir.endsWith(QLatin1Char('/')))
        d->dataDir.append(QLatin1Char('/'));
}

QString TorManager::unixSocketPath() const
{
#if defined(Q_OS_UNIX)
    /* For bundled Tor, use the configuration path. This function may
     * be called before Tor is started, so we can't check whether the
     * TorProcess exists for this. */
    if (!d->isSystemTorConfigured()) {
        QString path = QFileInfo(SettingsObject::defaultFile()->filePath()).absolutePath();
        /* Tor cannot handle unix socket paths containing spaces; see tor #18753. */
        if (path.contains(QLatin1Char(' '))) {
            qWarning() << "Using alternative path for unix sockets because bundled path contains spaces";
        } else {
            return path;
        }
    }

    /* XXX As an improvement, we could use the runtime path if the
     * tor process is running as the same user, but this would require
     * querying the uid from the control port and ensuring that this
     * function is not used too early. */

    /* To work around tor having PrivateTmp on debian, prefer using
     * /dev/shm/ for sockets if available and writable */
    QFileInfo shm(QStringLiteral("/dev/shm"));
    if (shm.exists() && shm.isWritable() && shm.isDir())
        return shm.filePath();

    /* Use the temporary files directory */
    return QDir::tempPath();
#else
    BUG() << "Local sockets are not supported on this platform";
    return QString();
#endif
}

QString TorManager::unixSocketPath(const QString &baseName) const
{
    return QStringLiteral("%1/%2_%3")
        .arg(unixSocketPath())
        .arg(baseName)
        .arg(QString::fromLatin1(SecureRNG::randomPrintable(6)));
}

bool TorManager::configurationNeeded() const
{
    return d->configNeeded;
}

QStringList TorManager::logMessages() const
{
    return d->logMessages;
}

bool TorManager::hasError() const
{
    return !d->errorMessage.isEmpty();
}

QString TorManager::errorMessage() const
{
    return d->errorMessage;
}

bool TorManagerPrivate::isSystemTorConfigured() const
{
    SettingsObject settings(QStringLiteral("tor"));
    return !settings.read("controlPort").isUndefined() ||
           !settings.read("controlSocket").isUndefined() ||
           !qEnvironmentVariableIsEmpty("TOR_CONTROL_PORT") ||
           !qEnvironmentVariableIsEmpty("TOR_CONTROL_SOCKET");
}

void TorManager::start()
{
    if (!d->errorMessage.isEmpty()) {
        d->errorMessage.clear();
        emit errorChanged();
    }

    SettingsObject settings(QStringLiteral("tor"));

    // If a control port is defined by config or environment, skip launching tor
    if (d->isSystemTorConfigured()) {
        QHostAddress address(settings.read("controlAddress").toString());
        quint16 port = (quint16)settings.read("controlPort").toInt();
        QByteArray password = settings.read("controlPassword").toString().toLatin1();
        QString socketPath = settings.read("controlSocket").toString();

        if (!qEnvironmentVariableIsEmpty("TOR_CONTROL_HOST"))
            address = QHostAddress(QString::fromLatin1(qgetenv("TOR_CONTROL_HOST")));

        if (!qEnvironmentVariableIsEmpty("TOR_CONTROL_PORT")) {
            bool ok = false;
            port = qgetenv("TOR_CONTROL_PORT").toUShort(&ok);
            if (!ok)
                port = 0;
        }

        if (!qEnvironmentVariableIsEmpty("TOR_CONTROL_SOCKET"))
            socketPath = QString::fromLatin1(qgetenv("TOR_CONTROL_SOCKET"));

        if (!qEnvironmentVariableIsEmpty("TOR_CONTROL_PASSWD"))
            password = qgetenv("TOR_CONTROL_PASSWD");

        if (!port && socketPath.isEmpty()) {
            d->setError(QStringLiteral("Invalid control port settings from environment or configuration"));
            return;
        }

        if (address.isNull())
            address = QHostAddress::LocalHost;

        d->control->setAuthPassword(password);

        if (!socketPath.isEmpty())
            d->control->connect(socketPath);
        else
            d->control->connect(address, port);
    } else {
        // Launch a bundled Tor instance
        QString executable = d->torExecutablePath();
        if (executable.isEmpty()) {
            d->setError(QStringLiteral("Cannot find tor executable"));
            return;
        }

        if (!d->process) {
            d->process = new TorProcess(this);
            connect(d->process, SIGNAL(stateChanged(int)), d, SLOT(processStateChanged(int)));
            connect(d->process, SIGNAL(errorMessageChanged(QString)), d,
                    SLOT(processErrorChanged(QString)));
            connect(d->process, SIGNAL(logMessage(QString)), d, SLOT(processLogMessage(QString)));
        }

        if (!QFile::exists(d->dataDir) && !d->createDataDir(d->dataDir)) {
            d->setError(QStringLiteral("Cannot write data location: %1").arg(d->dataDir));
            return;
        }

        QString defaultTorrc = d->dataDir + QStringLiteral("default_torrc");
        if (!QFile::exists(defaultTorrc) && !d->createDefaultTorrc(defaultTorrc)) {
            d->setError(QStringLiteral("Cannot write data files: %1").arg(defaultTorrc));
            return;
        }

        QFile torrc(d->dataDir + QStringLiteral("torrc"));
        if (!torrc.exists() || torrc.size() == 0) {
            d->configNeeded = true;
            emit configurationNeededChanged();
        }

        d->process->setExecutable(executable);
        d->process->setDataDir(d->dataDir);
        d->process->setDefaultTorrc(defaultTorrc);
        d->process->start();
    }
}

void TorManagerPrivate::processStateChanged(int state)
{
    qDebug() << Q_FUNC_INFO << state << TorProcess::Ready << process->controlPassword() << process->controlHost() << process->controlPort() << process->controlSocketPath();
    if (state == TorProcess::Ready) {
        control->setAuthPassword(process->controlPassword());
        if (!process->controlSocketPath().isEmpty())
            control->connect(process->controlSocketPath());
        else
            control->connect(process->controlHost(), process->controlPort());
    }
}

void TorManagerPrivate::processErrorChanged(const QString &errorMessage)
{
    qDebug() << "tor error:" << errorMessage;
    setError(errorMessage);
}

void TorManagerPrivate::processLogMessage(const QString &message)
{
    qDebug() << "tor:" << message;
    if (logMessages.size() >= 50)
        logMessages.takeFirst();
    logMessages.append(message);
}

void TorManagerPrivate::controlStatusChanged(int status)
{
    if (status == TorControl::Connected) {
        if (!configNeeded) {
            // If DisableNetwork is 1, trigger configurationNeeded
            connect(control->getConfiguration(QStringLiteral("DisableNetwork")),
                    SIGNAL(finished()), SLOT(getConfFinished()));
        }

        if (process) {
            // Take ownership via this control socket
            control->takeOwnership();
        }
    }
}

void TorManagerPrivate::getConfFinished()
{
    GetConfCommand *command = qobject_cast<GetConfCommand*>(sender());
    if (!command)
        return;

    if (command->get("DisableNetwork").toInt() == 1 && !configNeeded) {
        configNeeded = true;
        emit q->configurationNeededChanged();
    }
}

QString TorManagerPrivate::torExecutablePath() const
{
    SettingsObject settings(QStringLiteral("tor"));
    QString path = settings.read("executablePath").toString();
    if (!path.isEmpty())
        return path;

#ifdef Q_OS_WIN
    QString filename(QStringLiteral("/tor.exe"));
#else
    QString filename(QStringLiteral("/tor"));
#endif

    path = qApp->applicationDirPath();
    if (QFile::exists(path + filename))
        return path + filename;

#ifdef BUNDLED_TOR_PATH
    path = QStringLiteral(BUNDLED_TOR_PATH);
    if (QFile::exists(path + filename))
        return path + filename;
#endif

    // Try $PATH
    return filename.mid(1);
}

bool TorManagerPrivate::createDataDir(const QString &path)
{
    QDir dir(path);
    return dir.mkpath(QStringLiteral("."));
}

bool TorManagerPrivate::createDefaultTorrc(const QString &path)
{
    static const char defaultTorrcContent[] =
        /* Once it's available, we want to use OnionTrafficOnly for this
         * port. Right now, tor can only do NoIPv4Traffic or NoIPv6Traffic,
         * but not both, and still won't block DNS. See tor #18693. */
        "SocksPort auto NoIPv4Traffic IPv6Traffic\n"
        "AvoidDiskWrites 1\n"
        "DisableNetwork 1\n"
        "__ReloadTorrcOnSIGHUP 0\n";

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(defaultTorrcContent) < 0)
        return false;
    return true;
}

void TorManagerPrivate::setError(const QString &message)
{
    errorMessage = message;
    emit q->errorChanged();
}

#include "TorManager.moc"

