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

#include "esphome/core/component.h"
#include "esphome/components/socket/socket.h"

#include <memory>
#include <string>
#include <vector>
#include <map>

class StreamServerComponent : public esphome::Component {
public:
    StreamServerComponent() = default;

    void setup() override;
    void loop() override;
    void dump_config() override;
    void on_shutdown() override;

    float get_setup_priority() const override { return esphome::setup_priority::AFTER_WIFI; }

    void set_port(uint16_t port) { this->port_ = port; }
    int get_client_count() { return this->clients_.size(); }

    void setRegisterUint(uint8_t unit, uint8_t function, uint16_t address, uint16_t value, uint16_t maxage);
    void setRegisterFloat(uint8_t unit, uint8_t function, uint16_t address, float value, uint16_t maxage);

protected:
    void accept();
    void cleanup();
    void read();
    void write();

    int32_t getRegister(uint8_t unit, uint8_t function, uint16_t address, bool main);
    char* getHex(uint8_t *buffer, int len);

    struct Client {
        Client(std::unique_ptr<esphome::socket::Socket> socket, std::string identifier);

        std::unique_ptr<esphome::socket::Socket> socket{nullptr};
        std::string identifier{};
        bool disconnected{false};
        uint8_t offset = 0;
        uint8_t buffer[260]; // Max. modbus message length
    };

    struct UnitFunctionAddress {
        uint8_t unit;
        uint8_t function;
        uint16_t address;

        bool operator==(const UnitFunctionAddress &o) const {
            return (unit == o.unit && function == o.function && address == o.address);
        }

        bool operator<(const UnitFunctionAddress &o)  const {
            return (unit < o.unit || (unit == o.unit && function < o.function) || (unit == o.unit && function == o.function && address < o.address));
        }
    };

    struct ValueAge {
        uint16_t value;
        uint32_t expiration;
    };

    std::unique_ptr<esphome::socket::Socket> socket_{};
    uint16_t port_{502};
    std::vector<Client> clients_{};
    std::map<UnitFunctionAddress, ValueAge> registers_{};
};
