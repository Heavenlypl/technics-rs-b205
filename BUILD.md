# Building firmware v26

This sketch targets an ESP32-S3 with 16 MB flash and 8 MB octal PSRAM. Pin assignments are in [board_pins.h](kaseta_radio_s3/board_pins.h) and [magnet_buttons.h](kaseta_radio_s3/magnet_buttons.h). Check them against your hardware.

The build was prepared for Arduino ESP32 core 3.3.11, ESP8266Audio 2.4.1, PubSubClient 2.8, Adafruit GFX Library 1.12.6, and Adafruit BusIO 1.17.4. Install the core and libraries using Arduino IDE or Arduino CLI.

## Configure Wi-Fi and MQTT

In the repository root, run:

```sh
cp kaseta_radio_s3/network_config.example.h kaseta_radio_s3/network_private.h
cp kaseta_radio_s3/mqtt_config.example.h kaseta_radio_s3/mqtt_private.h
```

Edit the private files locally. Set your Wi-Fi credentials and enter your broker settings. Set `MAGNET_MQTT_ENABLED` to `true` when the broker is configured. Keep these private headers out of version control.

## Generate the cassette label font

The sketch uses a bitmap glyph atlas generated on macOS from Bradley Hand Bold. The atlas is built locally. Install the macOS Command Line Tools and make the typeface available in Font Book. From the repository root:

```sh
font_build=$(mktemp -d)
clang -fobjc-arc -framework Foundation -framework CoreText -framework CoreGraphics \
  kaseta_radio_s3/generate_hand_font.m -o "$font_build/technics-font"
"$font_build/technics-font" kaseta_radio_s3
```

This creates `kaseta_radio_s3/hand_station_font.h`, which can then be used to compile on another machine.

## Compile

Open `kaseta_radio_s3/kaseta_radio_s3.ino` in Arduino IDE and select ESP32S3 Dev Module, 16 MB flash, OPI PSRAM, the `app3M_fat9M_16MB` partition scheme, Hardware CDC and JTAG, and USB CDC on boot. The equivalent CLI command is:

```sh
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc \
  kaseta_radio_s3
```

If the bundled Arduino `ctags` fails with `bad CPU type in executable` on macOS, add `--build-property 'tools.ctags.cmd.path=/usr/bin/true'` to this command. The v26 sketch already declares the prototypes it needs.

## Connect Home Assistant and Music Assistant

Configure Music Assistant to use the deck as a UPnP/DLNA player and manage your stations in your own installation. The firmware receives HTTP audio stream addresses at runtime. MQTT Discovery exposes the six physical keys in Home Assistant. The default entity IDs start with `binary_sensor.technics_rs_b205_` and end in `play`, `pause`, `stop`, `prev`, `next`, or `pilot`. Map the entities to the corresponding actions in your HA setup.

For PAUSE, publish `toggle_pause` to `technics_rs_b205/playback/set` with retain disabled. This toggles local muting while keeping the stream active. For PLAY while paused, publish `resume` on the same topic. The deck reports its current state on `technics_rs_b205/playback/state`. Use Music Assistant to start a station or stop playback.

Update the example station names in [stations.h](kaseta_radio_s3/stations.h) to match the labels and media names your automation sends. After selecting a station, publish the matching `{"media":"Station media name","speak":"Cassette label"}` pair to `technics_rs_b205/station/set`. The firmware displays the label when both values match its station table.

The display initialization and SPI framing are based on the Waveshare team's example for the [4-inch TFT Touch Shield](https://www.waveshare.com/wiki/4inch_TFT_Touch_Shield). The display driver retains that attribution.
