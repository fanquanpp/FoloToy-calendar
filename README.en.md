<p align="right">
  <a href="README.md"><strong>简体中文</strong></a> · English
</p>

# FoloToy AI Passport · Calendar Play

A **calendar play** firmware for the **FoloToy AI Passport** (ESP32-C3 / 8 MB Flash / no PSRAM), built on the official [folotoy/ai-passport](https://github.com/folotoy/ai-passport) project. Version **0.1.0**.

## Features

A quiet, low-power desktop calendar:

- **Monthly calendar grid**: 6 rows × 7 columns, in the project's pixel-art style.
- **Countdown**: track a target date, shown as `D-xxx` / `D+xxx`.
- **Anniversary**: mark a month/day; it stays highlighted every year on the same month/day.
- **Today / target** distinguished by color.
- **Auto time calibration**: once on your own Wi-Fi, SNTP pulls the real date and re-bases today — nothing is hardcoded to a single network.
- **Silent-first**: no audio path — saves memory and battery.
- **Low power**: backlight auto-off after 30 s idle and deep low-power idle; any key re-lights it.

## Flash it

The installable file is `build/FoloToy-AI-Passport-full.bin` (merged image, ~1.5 MB).

Flash it from a browser with the official **FoloToy Web Tool** (WebSerial; set the address to `0x0`):

▶ Open the official flasher: <https://tool.folotoy.cn/>

Write the firmware, then reboot; the calendar play appears in the menu.

```bash
# or flash from the command line (requires ESP-IDF)
idf.py -p PORT flash
```

> The `.bin` is also attached to each Release by the automatic firmware build workflow (tag push).

## Official resources

- Official site / product page: <https://ai-passport.folotoy.cn/>
- Official flashing tool: <https://tool.folotoy.cn/>
- Official documentation: <https://docs.folotoy.com/>

## Network & auto time sync

The `Setup` play chooses how the board gets online — it is **not** tied to one fixed network:

- **Wi-Fi: auto join** — connects to the credentials already saved on the device (stored in NVS).
- **SoftAP: config page** — the board opens its own access point (`FoloToy-Calendar`, password `12345678`); open `http://192.168.4.1` from your phone and enter your network.
- **BLE: provision** — a small GATT service (`0xFFF0 / 0xFFF1`) accepts Wi-Fi credentials and a timestamp from another device.

Once online, SNTP pulls the real time and the calendar re-bases its date, then persists it — so calibration survives reboots and every user simply uses their own Wi-Fi.

## Controls

| Key | Action |
| --- | --- |
| Up / Down short press | previous / next month |
| In Setup: Up / Down | switch provisioning mode |
| In Setup: OK | run the selected provisioning mode |
| Up / Down long press | countdown target −1 / +1 day |
| OK short press | set countdown target = today, jump to this month |
| OK double press | set anniversary = today |
| OK long press | back to menu |

## Build

Requires ESP-IDF **5.5.3** and target `esp32c3`.

```bash
. $IDF_PATH/export.ps1          # or: source $IDF_PATH/export.sh
idf.py set-target esp32c3
idf.py build
idf.py merge-bin -o build/FoloToy-AI-Passport-full.bin
```

Validate:

```bash
./tools/validate.sh --static     # repo checks + host tests (calendar logic)
./tools/validate.sh --firmware   # ESP-IDF build + merged-image verification
```

## License

[MIT](./LICENSE) · Changelog: [docs/CHANGELOG.md](docs/CHANGELOG.md)