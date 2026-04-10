import serial
import time
import sys

OPCODES = {
    "NOP": 0x00, "LOAD": 0x10, "ADD": 0x20, "SUB": 0x30, "MUL": 0x40,
    "AND": 0x50, "OR": 0x60, "OUT": 0x70, "FETCH": 0x80, "SLOT": 0x90,
    "PUSH": 0xA0, "POP": 0xB0, "SETPAGE": 0xE0, "HALT": 0xF0
}

TEMP_SLOT = 14   # 연산용 임시 슬롯 (사용자에게 문서화 필요)
PAGE_SLOT = 15   # SETPAGE용 예약 슬롯

def get_const_asm(val):
    """
    레지스터 A만 사용하여 0~255 값 생성
    """
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

def parse_basic(line):
    """
    BASIC 스타일 구문을 어셈블리 명령어로 변환
    """
    line = line.strip().upper()
    if not line or line.startswith(';'):
        return []
    
    parts = line.split()
    cmd = parts[0]

    # [대입] LET <slot> = <val>
    if cmd == "LET":
        if len(parts) >= 4:
            slot = int(parts[1])
            if slot in (TEMP_SLOT, PAGE_SLOT):
                print(f"[경고] 슬롯 {slot}은 예약 슬롯입니다: {line}")
            seq = get_const_asm(int(parts[3]))
            seq.append(f"SLOT {slot}")
            return seq

    # [산술/논리] CMD <slot> <val>
    elif cmd in ["ADD", "SUB", "MUL", "AND", "OR"]:
        if len(parts) >= 3:
            slot = int(parts[1])
            if slot == TEMP_SLOT:
                print(f"[경고] 슬롯 {TEMP_SLOT}은 임시 슬롯으로 예약되어 있습니다: {line}")
            seq = get_const_asm(int(parts[2]))
            seq.append(f"SLOT {TEMP_SLOT}")
            seq.append(f"FETCH {slot}")
            seq.append(f"{cmd} {TEMP_SLOT}")
            seq.append(f"SLOT {slot}")
            return seq

    # [입출력] PRINT <slot>
    elif cmd == "PRINT":
        if len(parts) >= 2:
            return [f"FETCH {parts[1]}", "OUT"]

    # [시스템] PAGE <val>
    elif cmd == "PAGE":
        if len(parts) >= 2:
            return [f"LOAD {parts[1]}", f"SLOT {PAGE_SLOT}", "SETPAGE"]

    # [직접 명령어]
    elif cmd in OPCODES:
        return [line]

    else:
        print(f"[경고] 알 수 없는 명령어 무시됨: {line}")

    return []

def compile_basic_file(filepath):
    binary = bytearray()
    with open(filepath, 'r') as f:
        for lineno, line in enumerate(f, 1):
            asm_lines = parse_basic(line)
            for asm in asm_lines:
                parts = asm.split()
                if parts[0] not in OPCODES:
                    print(f"[오류] {lineno}번째 줄: 알 수 없는 opcode '{parts[0]}'")
                    continue
                opcode = OPCODES[parts[0]]
                operand = int(parts[1]) & 0x0F if len(parts) > 1 else 0
                binary.append(opcode | operand)

    # 프로그램 끝에 HALT 자동 추가
    binary.append(OPCODES["HALT"])
    return binary

def program_bpu(port, bank, page, binary_data):
    try:
        ser = serial.Serial(port, 115200, timeout=2)
        time.sleep(2)  # 아두이노 리셋 대기

        # 헤더 전송
        cmd = f":wb {bank} {page} {len(binary_data)}\n"
        ser.write(cmd.encode())
        ser.flush()

        # 아두이노가 명령 파싱할 시간 확보
        time.sleep(0.1)

        # 바이너리 데이터 전송
        ser.write(binary_data)
        ser.flush()

        # 완료 대기
        response = ser.readline().decode().strip()
        if response == "OK":
            print(f"[완료] Bank{bank} Page{page} 프로그래밍 성공")
        else:
            print(f"[오류] 예상치 못한 응답: '{response}'")

        ser.close()
    except serial.SerialException as e:
        print(f"[직렬포트 오류] {e}")
    except Exception as e:
        print(f"[오류] {e}")

if __name__ == "__main__":
    if len(sys.argv) < 5:
        print("Usage: python3 Interpreter.py <file.bas> <port> <bank> <page>")
        print(f"  슬롯 {TEMP_SLOT}: 연산용 임시 슬롯 (사용 금지)")
        print(f"  슬롯 {PAGE_SLOT}: PAGE 명령 예약 슬롯 (사용 금지)")
        sys.exit(1)

    bas_file = sys.argv[1]
    port     = sys.argv[2]
    bank     = int(sys.argv[3])
    page     = int(sys.argv[4])

    print(f"컴파일 중: {bas_file}")
    binary = compile_basic_file(bas_file)
    print(f"바이너리 크기: {len(binary)} bytes")
    print(f"바이너리: {binary.hex(' ')}")

    program_bpu(port, bank, page, binary)