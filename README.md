<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# FoloToy AI Passport · Calendar Play

A **calendar play** firmware for the **FoloToy AI Passport** (ESP32-C3 / 8 MB Flash / no PSRAM), built on the official [folotoy/ai-passport](https://github.com/folotoy/ai-passport) project. Version **0.1.0**.

## Flash it

The installable file is `build/FoloToy-AI-Passport-full.bin` (merged image, ~1.5 MB).

Flash it from a browser with the official **local play installation tool** — it reads the `.bin` entirely in your browser and never uploads it. Then simply reboot the device; the calendar play appears in the menu.

```bash
# or flash from the command line (requires ESP-IDF)
idf.py -p PORT flash
```

> The `.bin` is also attached as an artifact by the automatic firmware build workflow.

## What it does

- Monthly gregorian calendar grid (6 rows × 7 columns) in the project's pixel-art style.
- **Countdown** to a target date, shown as `D-xxx` / `D+xxx`.
- **Anniversary**: mark a month/day; it is highlighted every year on the same month/day.
- **Today / target** distinguished by color.
- **Auto time calibration**: once the board is on your Wi-Fi, SNTP fetches the real date and re-bases today — nothing is hardcoded to one network.
- **Silent-first**: no audio path — saves memory and battery.
- **Low power**: backlight auto-off after 30 s idle, any key re-lights it.

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