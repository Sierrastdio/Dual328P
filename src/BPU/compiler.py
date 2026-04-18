'''
=================================
  BPU ASSEMBLY & BASIC COMPILER
=================================

  NOTICE: ONLY COMPATIBLE WITH 'BPU system' (BPUmega.ino, BPUnano.ino)
  THIS CODE SENDS BINARY INSTRUCTIONS DIRECTLY TO THE ARDUINO.
  ONLY THE BPU SYSTEM, WHICH RECEIVES AND EXECUTES BINARY INSTRUCTIONS, IS COMPATIBLE.
'''

import serial
import time
import sys

print("START", flush=True)
sys.stdout.reconfigure(line_buffering=True)

OPCODES = {
    "NOP": 0x00, 
    "LOAD": 0x10, 
    "ADD":  0x20, 
    "SUB": 0x30, 
    "MUL": 0x40,
    "AND": 0x50, 
    "OR":   0x60, 
    "OUT":  0x70, 
    "FETCH": 0x80, 
    "SLOT": 0x90,
    "PUSH": 0xA0, 
    "POP": 0xB0, 
    "SETPAGE": 0xE0, 
    "HALT": 0xF0,
}

# Hardware constants — change here if specs change
MAX_VAL     = 15    # 4-bit max value (0xF)
NIBBLE_MASK = 0x0F  # lower-nibble operand mask
TEMP_SLOT   = 14    # reserved: scratch register
PAGE_SLOT   = 15    # reserved: page register

RESERVED_SLOTS = (TEMP_SLOT, PAGE_SLOT)
BAUD_RATE      = 115200
CHUNK_SIZE     = 64  # Nano/Uno: 64, Mega: 512

# ── helpers ──────────────────────────────────────────────────────────────────

def get_const_asm(val):
    if val <= MAX_VAL:
        return [f"LOAD {val}"]
    q, r = divmod(val, MAX_VAL)
    seq  = ["LOAD 15"] + [f"ADD {MAX_VAL}"] * (q - 1)
    if r:
        seq.append(f"ADD {r}")
    return seq

def not_asm(s):
    return [f"FETCH {s}", f"SLOT {TEMP_SLOT}", f"LOAD {MAX_VAL}", f"SUB {TEMP_SLOT}", f"SLOT {s}"]

# not_asm variant: ACC already holds the value, skips FETCH
def _not_from_acc(s):
    return [f"SLOT {TEMP_SLOT}", f"LOAD {MAX_VAL}", f"SUB {TEMP_SLOT}", f"SLOT {s}"]

def and_asm(s, v):
    return get_const_asm(v) + [f"SLOT {TEMP_SLOT}", f"FETCH {s}", f"AND {TEMP_SLOT}", f"SLOT {s}"]

def or_asm(s, v):
    return get_const_asm(v) + [f"SLOT {TEMP_SLOT}", f"FETCH {s}", f"OR {TEMP_SLOT}", f"SLOT {s}"]

def xor_asm(s, v):
    not_v = MAX_VAL - v
    return (
        get_const_asm(not_v) + [f"SLOT {TEMP_SLOT}", f"FETCH {s}", f"AND {TEMP_SLOT}", "PUSH"] +
        [f"FETCH {s}", f"SLOT {TEMP_SLOT}", f"LOAD {MAX_VAL}", f"SUB {TEMP_SLOT}", f"SLOT {TEMP_SLOT}"] +
        get_const_asm(v) + [f"AND {TEMP_SLOT}", f"SLOT {TEMP_SLOT}", "POP", f"OR {TEMP_SLOT}", f"SLOT {s}"]
    )

def xnor_asm(s, v):
    # Peep-hole: xor_asm ends with `OR TEMP, SLOT s`; not_asm starts with `FETCH s`.
    # Drop the redundant `SLOT s` + `FETCH s` pair by chaining directly into _not_from_acc.
    not_v = MAX_VAL - v
    return (
        get_const_asm(not_v) + [f"SLOT {TEMP_SLOT}", f"FETCH {s}", f"AND {TEMP_SLOT}", "PUSH"] +
        [f"FETCH {s}", f"SLOT {TEMP_SLOT}", f"LOAD {MAX_VAL}", f"SUB {TEMP_SLOT}", f"SLOT {TEMP_SLOT}"] +
        get_const_asm(v) + [f"AND {TEMP_SLOT}", f"SLOT {TEMP_SLOT}", "POP", f"OR {TEMP_SLOT}"] +
        _not_from_acc(s)   # ACC already holds XOR result — no FETCH needed
    )

# ── BASIC parser ─────────────────────────────────────────────────────────────

def _check_reserved(slot, line):
    if slot in RESERVED_SLOTS:
        print(f"[WARNING] Slot {slot} is a reserved slot: {line}")

