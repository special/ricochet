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

#include "ui/MainWindow.h"
#include "core/IdentityManager.h"
#include "tor/TorManager.h"
#include "tor/TorControl.h"
#include "utils/CryptoKey.h"
#include "utils/SecureRNG.h"
#include "utils/Settings.h"
#include <QApplication>
#include <QLibraryInfo>
#include <QSettings>
#include <QTime>
#include <QDir>
#include <QTranslator>
#include <QMessageBox>
#include <QLocale>
#include <QStandardPaths>
#include <openssl/crypto.h>

static bool initSettings(SettingsFile *settings, QString &errorMessage);
static bool importLegacySettings(SettingsFile *settings, const QString &oldPath);
static void initTranslation();

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationVersion(QLatin1String("1.0.2"));
    a.setOrganizationName(QStringLiteral("Ricochet"));
    initTranslation();

    QScopedPointer<SettingsFile> settings(new SettingsFile);
    SettingsObject::setDefaultFile(settings.data());

    {
        QString error;
        if (!initSettings(settings.data(), error)) {
            QMessageBox::critical(0, qApp->translate("Main", "Ricochet Error"), error);
            return 1;
        }
    }

    /* Initialize OpenSSL's allocator */
    CRYPTO_malloc_init();

    /* Seed the OpenSSL RNG */
    if (!SecureRNG::seed())
        qFatal("Failed to initialize RNG");
    qsrand(SecureRNG::randomInt(UINT_MAX));

    /* Tor control manager */
    Tor::TorManager *torManager = Tor::TorManager::instance();
    torManager->setDataDirectory(QFileInfo(settings->filePath()).path() + QStringLiteral("/tor/"));
    torControl = torManager->control();
    torManager->start();

    /* Identities */
    identityManager = new IdentityManager;

    /* Window */
    MainWindow w;

    return a.exec();
}

static QString userConfigPath()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
    QString oldPath = path.replace(QStringLiteral("Torsion"), QStringLiteral("Ricochet"));
    if (QFile::exists(oldPath))
        return oldPath;
    return path;
}

#ifdef Q_OS_MAC
static QString appBundlePath()
{
    QString path = QApplication::applicationDirPath();
    int p = path.lastIndexOf(QLatin1String(".app/"));
    if (p >= 0)
    {
        p = path.lastIndexOf(QLatin1Char('/'), p);
        path = path.left(p+1);
    }

    return path;
}
#endif

static bool initSettings(SettingsFile *settings, QString &errorMessage)
{
    /* If built in portable mode (default), configuration is stored in the 'config'
     * directory next to the binary. If not writable, launching fails.
     *
     * Portable OS X is an exception. In that case, configuration is stored in a
     * 'config.ricochet' folder next to the application bundle, unless the application
     * path contains "/Applications", in which case non-portable mode is used.
     *
     * When not in portable mode, a platform-specific per-user config location is used.
     *
     * This behavior may be overriden by passing a folder path as the first argument.
     */

    QString configPath;
    QStringList args = qApp->arguments();
    if (args.size() > 1) {
        configPath = args[1];
    } else {
#ifndef RICOCHET_NO_PORTABLE
# ifdef Q_OS_MAC
        if (!qApp->applicationDirPath().contains(QStringLiteral("/Applications"))) {
            // Try old configuration path first
            configPath = appBundlePath() + QStringLiteral("config.torsion");
            if (!QFile::exists(configPath))
                configPath = appBundlePath() + QStringLiteral("config.ricochet");
        }
# else
        configPath = qApp->applicationDirPath() + QStringLiteral("/config");
# endif
#endif
        if (configPath.isEmpty())
            configPath = userConfigPath();
    }

    QDir dir(configPath);
    settings->setFilePath(dir.filePath("ricochet.json"));
    if (settings->hasError()) {
        errorMessage = settings->errorMessage();
        return false;
    }

    if (settings->root()->data().isEmpty()) {
        QString filePath = dir.filePath(QStringLiteral("Torsion.ini"));
        if (!QFile::exists(filePath))
            filePath = dir.filePath(QStringLiteral("ricochet.ini"));
        if (QFile::exists(filePath))
            importLegacySettings(settings, filePath);
    }

    QDir::setCurrent(dir.absolutePath());
    return true;
}

