## Wiring

This pinout is for the **ESP32-S3-WROOM-1 N16R8** build running firmware v26. The GPIO numbers refer to the labels on the board. Disconnect the deck from mains power and unplug USB before changing any wiring. Connect the ESP32-S3, display, DAC, and motor driver grounds together.

### Waveshare 4-inch TFT Touch Shield (13587)

| Display pin | Connect to |
| --- | --- |
| `5V` | 5 V supply |
| `GND` | Common ground |
| `SCLK` | GPIO12 |
| `MOSI` | GPIO11 |
| `LCD_CS` | GPIO10 |
| `LCD_DC` | GPIO9 |
| `LCD_RST` | GPIO8 |
| `LCD_BL` | GPIO7 |
| `TP_CS`, `SD_CS` | 3.3 V to keep touch and SD disabled |
| `MISO` | Leave disconnected for this build |

Set the three **SPI Config** switches to route SPI to the side header. Use `LCD_RST` on the side header and leave `RESET` on the power header disconnected.

### PCM5102A DAC

| DAC pin | Connect to |
| --- | --- |
| `VIN` | ESP32-S3 3.3 V, as used with the breakout in this build |
| `GND` | Common ground |
| `BCK` | GPIO4 |
| `LCK` / `LRCK` | GPIO5 |
| `DIN` | GPIO6 |

The DAC's left and right analog outputs feed the RCA jacks and the separate, adjustable VU meter inputs. See the **Original VU Meter and Line Output** section for those connections.

### Front-panel microswitches

Use the normally open contacts on each microswitch. Connect one contact to the GPIO listed below and the other to ESP32-S3 ground. The firmware enables internal pull-ups, so pressing a key pulls its input low.

| Deck key | Firmware function | ESP32-S3 |
| --- | --- | --- |
| REW | Previous station | GPIO14 |
| FF | Next station | GPIO15 |
| PLAY | Play | GPIO16 |
| PAUSE | Pause / resume | GPIO17 |
| STOP/EJECT | Stop | GPIO18 |
| REC | Remote control action (`Pilot`) | GPIO21 |

### Mechanical tape counter

Connect the **5 V 28BYJ-48** motor to the connector on the **ULN2003** driver board.

| ULN2003 pin | Connect to |
| --- | --- |
| `IN1` | GPIO39 |
| `IN2` | GPIO40 |
| `IN3` | GPIO41 |
| `IN4` | GPIO42 |
| `VCC` | 5 V motor supply |
| `GND` | Common ground |

Power the motor driver from a separate 5 V branch. If the motor has its own power supply, join the grounds and keep the two positive supply rails separate.
