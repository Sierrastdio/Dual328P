'''
'============================================================================================================
'   BPU ASSEMBLY & BASIC COMPILER
'============================================================================================================
'
'   WARNING: THIS IS ONLY COMPATIBLE WITH 'BPU system' (BPUmega.ino, BPUnano.ino)
'   BECAUSE THIS CODE SENDS THE 'BINARY INSTRUCTION' DIRECTLY TO THE ARDUINO,
'   THEREFORE ONLY THE BPU SYSTEM, WHICH RECEIVES THE 'BINARY INSTRUCTION' AND EXECUTES THEM, IS COMPATIBLE.
'
'
'''


import serial
import time
import sys
print("START", flush=True)
sys.stdout.reconfigure(line_buffering=True)

OPCODES = {
    "NOP": 0x00, 
    "LOAD": 0x10, 
    "ADD": 0x20, 
    "SUB": 0x30, 
    "MUL": 0x40,
    "AND": 0x50, 
    "OR": 0x60, 
    "OUT": 0x70, 
    "FETCH": 0x80, 
    "SLOT": 0x90,
    "PUSH": 0xA0, 
    "POP": 0xB0, 
    "SETPAGE": 0xE0, 
    "HALT": 0xF0
}

TEMP_SLOT = 14
PAGE_SLOT = 15
RESERVED_SLOTS = (TEMP_SLOT, PAGE_SLOT)

BAUD_RATE  = 115200   # 115200
CHUNK_SIZE = 64      # Nano/Uno는 64, Mega는 512

def get_const_asm(val):
    if val == 0:
        return ["LOAD 0"]
    if val <= 15:
        return [f"LOAD {val}"]
    lines = []
    q = val // 15
    r = val % 15
    lines.append("LOAD 15")
    for _ in range(q - 1):
        lines.append("ADD 15")
    if r > 0:
        lines.append(f"ADD {r}")
    return lines

def not_asm(slot):
    return [
        f"FETCH {slot}",
        f"SLOT {TEMP_SLOT}",
        "LOAD 15",
        f"SUB {TEMP_SLOT}",
        f"SLOT {slot}",
    ]

def and_asm(slot, val):
    seq  = get_const_asm(val)
    seq += [f"SLOT {TEMP_SLOT}", f"FETCH {slot}", f"AND {TEMP_SLOT}", f"SLOT {slot}"]
    return seq

def or_asm(slot, val):
    seq  = get_const_asm(val)
    seq += [f"SLOT {TEMP_SLOT}", f"FETCH {slot}", f"OR {TEMP_SLOT}", f"SLOT {slot}"]
    return seq

def xor_asm(slot, val):
    not_val = 15 - val
    seq = []
    seq += get_const_asm(not_val)
    seq += [f"SLOT {TEMP_SLOT}", f"FETCH {slot}", f"AND {TEMP_SLOT}"]
    seq += ["PUSH"]
    seq += [f"FETCH {slot}", f"SLOT {TEMP_SLOT}", "LOAD 15", f"SUB {TEMP_SLOT}", f"SLOT {TEMP_SLOT}"]
    seq += get_const_asm(val)
    seq += [f"AND {TEMP_SLOT}", f"SLOT {TEMP_SLOT}"]
    seq += ["POP", f"OR {TEMP_SLOT}", f"SLOT {slot}"]
    return seq