def parse_basic(line):
    line  = line.strip().upper()
    if not line or line[0] in (";", "'"):
        return []

    parts = line.split()
    cmd   = parts[0]

    if cmd == "LET" and len(parts) >= 4:
        s = int(parts[1]); _check_reserved(s, line)
        return get_const_asm(int(parts[3])) + [f"SLOT {s}"]

    if cmd in ("ADD", "SUB", "MUL", "AND", "OR") and len(parts) >= 3:
        s, v = int(parts[1]), int(parts[2]); _check_reserved(s, line)
        return get_const_asm(v) + [f"SLOT {TEMP_SLOT}", f"FETCH {s}", f"{cmd} {TEMP_SLOT}", f"SLOT {s}"]

    if cmd == "NOT" and len(parts) >= 2:
        s = int(parts[1]); _check_reserved(s, line)
        return not_asm(s)

    if cmd == "NAND" and len(parts) >= 3:
        s, v = int(parts[1]), int(parts[2]); _check_reserved(s, line)
        return and_asm(s, v) + not_asm(s)

    if cmd == "NOR" and len(parts) >= 3:
        s, v = int(parts[1]), int(parts[2]); _check_reserved(s, line)
        return or_asm(s, v) + not_asm(s)

    if cmd == "XOR" and len(parts) >= 3:
        s, v = int(parts[1]), int(parts[2]); _check_reserved(s, line)
        return xor_asm(s, v)

    if cmd == "XNOR" and len(parts) >= 3:
        s, v = int(parts[1]), int(parts[2]); _check_reserved(s, line)
        return xnor_asm(s, v)  # optimized: no redundant SLOT/FETCH pair

    if cmd == "PRINT" and len(parts) >= 2:
        return [f"FETCH {parts[1]}", "OUT"]

    if cmd == "PAGE" and len(parts) >= 2:
        return [f"LOAD {parts[1]}", f"SLOT {PAGE_SLOT}", "SETPAGE"]

    if cmd in OPCODES:
        return [line]

    print(f"[WARNING] Unknown instruction ignored: {line}")
    return []

# ── compiler ─────────────────────────────────────────────────────────────────

def compile_basic_file(filepath):
    binary = bytearray()
    with open(filepath, encoding="utf-8-sig") as f:
        for lineno, line in enumerate(f, 1):
            for asm in parse_basic(line):
                parts = asm.split()
                if parts[0] not in OPCODES:
                    # Fatal: unknown opcode would corrupt binary
                    raise SystemExit(f"[ERROR] Line {lineno}: unknown opcode '{parts[0]}'")
                operand = int(parts[1]) & NIBBLE_MASK if len(parts) > 1 else 0
                binary.append(OPCODES[parts[0]] | operand)
    binary.append(OPCODES["HALT"])
    return binary

# ── uploader ─────────────────────────────────────────────────────────────────

def program_bpu(port, bank, page, binary_data):
    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=5)
        time.sleep(2)
        ser.reset_input_buffer()

        # Reset cores before writing SRAM
        ser.write(b":rst\n"); ser.flush()
        resp = ser.readline().decode().strip()
        if resp != "RST":
            print(f"[warning] unexpected reset response: '{resp}', continuing anyway")

        total = len(binary_data)
        print(f"Transmission started: {total} bytes, {BAUD_RATE} baud, chunk {CHUNK_SIZE}B")

        ser.write(f":wb {bank} {page} {total}\n".encode())
        ser.flush()

        resp = ser.readline().decode().strip()
        if resp != "READY":
            print(f"[error] No READY response: '{resp}'")
            ser.close()
            return

        sent = chunk_idx = 0
        while sent < total:
            chunk = binary_data[sent:sent + CHUNK_SIZE]
            ser.write(chunk); ser.flush()
            sent += len(chunk); chunk_idx += 1
            print(f"  chunk {chunk_idx}: {sent}/{total} bytes ({sent * 100 // total}%)")

            resp = ser.readline().decode().strip()
            if resp == "ACK":
                continue
            elif resp == "OK":
                print(f"[OK] Bank{bank} Page{page} Programming Successful ({total} bytes)")
                break
            else:
                print(f"[error] unexpected response: '{resp}' (sent={sent})")
                break
        else:
            print("[warning] loop ended without receiving 'OK' response")

        ser.close()

    except serial.SerialException as e:
        print(f"['COM' PORT ERROR] {e}")
    except Exception as e:
        print(f"[UNEXPECTED ERROR] {e}")

# ── entry point ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    # Usage: python bpu.py <filepath> <port> <bank> <page> -b
    filepath    = sys.argv[1]
    port        = sys.argv[2]
    bank        = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    page        = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    show_binary = "-b" in sys.argv

    binary = compile_basic_file(filepath)
    if show_binary:
        print("Binary:", " ".join(f"{b:08b}" for b in binary))
    program_bpu(port, bank, page, binary)