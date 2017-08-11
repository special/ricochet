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

class RPCReadStream
{
public:
    std::thread thread;
    grpc::ClientContext ctx;

    RPCReadStream(std::function<void(grpc::ClientContext*)> func)
    {
        thread = std::thread(func, &ctx);
    }

    void stop()
    {
        ctx.TryCancel();
        if (thread.joinable())
            thread.join();
    }
};

BackendRPC *backend = 0;

BackendRPC::BackendRPC(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ricochet::NetworkStatus>("ricochet::NetworkStatus");
    qRegisterMetaType<ricochet::ContactEvent>("ricochet::ContactEvent");
    qRegisterMetaType<ricochet::ConversationEvent>("ricochet::ConversationEvent");
}

BackendRPC::~BackendRPC()
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

bool BackendRPC::sendMessage(ricochet::Message &msg)
{
    ClientContext ctx;
    ricochet::Message reply;
    Status status = client->SendMessage(&ctx, msg, &reply);
    if (!status.ok()) {
        qDebug() << "RPC connection failed:" << QString::fromStdString(status.error_message());
        return false;
    }

    msg = reply;
    return true;
}

bool BackendRPC::addContactRequest(const ricochet::ContactRequest &request, ricochet::Contact &newContact)
{
    ClientContext ctx;
    Status status = client->AddContactRequest(&ctx, request, &newContact);
    if (!status.ok()) {
        qDebug() << "RPC connection failed:" << QString::fromStdString(status.error_message());
        return false;
    }
    return true;
}

void BackendRPC::startMonitorNetwork()
{
    if (monitorNetwork) {
        qDebug() << "Cannot start network monitoring repeatedly";
        return;
    }

    monitorNetwork.reset(new RPCReadStream(
        [this](grpc::ClientContext *ctx)
        {
            MonitorNetworkRequest req;
            std::unique_ptr<ClientReader<NetworkStatus>> reader(client->MonitorNetwork(ctx, req));

            NetworkStatus netStatus;
            while (reader->Read(&netStatus)) {
                emit networkStatusChanged(netStatus);
            }

            Status status = reader->Finish();
            if (!status.ok()) {
                qDebug() << "RPC connection failed:" << QString::fromStdString(status.error_message());
            }
        }
    ));
}

void BackendRPC::stopMonitorNetwork()
{
    if (monitorNetwork)
        monitorNetwork->stop();
}

void BackendRPC::startMonitorContacts()
{
    if (monitorContacts) {
        qDebug() << "Cannot start contacts monitoring repeatedly";
        return;
    }

    monitorContacts.reset(new RPCReadStream(
        [this](grpc::ClientContext *ctx)
        {
            MonitorContactsRequest req;
            std::unique_ptr<ClientReader<ContactEvent>> reader(client->MonitorContacts(ctx, req));

            ContactEvent event;
            while (reader->Read(&event)) {
                emit contactEvent(event);
            }

            Status status = reader->Finish();
            if (!status.ok()) {
                qDebug() << "RPC connection failed:" << QString::fromStdString(status.error_message());
            }
        }
    ));
}

void BackendRPC::stopMonitorContacts()
{
    if (monitorContacts)
        monitorContacts->stop();
}

void BackendRPC::startMonitorConversations()
{
    if (monitorConversations) {
        qDebug() << "Cannot start conversations monitoring repeatedly";
        return;
    }

    monitorConversations.reset(new RPCReadStream(
        [this](grpc::ClientContext *ctx)
        {
            MonitorConversationsRequest req;
            std::unique_ptr<ClientReader<ConversationEvent>> reader(client->MonitorConversations(ctx, req));

            ConversationEvent event;
            while (reader->Read(&event)) {
                emit conversationEvent(event);
            }

            Status status = reader->Finish();
            if (!status.ok()) {
                qDebug() << "RPC connection failed:" << QString::fromStdString(status.error_message());
            }
        }
    ));
}

void BackendRPC::stopMonitorConversations()
{
    if (monitorConversations)
        monitorConversations->stop();
}
