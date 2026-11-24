"""Offline, exact-image validation. Never opens a USB/Bluetooth device."""
from __future__ import annotations
import hashlib

FLASH_SIZE = 0x80000
APP_BASE = 0x6000
APP_END = 0x18400
STOCK_SHA = 'dfd443c77f831008aa453ee2c1c9cb6871c36f8947c548e61d4387fba0a9da70'
BOOT_SHA = 'd64d7096c29f0f4ee21981df4b3f738c7895f209cdbdeb81f941838820be340d'

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def backup(data: bytes) -> None:
    if len(data) != FLASH_SIZE:
        raise ValueError('Expected a complete 512 KiB backup.')
    if sha(data[:APP_BASE]) != BOOT_SHA or sha(data[APP_BASE:APP_END]) != STOCK_SHA:
        raise ValueError('Bootloader or stock bank A does not match Wireless 301B v1.09.')
    if data[0x40000:0x52400] != data[APP_BASE:APP_END]:
        raise ValueError('Recovery bank differs from the reviewed stock image.')
    if data[APP_END:0x3f000] != b'\xff' * (0x3f000 - APP_END):
        raise ValueError('Expected erased extension and bond space in the original backup.')
    if any(data[0x3f018:0x3f01c] + data[0x3f020:0x3f024]):
        raise ValueError('Original backup contains vendor update metadata.')