static void copyKeys(QSettings &old, SettingsObject *object)
{
    foreach (const QString &key, old.childKeys()) {
        QVariant value = old.value(key);
        if ((QMetaType::Type)value.type() == QMetaType::QDateTime)
            object->write(key, value.toDateTime());
        else if ((QMetaType::Type)value.type() == QMetaType::QByteArray)
            object->write(key, Base64Encode(value.toByteArray()));
        else
            object->write(key, value.toString());
    }
}

static bool importLegacySettings(SettingsFile *settings, const QString &oldPath)
{
    QSettings old(oldPath, QSettings::IniFormat);
    SettingsObject *root = settings->root();
    QVariant value;

    qDebug() << "Importing legacy format settings from" << oldPath;

    if (!(value = old.value("tor/controlIp")).isNull())
        root->write("tor.controlAddress", value.toString());
    if (!(value = old.value("tor/controlPort")).isNull())
        root->write("tor.controlPort", value.toInt());
    if (!(value = old.value("tor/authPassword")).isNull())
        root->write("tor.controlPassword", value.toString());
    if (!(value = old.value("tor/socksIp")).isNull())
        root->write("tor.socksAddress", value.toString());
    if (!(value = old.value("tor/socksPort")).isNull())
        root->write("tor.socksPort", value.toInt());
    if (!(value = old.value("tor/executablePath")).isNull())
        root->write("tor.executablePath", value.toString());
    if (!(value = old.value("core/neverPublishService")).isNull())
        root->write("tor.neverPublishServices", value.toBool());
    if (!(value = old.value("identity/0/dataDirectory")).isNull())
        root->write("identity.dataDirectory", value.toString());
    if (!(value = old.value("identity/0/createNewService")).isNull())
        root->write("identity.initializing", value.toBool());
    if (!(value = old.value("core/listenIp")).isNull())
        root->write("identity.localListenAddress", value.toString());
    if (!(value = old.value("core/listenPort")).isNull())
        root->write("identity.localListenPort", value.toInt());

    {
        old.beginGroup(QStringLiteral("contacts"));
        QStringList ids = old.childGroups();
        foreach (const QString &id, ids) {
            old.beginGroup(id);
            SettingsObject userObject(root, QString("contacts.%1").arg(id));

            copyKeys(old, &userObject);

            if (old.childGroups().contains(QStringLiteral("request"))) {
                old.beginGroup(QStringLiteral("request"));
                QStringList requestKeys = old.childKeys();
                foreach (const QString &key, requestKeys)
                    userObject.write("request." + key, old.value(key).toString());
                old.endGroup();
            }

            old.endGroup();
        }
        old.endGroup();
    }

    {
        old.beginGroup(QStringLiteral("contactRequests"));
        QStringList contacts = old.childGroups();

        foreach (const QString &hostname, contacts) {
            old.beginGroup(hostname);
            SettingsObject requestObject(root, QString("contactRequests.%1").arg(hostname));
            copyKeys(old, &requestObject);
            old.endGroup();
        }

        old.endGroup();
    }

    if (!(value = old.value("core/hostnameBlacklist")).isNull()) {
        QStringList blacklist = value.toStringList();
        root->write("identity.hostnameBlacklist", QJsonArray::fromStringList(blacklist));
    }

    return true;
}

static void initTranslation()
{
    QTranslator *translator = new QTranslator;

    bool ok = false;
    QString appPath = qApp->applicationDirPath();
    QString resPath = QLatin1String(":/lang/");

    QLocale locale = QLocale::system();
    if (!qgetenv("RICOCHET_LOCALE").isEmpty())
        locale = QLocale(QString::fromLatin1(qgetenv("RICOCHET_LOCALE")));

    ok = translator->load(locale, QStringLiteral("ricochet"), QStringLiteral("_"), appPath);
    if (!ok)
        ok = translator->load(locale, QStringLiteral("ricochet"), QStringLiteral("_"), resPath);

    if (ok) {
        qApp->installTranslator(translator);

        QTranslator *qtTranslator = new QTranslator;
        ok = qtTranslator->load(QStringLiteral("qt_") + locale.name(), QLibraryInfo::location(QLibraryInfo::TranslationsPath));
        if (ok)
            qApp->installTranslator(qtTranslator);
        else
            delete qtTranslator;
    } else
        delete translator;
}

