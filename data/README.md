# dispatch.json

`dispatch.json` maps NFC tag UIDs to media items that Home Assistant plays on a
Sonos device via webhook.

| Field       | Description                                                        |
| ----------- | ------------------------------------------------------------------ |
| `tagId`     | NFC tag UID as printed by the RC522 firmware, e.g. `REDACTED_TAG_UID` |
| `contentId` | Spotify URI (`spotify:track:...`, `spotify:playlist:...`) or Sonos favorite (`SQ:0`) |
| `contentType` | `music` (default), or `favorite_item_id` for Sonos favorites     |
| `description` | Human-readable label used in logs only                          |

The values in this file are placeholders. Replace them with your own before
deploying:

1. Tap a card against the reader and read the printed UID from the serial
   monitor (or write the tag with an NFC tool).
2. In the Spotify app copy the link of the track/playlist you want `contentId`
   to reference.
3. Keep one entry per card, ensuring `tagId` values are unique.