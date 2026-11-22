# Technics RS-B205 Streaming Deck

<img width="4032" height="2268" alt="IMG_6098" src="https://github.com/user-attachments/assets/fa0e871d-bc29-4b63-9f1a-2af0edee31c8" />



https://github.com/user-attachments/assets/d058e1c5-11b0-4aa2-890f-c3e3fc9590a0



This project started with a broken Technics RS-B205 cassette deck. The tape mechanism had failed, but the enclosure, control keys, and original VU meter could be reused. A similar conversion can be done with another cassette deck, even one with a faulty main board. If you want to keep the original VU meter, make sure it still works.

The goal was to build a streaming player that keeps the look and feel of a classic cassette deck while integrating with Home Assistant (HA) and Music Assistant (MA). The project uses all six keys below the cassette door, displays an animated cassette with moving tape and reels, and retains the original VU meter and mechanical tape counter.

## Hardware

| Component | Model or purpose |
| Microcontroller | ESP32-S3-WROOM-1 N16R8 with 16 MB flash and 8 MB PSRAM |
| Audio DAC | PCM5102A stereo I²S DAC |
| Display | Waveshare 4-inch TFT Touch Shield (13587), ILI9486, 480 × 320 pixels |
| Tape counter drive | 5 V 28BYJ-48 stepper motor with a ULN2003 driver |
| Microswitches | Six, one under each control key |
| Perfboards | For mounting components and making connections |

<img width="3250" height="2066" alt="IMG_6079" src="https://github.com/user-attachments/assets/6be150d4-bf43-4a39-b576-7fbe64fb7757" />
<img width="2619" height="1964" alt="IMG_6065" src="https://github.com/user-attachments/assets/490e0f90-c167-4194-9bb1-f2240a10b7a3" />



I chose the display because it fills the visible area of the RS-B205 cassette window. If you’re using a different deck, check the window dimensions and the space behind it before choosing a display. The cassette door is fixed shut because it never needs to open during normal use.

A 3D printer is handy for making small parts such as the display frame and mounting brackets. Most can be made using other methods. The tape counter motor pulley is the main part worth printing, though you can also make one by hand.

<img width="4032" height="2268" alt="IMG_6100" src="https://github.com/user-attachments/assets/d756fd61-23a0-4336-a721-24b2c783fd68" />


## Firmware

The **v26 firmware** was written by AI based on the project requirements. The animation simulates a cassette with 30 minutes of tape per side. The reels are white on side A and yellow on side B. As the tape moves from one reel to the other, the reel with less tape spins faster. Fast-forward and rewind speed up the animation in the corresponding direction. **With this hardware setup, the animation runs at 29 fps while decoding FLAC audio.**

All six control keys work. Pressing **NEXT** or **PREV** triggers a two-second fast-forward or rewind animation and switches to the next or previous stream configured in Home Assistant. HA sends the station name back to the deck. It appears as a label on the cassette, with the text size adjusted to fit its length.

The animated reels stop when the stream drops or when you press **STOP** or **PAUSE**. The color of the **TDK** lettering indicates the stream status.

- **Red** means the stream has dropped or was closed after pressing **STOP**.
- **Green** means the stream is still being processed. Pressing **PAUSE** mutes the output and stops the reels, while the lettering stays green as long as the stream is active.

The mechanical tape counter motor stops in the same situations as the animated reels. When playback switches sides, the counter continues counting in the same direction.

## Adapting it to your setup

**Flashing the firmware alone won’t give you a working internet radio.** I built it around my home setup because I already use Home Assistant and Music Assistant and wanted the deck to work with both.

To run it at home, adapt the wiring and configure Wi-Fi, MQTT, Home Assistant, and Music Assistant for your setup. The firmware contains no stream URLs. You configure stations and audio sources in your own HA and MA installation.

**The code is available free of charge.** You can give it to your preferred AI assistant and ask it to adapt the firmware to your deck, components, and home setup.
