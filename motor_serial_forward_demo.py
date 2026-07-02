#!/usr/bin/env python3
"""通过电机串口发送按距离前进命令。"""

import argparse
import os
import select
import sys
import termios
import time


FRAME_HEADER = bytes((0xAA, 0x55))
FRAME_TAIL = 0xFF
MOVE_DISTANCE_COMMAND = 0x25
MOVE_DONE_RESPONSE = bytes((0xAA, 0x55, 0x21, 0xFF))
MOVE_INTERRUPTED_RESPONSE = bytes((0xAA, 0x55, 0x23, 0xFF))

BAUD_RATES = {
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
}


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build_move_frame(distance_mm: int) -> bytes:
    if not 1 <= distance_mm <= 0xFFFF:
        raise ValueError("前进距离必须在 1 到 65535 mm 之间")

    payload = distance_mm.to_bytes(2, byteorder="big")
    body = bytes((MOVE_DISTANCE_COMMAND, 0x00, len(payload))) + payload
    crc = crc16_modbus(body)
    return FRAME_HEADER + body + crc.to_bytes(2, byteorder="big") + bytes((FRAME_TAIL,))


def configure_serial(fd: int, baudrate: int) -> None:
    speed = BAUD_RATES.get(baudrate)
    if speed is None:
        supported = ", ".join(str(value) for value in sorted(BAUD_RATES))
        raise ValueError(f"不支持波特率 {baudrate}，可用值：{supported}")

    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CLOCAL | termios.CREAD | termios.CS8
    attrs[3] = 0
    attrs[4] = speed
    attrs[5] = speed
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)


def write_all(fd: int, data: bytes) -> None:
    offset = 0
    while offset < len(data):
        _, writable, _ = select.select([], [fd], [], 1.0)
        if not writable:
            raise TimeoutError("串口发送超时")
        offset += os.write(fd, data[offset:])
    termios.tcdrain(fd)


def wait_for_response(fd: int, timeout_seconds: float) -> str:
    deadline = time.monotonic() + timeout_seconds
    received = bytearray()

    while time.monotonic() < deadline:
        remaining = max(0.0, deadline - time.monotonic())
        readable, _, _ = select.select([fd], [], [], min(0.2, remaining))
        if not readable:
            continue

        chunk = os.read(fd, 256)
        if not chunk:
            continue
        received.extend(chunk)
        print(f"收到: {chunk.hex(' ').upper()}")

        if MOVE_DONE_RESPONSE in received:
            return "done"
        if MOVE_INTERRUPTED_RESPONSE in received:
            return "interrupted"
        if len(received) > 1024:
            del received[:-16]

    return "timeout"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="向 /dev/ttyS8 发送按距离前进命令")
    parser.add_argument("distance", nargs="?", type=int, help="前进距离，单位 mm")
    parser.add_argument("--port", default="/dev/ttyS8", help="电机串口，默认 /dev/ttyS8")
    parser.add_argument("--baudrate", type=int, default=115200, help="波特率，默认 115200")
    parser.add_argument("--timeout", type=float, default=10.0, help="等待完成回包的秒数")
    parser.add_argument("--dry-run", action="store_true", help="只显示指令帧，不打开串口")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    distance_mm = args.distance
    if distance_mm is None:
        try:
            distance_mm = int(input("请输入前进距离(mm): ").strip())
        except (EOFError, ValueError):
            print("错误：请输入整数距离", file=sys.stderr)
            return 2

    try:
        frame = build_move_frame(distance_mm)
    except ValueError as error:
        print(f"错误：{error}", file=sys.stderr)
        return 2

    print(f"串口: {args.port} @ {args.baudrate} 8N1")
    print(f"目标距离: {distance_mm} mm")
    print(f"发送帧: {frame.hex(' ').upper()}")
    if args.dry_run:
        return 0

    fd = -1
    result = "timeout"
    try:
        fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        configure_serial(fd, args.baudrate)
        termios.tcflush(fd, termios.TCIFLUSH)
        print(f"发送前进 {distance_mm} mm")
        write_all(fd, frame)
        result = wait_for_response(fd, args.timeout)
    except (OSError, TimeoutError, ValueError) as error:
        print(f"串口操作失败：{error}", file=sys.stderr)
        print("请确认串口存在、当前用户有权限，且 main_process 未占用该串口。", file=sys.stderr)
        return 1
    finally:
        if fd >= 0:
            os.close(fd)

    if result == "done":
        print(f"目标运动完成：累计前进 {distance_mm} mm")
        return 0
    if result == "interrupted":
        print("运动被下位机按键中断：收到 AA 55 23 FF", file=sys.stderr)
        return 3

    print(f"等待 {args.timeout:g} 秒未收到运动完成回包", file=sys.stderr)
    return 4


if __name__ == "__main__":
    raise SystemExit(main())
