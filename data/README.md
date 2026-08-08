# dispatch.json — your tag mapping (local, private)

`data/dispatch.json` is your **local** mapping of NFC tag UIDs to media items
that Home Assistant plays on a Sonos device via webhook. It is gitignored on
purpose, so your tags and Spotify URIs never end up in the repository.

A placeholder sample is committed as `data/dispatch.json.example`.

| Field       | Description                                                        |
| ----------- | ------------------------------------------------------------------ |
| `tagId`     | NFC tag UID as printed by the RC522 firmware, e.g. `04 AA BB CC DD EE FF` |
| `contentId` | Spotify URI (`spotify:track:...`, `spotify:playlist:...`) or Sonos favorite (`SQ:0`) |
| `contentType` | `music` (default), or `favorite_item_id` for Sonos favorites     |
| `description` | Human-readable label used in logs only                          |

Workflow:

1. Copy the example and fill in your own values:
   ```
   cp data/dispatch.json.example data/dispatch.json
   ```
2. Tap a card against the reader and read the printed UID from the serial
   monitor (or write the tag with an NFC tool). Put that value in `tagId`.
3. In the Spotify app copy the link of the track/playlist you want `contentId`
   to reference.
4. Keep one entry per card, ensuring `tagId` values are unique. Re-flash the
   SPIFFS image (`idf.py flash`) after editing.