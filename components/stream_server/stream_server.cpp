/* Copyright (C) 2020-2022 Oxan van Leeuwen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "stream_server.h"

#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

static const char *const TAG = "streamserver";

using namespace esphome;

void StreamServerComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up stream server...");

  // Use ESPHome's platform-neutral listening socket. It selects IPv4 or IPv6
  // for the current build and returns the dedicated raw-lwIP listener type
  // where that implementation requires one.
  this->socket_ = socket::socket_ip_loop_monitored(SOCK_STREAM, 0);
  if (!this->socket_) {
    this->socket_setup_failed_("creation");
    return;
  }

  int enable = 1;
  if (this->socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) != 0)
    ESP_LOGW(TAG, "SO_REUSEADDR failed: errno %d (%s)", errno, strerror(errno));

  if (this->socket_->setblocking(false) != 0) {
    this->socket_setup_failed_("nonblocking");
    return;
  }

  this->clients_.reserve(this->max_clients_);

  struct sockaddr_storage bind_address{};
  socklen_t bind_length = socket::set_sockaddr_any(reinterpret_cast<struct sockaddr *>(&bind_address),
                                                   sizeof(bind_address), this->port_);
  if (bind_length == 0) {
    this->socket_setup_failed_("set address");
    return;
  }

  if (this->socket_->bind(reinterpret_cast<struct sockaddr *>(&bind_address), bind_length) != 0) {
    this->socket_setup_failed_("bind");
    return;
  }

  if (this->socket_->listen(this->max_clients_) != 0) {
    this->socket_setup_failed_("listen");
    return;
  }
}

void StreamServerComponent::loop() {
  if (this->is_failed())
    return;

  this->accept_client_();
  for (Client &client : this->clients_) {
    if (!client.disconnected)
      this->service_client_(client);
  }
  this->cleanup_clients_();
}

void StreamServerComponent::accept_client_() {
  struct sockaddr_storage client_address{};
  socklen_t client_address_length = sizeof(client_address);
  std::unique_ptr<socket::Socket> client_socket =
      this->socket_->accept(reinterpret_cast<struct sockaddr *>(&client_address), &client_address_length);

  if (!client_socket) {
    if (errno != EAGAIN && errno != EWOULDBLOCK)
      ESP_LOGW(TAG, "Accept failed: errno %d (%s)", errno, strerror(errno));
    return;
  }

  char peer[esphome::socket::SOCKADDR_STR_LEN]{};
  client_socket->getpeername_to(peer);

  if (this->clients_.size() >= this->max_clients_) {
    ESP_LOGW(TAG, "Rejecting client %s: maximum of %u clients reached", peer, this->max_clients_);
    client_socket->shutdown(SHUT_RDWR);
    return;
  }

  if (client_socket->setblocking(false) != 0) {
    ESP_LOGW(TAG, "Could not make client %s non-blocking: errno %d (%s)", peer, errno, strerror(errno));
    client_socket->shutdown(SHUT_RDWR);
    return;
  }

  int no_delay = this->no_tcp_delay_ ? 1 : 0;
  if (client_socket->setsockopt(IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)) != 0)
    ESP_LOGW(TAG, "TCP_NODELAY for client %s failed: errno %d (%s)", peer, errno, strerror(errno));

  this->clients_.emplace_back(std::move(client_socket), peer);
  ESP_LOGI(TAG, "New client #%d connected from %s", this->get_client_count(), peer);
}

void StreamServerComponent::service_client_(Client &client) {
  // A previous non-blocking write may still have bytes pending. Do not accept
  // another request from this client until its response has been completed.
  if (!this->flush_response_(client) || client.disconnected)
    return;

  if (!this->read_request_(client) || client.disconnected)
    return;

  ESP_LOGD(TAG, "Received %u bytes from %s: %s", static_cast<unsigned>(client.request_offset),
           client.identifier.data(), format_hex(client.request.data(), client.request_offset).c_str());

  this->process_request_(client);
  client.request_offset = 0;
  this->flush_response_(client);
}

bool StreamServerComponent::read_request_(Client &client) {
  size_t target_length = MBAP_HEADER_SIZE;

  while (true) {
    if (client.request_offset >= MBAP_HEADER_SIZE) {
      const uint16_t message_length =
          (static_cast<uint16_t>(client.request[4]) << 8) | client.request[5];
      target_length = MBAP_HEADER_SIZE + message_length;
      if (target_length > client.request.size()) {
        ESP_LOGW(TAG, "Client %s sent an oversized frame (%u bytes)", client.identifier.data(),
                 static_cast<unsigned>(target_length));
        client.disconnected = true;
        return false;
      }
    }

    if (client.request_offset >= target_length)
      return true;

    const ssize_t received = client.socket->read(client.request.data() + client.request_offset,
                                                 target_length - client.request_offset);
    if (received > 0) {
      client.request_offset += static_cast<size_t>(received);
      client.last_activity = esphome::millis();
      App.feed_wdt();
      continue;
    }

    if (received == 0) {
      ESP_LOGI(TAG, "Client %s closed the connection", client.identifier.data());
      client.disconnected = true;
      return false;
    }

    const int error = errno;
    if (error == EAGAIN || error == EWOULDBLOCK)
      return false;

    ESP_LOGW(TAG, "Read from client %s failed: errno %d (%s)", client.identifier.data(), error,
             strerror(error));
    client.disconnected = true;
    return false;
  }
}

bool StreamServerComponent::flush_response_(Client &client) {
  while (client.response_offset < client.response_length) {
    const ssize_t sent = client.socket->write(client.response.data() + client.response_offset,
                                              client.response_length - client.response_offset);
    if (sent > 0) {
      client.response_offset += static_cast<size_t>(sent);
      App.feed_wdt();
      continue;
    }

    if (sent == 0) {
      ESP_LOGW(TAG, "Write to client %s made no progress", client.identifier.data());
      client.disconnected = true;
      return false;
    }

    const int error = errno;
    if (error == EAGAIN || error == EWOULDBLOCK)
      return false;

    ESP_LOGW(TAG, "Write to client %s failed: errno %d (%s)", client.identifier.data(), error,
             strerror(error));
    client.disconnected = true;
    return false;
  }

  client.response_length = 0;
  client.response_offset = 0;
  return true;
}

void StreamServerComponent::process_request_(Client &client) {
  if (client.request_offset != READ_REQUEST_SIZE) {
    ESP_LOGW(TAG, "Client %s sent an unsupported request length (%u bytes)", client.identifier.data(),
             static_cast<unsigned>(client.request_offset));
    client.disconnected = true;
    return;
  }

  const uint16_t protocol =
      (static_cast<uint16_t>(client.request[2]) << 8) | client.request[3];
  const uint16_t message_length =
      (static_cast<uint16_t>(client.request[4]) << 8) | client.request[5];
  const uint8_t unit = client.request[6];
  const uint8_t function = client.request[7];
  const uint16_t address =
      (static_cast<uint16_t>(client.request[8]) << 8) | client.request[9];
  const uint16_t count =
      (static_cast<uint16_t>(client.request[10]) << 8) | client.request[11];

  ESP_LOGD(TAG, "Transaction %u protocol %u length %u unit %u function %u address %04x count %u",
           (static_cast<uint16_t>(client.request[0]) << 8) | client.request[1], protocol, message_length, unit,
           function, address, count);

  // A non-zero protocol ID means the MBAP header itself is invalid; there is
  // no valid Modbus transaction to which an exception can be attached.
  if (protocol != 0) {
    ESP_LOGW(TAG, "Client %s used unsupported protocol ID %u", client.identifier.data(), protocol);
    client.disconnected = true;
    return;
  }

  if (function != 3 && function != 4) {
    this->queue_exception_(client, unit, function, 0x01);  // Illegal Function
    return;
  }

  const uint32_t range_end = static_cast<uint32_t>(address) + count;
  if (message_length != 6 || count == 0 || count > MAX_REGISTERS_PER_REQUEST || range_end > 0x10000UL) {
    this->queue_exception_(client, unit, function, 0x03);  // Illegal Data Value
    return;
  }

  client.response[0] = client.request[0];
  client.response[1] = client.request[1];
  client.response[2] = 0;
  client.response[3] = 0;
  const size_t data_length = static_cast<size_t>(count) * 2;
  const uint16_t response_mbap_length = static_cast<uint16_t>(3 + data_length);
  client.response[4] = static_cast<uint8_t>(response_mbap_length >> 8);
  client.response[5] = static_cast<uint8_t>(response_mbap_length & 0xFF);
  client.response[6] = unit;
  client.response[7] = function;
  client.response[8] = static_cast<uint8_t>(data_length);

  for (uint16_t index = 0; index < count; index++) {
    uint16_t value;
    const uint16_t register_address = static_cast<uint16_t>(address + index);
    if (!this->get_register_(unit, function, register_address, value)) {
      this->queue_exception_(client, unit, function, 0x02);  // Illegal Data Address
      return;
    }
    const size_t response_index = 9 + static_cast<size_t>(index) * 2;
    client.response[response_index] = static_cast<uint8_t>(value >> 8);
    client.response[response_index + 1] = static_cast<uint8_t>(value & 0xFF);
  }

  client.response_length = 9 + data_length;
  client.response_offset = 0;
  ESP_LOGD(TAG, "Queued response for %s: %s", client.identifier.data(),
           format_hex(client.response.data(), client.response_length).c_str());
}

void StreamServerComponent::queue_exception_(Client &client, uint8_t unit, uint8_t function, uint8_t exception) {
  client.response[0] = client.request[0];
  client.response[1] = client.request[1];
  client.response[2] = 0;
  client.response[3] = 0;
  client.response[4] = 0;
  client.response[5] = 3;
  client.response[6] = unit;
  client.response[7] = function | 0x80;
  client.response[8] = exception;
  client.response_length = 9;
  client.response_offset = 0;

  ESP_LOGW(TAG, "Queued Modbus exception %u for client %s: %s", exception, client.identifier.data(),
           format_hex(client.response.data(), client.response_length).c_str());
}

void StreamServerComponent::cleanup_clients_() {
  const uint32_t now = esphome::millis();
  for (Client &client : this->clients_) {
    if (!client.disconnected && this->max_inactivity_time_ != 0 &&
        now - client.last_activity >= this->max_inactivity_time_) {
      client.disconnected = true;
      ESP_LOGW(TAG, "Client %s inactive for %u s", client.identifier.data(),
               static_cast<unsigned>((now - client.last_activity) / 1000));
    }
  }

  const size_t previous_count = this->clients_.size();
  this->clients_.erase(
      std::remove_if(this->clients_.begin(), this->clients_.end(),
                     [](const Client &client) { return client.disconnected; }),
      this->clients_.end());

  if (previous_count != this->clients_.size())
    ESP_LOGI(TAG, "%d clients connected", this->get_client_count());
}

void StreamServerComponent::setRegisterUint16(uint8_t unit, uint8_t function, uint16_t address,
                                              uint16_t value, uint32_t maxage) {
  if (function != 3 && function != 4) {
    ESP_LOGW(TAG, "Ignoring register %04x with unsupported function %u", address, function);
    return;
  }
  this->registers_[{unit, function, address}] = {value, esphome::millis(), maxage};
}

void StreamServerComponent::setRegisterSint32(uint8_t unit, uint8_t function, uint16_t address,
                                              int32_t value, uint32_t maxage) {
  if (function != 3 && function != 4) {
    ESP_LOGW(TAG, "Ignoring register %04x with unsupported function %u", address, function);
    return;
  }
  if (address == 0xFFFF) {
    ESP_LOGW(TAG, "Ignoring 32-bit register at %04x: address + 1 would overflow", address);
    return;
  }

  const uint32_t raw_value = static_cast<uint32_t>(value);
  const uint32_t now = esphome::millis();

  // Preserve the original component's low-word-first register ordering.
  this->registers_[{unit, function, address}] = {
      static_cast<uint16_t>(raw_value & 0xFFFF), now, maxage};
  this->registers_[{unit, function, static_cast<uint16_t>(address + 1)}] = {
      static_cast<uint16_t>(raw_value >> 16), now, maxage};
}

bool StreamServerComponent::get_register_(uint8_t unit, uint8_t function, uint16_t address,
                                          uint16_t &value) {
  const auto reg = this->registers_.find({unit, function, address});
  if (reg == this->registers_.end()) {
    ESP_LOGW(TAG, "Address %04x not available for unit %u/function %u", address, unit, function);
    return false;
  }

  const uint32_t now = esphome::millis();
  if (reg->second.max_age != 0 && now - reg->second.updated_at >= reg->second.max_age) {
    ESP_LOGW(TAG, "Value at address %04x expired %u ms ago", address,
             static_cast<unsigned>(now - reg->second.updated_at - reg->second.max_age));
    return false;
  }

  value = reg->second.value;
  return true;
}

void StreamServerComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Stream Server:");
  ESP_LOGCONFIG(TAG, "  Port: %u", this->port_);
  ESP_LOGCONFIG(TAG, "  Maximum clients: %u", this->max_clients_);
  ESP_LOGCONFIG(TAG, "  Maximum inactivity: %u ms", static_cast<unsigned>(this->max_inactivity_time_));
  ESP_LOGCONFIG(TAG, "  TCP no-delay: %s", YESNO(this->no_tcp_delay_));

  bool address_found = false;
  for (const auto &ip : network::get_ip_addresses()) {
    if (!ip.is_set())
      continue;
    char address[network::IP_ADDRESS_BUFFER_SIZE];
    ip.str_to(address);
    ESP_LOGCONFIG(TAG, "  Address: %s:%u", address, this->port_);
    address_found = true;
  }
  if (!address_found)
    ESP_LOGCONFIG(TAG, "  Address: not assigned");
}

void StreamServerComponent::on_shutdown() {
  for (Client &client : this->clients_)
    client.socket->shutdown(SHUT_RDWR);
  this->clients_.clear();

  if (this->socket_) {
    this->socket_->close();
    this->socket_.reset();
  }
}

void StreamServerComponent::socket_setup_failed_(const char *operation) {
  ESP_LOGE(TAG, "Socket %s failed: errno %d (%s)", operation, errno, strerror(errno));
  static_cast<void>(operation);
  if (this->socket_) {
    this->socket_->close();
    this->socket_.reset();
  }
  this->mark_failed();
}

StreamServerComponent::Client::Client(std::unique_ptr<esphome::socket::Socket> socket,
                                      const char *identifier)
    : socket(std::move(socket)), last_activity(esphome::millis()) {
  snprintf(this->identifier.data(), this->identifier.size(), "%s", identifier);
}
