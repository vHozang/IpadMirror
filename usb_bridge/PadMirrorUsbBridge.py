#!/usr/bin/env python3
"""Safe Windows USB screen bridge using Apple's user-space services."""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import json
import struct
import sys
import uuid
from dataclasses import dataclass
from typing import Any


MAX_FRAME_BYTES = 32 * 1024 * 1024
FRAME_VIDEO_KEY = 1
FRAME_VIDEO_DELTA = 2
FRAME_AUDIO_AAC_ELD = 3
HEVC_KEY_TYPES = {19, 20, 21}
ANNEX_B_START_CODE = b"\x00\x00\x00\x01"


def emit_status(event: str, message: str, **details: Any) -> None:
    payload = {"event": event, "message": message, **details}
    print("PADMIRROR " + json.dumps(payload, separators=(",", ":")), file=sys.stderr, flush=True)


def product_major_version(version: str) -> int | None:
    try:
        return int(version.split(".", 1)[0])
    except (TypeError, ValueError):
        return None


class BridgeError(RuntimeError):
    def __init__(self, code: str, message: str, exit_code: int = 1) -> None:
        super().__init__(message)
        self.code = code
        self.exit_code = exit_code


class BinaryWriter:
    def __init__(self) -> None:
        self._lock = asyncio.Lock()

    async def write(self, frame_type: int, pts_ns: int, payload: bytes) -> None:
        if not payload or len(payload) > MAX_FRAME_BYTES:
            return
        header = struct.pack(">BQI", frame_type, max(0, pts_ns), len(payload))
        async with self._lock:
            sys.stdout.buffer.write(header)
            sys.stdout.buffer.write(payload)
            sys.stdout.buffer.flush()


@dataclass
class RtpState:
    local_ssrc: int
    remote_ssrc: int
    destination: tuple[str, int] | None
    highest_sequence: int = 0
    packets_received: int = 0

    def observe(self, sequence: int) -> None:
        self.packets_received += 1
        current = self.highest_sequence
        cycles = (current >> 16) & 0xFFFF
        last = current & 0xFFFF
        if sequence < last and last - sequence > 0x8000:
            cycles = (cycles + 1) & 0xFFFF
        extended = (cycles << 16) | sequence
        if current == 0 or ((extended - current) & 0xFFFFFFFF) < 0x80000000:
            self.highest_sequence = extended


def build_rtcp_report(state: RtpState) -> bytes:
    report = struct.pack(
        ">BBHIIBBBBIIII",
        0x81,
        0xC9,
        7,
        state.local_ssrc & 0xFFFFFFFF,
        state.remote_ssrc & 0xFFFFFFFF,
        0,
        0,
        0,
        0,
        state.highest_sequence & 0xFFFFFFFF,
        0,
        0,
        0,
    )
    sdes = struct.pack(
        ">BBHIBBBB",
        0x81,
        0xCA,
        2,
        state.local_ssrc & 0xFFFFFFFF,
        0x01,
        0x00,
        0x00,
        0x00,
    )
    return report + sdes


def build_rtcp_pli(state: RtpState) -> bytes:
    return struct.pack(
        ">BBHII",
        0x81,
        0xCE,
        2,
        state.local_ssrc & 0xFFFFFFFF,
        state.remote_ssrc & 0xFFFFFFFF,
    )


def rtp_payload(packet: bytes) -> tuple[int, int, int, bytes] | None:
    if len(packet) < 12 or packet[0] >> 6 != 2:
        return None
    payload_type = packet[1] & 0x7F
    if 64 <= payload_type <= 95:
        return None
    marker = packet[1] >> 7
    sequence = int.from_bytes(packet[2:4], "big")
    timestamp = int.from_bytes(packet[4:8], "big")
    header_size = 12 + (packet[0] & 0x0F) * 4
    if header_size > len(packet):
        return None
    if packet[0] & 0x10:
        if header_size + 4 > len(packet):
            return None
        extension_words = int.from_bytes(packet[header_size + 2 : header_size + 4], "big")
        header_size += 4 + extension_words * 4
    if header_size > len(packet):
        return None
    return marker, sequence, timestamp, packet[header_size:]


def timestamp_ns(timestamp: int, first_timestamp: int | None, clock_rate: int) -> tuple[int, int]:
    if first_timestamp is None:
        first_timestamp = timestamp
    delta = (timestamp - first_timestamp) & 0xFFFFFFFF
    return first_timestamp, delta * 1_000_000_000 // clock_rate


async def send_rtcp(transport: Any, state: RtpState, allow_empty: bool) -> None:
    while True:
        await asyncio.sleep(1.0)
        if state.destination is None or (not allow_empty and state.packets_received == 0):
            continue
        await transport.sendto(build_rtcp_report(state), *state.destination)


