# esphome modbus TCP server

This project is forked from https://github.com/tube0013/esphome-stream-server-v2 and modified to work as a modbus TCP server.

Serving single or single range input and holding registers is only supported for now.

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
        - lambda: id(tcp).setRegisterSint32(1, 3, 0x0000, 1000, 2500);
```

Interface functions
-------------------

```
void setRegisterUint16(uint8_t unit, uint8_t function, uint16_t address, uint16_t value, uint16_t maxage);
void setRegisterSint32(uint8_t unit, uint8_t function, uint16_t address, int32_t value, uint16_t maxage);
```

*unit* is the device address, generally 1 or 2.

*function* is the modbus function code, 3 for holding registers and 4 for input registers.

*address* is the register address.

*maxage* is the number of milliseconds a register value is valid. You can use zero for infinitely valid.

License
-------

[GNU General Public License version 3](https://github.com/M66B/esphome-stream-server-v2/blob/main/LICENSE.txt)
