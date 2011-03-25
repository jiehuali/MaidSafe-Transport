/* Copyright (c) 2010 maidsafe.net limited
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
    * Neither the name of the maidsafe.net limited nor the names of its
    contributors may be used to endorse or promote products derived from this
    software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "maidsafe-dht/transport/udt_acceptor.h"

#include <cassert>

#include "maidsafe-dht/transport/udt_socket.h"
#include "maidsafe-dht/transport/udt_handshake_packet.h"
#include "maidsafe-dht/transport/udt_multiplexer.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/log.h"

namespace asio = boost::asio;
namespace ip = boost::asio::ip;
namespace bs = boost::system;
namespace bptime = boost::posix_time;
namespace arg = std::placeholders;

namespace maidsafe {

namespace transport {

UdtAcceptor::UdtAcceptor(UdtMultiplexer &multiplexer)
  : multiplexer_(multiplexer),
    waiting_accept_(multiplexer.socket_.get_io_service()),
    waiting_accept_socket_(0) {
  waiting_accept_.expires_at(boost::posix_time::pos_infin);
  multiplexer_.dispatcher_.SetAcceptor(this);
}

UdtAcceptor::~UdtAcceptor() {
  if (IsOpen())
    multiplexer_.dispatcher_.SetAcceptor(0);
}

bool UdtAcceptor::IsOpen() const {
  return multiplexer_.dispatcher_.GetAcceptor() == this;
}

void UdtAcceptor::Close() {
  pending_requests_.clear();
  waiting_accept_.cancel();
  if (IsOpen())
    multiplexer_.dispatcher_.SetAcceptor(0);
}

void UdtAcceptor::StartAccept(UdtSocket &socket) {
  assert(waiting_accept_socket_ == 0); // Only one accept operation at a time.

  if (!pending_requests_.empty()) {
    socket.remote_id_ = pending_requests_.front().remote_id;
    socket.remote_endpoint_ = pending_requests_.front().remote_endpoint;
    socket.id_ = multiplexer_.dispatcher_.AddSocket(&socket);
    pending_requests_.pop_front();
    waiting_accept_.cancel();
  } else {
    waiting_accept_socket_ = &socket;
  }
}

void UdtAcceptor::HandleReceiveFrom(const asio::const_buffer &data,
                                    const asio::ip::udp::endpoint &endpoint) {
  UdtHandshakePacket packet;
  if (packet.Decode(data)) {
    if (UdtSocket* socket = waiting_accept_socket_) {
      // A socket is ready and waiting to accept the new connection.
      socket->remote_id_ = packet.SocketId();
      socket->remote_endpoint_ = endpoint;
      socket->id_ = multiplexer_.dispatcher_.AddSocket(socket);
      waiting_accept_socket_ = 0;
      waiting_accept_.cancel();
    } else {
      // There's no socket waiting, queue it for later.
      PendingRequest pending_request;
      pending_request.remote_id = packet.SocketId();
      pending_request.remote_endpoint = endpoint;
      pending_requests_.push_back(pending_request);
    }
  } else {
    DLOG(ERROR) << "Acceptor ignoring invalid packet from " << endpoint << std::endl;
  }
}

}  // namespace transport

}  // namespace maidsafe

