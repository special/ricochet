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

#include "BackendRPC.h"
#include <QDebug>
#include <grpc++/grpc++.h>
#include <thread>
#include "rpc/core.grpc.pb.h"

using namespace grpc;
using namespace ricochet;

BackendRPC *backend = 0;

BackendRPC::BackendRPC(QObject *parent)
    : QObject(parent)
{
}

bool BackendRPC::connect()
{
    client = RicochetCore::NewStub(grpc::CreateChannel("localhost:51515", grpc::InsecureChannelCredentials()));

    ServerStatusRequest req;
    req.set_rpcversion(1);
    ClientContext ctx;
    ServerStatusReply reply;
    Status status = client->GetServerStatus(&ctx, req, &reply);

    if (!status.ok()) {
        qDebug() << "RPC connection failed:" << QString::fromStdString(status.error_message());
        return false;
    }

    qDebug() << "RPC connection successful; server version:" << QString::fromStdString(reply.serverversion());
    return true;
}

bool BackendRPC::getIdentity(ricochet::Identity &reply)
{
    IdentityRequest req;
    ClientContext ctx;
    Status status = client->GetIdentity(&ctx, req, &reply);
    if (!status.ok()) {
        qDebug() << "RPC connection failed:" << QString::fromStdString(status.error_message());
        return false;
    }

    return true;
}

// Start steaming network status events, which will be emitted in networkStatusChanged
void BackendRPC::startMonitorNetwork()
{
    if (monitorNetworkThread.joinable()) {
        qDebug() << "Cannot start network monitoring repeatedly";
        return;
    }

    monitorNetworkCtx.reset(new ClientContext);
    monitorNetworkThread = std::thread(
        [this]() {
            MonitorNetworkRequest req;
            std::unique_ptr<ClientReader<NetworkStatus>> reader(client->MonitorNetwork(monitorNetworkCtx.get(), req));

            NetworkStatus netStatus;
            while (reader->Read(&netStatus)) {
                emit networkStatusChanged(netStatus);
            }

            Status status = reader->Finish();
            if (!status.ok()) {
                qDebug() << "RPC connection failed:" << QString::fromStdString(status.error_message());
            }
        });
}

// Stop stremaing network status events
void BackendRPC::stopMonitorNetwork()
{
    if (monitorNetworkThread.joinable()) {
        monitorNetworkCtx->TryCancel();
        monitorNetworkThread.join();
    }
}
