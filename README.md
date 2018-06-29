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

## Protocols

### Device Protocol

This uses the existing specification from Bitraf IoT:
https://bitraf.no/wiki/IoT#Device.

It can be summarized like this:

* Online status is published to `$prefix/device/$device-id/online`.
  Values are ASCII "1" or "0". Used with last will message.
* Logical name is published to `$prefix/device/$device-id/name`. The
  device may subscribe to this topic and update its own name.
* Firmware version info is published to
  `$prefix/device/$device-id/firmware`.
* Current IP is published to `$prefix/device/$device-id/ip`.

### Machine Access Protocol

Each device can have any number of locks, index from 0 to N. At Bitraf
p2k16 has the mapping between device and contained locks.

#### Payload format

The Machine Access Protocol uses a key-value format as a payload:

The format is text oriented. Each key-value pair is newline separated.
The key is separated with a "=" character, the value is the rest of
the line until a newline or end of message.

Example: `keyA=valueA\nkeyB=valueB`

#### Refreshing current state

If a client has been disconnected it can request that all state topics
are refreshed by sending a message with the key `command` and value
`refresh` to `$prefix/machine-access/command`.

Supporting this topic is an optional feature.

#### Lock State

Each lock's state is published under
`$prefix/machine-access/$lock-index/locked`.

The `state` key is REQUIRED and SHOULD have a value of either "locked"
or "unlocked". The `request` key MAY be present. Its value is the
value of the last command's `request` value that changed the lock.

#### Controlling the lock

A lock can be controlled by sending a message to
`$prefix/machine-access/$lock-index/command` with a key `command`
equal to `unlock` or `lock`. The `request` key should be included.
It's value can be anything, but shouldn't be too long.

Example payload:

    request=20400404
    command=unlock|lock

When the lock's state is changed a message MUST be publised to
the `../locked` topic.

#### Status Messages

The lock can send status messages on the generic `../status` topic.
This can be used for for anything, but in particular error messages
and statistics are useful.

Optional keys:

* `message`: human readable message
* `level`: log level. Possible values: debug, info, warning
* `request`: a related request

Example:

    message=Unknown lock id: 14
    OPTIONAL request=$request-id
