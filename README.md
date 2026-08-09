# sonotify-nfc-esp32

ESP32 firmware that reads NFC tags on an RC522 RFID module and triggers a Home Assistant webhook to play media on a Sonos speaker. NFC tag UIDs are mapped to media (Spotify tracks/playlists or Sonos favorites) through a JSON dispatch table.

![20260207_164626](https://github.com/user-attachments/assets/1084bc28-154e-4e23-965f-d83a3879699b)

## Features

- Reads NFC tag UIDs via an RC522 module over SPI
- Maps card UIDs to media items at boot through a `dispatch.json` table
- Fetches and refreshes `dispatch.json` from a web URL on every boot (optional), falling back to the copy stored in SPIFFS
- Triggers a Home Assistant webhook so Home Assistant can play the media item on a Sonos device
- Status LED communicates boot, Wi-Fi, and error states
- Prints unknown card UIDs so you can easily add tags to `dispatch.json`

## Repository layout

```
main/
  main.c            app_main boot sequence
  led.c/.h          status LED control
  wifi.c/.h         Wi-Fi STA connection (blocking, with retries)
  dispatch.c/.h     SPIFFS mount, dispatch.json load, web refresh, UID lookup
  ha_client.c/.h    Home Assistant webhook client
  rc522_scanner.c/.h  RC522 reader setup and card event handler
  Kconfig.projbuild  all configuration via menuconfig
data/
  dispatch.json     your local dispatch table (gitignored)
  dispatch.json.example  committed example table
tests/
    host-side unit tests for dispatch lookup (no hardware required)
.github/workflows/ci.yml  runs the tests on push/PR
```

## How it works

1. On boot: init LED, NVS, netif, event loop; connect to Wi-Fi.
2. Read `dispatch.json`:
   - If `DISPATCH_SOURCE_URL` is configured, fetch it (http or https), validate it, and store the fresh copy in SPIFFS.
   - On any failure (fetch or validation), fall back to the copy already stored in SPIFFS.
3. Start the RC522 scanner.
4. When a card is tapped: look up its UID in the dispatch table to get the media item, then POST `content_id`, `entity_id`, `content_type` to the Home Assistant webhook.

Unknown, unlisted cards print a message telling you exactly which `tagId` to add:

```
W (59967) rc522: Card not in dispatch.json. Add an entry with tagId "53 8C 96 B1 11 00 01" to add it
```

### Dispatch table format

A JSON array of entries, e.g. `data/dispatch.json`:

```json
[
  {
    "tagId": "04 AA BB CC DD EE FF",
    "contentId": "spotify:track:6sH1FvX2nqd7ZFMeKyi0cs",
    "description": "Ana Moura - Andorinhas",
    "contentType": "music"
  },
  {
    "tagId": "04 11 22 33 44 55 66",
    "contentId": "SQ:0",
    "description": "Sonos queue",
    "contentType": "favorite_item_id"
  }
]
```

- `tagId` — the space-separated hex UID printed by the reader firmware
- `contentId` — Spotify URI (`spotify:track:`…, `spotify:playlist:`…) or a Sonos queue/favorite reference (e.g. `SQ:0`)
- `contentType` — `music` (default) or `favorite_item_id`

The board expects `/spiffs/dispatch.json`; `data/dispatch.json` local copy is packed into SPIFFS at build time by ESP-IDF.

## Configuration

Run `idf.py menuconfig` and set:

**WiFi Configuration**
| Option | Description |
| --- | --- |
| `WIFI_SSID` | Wi-Fi SSID to join |
| `WIFI_PASSWORD` | Wi-Fi password |
| `WIFI_MAX_RETRY` | Connection attempts before giving up |

**Hardware Pin Configuration**
| Option | Description |
| --- | --- |
| `RC522_SPI_BUS_GPIO_MISO` | RC522 MISO (default 25) |
| `RC522_SPI_BUS_GPIO_MOSI` | RC522 MOSI (default 23) |
| `RC522_SPI_BUS_GPIO_SCLK` | RC522 SCLK (default 19) |
| `RC522_SPI_SCANNER_GPIO_SDA` | RC522 chip-select (default 22) |
| `RC522_SCANNER_GPIO_RST` | RC522 reset (default 21) |
| `RC522_SPI_CLOCK_HZ` | SPI clock (default 200000) |
| `LED_GPIO` | Status LED (default 32) |

**Media & Dispatch Configuration**
| Option | Description |
| --- | --- |
| `HA_WEBHOOK_URL` | Home Assistant webhook to trigger |
| `SONOS_ENTITY_ID` | Sonos media_player entity, e.g. `media_player.roam_2` |
| `DISPATCH_SOURCE_URL` | Optional URL to fetch `dispatch.json` from on boot (http/https); empty = use SPIFFS copy only |

## Build & flash

Hardware is a plain ESP32 with a 2 MB flash. Built and flashed with ESP-IDF:

```sh
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyACM0 -b 115200 flash monitor
```

`idf.py menuconfig` writes your choices into the local `sdkconfig` (gitignored). `sdkconfig.defaults` ships placeholder Wi-Fi credentials (`changeme`); menus override it for each new build. `data/dispatch.json` is auto-packed into SPIFFS during build via the `spiffs` partition table entry.

## Tests

Host-side unit tests for the dispatch logic run without any dev board. Run them manually:

```sh
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

The GitHub Actions workflow performs the same steps on every push/PR to `main`.

## Troubleshooting

| Problem | Likely cause / fix |
| --- | --- |
| No LEDs / no boot-left logs | Check `-b 115200` and the USB serial device |
| "Failed to mount or format SPIFFS" | SPIFFS partition size; `data/` may not be flashable |
| Card UID printed but nothing plays | `tagId` missing/typo in `dispatch.json`, or webhook/entity misconfig |
| Internet fetch fails | `DISPATCH_SOURCE_URL` unreachable or invalid JSON; firmware falls back to SPIFFS copy |

## License

MIT
Copyright (c) 2026 André Santos