"""Exercise a native-sim binary using an isolated UDP-CAN bus (standard library)."""

from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time

FRAME = struct.Struct("<IBB2x64s")
NODE = 13
GATEWAY = ("127.0.0.1", 46100)
MODULE = ("127.0.0.1", 46114)


def check(binary):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as bus, tempfile.TemporaryFile() as log:
        bus.bind(GATEWAY)
        bus.settimeout(0.2)
        process = subprocess.Popen([str(Path(binary).resolve())], stdout=log, stderr=log)

        def send(can_id, payload):
            bus.sendto(FRAME.pack(can_id, len(payload), 0, payload), MODULE)

        def wait_for(predicate, request=None, timeout=8):
            deadline = time.monotonic() + timeout
            next_request = 0
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise AssertionError(f"Simulator exited: {process.returncode}")
                if request and time.monotonic() >= next_request:
                    request()
                    next_request = time.monotonic() + 0.5
                try:
                    packet, peer = bus.recvfrom(2048)
                except socket.timeout:
                    continue
                if len(packet) != FRAME.size:
                    raise AssertionError(f"Wrong UDP-CAN datagram length: {len(packet)}")
                can_id, dlc, flags, data = FRAME.unpack(packet)
                if peer[1] != MODULE[1] or dlc > 8 or flags != 0:
                    continue
                if predicate(can_id, data[:dlc]):
                    return data[:dlc]
            raise AssertionError("Timed out waiting for CANopen feedback")

        def sdo(subindex, value=None, index=0x2102):
            request = struct.pack(
                "<BHB4s", 0x40 if value is None else 0x2F,
                index, subindex, bytes(4) if value is None else bytes([value, 0, 0, 0]),
            )
            response = wait_for(
                lambda can_id, data: can_id == 0x580 + NODE
                and len(data) == 8 and data[1:4] == request[1:4],
                lambda: send(0x600 + NODE, request),
            )
            if response[0] == 0x80:
                raise AssertionError(f"SDO aborted: {response.hex()}")
            assert response[0] == (0x4F if value is None else 0x60), response.hex()
            return response[4] if value is None else None

        def tpdo(expected, request=None):
            wait_for(
                lambda can_id, data: can_id == 0x180 + NODE and data == bytes(expected),
                request=request,
            )

        try:
            # An upload also wakes the native UDP worker during startup.
            assert sdo(1) == 1, "Output 1 must boot OFF"
            wait_for(lambda can_id, data: can_id == 0x700 + NODE and data == b"\x05")
            for index in range(1, 9):
                assert sdo(index) == 1, f"Output {index} must boot OFF"
            tpdo([1] * 8)
            idle_heartbeats = 0
            idle_tpdos = 0

            def idle_feedback(can_id, data):
                nonlocal idle_heartbeats, idle_tpdos
                idle_heartbeats += int(can_id == 0x700 + NODE and data == b"\x05")
                idle_tpdos += int(can_id == 0x180 + NODE and data == bytes([1] * 8))
                return idle_heartbeats >= 2 and idle_tpdos >= 2

            # No incoming CAN traffic: periodic timers must work standalone.
            wait_for(idle_feedback, timeout=4)
            print("PASS: Operational heartbeat, safe defaults, periodic 8-byte TPDO", flush=True)

            sdo(1, 0)
            assert sdo(1) == 0
            tpdo([0] + [1] * 7)
            sdo(1, 1)
            assert sdo(1) == 1
            print("PASS: active-low SDO output commands", flush=True)

            outputs = [1, 0] + [1] * 6
            tpdo(outputs, request=lambda: send(0x200 + NODE, bytes(outputs)))
            assert sdo(2) == 0
            print("PASS: RPDO updates and authoritative TPDO/SDO feedback", flush=True)

            send(0, bytes([0x82, NODE]))
            # The node must resume with every output OFF, not the prior RPDO.
            tpdo([1] * 8, request=lambda: send(0x600 + NODE, bytes.fromhex("4002210100000000")))
            assert sdo(2) == 1
            print("PASS: communication reset restores simulated safe defaults", flush=True)
        except Exception:
            log.seek(0)
            sys.stderr.write(log.read().decode(errors="replace"))
            raise
        finally:
            send(0x200 + NODE, bytes([1] * 8))
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


if __name__ == "__main__":
    check(sys.argv[1] if len(sys.argv) > 1 else "build/native_sim/zephyr/zephyr.exe")
