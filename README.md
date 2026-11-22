# Gartennode I/O module

CANopen node **13**, with eight active-low outputs at `0x2102:1..8` and eight
inputs at `0x2103:1..8`. Output values: **0 = ON, 1 = OFF**.

## Native simulator

Native support lives in this repository, like the cistern module:

- `boards/native_sim.conf` and `boards/native_sim.overlay` are selected
  automatically by `west build -b native_sim`.
- `src/main.c` selects GPIO emulation and host-clock CANopen processing only
  for `CONFIG_BOARD_NATIVE_SIM`. Hardware still uses the PCF857x GPIO expander.
- `dts/bindings/power-switch.yaml` describes the emulated output pins.

### Development container

Open this repository in its `.devcontainer`. The image provides a Zephyr west
workspace at `/workspace`, CANopenNodeZephyr, and the native UDP-CAN module.
The post-create command prepares the UDP dependency; it does not rewrite
application sources.

Run from `/workspace`:

```sh
west build -b native_sim application --build-dir application/build/native_sim \
  -- -DZEPHYR_EXTRA_MODULES="/workspace/canopennodezephyr;/workspace/zephyr-native-linux-udp-can"
./application/build/native_sim/zephyr/zephyr.exe
```

### Existing west workspace

Requires Linux, the Zephyr native-sim host toolchain, CANopenNodeZephyr (with
its CANopenNode submodule), and `jaster-prj/zephyr-native-linux-udp-can`.
The development image currently supplies Zephyr 4.5. The UDP driver owns its
wire format, nonblocking RX behavior, and devicetree binding discovery.

From this repository, with dependencies next to it:

```sh
west build -b native_sim . --build-dir build/native_sim \
  -- -DZEPHYR_EXTRA_MODULES="$(realpath ../canopennodezephyr);$(realpath ../zephyr-native-linux-udp-can)"
./build/native_sim/zephyr/zephyr.exe
```


### Behavior and endpoints

- Automatically enters Operational, with a 1s heartbeat on CAN-ID `0x70D`.
- Reports all eight raw output values every 250ms on TPDO `0x18D`.
- Accepts full-output RPDO `0x20D` and individual SDO writes to `0x2102`.
- Starts all simulated outputs OFF, including after a communication reset.
- Uses UDP gateway `127.0.0.1:46100`, listening locally on port **46114**.
  Change the endpoint using an additional devicetree overlay if needed.
- Inputs stay at their object-dictionary defaults; PCF857x input acquisition,
  physical CAN faults, MCUboot, and firmware download are not emulated.

The safe simulated boot defaults do **not** establish hardware boot safety.
TPDO values report requested outputs, not physical actuator/flow feedback.

### Isolated smoke test (no Virtual Garden required)

Stop a separately running simulator first. The test starts its own process and
UDP gateway socket, reads SDOs, checks heartbeat/TPDOs, switches simulated
outputs via SDO/RPDO, and checks communication-reset defaults:

```sh
python3 tests/native_sim_smoke.py build/native_sim/zephyr/zephyr.exe
python3 -m unittest discover -s tests
```

Run these in an isolated container/network namespace if ports 46100/46114 are
already used. No MQTT, Home Assistant, or physical CAN hardware is required.

## Hardware build and signing

With the hardware board definitions and MCUboot key installed in the west
workspace, build this application (not the cistern application):

```sh
west build -b gartennode . --build-dir build/hardware
python3 scripts/check_firmware.py \
  build/hardware/zephyr/zephyr.signed.bin build/hardware/zephyr/.config
python3 ../bootloader/mcuboot/scripts/imgtool.py verify \
  --key ../bootloader/mcuboot/gartennode.pem \
  build/hardware/zephyr/zephyr.signed.bin
python3 scripts/prepare_eds.py build/hardware/zephyr/.config \
  objdict/objdict.eds build/hardware/objdict.eds
```

Publish Zephyr's signed output without re-signing it with a hard-coded header.
See [firmware-updates.md](docs/firmware-updates.md) for CAN updates, matching
MCUboot, optional diagnostics and full-BIN recovery.
