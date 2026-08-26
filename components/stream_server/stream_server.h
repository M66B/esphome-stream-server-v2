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

#pragma once

#include "esphome/components/socket/socket.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

class StreamServerComponent : public esphome::Component {
 public:
  StreamServerComponent() = default;

  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;

  float get_setup_priority() const override { return esphome::setup_priority::AFTER_WIFI; }

  void set_port(uint16_t port) { this->port_ = port; }
  int get_client_count() const { return static_cast<int>(this->clients_.size()); }
  void set_max_clients(uint8_t max_clients) { this->max_clients_ = max_clients; }
  void set_max_inactivity_time(uint32_t duration) { this->max_inactivity_time_ = duration; }
  void set_no_tcp_delay(bool enabled) { this->no_tcp_delay_ = enabled; }

  void setRegisterUint16(uint8_t unit, uint8_t function, uint16_t address, uint16_t value, uint32_t maxage);
  void setRegisterSint32(uint8_t unit, uint8_t function, uint16_t address, int32_t value, uint32_t maxage);

 protected:
  static constexpr size_t MBAP_HEADER_SIZE = 6;
  static constexpr size_t READ_REQUEST_SIZE = 12;
  static constexpr size_t MAX_REQUEST_SIZE = 260;
  static constexpr uint16_t MAX_REGISTERS_PER_REQUEST = 100;
  static constexpr size_t MAX_RESPONSE_SIZE = 9 + MAX_REGISTERS_PER_REQUEST * 2;

  struct Client {
    Client(std::unique_ptr<esphome::socket::Socket> socket, const char *identifier);

    std::unique_ptr<esphome::socket::Socket> socket;
    std::array<char, esphome::socket::SOCKADDR_STR_LEN> identifier{};
    bool disconnected{false};
    std::array<uint8_t, MAX_REQUEST_SIZE> request{};
    size_t request_offset{0};
    std::array<uint8_t, MAX_RESPONSE_SIZE> response{};
    size_t response_length{0};
    size_t response_offset{0};
    uint32_t last_activity{0};
  };

  struct UnitFunctionAddress {
    uint8_t unit;
    uint8_t function;
    uint16_t address;

    bool operator<(const UnitFunctionAddress &other) const {
      if (this->unit != other.unit)
        return this->unit < other.unit;
      if (this->function != other.function)
        return this->function < other.function;
      return this->address < other.address;
    }
  };

  struct ValueAge {
    uint16_t value;
    uint32_t updated_at;
    uint32_t max_age;
  };

  void accept_client_();
  void service_client_(Client &client);
  bool read_request_(Client &client);
  bool flush_response_(Client &client);
  void process_request_(Client &client);
  void queue_exception_(Client &client, uint8_t unit, uint8_t function, uint8_t exception);
  bool get_register_(uint8_t unit, uint8_t function, uint16_t address, uint16_t &value);
  void cleanup_clients_();
  void socket_setup_failed_(const char *operation);

  std::unique_ptr<esphome::socket::ListenSocket> socket_{};
  uint16_t port_{502};
  uint32_t max_inactivity_time_{5 * 60 * 1000};
  uint8_t max_clients_{4};
  bool no_tcp_delay_{true};
  std::vector<Client> clients_{};
  std::map<UnitFunctionAddress, ValueAge> registers_{};
};
