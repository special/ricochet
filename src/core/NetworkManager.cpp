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

#include "NetworkManager.h"
#include "core/BackendRPC.h"
#include "utils/StringUtil.h"
#include <QCoreApplication>
#include <QDebug>

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ricochet::NetworkStatus>("ricochet::NetworkStatus");
    connect(backend, &BackendRPC::networkStatusChanged, this, &NetworkManager::onNetworkStatusChanged);
    backend->startMonitorNetwork();
}

NetworkManager *NetworkManager::instance()
{
    static NetworkManager *p = 0;
    if (!p)
        p = new NetworkManager(qApp);
    return p;
}

NetworkManager::ControlStatus NetworkManager::controlStatus() const
{
    return static_cast<ControlStatus>(m_status.control().status());
}

QString NetworkManager::controlError() const
{
    return QString::fromStdString(m_status.control().errormessage());
}

NetworkManager::ConnectionStatus NetworkManager::connectionStatus() const
{
    return static_cast<ConnectionStatus>(m_status.connection().status());
}

QVariantMap NetworkManager::bootstrapStatus() const
{
    QVariantMap bootstrap;
    QList<QByteArray> tokens = splitQuotedStrings(QByteArray::fromStdString(m_status.connection().bootstrapprogress()), ' ');
    if (tokens.isEmpty())
        return bootstrap;

    // WARN or NOTICE
    bootstrap[QStringLiteral("severity")] = tokens.value(0);
    for (int i = 1; i < tokens.size(); i++) {
        int equals = tokens[i].indexOf('=');
        QString key = QString::fromLatin1(tokens[i].mid(0, equals));
        QString value;
        if (equals >= 0)
            value = QString::fromLatin1(unquotedString(tokens[i].mid(equals + 1)));
        bootstrap[key.toLower()] = value;
    }

    return bootstrap;
}

QString NetworkManager::torVersion() const
{
    return QString::fromStdString(m_status.control().torversion());
}

void NetworkManager::onNetworkStatusChanged(const ricochet::NetworkStatus &status)
{
    m_status = status;
    qDebug() << "NetworkManager: network status changed: control" << controlStatus() << "connection" << connectionStatus() << "version" << torVersion();
    emit networkStatusChanged();
}
