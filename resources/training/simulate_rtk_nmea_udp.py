#!/usr/bin/env python3
"""Send simulated RTK NMEA GGA/RMC sentences to QGroundControl over UDP."""

from __future__ import annotations

import argparse
import datetime as dt
import math
import socket
import sys
import time


EARTH_RADIUS_M = 6378137.0
KNOTS_PER_MPS = 1.9438444924406048


def nmea_checksum(sentence_body: str) -> str:
    checksum = 0
    for char in sentence_body:
        checksum ^= ord(char)
    return f"{checksum:02X}"


def nmea_sentence(sentence_body: str) -> str:
    return f"${sentence_body}*{nmea_checksum(sentence_body)}\r\n"


def decimal_to_nmea(value: float, is_lat: bool) -> tuple[str, str]:
    direction = "N" if is_lat else "E"
    if value < 0:
        direction = "S" if is_lat else "W"

    abs_value = abs(value)
    degrees = int(abs_value)
    minutes = (abs_value - degrees) * 60.0

    if is_lat:
        return f"{degrees:02d}{minutes:011.8f}", direction
    return f"{degrees:03d}{minutes:011.8f}", direction


def meters_to_lat_lon(center_lat: float, center_lon: float, north_m: float, east_m: float) -> tuple[float, float]:
    lat_rad = math.radians(center_lat)
    lat = center_lat + math.degrees(north_m / EARTH_RADIUS_M)
    lon = center_lon + math.degrees(east_m / (EARTH_RADIUS_M * math.cos(lat_rad)))
    return lat, lon


def course_from_velocity(vn: float, ve: float) -> float:
    speed = math.hypot(vn, ve)
    if speed < 0.01:
        return 0.0
    course = math.degrees(math.atan2(ve, vn))
    return course + 360.0 if course < 0.0 else course


def circular_motion(elapsed: float, speed: float, radius: float) -> tuple[float, float, float, float]:
    radius = max(radius, 1.0)
    angular_rate = speed / radius
    theta = angular_rate * elapsed
    north = radius * math.sin(theta)
    east = radius * math.cos(theta)
    vn = speed * math.cos(theta)
    ve = -speed * math.sin(theta)
    return north, east, vn, ve


def linear_motion(elapsed: float, speed: float, heading_deg: float) -> tuple[float, float, float, float]:
    heading_rad = math.radians(heading_deg)
    vn = speed * math.cos(heading_rad)
    ve = speed * math.sin(heading_rad)
    return vn * elapsed, ve * elapsed, vn, ve


def back_and_forth_motion(elapsed: float, speed: float, heading_deg: float, length: float) -> tuple[float, float, float, float]:
    length = max(length, 1.0)
    period = (2.0 * length) / max(speed, 0.01)
    phase = elapsed % period
    if phase <= period / 2.0:
        distance = phase * speed
        direction = 1.0
    else:
        distance = length - ((phase - period / 2.0) * speed)
        direction = -1.0

    heading_rad = math.radians(heading_deg)
    vn_axis = math.cos(heading_rad)
    ve_axis = math.sin(heading_rad)
    north = vn_axis * distance
    east = ve_axis * distance
    vn = vn_axis * speed * direction
    ve = ve_axis * speed * direction
    return north, east, vn, ve


def build_gga(now_utc: dt.datetime, lat: float, lon: float, alt: float, fix_quality: int, sats: int, hdop: float) -> str:
    time_field = now_utc.strftime("%H%M%S") + f".{int(now_utc.microsecond / 10000):02d}"
    lat_field, lat_dir = decimal_to_nmea(lat, True)
    lon_field, lon_dir = decimal_to_nmea(lon, False)
    body = (
        f"GNGGA,{time_field},{lat_field},{lat_dir},{lon_field},{lon_dir},"
        f"{fix_quality},{sats:02d},{hdop:.1f},{alt:.4f},M,-4.0100,M,,"
    )
    return nmea_sentence(body)


