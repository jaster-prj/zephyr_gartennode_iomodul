# I/O module firmware updates

The update fixes are ported from the cistern module. Cistern hardware tests do
not establish that the I/O module has passed a hardware update test.
Node ID remains **13** (`0x0d`). Eight active-low outputs (`0 = ON`, `1 = OFF`),
PCF857x input acquisition, NMT startup policy and existing native-sim behavior
are unchanged. Native-sim does not emulate MCUboot or firmware download.

## Build and release

Use the Gartennode west workspace and matching private signing key at
`bootloader/mcuboot/gartennode.pem`. Never commit or publish the key.
From the workspace root:

```sh
west build -b gartennode application -d build/io
python3 application/scripts/check_firmware.py \
  build/io/zephyr/zephyr.signed.bin build/io/zephyr/.config
python3 bootloader/mcuboot/scripts/imgtool.py verify \
  --key bootloader/mcuboot/gartennode.pem build/io/zephyr/zephyr.signed.bin
python3 application/scripts/prepare_eds.py \
  build/io/zephyr/.config application/objdict/objdict.eds objdict.eds
python3 -m unittest discover -s application/scripts -p 'test_*.py'
python3 -m unittest discover -s application/tests -p 'test_*.py'
```

Publish Zephyr's `zephyr.signed.bin`, not an image re-signed with a hard-coded
header size. Zephyr 4.5 uses a `0x400` STM32 header. The default signed version
is `1.3.1`; override `CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION` for each new release.
The running `0x100A` string and generated release EDS use that same version.
CI retains the standalone BIN and also packages the matching EDS with it.

Both application and supplied MCUboot configuration use **swap using scratch**:

| Region | Address/offset | Size |
| --- | --- | --- |
| MCUboot, internal | `0x08000000` | 64 KiB |
| Primary, internal | `0x08010000` | 192 KiB |
| Secondary, external | `0` | 192 KiB |
| Scratch, external | `0x30000` | 192 KiB |

Build matching MCUboot separately:

```sh
west build -b gartennode bootloader/mcuboot/boot/zephyr \
  -d build/io-boot \
  -- -DEXTRA_CONF_FILE="$PWD/application/bootloader.conf"
```

Verify the installed bootloader's layout and signing key before deployment.
The supplied bootloader disables watchdog support. This port does not add a
watchdog to the application or change its I/O initialization policy.

## CAN update

Use node `0xd` and the uploaded image's version:

```sh
curl --cacert CA.pem \
  -H 'Content-Type: application/octet-stream' \
  --data-binary @build/io/zephyr/zephyr.signed.bin \
  'https://YOUR_GATEWAY/canopenrest/api/v1/flash?node=0xd&version=1.3.1'
```

Poll `GET /canopenrest/api/v1/flash?id=<returned-id>` to completion.
Require reboot, matching `0x100A`, and explicit confirmation—not just successful
data reception.

STOP/CLEAR/START retain upstream PRE-OPERATIONAL restrictions. CLEAR erases the
secondary slot; reception-time stream-flash erasing is disabled to avoid
erasing it again during SDO handling.

The adapter permits explicit `0x1F51:1 = 0x80` confirmation while OPERATIONAL
as well as PRE-OPERATIONAL, without requiring another NMT transition after
gateway verification. It never automatically confirms an image; MCUboot
rollback remains available for unconfirmed test images.

The cistern's swap/startup took about 16.8 seconds, longer than the gateway's
fixed ten-second initial wait. Allow adequate reboot/verification time;
I/O module timing has not been measured.

## SWD recovery

Back up internal flash before recovery. Use a **full padded BIN**, not sparse
HEX: sparse programming can leave old swap-status bytes in a primary trailer
page and cause MCUboot to interpret an invalid interrupted swap.

Generate a confirmed baseline separately:

```sh
west build -b gartennode application -d build/io-recovery \
  -- -DCONFIG_MCUBOOT_GENERATE_CONFIRMED_IMAGE=y
```

Verify layout/signature and exact `0x30000`-byte length before programming
`zephyr.signed.confirmed.bin` at `0x08010000`. Resolve pending external swap
state first; do not resume a pending swap against an unrelated replacement
primary. Avoid mass erases of the bootloader or external settings.
Do not use another module's firmware as an I/O module recovery image.

## Optional diagnostics

Add `-DEXTRA_CONF_FILE=debug/update.conf` when building the application.
It records reset causes, download byte counts and callback latency using
deferred RTT logs, filtering GPIO-expander log noise without disabling I/O.
Read `io_update_diagnostics` and RTT only after a transfer;
live SWD reads or halts can disturb timing.

`debug/bootloader.conf` is an optional second MCUboot fragment for RTT output.
See the README for native-sim build preparation and isolated I/O smoke tests.

## Port validation

Normal firmware, diagnostic firmware, matching MCUboot and native-sim builds
passed with Zephyr 4.5.0. Both hardware images are 196608 bytes and pass layout,
vector-table and signature verification; generated EDS version is `1.3.1`.
All 11 update regression tests and both existing dependency-preparation tests
pass. The isolated native-sim smoke test also passes: Operational heartbeat,
safe simulated defaults, periodic TPDOs, active-low SDO commands, RPDO updates
and communication-reset defaults.

Existing uncommitted native-sim work was preserved. These checks do not verify
physical I/O boot safety or CAN firmware updates on hardware. No hardware was
flashed as part of this port.