async def video_loop(transport: Any, state: RtpState, writer: BinaryWriter) -> None:
    from pymobiledevice3.remote.core_device.screen_stream import depacketize_hevc

    first_timestamp: int | None = None
    last_sequence: int | None = None
    fu_buffer = bytearray()
    access_unit: list[bytes] = []
    access_unit_is_key = False
    access_unit_corrupt = False

    while True:
        packet = await transport.recv(65535)
        parsed = rtp_payload(packet)
        if parsed is None:
            continue
        marker, sequence, timestamp, payload = parsed
        state.observe(sequence)
        if last_sequence is not None and sequence != ((last_sequence + 1) & 0xFFFF):
            fu_buffer.clear()
            access_unit_corrupt = True
        if last_sequence is None or ((sequence - last_sequence) & 0xFFFF) < 0x8000:
            last_sequence = sequence

        nals: list[bytes] = []
        depacketize_hevc(payload, fu_buffer, nals)
        for nal in nals:
            if not nal:
                continue
            access_unit.append(nal)
            if ((nal[0] >> 1) & 0x3F) in HEVC_KEY_TYPES:
                access_unit_is_key = True

        if not marker:
            continue
        if access_unit_corrupt:
            if state.destination and state.local_ssrc and state.remote_ssrc:
                with contextlib.suppress(OSError):
                    await transport.sendto(build_rtcp_pli(state), *state.destination)
        elif access_unit:
            first_timestamp, pts_ns = timestamp_ns(timestamp, first_timestamp, 24000)
            annex_b = b"".join(ANNEX_B_START_CODE + nal for nal in access_unit)
            await writer.write(
                FRAME_VIDEO_KEY if access_unit_is_key else FRAME_VIDEO_DELTA,
                pts_ns,
                annex_b,
            )
        access_unit = []
        access_unit_is_key = False
        access_unit_corrupt = False


async def audio_loop(transport: Any, state: RtpState, writer: BinaryWriter) -> None:
    first_timestamp: int | None = None
    while True:
        packet = await transport.recv(65535)
        parsed = rtp_payload(packet)
        if parsed is None:
            continue
        _, sequence, timestamp, payload = parsed
        state.observe(sequence)
        if not payload:
            continue
        first_timestamp, pts_ns = timestamp_ns(timestamp, first_timestamp, 48000)
        await writer.write(FRAME_AUDIO_AAC_ELD, pts_ns, payload)


async def prepare_device() -> tuple[str, str]:
    from pymobiledevice3.lockdown import create_using_usbmux
    from pymobiledevice3.services.amfi import AmfiService

    emit_status("connecting", "Connecting through Apple Mobile Device Service")
    try:
        async with await create_using_usbmux(connection_type="USB") as lockdown:
            device_name = lockdown.display_name or "iPad"
            emit_status(
                "device",
                f"Detected {device_name}",
                name=device_name,
                udid=lockdown.udid,
                version=lockdown.product_version,
            )
            major_version = product_major_version(lockdown.product_version)
            if major_version is not None and major_version < 27:
                raise BridgeError(
                    "usb_video_requires_ios27",
                    "Apple enables safe USB video only on iPadOS 27 or later. Switching to AirPlay Wi-Fi.",
                    27,
                )
            if not await lockdown.get_developer_mode_status():
                with contextlib.suppress(Exception):
                    await AmfiService(lockdown).reveal_developer_mode_option_in_ui()
                raise BridgeError(
                    "developer_mode_disabled",
                    "Enable Developer Mode on the iPad: Settings > Privacy & Security > Developer Mode, reboot the iPad, then start USB again.",
                    20,
                )

            emit_status("mounting", "Preparing Apple's Developer Disk Image")
            try:
                from pymobiledevice3.exceptions import AlreadyMountedError
                from pymobiledevice3.services.mobile_image_mounter import auto_mount

                await auto_mount(lockdown)
            except AlreadyMountedError:
                pass
            except Exception as exc:
                text = str(exc).strip() or type(exc).__name__
                raise BridgeError(
                    "developer_image_failed",
                    f"Apple Developer Disk Image setup failed: {text}. Check the Internet connection and try again.",
                    26,
                ) from exc
            return lockdown.udid, device_name
    except BridgeError:
        raise
    except Exception as exc:
        text = str(exc).strip() or type(exc).__name__
        lowered = text.lower()
        if "pair" in lowered or "trust" in lowered:
            raise BridgeError(
                "trust_required",
                "Unlock the iPad, tap Trust This Computer, and reconnect the USB cable.",
                21,
            ) from exc
        if "no device" in lowered or "nodevice" in lowered or "not found" in lowered:
            raise BridgeError("no_device", "No iPad was found through Apple Devices.", 22) from exc
        raise BridgeError("apple_connection_failed", f"Apple USB connection failed: {text}", 23) from exc


def state_from_config(config: dict[str, Any], sender_ip: str) -> RtpState:
    source_port = int(config.get("SourcePort", 0))
    return RtpState(
        local_ssrc=int(config.get("RemoteSSRC", 0)),
        remote_ssrc=int(config.get("LocalSSRC", 0)),
        destination=(sender_ip, source_port) if source_port else None,
    )


