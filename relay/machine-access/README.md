Alternate software for relay boards that uses Espressif's new SDK.

* Make sure `xtensa-lx106-elf-gcc` is available on your PATH.
* The Wi-Fi settings in Kconfig is set to the default SDK defaults so
  you probably want to do this when building:

    ```
    export WIFI_SSID=foo WIFI_PASSWORD=bar
    ```

  `MQTT_HOST` and `MQTT_PORT` can also be overridden.

  This is done so we don't have to store credentials in Git.

Building:

* Make sure you have updated the submodules: `git submodule update --init`
* Set configuration overrides
* Build it: `make`

To upload run `make flash` (after the first upload you can use the
slightly quicker `make app-flash`). If something becomes messed up,
you can erase the entire flash with `make erase_flash`.

While developing you can use `make monitor` and run `make app-flash`
with `C-t a`. `C-t r` resets the board.
