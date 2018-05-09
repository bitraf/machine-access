# Machine access

Device for controlling access to dangerous and expensive machines, like lasercutter and CNC.

The device exposes an MQTT-based protocol for controlling and sending status.
The client that reads/sends the MQTT messages is responsible for providing a user interface,
authentication and checking whether the user is permitted to unlock.
At Bitraf we use [p2k16](https://github.com/bitraf/p2k16) for this.

## Electronics

![Schematics](./doc/schematics.png)

## Firmware

`TODO`

## Protocol

Device subscribes to these MQTT topics:

    $prefix/$device_name/lock        <any string>       # lock machine, can not run
    $prefix/$device_name/unlock      <any string>       # unlock machine, can now run

Device publishes on these MQTT topics:

    $prefix/$device_name/error       "errormessage"     # error occurred for lock/unlock command
    $prefix/$device_name/is_locked   "true" | "false"   # whether machine is locked or not
    $prefix/$device_name/is_running  "true" | "false"   # machine is actively running or not

Example $prefix is `/bitraf/machines` and $device_name `pick_and_place`.