async def stream_device(udid: str, device_name: str) -> None:
    from pymobiledevice3.remote.core_device.display_service import DisplayService
    from pymobiledevice3.remote.core_device.screen_stream import open_media_receiver
    from pymobiledevice3.remote.userspace_tunnel import UserspaceRsdTunnel

    writer = BinaryWriter()
    emit_status("tunnel", "Starting safe user-space USB tunnel")
    async with UserspaceRsdTunnel(serial=udid, autopair=True) as rsd:
        sender_ip = rsd.service.address[0]
        session_id = uuid.uuid4()
        video_service = DisplayService(rsd)
        audio_service = DisplayService(rsd)
        video_transport = None
        audio_transport = None
        video_session_id = None
        audio_session_id = None
        try:
            await video_service.connect()
            video_transport, video_ip = open_media_receiver(
                video_service, (8 * 1024 * 1024, 4 * 1024 * 1024)
            )
            video_answer = await video_service.start_video_stream(
                receiver_ip=video_ip,
                receiver_port=video_transport.port,
                sender_ip=sender_ip,
                display_id=1,
                client_session_id=session_id,
                ltrp_enabled=False,
            )
            video_session_id = video_answer["connection"]["options"][
                "avcMediaStreamOptionClientSessionID"
            ]["uuid"]
            video_state = state_from_config(video_answer["connection"].get("streamConfig", {}), sender_ip)

            await audio_service.connect()
            audio_transport, audio_ip = open_media_receiver(
                audio_service, (4 * 1024 * 1024, 1 * 1024 * 1024)
            )
            audio_answer = await audio_service.start_audio_stream(
                receiver_ip=audio_ip,
                receiver_port=audio_transport.port,
                sender_ip=sender_ip,
                client_session_id=session_id,
            )
            audio_session_id = audio_answer["connection"]["options"][
                "avcMediaStreamOptionClientSessionID"
            ]["uuid"]
            audio_state = state_from_config(audio_answer["connection"].get("streamConfig", {}), sender_ip)

            emit_status("streaming", f"USB Gaming Mode active - {device_name}")
            tasks = [
                asyncio.create_task(video_loop(video_transport, video_state, writer)),
                asyncio.create_task(audio_loop(audio_transport, audio_state, writer)),
                asyncio.create_task(send_rtcp(video_transport, video_state, allow_empty=False)),
                asyncio.create_task(send_rtcp(audio_transport, audio_state, allow_empty=True)),
            ]
            done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_EXCEPTION)
            for task in pending:
                task.cancel()
            for task in pending:
                with contextlib.suppress(asyncio.CancelledError):
                    await task
            for task in done:
                task.result()
        finally:
            for service, stream_id in (
                (audio_service, audio_session_id),
                (video_service, video_session_id),
            ):
                if stream_id is not None:
                    if not isinstance(stream_id, uuid.UUID):
                        stream_id = uuid.UUID(str(stream_id))
                    with contextlib.suppress(Exception):
                        await asyncio.wait_for(service.stop_media_stream(stream_id), timeout=2.0)
                with contextlib.suppress(Exception):
                    await asyncio.wait_for(service.close(), timeout=2.0)
            if audio_transport is not None:
                audio_transport.close()
            if video_transport is not None:
                video_transport.close()


async def run_bridge() -> int:
    try:
        udid, device_name = await prepare_device()
        await stream_device(udid, device_name)
    except BridgeError as exc:
        emit_status("error", str(exc), code=exc.code)
        return exc.exit_code
    except asyncio.CancelledError:
        return 0
    except Exception as exc:
        message = str(exc).strip() or type(exc).__name__
        if "remote control requires ios 27.0 or later" in message.lower():
            emit_status(
                "error",
                "Apple enables safe USB video only on iPadOS 27 or later. Switching to AirPlay Wi-Fi.",
                code="usb_video_requires_ios27",
            )
            return 27
        if "displayservice" in message.lower() or "no such service" in message.lower():
            emit_status(
                "error",
                "Apple DisplayService is unavailable. Enable Developer Mode, reconnect the iPad, and start USB again.",
                code="display_service_unavailable",
            )
            return 24
        emit_status("error", f"USB bridge stopped: {message}", code="bridge_failed")
        return 25
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="PadMirror safe Apple USB bridge")
    parser.add_argument("--probe", action="store_true", help="Check imports and Apple USB access")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.probe:
        try:
            import developer_disk_image  # noqa: F401
            import pyimg4  # noqa: F401
            from pymobiledevice3.lockdown import create_using_usbmux  # noqa: F401
            from pymobiledevice3.services.mobile_image_mounter import auto_mount  # noqa: F401
            from pymobiledevice3.remote.userspace_tunnel import UserspaceRsdTunnel  # noqa: F401
        except Exception as exc:
            emit_status("error", f"USB bridge dependency check failed: {exc}", code="dependency_error")
            return 2
        emit_status("ready", "USB bridge dependencies are available")
        return 0
    try:
        return asyncio.run(run_bridge())
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
