# Machine access

Device for controlling access to dangerous and expensive machines, like lasercutter and CNC.

The device exposes an MQTT-based protocol for controlling and sending status.
The client that reads/sends the MQTT messages is responsible for providing a user interface,
authentication and checking whether the user is permitted to unlock.
At Bitraf we use [p2k16](https://github.com/bitraf/p2k16) for this.

## Electronics

![Schematics](./doc/schematics.png)

## Firmware

[relay](relay)

## Protocol

### Device status
Mandatory

    $prefix/device/$id/online

0 for offline
1 for online

$id is an arbitrary unique identifier. For instance the MAC of device, or an id stored in Flash.

Device must use MQTT last will 0 here.


Optional informational topics

    $prefix/device/$id/firmware
    $prefix/device/$id/ip
    $prefix/device/$id/status
    $prefix/device/$id/name


### Locks

N locks per device.
p2k16 has the mapping between device and contained locks.

Payload format: keyA=valueA\nkeyB=valueB\n

    $prefix/$lockid/command


    request=20400404
    command=unlock|lock

Lock state

    $prefix/$lockid/locked

    state=unlock|lock
    OPTIONAL request=$request-id

Upon successful command, the `locked` state shall be updated.

Error messages

    $prefix/$lockid/error

    message=some-desciptive-text
    OPTIONAL request=$request-id

