wifi relay
==========

turn the power on or off for a machine.

- https://bitraf.no/wiki/Verkt%C3%B8yl%C3%A5s

to do
-----

- [x] open / close relay on lock / unlock messages
- [x] publish lock / unlock status
- [ ] protect mqtt topics
- [ ] enable over-the-air firmware updates
- [ ] measure and report whether the machine is running

example commands
----------------

subscribe to changes:
```
mosquitto_sub -h mqtt.bitraf.no -v -t "bitraf/machineaccess/pick_and_place/is_locked"
```

lock pick and place machine:
```
mosquitto_pub -h mqtt.bitraf.no -m "" -t "bitraf/machineaccess/pick_and_place/lock"
```

unlock pick and place machine:
```
mosquitto_pub -h mqtt.bitraf.no -m "" -t "bitraf/machineaccess/pick_and_place/unlock"
```

dependencies
------------

- ESP8266WiFi (included with ESP8266 "board"): https://github.com/esp8266/Arduino
- PubSubClient (included): https://github.com/knolleary/pubsubclient
