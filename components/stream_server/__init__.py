# Copyright (C) 2021-2022 Oxan van Leeuwen
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT
from esphome.types import ConfigType

AUTO_LOAD = ["socket"]
DEPENDENCIES = ["network"]
MULTI_CONF = True

CONF_MAX_CLIENTS = "max_clients"
CONF_MAX_INACTIVITY = "max_inactivity"
CONF_NO_TCP_DELAY = "no_tcp_delay"

ns = cg.global_ns
StreamServerComponent = ns.class_("StreamServerComponent", cg.Component)


def _consume_stream_server_sockets(config: ConfigType) -> ConfigType:
    from esphome.components import socket

    socket.consume_sockets(config[CONF_MAX_CLIENTS], "stream_server")(config)
    socket.consume_sockets(1, "stream_server", socket.SocketType.TCP_LISTEN)(
        config
    )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(StreamServerComponent),
            cv.Optional(CONF_PORT, default=502): cv.port,
            cv.Optional(CONF_MAX_CLIENTS, default=4): cv.int_range(min=1, max=8),
            cv.Optional(
                CONF_MAX_INACTIVITY, default="5min"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_NO_TCP_DELAY, default=True): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _consume_stream_server_sockets,
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_max_clients(config[CONF_MAX_CLIENTS]))
    cg.add(
        var.set_max_inactivity_time(config[CONF_MAX_INACTIVITY].total_milliseconds)
    )
    cg.add(var.set_no_tcp_delay(config[CONF_NO_TCP_DELAY]))
    await cg.register_component(var, config)
