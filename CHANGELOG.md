# Changelog

Project: `barcode-scanner-layout-bridge`

## 0.1.0 - Initial working bridge

- Added `scanner-dump` to inspect raw `evdev` key events from the scanner.
- Added `scanner-bridge` using `EVIOCGRAB` plus `/dev/uinput` so broken scanner keystrokes do not reach applications.
- Autodetects the tested `34eb:1502 WCM HIDKeyBoard` scanner.
- Decodes scanner input as US HID keyboard events.
- Emits corrected keystrokes for selectable output layouts: `es`, `us`, `fr`, `de`.
- Defaults to Spanish output layout and timing validated on the real machine:
  `--key-delay-us 1000 --char-delay-us 3000`.
- Drops incomplete or invalid URL fragments by default.
- Queues scans and emits from a separate thread to tolerate repeated scans.
- Re-detects the scanner after disconnect/reconnect.
- Includes `systemd`, `udev`, and `modules-load.d` packaging files.
