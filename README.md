# esphome modbus TCP server

This project is forked from https://github.com/tube0013/esphome-stream-server-v2 and modified to work as a modbus TCP server.

Serving input and holding registers is supported only.

Basic configuration
-------------------


```
external_components:
  - source: github://m66b/esphome-stream-server-v2

stream_server:
  id: tcp
  port: 502

sensor:
  - platform: ...
    ...
    on_value:
      then:
        - lambda: id(tcp).setValueFloat(1, 3, 0x0000, 1000, 2500);
```

Interface functions
-------------------

```
void setValueUint(uint8_t unit, uint8_t function, uint16_t address, uint16_t value, uint16_t maxage);
void setValueFloat(uint8_t unit, uint8_t function, uint16_t address, float value, uint16_t maxage);
```

*maxage* is the number of milliseconds a register value is valid. You can use zero for infinitely valid.

license
-------

[GNU General Public License version 3](https://github.com/M66B/esphome-stream-server-v2/blob/main/LICENSE.txt)
