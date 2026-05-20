import os
import struct
import binascii
import argparse
import subprocess
import sys

def create_otadata(seq):
    # otadata section is 8KB (2 sectors of 4KB)
    # The first 32-byte struct in the first sector defines the OTA state.
    # struct esp_ota_select_entry_t {
    #     uint32_t ota_seq;
    #     uint8_t mac[20];
    #     uint32_t ota_state;
    #     uint32_t crc;
    # } (32 bytes)
    
    # We set mac to all 0xFF, state to 0xFFFFFFFF (undefined).
    payload = struct.pack('<I', seq) + b'\xff' * 24
    
    # CRC is over the first 4 bytes (ota_seq) only, not the full 28 bytes!
    crc = binascii.crc32(struct.pack('<I', seq), 0xFFFFFFFF) % (1 << 32)
    crc_bytes = struct.pack('<I', crc)
    
    sector = payload + crc_bytes + (b'\xff' * (4096 - 32))
    
    # Second sector is typically 0xFF (empty)
    sector2 = b'\xff' * 4096
    
    return sector + sector2

def main():
    parser = argparse.ArgumentParser(description="Switch ESP32 OTA Boot Partition")
    parser.add_argument('partition', choices=['app0', 'app1', 'clear'], help="Partition to boot from (app0, app1) or clear otadata.")
    parser.add_argument('--port', default='COM4', help="Serial port (e.g. COM4)")
    parser.add_argument('--flash', action='store_true', help="Flash the generated bin to the device")
    
    args = parser.parse_args()
    
    if args.partition == 'clear':
        print(f"Clearing otadata partition... This will default boot to app0 (ota_0).")
        if args.flash:
            cmd = ["esptool", "--port", args.port, "erase_region", "0xE000", "0x2000"]
            subprocess.run(cmd, check=True)
            print("Successfully cleared otadata. Device will boot to app0.")
        return

    # app0 is ota_0 (seq=1), app1 is ota_1 (seq=2)
    seq = 1 if args.partition == 'app0' else 2
    
    data = create_otadata(seq)
    filename = f"otadata_boot_{args.partition}.bin"
    
    with open(filename, 'wb') as f:
        f.write(data)
        
    print(f"Generated {filename} successfully.")
    
    if args.flash:
        print(f"Flashing {filename} to 0xE000...")
        cmd = ["esptool", "--port", args.port, "write_flash", "0xE000", filename]
        subprocess.run(cmd, check=True)
        print(f"Successfully switched boot partition to {args.partition}.")

if __name__ == '__main__':
    main()
