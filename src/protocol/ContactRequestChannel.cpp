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

#include "ContactRequestChannel.h"
#include "Channel_p.h"

using namespace Protocol;

ContactRequestChannel::ContactRequestChannel(Direction direction, Connection *connection)
    : Channel(QStringLiteral("im.ricochet.contact.request"), direction, connection)
    , m_responseStatus(Data::ContactRequest::Response::Undefined)
{
}

QString ContactRequestChannel::message() const
{
    return m_message;
}

void ContactRequestChannel::setMessage(const QString &message)
{
    if (direction() != Outbound) {
        BUG() << "Request messages can only be set on outbound messages";
        return;
    }

    // Only valid before channel opened
    if (isOpened() || identifier() >= 0) {
        BUG() << "Request data must be set before opening channel";
        return;
    }

    // XXX length limit & validity
    m_message = message;
}

QString ContactRequestChannel::nickname() const
{
    return m_nickname;
}

void ContactRequestChannel::setNickname(const QString &nickname)
{
    if (direction() != Outbound) {
        BUG() << "Request messages can only be set on outbound messages";
        return;
    }

    if (isOpened() || identifier() >= 0) {
        BUG() << "Request data must be set before opening channel";
        return;
    }

    // XXX length limit & validity
    m_nickname = nickname;
}

bool ContactRequestChannel::allowInboundChannelRequest(const Data::Control::OpenChannel *request, Data::Control::ChannelResult *result)
{
    using namespace Data::ContactRequest;

    // If this connection is already KnownContact, report that the request is accepted
    if (connection()->purpose() == Connection::Purpose::KnownContact) {
        QScopedPointer<Response> response(new Response);
        response->set_status(Response::Accepted);
        result->SetAllocatedExtension(Data::ContactRequest::response, response.take());
        return false;
    }

    // We'll only accept requests on inbound connections with an unknown purpose
    if (connection()->direction() != Connection::ServerSide ||
        connection()->purpose() != Connection::Purpose::Unknown)
    {
        result->set_common_error(Data::Control::ChannelResult::UnauthorizedError);
        result->set_error_message(QStringLiteral("Only a new client may use this channel").toStdString());
        return false;
    }

    // Only allow one ContactRequestChannel
    if (connection()->findChannel<ContactRequestChannel>()) {
        result->set_common_error(Data::Control::ChannelResult::UnauthorizedError);
        result->set_error_message(QStringLiteral("Only one instance of this channel may be created").toStdString());
        return false;
    }

    // Require HiddenServiceAuth
    if (!connection()->hasAuthenticated(Connection::HiddenServiceAuth)) {
        result->set_common_error(Data::Control::ChannelResult::UnauthorizedError);
        result->set_error_message(QStringLiteral("Only authenticated clients may use this channel").toStdString());
        return false;
    }

    if (!request->HasExtension(Data::ContactRequest::contact_request)) {
        result->set_error_message(QStringLiteral("Expected a request object").toStdString());
        return false;
    }

    ContactRequest contactData = request->GetExtension(Data::ContactRequest::contact_request);
    QString nickname = QString::fromStdString(contactData.nickname());
    QString message = QString::fromStdString(contactData.message_text());

    // XXX validate nickname (length, content)
    // XXX validate message

    m_nickname = nickname;
    m_message = message;

    m_responseStatus = Response::Undefined;
    emit requestReceived();
    if (m_responseStatus == Response::Undefined) {
        BUG() << "No response to incoming contact request after requestReceived signal";
        setResponseStatus(Response::Error, QStringLiteral("internal error"));
    }

    // XXX figure out purpose relationship here; I think request is responsible, but we'll see..

    QScopedPointer<Response> response(new Response);
    response->set_status(m_responseStatus);
    if (!m_responseErrorMessage.isEmpty())
        response->set_error_message(m_responseErrorMessage.toStdString());
    result->SetAllocatedExtension(Data::ContactRequest::response, response.take());

    // If the response is final, close the channel immediately once it's fully open
    if (m_responseStatus > Response::Pending)
        connect(this, &Channel::channelOpened, this, &Channel::closeChannel, Qt::QueuedConnection);
    return true;
}

void ContactRequestChannel::setResponseStatus(Status status, const QString &message)
{
    if (m_responseStatus == status)
        return;

    if (direction() != Inbound) {
        BUG() << "Can't set the response on an outbound contact request";
        return;
    }

    using namespace Data::ContactRequest;
    if (m_responseStatus > Response::Pending)
        BUG() << "Response status is already a final state" << m_responseStatus << "but was changed to" << status;

    m_responseStatus = status;
    m_responseErrorMessage = message;

    // If the channel is already open, the response is sent as a separate packet
    if (isOpened()) {
        Response response;
        response.set_status(m_responseStatus);
        if (!m_responseErrorMessage.isEmpty())
            response.set_error_message(m_responseErrorMessage.toStdString());
        sendMessage(response);

        if (m_responseStatus > Response::Pending)
            closeChannel();
    }
}

bool ContactRequestChannel::allowOutboundChannelRequest(Data::Control::OpenChannel *request)
{
    if (connection()->direction() != Connection::ClientSide ||
        connection()->purpose() != Connection::Purpose::OutboundRequest)
    {
        BUG() << "ContactRequestChannel can only be used on OutboundRequest connections. Has purpose"
              << int(connection()->purpose());
        return false;
    }

    if (connection()->findChannel<ContactRequestChannel>()) {
        BUG() << "ContactRequestChannel can only be used once per connection";
        return false;
    }

    QScopedPointer<Data::ContactRequest::ContactRequest> contactData(new Data::ContactRequest::ContactRequest);
    if (!m_nickname.isEmpty())
        contactData->set_nickname(m_nickname.toStdString());
    if (!m_message.isEmpty())
        contactData->set_message_text(m_message.toStdString());

    request->SetAllocatedExtension(Data::ContactRequest::contact_request, contactData.take());
    return true;
}

bool ContactRequestChannel::processChannelOpenResult(const Data::Control::ChannelResult *result)
{
    if (!result->HasExtension(Data::ContactRequest::response)) {
        qDebug() << "Expected a response for the contact request";
        return false;
    }

    Data::ContactRequest::Response response = result->GetExtension(Data::ContactRequest::response);
    return handleResponse(&response);
}

void ContactRequestChannel::receivePacket(const QByteArray &packet)
{
    Data::ContactRequest::Response response;
    if (!response.ParseFromArray(packet.constData(), packet.size())) {
        closeChannel();
        return;
    }

    if (!handleResponse(&response))
        closeChannel();
}

bool ContactRequestChannel::handleResponse(const Data::ContactRequest::Response *response)
{
    // XXX validate status
    if (response->status() == Data::ContactRequest::Response::Undefined) {
        qDebug() << "Got an invalid response (undefined status) to a contact request";
        return false;
    }

    // XXX validate message
    emit requestStatusChanged(response->status(), QString::fromStdString(response->error_message()));

    // XXX If this response is final, the channel has to be closed now
    return true;
}
