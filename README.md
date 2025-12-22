# sonotify-nfc-esp32

ESP32 firmware that reads a dispatch JSON (list of NFC tag -> Spotify URIs) and sends a Home Assistant webhook to play a selected Spotify URI on a Sonos device.

This repository contains a simple example using:
- ESP-IDF
- SPIFFS (for storing `dispatch.json`)
- cJSON (JSON parsing)
- esp_http_client (downstream HTTP POST to Home Assistant webhook)
- Wi‑Fi connectivity

## Repository layout

- `main/sonotify-nfc-esp32.c` — main application source.
- `main/CMakeLists.txt`, `main/idf_component.yml`, `main/Kconfig.projbuild` — component build metadata.
- `data/dispatch.json` — example JSON containing tag entries (copied into SPIFFS at deploy time).
- Other project files for build and configuration are present at the repo root.

## Features / behavior

- Connects to Wi‑Fi using `WIFI_SSID` / `WIFI_PASSWORD` (provided via menuconfig).
- Initializes SPIFFS and reads `/spiffs/dispatch.json`.
- Selects the first entry and extracts `spotifyUri`.
- Performs an HTTP POST to a Home Assistant webhook URL with form fields `uri` and `entity_id`.

## Configuration

- Wi‑Fi SSID and password are configured in menuconfig (`main/Kconfig.projbuild`):
  - `WIFI_SSID`
  - `WIFI_PASSWORD`

- Home Assistant webhook URL and Sonos entity ID are hardcoded in `main/sonotify-nfc-esp32.c`:
  - `HA_WEBHOOK_URL`
  - `SONOS_ENTITY_ID`

Adjust these defines in code or convert them into menuconfig options if you prefer runtime configuration.

## Preparing SPIFFS

This project expects `dispatch.json` to be available under SPIFFS at `/spiffs/dispatch.json`. Typical workflow:

1. Keep your `dispatch.json` under `data/dispatch.json` (example provided).
2. Create a data partition or use ESP-IDF's spiffs partition tool to embed or flash the file to SPIFFS.

Refer to the ESP-IDF documentation for creating/packing SPIFFS data (component `spiffs` and `partition_table` usage).

## Build & flash (quick)

1. Configure the project:
   - idf.py menuconfig
   - Set Wi‑Fi values under the "WiFi Configuration" menu

2. Build and flash:
   - idf.py build
   - idf.py -p /dev/ttyUSB0 flash monitor

Replace `/dev/ttyUSB0` with your serial adapter device.

## Troubleshooting

- If the device fails to mount SPIFFS, check partition table and that the SPIFFS partition exists.
- If the HTTP POST fails, verify the `HA_WEBHOOK_URL` and the device has network connectivity.
- Use the serial monitor (`idf.py monitor`) to view ESP_LOG output.

## License

This repository's files do not include a specific license file. Add an appropriate LICENSE if you plan to publish or redistribute.
