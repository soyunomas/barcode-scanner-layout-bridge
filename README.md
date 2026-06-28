# barcode-scanner-layout-bridge

User-space Linux bridge for a problematic USB barcode scanner that behaves as a HID keyboard and types URL characters with the wrong keyboard layout.

Repository:

```text
https://github.com/soyunomas/barcode-scanner-layout-bridge
```

This project was created for a scanner that appears on Linux as:

```text
Bus 001 Device 004: ID 34eb:1502 WCM HIDKeyBoard
```

On a Spanish keyboard layout, scanning:

```text
http://www.example.com
```

could produce:

```text
httpÑ--www.example.com
```

The scanner firmware only exposes `UNITED STATES` and `FRANCE` keyboard modes, with no Spanish mode. This bridge captures only that scanner, translates its raw US HID keycodes into URL text, and re-emits corrected keystrokes through a virtual keyboard without changing the system keyboard layout.

## Status

Working for the tested device and Spanish desktop layout.

Validated examples:

```text
http://www.example.com
https://dominio.test/path_file-1?q=a+b%20c#frag
```

The default emission timing is:

```text
--key-delay-us 1000 --char-delay-us 3000
```

The bridge now includes reconnect handling, a systemd unit, udev rules, and selectable output layout profiles:

```text
--output-layout es|us|fr|de
```

## Tested Device

Known affected hardware:

| Field | Value |
|---|---|
| USB VID:PID | `34eb:1502` |
| Manufacturer string | `WCM` |
| Product string | `HIDKeyBoard` |
| HID name | `WCM HIDKeyBoard` |
| Serial observed | `00000000011C` |
| Interface | USB HID keyboard |
| Linux event example | `/dev/input/event6` |

Useful commands to identify the device:

```bash
lsusb
lsusb -v -d 34eb:1502
udevadm info /dev/input/event6
```

Expected clues:

```text
ID_VENDOR_ID=34eb
ID_MODEL_ID=1502
ID_SERIAL=WCM_HIDKeyBoard_00000000011C
ID_INPUT_KEYBOARD=1
```

## Does This Work For Other Languages?

The scanner input is decoded as US HID keyboard events. The output side can be selected with `--output-layout`:

| Scanner output mode | Desktop layout | Option | Status |
|---|---|---|---|
| US keyboard | Spanish layout | `--output-layout es` | Implemented and tested |
| US keyboard | US layout | `--output-layout us` | Implemented from XKB basic map |
| US keyboard | French layout | `--output-layout fr` | Implemented from XKB basic AZERTY map, needs real-hardware validation |
| US keyboard | German layout | `--output-layout de` | Implemented from XKB basic QWERTZ map, needs real-hardware validation |
| French scanner mode | Any layout | Not applicable | Not implemented; use scanner `UNITED STATES` mode |

The important distinction:

- Input side: the scanner is decoded as US HID keyboard events.
- Output side: the virtual keyboard emits key combinations that produce the intended URL on the selected desktop layout.

To support another desktop layout, add a new output mapping function in [src/scanner_bridge.c](src/scanner_bridge.c) and register it in `layout_profiles`.

## How It Works

`scanner-bridge`:

1. Finds the scanner by USB vendor/product `34eb:1502`.
2. Opens its `/dev/input/eventX` device.
3. Uses `EVIOCGRAB` so the broken original keystrokes do not reach applications.
4. Converts raw Linux `EV_KEY` events as US keyboard input.
5. Queues complete scans so fast repeated scans do not block input reading.
6. Emits corrected key combinations through `/dev/uinput` for the selected output layout.
7. Re-detects the scanner if it is unplugged and plugged back in.

It does not change your normal keyboard layout.

## Build

Requirements:

- Linux
- C compiler
- kernel headers exposing `linux/input.h` and `linux/uinput.h`
- access to `/dev/input/eventX`
- access to `/dev/uinput`

Build:

```bash
make
```

Clean:

```bash
make clean
```

## Install As A Service

Install files:

```bash
sudo make install
```

Reload udev and systemd:

```bash
sudo modprobe uinput
sudo udevadm control --reload-rules
sudo udevadm trigger
sudo systemctl daemon-reload
```

Start on boot:

```bash
sudo systemctl enable --now escanner.service
```

Watch logs:

```bash
journalctl -u escanner.service -f
```

Default runtime configuration is installed at:

```text
/etc/default/escanner
```

The installer also adds:

```text
/etc/modules-load.d/escanner.conf
```

so the `uinput` kernel module is loaded automatically on boot.

For a Spanish desktop layout:

```text
ESCANNER_OUTPUT_LAYOUT=es
```

For another desktop layout, set one of:

```text
ESCANNER_OUTPUT_LAYOUT=us
ESCANNER_OUTPUT_LAYOUT=fr
ESCANNER_OUTPUT_LAYOUT=de
```

Then restart:

```bash
sudo systemctl restart escanner.service
```

The service runs `scanner-bridge` continuously. If the scanner is unplugged, it retries and uses the new `/dev/input/eventX` path when the same `34eb:1502` device appears again.

## Manual Use Without sudo

The installed udev rules grant access to the tested scanner event node and `/dev/uinput` through `TAG+="uaccess"` for active desktop sessions, and through the `input` group.

If your distro does not apply `uaccess` to your session, add your user to `input` and log out/in:

```bash
sudo usermod -aG input "$USER"
```

Security note: membership in `input` can allow reading input devices. Prefer the systemd service for normal use.

## Tools

### `scanner-dump`

Debug tool. It reads the scanner and prints raw key events plus reconstructed text.

```bash
sudo build/scanner-dump
```

Then scan:

```text
http://www.example.com
```

Expected output:

```text
SCAN[enter]: "http://www.example.com"
```

Use `--grab` if you want to prevent the original broken text from reaching the focused application while debugging:

```bash
sudo build/scanner-dump --grab
```

### `scanner-bridge`

Runtime bridge. It captures the scanner and emits corrected text.

```bash
sudo build/scanner-bridge --output-layout es
```

Keep it running, focus a text editor or input field, then scan a URL.

If your desktop or application drops characters, increase emission delays:

```bash
sudo build/scanner-bridge --key-delay-us 2000 --char-delay-us 6000
```

For a more conservative output:

```bash
sudo build/scanner-bridge --key-delay-us 5000 --char-delay-us 15000
```

Dry run mode captures and decodes the scanner but does not emit through `/dev/uinput`:

```bash
sudo build/scanner-bridge --dry-run
```

Expected:

```text
DRY-SCAN: "http://www.example.com"
```

## URL Safety Filter

By default, `scanner-bridge` only emits scans that look like complete URLs:

```text
http://...
https://...
```

If it receives a fragment such as:

```text
https:/frag
```

it logs:

```text
DROP: escaneo incompleto o URL invalida; no se emite.
```

For debugging only, allow invalid fragments with:

```bash
sudo build/scanner-bridge --emit-invalid
```

## Current Limitations

- Only the tested `34eb:1502 WCM HIDKeyBoard` is autodetected.
- `es` is the only output profile validated on the real machine so far.
- `us`, `fr`, and `de` are implemented from XKB basic layouts, but need real desktop validation.
- It is a user-space bridge, not a kernel driver.
- Running manually without `sudo` depends on udev/logind permissions or `input` group membership.

## Roadmap

See [todo.md](todo.md).

Main pending work:

- broader URL character test matrix
- real validation of `us`, `fr`, and `de` output profiles

## Development Notes

See [lessons.md](lessons.md) for the hardware diagnosis and implementation lessons learned.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

## License

MIT. See [LICENSE](LICENSE).