def parse_basic(line):
    line = line.strip().upper()
    if not line or line.startswith(';') or line.startswith("'"): return []

    parts = line.split()
    cmd   = parts[0]

    if cmd == "LET":
        if len(parts) >= 4:
            slot = int(parts[1])
            if slot in RESERVED_SLOTS:
                print(f"[경고] 슬롯 {slot}은 예약 슬롯입니다: {line}")
            seq = get_const_asm(int(parts[3]))
            seq.append(f"SLOT {slot}")
            return seq

    elif cmd in ["ADD", "SUB", "MUL", "AND", "OR"]:
        if len(parts) >= 3:
            slot = int(parts[1])
            val  = int(parts[2])
            if slot in RESERVED_SLOTS:
                print(f"[경고] 슬롯 {slot}은 예약 슬롯입니다: {line}")
            seq  = get_const_asm(val)
            seq += [f"SLOT {TEMP_SLOT}", f"FETCH {slot}", f"{cmd} {TEMP_SLOT}", f"SLOT {slot}"]
            return seq
        elif cmd in OPCODES:   # ← BASIC 조건 불만족 시 raw 어셈블리로 폴백
            return [line]

    elif cmd == "NOT":
        if len(parts) >= 2:
            slot = int(parts[1])
            if slot in RESERVED_SLOTS:
                print(f"[경고] 슬롯 {slot}은 예약 슬롯입니다: {line}")
            return not_asm(slot)

    elif cmd == "NAND":
        if len(parts) >= 3:
            slot = int(parts[1])
            val  = int(parts[2])
            if slot in RESERVED_SLOTS:
                print(f"[경고] 슬롯 {slot}은 예약 슬롯입니다: {line}")
            return and_asm(slot, val) + not_asm(slot)

    elif cmd == "NOR":
        if len(parts) >= 3:
            slot = int(parts[1])
            val  = int(parts[2])
            if slot in RESERVED_SLOTS:
                print(f"[경고] 슬롯 {slot}은 예약 슬롯입니다: {line}")
            return or_asm(slot, val) + not_asm(slot)

    elif cmd == "XOR":
        if len(parts) >= 3:
            slot = int(parts[1])
            val  = int(parts[2])
            if slot in RESERVED_SLOTS:
                print(f"[경고] 슬롯 {slot}은 예약 슬롯입니다: {line}")
            return xor_asm(slot, val)

    elif cmd == "XNOR":
        if len(parts) >= 3:
            slot = int(parts[1])
            val  = int(parts[2])
            if slot in RESERVED_SLOTS:
                print(f"[경고] 슬롯 {slot}은 예약 슬롯입니다: {line}")
            return xor_asm(slot, val) + not_asm(slot)

    elif cmd == "PRINT":
        if len(parts) >= 2:
            return [f"FETCH {parts[1]}", "OUT"]

    elif cmd == "PAGE":
        if len(parts) >= 2:
            return [f"LOAD {parts[1]}", f"SLOT {PAGE_SLOT}", "SETPAGE"]

    elif cmd in OPCODES:
        return [line]

    else:
        print(f"[경고] 알 수 없는 명령어 무시됨: {line}")

    return []

def compile_basic_file(filepath):
    binary = bytearray()
    with open(filepath, 'r', encoding='utf-8-sig') as f:
        for lineno, line in enumerate(f, 1):
            asm_lines = parse_basic(line)
            for asm in asm_lines:
                parts = asm.split()
                if parts[0] not in OPCODES:
                    print(f"[오류] {lineno}번째 줄: 알 수 없는 opcode '{parts[0]}'")
                    continue
                opcode  = OPCODES[parts[0]]
                operand = int(parts[1]) & 0x0F if len(parts) > 1 else 0
                binary.append(opcode | operand)

    binary.append(OPCODES["HALT"])
    return binary

def program_bpu(port, bank, page, binary_data):
    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=5)
        time.sleep(2)
        ser.reset_input_buffer()

        total = len(binary_data)
        print(f"전송 시작: {total} bytes, {BAUD_RATE} baud, 청크 {CHUNK_SIZE}B")

        header = f":wb {bank} {page} {total}\n"
        ser.write(header.encode())
        ser.flush()

        resp = ser.readline().decode().strip()
        if resp != "READY":
            print(f"[오류] READY 응답 없음: '{resp}'")
            ser.close()
            return

        sent = 0
        chunk_idx = 0
        while sent < total:
            chunk = binary_data[sent : sent + CHUNK_SIZE]
            ser.write(chunk)
            ser.flush()
            sent += len(chunk)
            chunk_idx += 1

            pct = sent * 100 // total
            print(f"  청크 {chunk_idx}: {sent}/{total} bytes ({pct}%)")  # \r 제거

            resp = ser.readline().decode().strip()
            if resp == "ACK":
                continue
            elif resp == "OK":
                print(f"[완료] Bank{bank} Page{page} 프로그래밍 성공 ({total} bytes)")
                break
            else:
                print(f"[오류] 예상치 못한 응답: '{resp}' (sent={sent})")
                break
        else:
            print("[경고] 루프 종료 후 OK 미수신")

        ser.close()

    except serial.SerialException as e:
        print(f"[직렬포트 오류] {e}")
    except Exception as e:
        print(f"[오류] {e}")

if __name__ == "__main__":
    # 사용법: python bpu.py <파일경로> <포트> <뱅크> <페이지> -b
    filepath   = sys.argv[1]
    port       = sys.argv[2]
    bank       = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    page       = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    show_binary = "-b" in sys.argv

    binary = compile_basic_file(filepath)
    if show_binary:
        print("바이너리:", " ".join(f"{b:08b}" for b in binary))
    program_bpu(port, bank, page, binary)