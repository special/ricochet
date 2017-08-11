/* Ricochet - https://ricochet.im/
 * Copyright (C) 2017, John Brooks <john.brooks@dereferenced.net>
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

#ifndef BACKENDRPC_H
#define BACKENDRPC_H

#include <QObject>
#include <thread>
#include <grpc++/grpc++.h>
#include "rpc/core.pb.h"
#include "rpc/core.grpc.pb.h"

class BackendRPC : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(BackendRPC)

public:
    BackendRPC(QObject *parent = 0);

    bool getIdentity(ricochet::Identity &reply);

public slots:
    bool connect();
    void startMonitorNetwork();
    void stopMonitorNetwork();

    void startMonitorContacts();
    void stopMonitorContacts();

signals:
    void networkStatusChanged(const ricochet::NetworkStatus &status);
    void contactEvent(const ricochet::ContactEvent &event);

private:
    std::unique_ptr<ricochet::RicochetCore::Stub> client;

    std::thread monitorNetworkThread;
    std::unique_ptr<grpc::ClientContext> monitorNetworkCtx;

    std::thread monitorContactsThread;
    std::unique_ptr<grpc::ClientContext> monitorContactsCtx;
};

extern BackendRPC *backend;

#endif