def build_rmc(now_utc: dt.datetime, lat: float, lon: float, vn: float, ve: float, mode: str) -> str:
    time_field = now_utc.strftime("%H%M%S") + f".{int(now_utc.microsecond / 10000):02d}"
    date_field = now_utc.strftime("%d%m%y")
    lat_field, lat_dir = decimal_to_nmea(lat, True)
    lon_field, lon_dir = decimal_to_nmea(lon, False)
    speed_knots = math.hypot(vn, ve) * KNOTS_PER_MPS
    course = course_from_velocity(vn, ve)
    body = (
        f"GNRMC,{time_field},A,{lat_field},{lat_dir},{lon_field},{lon_dir},"
        f"{speed_knots:.3f},{course:.1f},{date_field},5.8,W,{mode},S"
    )
    return nmea_sentence(body)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Simulate an RTK mushroom-head target by sending NMEA GGA/RMC to QGC over UDP."
    )
    parser.add_argument("--host", default="127.0.0.1", help="QGC host/IP. Default: 127.0.0.1")
    parser.add_argument("--port", type=int, default=10110, help="QGC NMEA UDP port. Default: 10110")
    parser.add_argument("--lat", type=float, default=31.8511168, help="Start/center latitude in decimal degrees")
    parser.add_argument("--lon", type=float, default=117.2292701, help="Start/center longitude in decimal degrees")
    parser.add_argument("--alt", type=float, default=45.0, help="Target altitude AMSL in meters")
    parser.add_argument("--speed", type=float, default=2.0, help="Target ground speed in m/s")
    parser.add_argument("--rate", type=float, default=5.0, help="NMEA output rate in Hz")
    parser.add_argument(
        "--pattern",
        choices=("circle", "east", "north", "line"),
        default="circle",
        help="Motion pattern. Default: circle",
    )
    parser.add_argument("--radius", type=float, default=30.0, help="Circle radius in meters")
    parser.add_argument("--line-length", type=float, default=80.0, help="Back-and-forth line length in meters")
    parser.add_argument("--fix-quality", type=int, default=4, help="GGA fix quality. 4 means RTK fixed")
    parser.add_argument("--sats", type=int, default=22, help="Satellite count in GGA")
    parser.add_argument("--hdop", type=float, default=0.8, help="HDOP value in GGA")
    parser.add_argument("--rmc-mode", default="A", help="RMC mode indicator. A is widely accepted by parsers")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.rate <= 0.0:
        print("error: --rate must be greater than zero", file=sys.stderr)
        return 2
    if args.speed < 0.0:
        print("error: --speed must not be negative", file=sys.stderr)
        return 2

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    destination = (args.host, args.port)
    interval = 1.0 / args.rate
    start_monotonic = time.monotonic()
    next_print = 0.0

    print(
        "Sending simulated RTK NMEA to "
        f"{args.host}:{args.port}, pattern={args.pattern}, rate={args.rate:.1f}Hz"
    )
    print("Press Ctrl+C to stop.")

    try:
        while True:
            elapsed = time.monotonic() - start_monotonic
            if args.pattern == "circle":
                north, east, vn, ve = circular_motion(elapsed, args.speed, args.radius)
            elif args.pattern == "east":
                north, east, vn, ve = linear_motion(elapsed, args.speed, 90.0)
            elif args.pattern == "north":
                north, east, vn, ve = linear_motion(elapsed, args.speed, 0.0)
            else:
                north, east, vn, ve = back_and_forth_motion(elapsed, args.speed, 90.0, args.line_length)

            lat, lon = meters_to_lat_lon(args.lat, args.lon, north, east)
            now_utc = dt.datetime.now(dt.timezone.utc)
            packet = (
                build_gga(now_utc, lat, lon, args.alt, args.fix_quality, args.sats, args.hdop)
                + build_rmc(now_utc, lat, lon, vn, ve, args.rmc_mode)
            ).encode("ascii")
            sock.sendto(packet, destination)

            if elapsed >= next_print:
                print(
                    f"t={elapsed:6.1f}s lat={lat:.8f} lon={lon:.8f} alt={args.alt:.1f}m "
                    f"speed={math.hypot(vn, ve):.2f}m/s course={course_from_velocity(vn, ve):.1f}deg"
                )
                next_print = elapsed + 1.0

            time.sleep(interval)
    except KeyboardInterrupt:
        print("\nStopped.")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
