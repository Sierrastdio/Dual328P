# 8-bit Dual Core System Hardware Specification (Final Edition)

## 시스템 개요

본 시스템은 2개의 ATmega328P 코어가 ROM의 바이트코드를 병렬 실행하는 프로그래머블 듀얼코어 컴퓨터입니다.

### 주요 구성
- **Core 1, 2**: VM 펌웨어 실행 (ROM 바이트코드 해석)
- **Nano #1**: 어셈블러 & ROM 프로그래머
- **Nano #2**: LCD 디스플레이 모니터
- **PC**: Python 어셈블러 인터페이스

---

## 1. 전원 및 클럭 (Power & Clock)

| 부품 | 핀 번호 | 연결 대상 | 비고 |
|------|---------|-----------|------|
| **VCC** | 공통 | 모든 IC의 VCC 핀 | +5V DC |
| **GND** | 공통 | 모든 IC의 GND 핀 | 0V Ground |
| **Crystal** | 9, 10 | Core 1(9, 10번) & 74HC125 (2, 5번) | 16MHz 발진 및 버퍼 입력 |
| **Capacitor** | - | Crystal의 각 다리와 GND 사이 | 22pF x 2개 (발진 유지용) |
| **Reset** | 1 | Core 1 & Core 2 (1번 공유) | 10kΩ 풀업 저항 (VCC 연결) |
| **74HC125** | 3번 (Out) | Core 1의 9번 (XTAL1) | Core 1용 정제된 클럭 공급 |
| **74HC125** | 6번 (Out) | Core 2의 9번 (XTAL1) | Core 2용 정제된 클럭 공급 |

---

## 2. 컨트롤러 (ATmega328P)

| 구분 | 기능 | 핀 번호 | 연결 대상 |
|------|------|---------|-----------|
| **Core 1** | 데이터 버스 | 4~6, 11~15 (PD2-PD7, PB0~PB1) | Core 1 데이터 버퍼 (A단: 2~9번) |
| **Core 1** | 주소 버스(L) | 16~19, 23~25 (A0-A6) | Core 1 주소 버퍼 (A단: 2~8번) |
| **Core 1** | 통행권 제어 | 27 (PC4) | Core 1의 데이터 버퍼, 주소 버퍼(19번) & 74HC04(1번) |
| **Core 1** | 방향 제어 | 28 (PC5) | 모든 데이터 버퍼 (1번 DIR) |
| **Core 2** | 데이터 버스 | 4~6, 11~15 (PD2-PD7, PB0~PB1) | Core 2 데이터 버퍼 (A단: 2~9번) |
| **Core 2** | 주소 버스(L) | 16~19, 23~25 (A0-A6) | Core 2 주소 버퍼 (A단: 2~8번) |
| **Core 2** | 입력모드 | 27 (PC4) | 본인소유 데이터 버퍼, 주소버퍼의 19번핀, 74HC04의 2번핀 |

---

## 3. 74HC245 & 28C256 연결

### 데이터 버스 연결

| Core 1,2 데이터 버퍼 핀 | 28C256 핀 |
|------------------------|-----------|
| 18 | 11 (D0) |
| 17 | 12 (D1) |
| 16 | 13 (D2) |
| 15 | 15 (D3) |
| 14 | 16 (D4) |
| 13 | 17 (D5) |
| 12 | 18 (D6) |
| 11 | 19 (D7) |

### 주소 버스 연결 (A0~A6만 코어가 제어)

| Core 1,2 주소 버퍼 핀 | 28C256 핀 | 신호명 |
|----------------------|-----------|--------|
| 18 | 10 | A0 |
| 17 | 9 | A1 |
| 16 | 8 | A2 |
| 15 | 7 | A3 |
| 14 | 6 | A4 |
| 13 | 5 | A5 |
| 12 | 4 | A6 |

### A14 뱅크 분할 (하드와이어)

| Core | 28C256 A14 (Pin 27) | 접근 영역 |
|------|---------------------|-----------|
| **Core 1** | **GND (0)** | 0x0000 ~ 0x3FFF (하위 16KB) |
| **Core 2** | **VCC (1)** | 0x4000 ~ 0x7FFF (상위 16KB) |

**중요**: A14 핀을 각 코어의 주소 버퍼 출력에서 **분리**하고, Core 1은 GND에, Core 2는 VCC에 직접 연결합니다.

### 74HC138 디코더 연결

| 74HC138 핀 | 신호 | 연결 대상 |
|-----------|------|-----------|
| 1 (A) | A5 | 주소 버퍼 13번 |
| 2 (B) | A6 | 주소 버퍼 12번 |
| 3 (C) | GND | 고정 (항상 0) |
| 4 (E1) | GND | 활성화 |
| 5 (E2) | GND | 활성화 |
| 6 (E3) | VCC | 활성화 |
| 15 (Y0) | 28C256 CE (Pin 20) | ROM 칩 선택 |
| 14 (Y1) | Arduino Nano #2 D2 | Core 1 Reg A 감지 |
| 11 (Y4) | Arduino Nano #2 D3 | Core 2 Reg A 감지 |

### 기타 연결

- 74HC04 (1번 Out) → core1 (27), core1 데이터 버퍼 (19), core1 주소 버퍼(19)
- 74HC04 (2번 In) → core1 (27)
- 74HC04 (3번 Out) → core2 (27), core2 데이터 버퍼 (19), core2 주소 버퍼(19)

---

## 4. 28C256 제어 핀 설정

| 핀 | 신호 | 연결 | 비고 |
|----|------|------|------|
| 20 | CE (Chip Enable) | 74HC138 Y0 (Pin 15) | Active LOW |
| 22 | OE (Output Enable) | Arduino Nano #1 D12 | Active LOW (나노 제어) |
| 27 | WE (Write Enable) | Arduino Nano #1 D13 | Active LOW (10kΩ 풀업) |

**주의**: 코어 동작 중에는 OE=GND 고정, WE=VCC 고정. 나노 프로그래밍 시에만 제어.

---

## 5. 방향 제어 신호 (DIR - PC5)

- Core 1(Master)만 28번 핀(PC5)을 출력으로 설정하여 방향 제어
- Core 2(Slave)는 28번 핀을 입력으로 설정하여 모니터링만 수행
- 모든 74HC245 데이터 버퍼의 1번 핀(DIR)에 공통 연결

---

# Materials

이 프로젝트에 사용되는 주요 IC 칩 및 전자 부품 목록입니다.

## 1. 주요 IC 칩 (Integrated Circuits)

| 부품 번호 | 부품 명칭 | 수량 | 주요 역할 |
|----------|----------|------|----------|
| **ATmega328P-PU** | 8-bit Microcontroller | **2** | 시스템 메인 코어 (Core 1, Core2) |
| **74HC245** | Octal Bus Transceiver | **5** | 데이터/주소 버퍼 (코어당 2개) + 나노#1 데이터 버퍼 1개 |
| **74HC138** | 3-to-8 Line Decoder | **1** | I/O 레지스터 선택 및 ROM CE 제어 |
| **74HC04** | Hex Inverter | **1** | Core 2 통행권 제어를 위한 신호 반전 |
| **28C256** | 256K (32K x 8) EEPROM | **1** | 프로그램 저장 및 데이터 로드용 ROM |
| **74HC595** | 8-bit Shift Register | **2** | 나노 #1의 A0~A13 주소 확장 (14비트) |
| **74HC125** | Quad Buffer | **1** | 클럭 신호 버퍼링 |

## 2. 제어 및 통신용 보드

| 부품 명칭 | 수량 | 주요 역할 |
|----------|------|----------|
| **Arduino Nano #1** | **1** | ROM 프로그래머 & 어셈블러 |
| **Arduino Nano #2** | **1** | LCD 디스플레이 모니터 |
| **LCD 16x2** | **1** | 실시간 결과 표시 |

## 3. 수동 소자 및 기타 부품

| 부품 명칭 | 사양 | 수량 | 비고 |
|----------|------|------|------|
| **Crystal** | 16MHz | **1** | 두 코어의 클럭 동기화용 |
| **Ceramic Capacitor** | 22pF | **2** | 크리스탈 발진 안정화용 |
| **Ceramic Capacitor** | 0.1uF | **10** | 각 IC 전원 노이즈 제거용 (Bypass) |
| **Electrolytic Cap** | 10uF ~ 47uF | **1** | 전체 회로 전원 평활용 |
| **Resistor** | 10kΩ | **2** | 리셋 라인 풀업용, WE 풀업용 |
| **Push Button** | 2-pin / 4-pin | **1** | 시스템 하드웨어 리셋용 |
| **Breadboard / PCB** | - | **2** | 부품 실장 및 배선용 |

---

# Arduino Nano #1: ROM Programmer & Assembler

## 1. Pin Assignment

| Function | Pin | Signal | Target | Description |
|----------|-----|--------|--------|-------------|
| **Serial COM** | D0 | RX | PC/Laptop | USB Serial Communication |
| | D1 | TX | PC/Laptop | USB Serial Communication |
| **Data Bus** | D2 | D0 | 74HC245 추가 버퍼 A2 | Bus Programming |
| | D3 | D1 | 74HC245 추가 버퍼 A3 | Bus Programming |
| | D4 | D2 | 74HC245 추가 버퍼 A4 | Bus Programming |
| | D5 | D3 | 74HC245 추가 버퍼 A5 | Bus Programming |
| | D6 | D4 | 74HC245 추가 버퍼 A6 | Bus Programming |
| | D7 | D5 | 74HC245 추가 버퍼 A7 | Bus Programming |
| | D8 | D6 | 74HC245 추가 버퍼 A8 | Bus Programming |
| | D9 | D7 | 74HC245 추가 버퍼 A9 | Bus Programming |
| **ROM Control** | D11 | A14 | 28C256 Pin 27 | Bank Selection (0=Core1, 1=Core2) |
| | D12 | OE | 28C256 Pin 22 | Output Enable (Active LOW) |
| | D13 | WE | 28C256 Pin 27 | Write Enable (Active LOW, 10kΩ Pull-up) |
| **Shift Reg.** | A1 | DS | 74HC595 #1 Pin 14 | Serial Data Input |
| (A0~A13) | A2 | SHCP | 74HC595 #1,2 Pin 11 | Shift Clock (Common) |
| | A3 | STCP | 74HC595 #1,2 Pin 12 | Storage Clock / Latch (Common) |
| **System Ctrl** | A0 | RESET | Core 1, 2 Pin 1 | System Halt for Bus Ownership |
| **Buffer Ctrl** | D10 | DIR | 74HC245 추가 버퍼 Pin 1 | Data direction (LOW=A→B) |

## 2. 74HC245 추가 데이터 버퍼 (Nano #1 전용)

이 버퍼는 나노가 ROM을 프로그래밍할 때 사용됩니다.

| 74HC245 핀 | 신호 | 연결 대상 |
|-----------|------|-----------|
| 1 (DIR) | 방향 제어 | Nano #1 D10 (LOW=A→B) |
| 2 (A1) | D0 | Nano #1 D2 |
| 3 (A2) | D1 | Nano #1 D3 |
| 4 (A3) | D2 | Nano #1 D4 |
| 5 (A4) | D3 | Nano #1 D5 |
| 6 (A5) | D4 | Nano #1 D6 |
| 7 (A6) | D5 | Nano #1 D7 |
| 8 (A7) | D6 | Nano #1 D8 |
| 9 (A8) | D7 | Nano #1 D9 |
| 11 (B8) | D7 | 28C256 D7 (19번) |
| 12 (B7) | D6 | 28C256 D6 (18번) |
| 13 (B6) | D5 | 28C256 D5 (17번) |
| 14 (B5) | D4 | 28C256 D4 (16번) |
| 15 (B4) | D3 | 28C256 D3 (15번) |
| 16 (B3) | D2 | 28C256 D2 (13번) |
| 17 (B2) | D1 | 28C256 D1 (12번) |
| 18 (B1) | D0 | 28C256 D0 (11번) |
| 19 (OE) | 버퍼 활성화 | GND (항상 활성) |

**역할**: 나노의 데이터 출력을 ROM 데이터 버스로 전달하며, 코어 버퍼와의 충돌 방지.

## 3. 74HC595 Daisy-Chain Configuration (14-bit Address A0~A13)

### 3.1 First 74HC595 (#1: Lower Address A0 ~ A6)

| Pin | Signal | Target | Description |
|-----|--------|--------|-------------|
| 15 (Q0) | A0 | 28C256 A0 (Pin 10) | Address Bit 0 |
| 1 (Q1) | A1 | 28C256 A1 (Pin 9) | Address Bit 1 |
| 2 (Q2) | A2 | 28C256 A2 (Pin 8) | Address Bit 2 |
| 3 (Q3) | A3 | 28C256 A3 (Pin 7) | Address Bit 3 |
| 4 (Q4) | A4 | 28C256 A4 (Pin 6) | Address Bit 4 |
| 5 (Q5) | A5 | 28C256 A5 (Pin 5) | Address Bit 5 |
| 6 (Q6) | A6 | 28C256 A6 (Pin 4) | Address Bit 6 |
| 7 (Q7) | N/C | - | Not Used (cascaded to #2) |
| 9 | Q7S | **74HC595 #2 Pin 14** | **Serial Data Out (To Next Chip)** |
| 11 | SHCP | Nano #1 A2 | Shift Clock |
| 12 | STCP | Nano #1 A3 | Latch Clock |
| 14 | DS | **Nano #1 A1** | Serial Data Input |
| 10 | MR | VCC | Master Reset (disabled) |
| 13 | OE | GND | Output Enable (always on) |

### 3.2 Second 74HC595 (#2: Upper Address A7 ~ A13)

| Pin | Signal | Target | Description |
|-----|--------|--------|-------------|
| 15 (Q0) | A7 | 28C256 A7 (Pin 3) | Address Bit 7 |
| 1 (Q1) | A8 | 28C256 A8 (Pin 25) | Address Bit 8 |
| 2 (Q2) | A9 | 28C256 A9 (Pin 24) | Address Bit 9 |
| 3 (Q3) | A10 | 28C256 A10 (Pin 21) | Address Bit 10 |
| 4 (Q4) | A11 | 28C256 A11 (Pin 23) | Address Bit 11 |
| 5 (Q5) | A12 | 28C256 A12 (Pin 2) | Address Bit 12 |
| 6 (Q6) | A13 | 28C256 A13 (Pin 26) | Address Bit 13 |
| 7 (Q7) | N/C | - | Not Used |
| 9 | Q7S | N/C | Not Used |
| 11 | SHCP | Nano #1 A2 | Shift Clock |
| 12 | STCP | Nano #1 A3 | Latch Clock |
| 14 | DS | **74HC595 #1 Pin 9** | **Serial Data In (From Prev Chip)** |
| 10 | MR | VCC | Master Reset (disabled) |
| 13 | OE | GND | Output Enable (always on) |

---

# Arduino Nano #2: LCD Display Monitor

## 1. Pin Assignment

| Function | Pin | Signal | Target | Description |
|----------|-----|--------|--------|-------------|
| **Data Bus** | A0 | D0 | 28C256 D0 (Pin 11) | Bus Monitoring (Read-Only) |
| (Read-Only) | A1 | D1 | 28C256 D1 (Pin 12) | Bus Monitoring (Read-Only) |
| | A2 | D2 | 28C256 D2 (Pin 13) | Bus Monitoring (Read-Only) |
| | A3 | D3 | 28C256 D3 (Pin 15) | Bus Monitoring (Read-Only) |
| | D8 | D4 | 28C256 D4 (Pin 16) | Bus Monitoring (Read-Only) |
| | D9 | D5 | 28C256 D5 (Pin 17) | Bus Monitoring (Read-Only) |
| | D10 | D6 | 28C256 D6 (Pin 18) | Bus Monitoring (Read-Only) |
| | D11 | D7 | 28C256 D7 (Pin 19) | Bus Monitoring (Read-Only) |
| **Core Detect** | D2 | Y1 | 74HC138 Pin 14 | Core 1 Reg A Detection |
| | D3 | Y4 | 74HC138 Pin 11 | Core 2 Reg A Detection |
| **LCD (4-bit)** | D12 | RS | LCD RS | Register Select |
| | D13 | EN | LCD EN | Enable |
| | D4 | D4 | LCD D4 | Data 4 |
| | D5 | D5 | LCD D5 | Data 5 |
| | D6 | D6 | LCD D6 | Data 6 |
| | D7 | D7 | LCD D7 | Data 7 |
| | GND | RW | LCD RW | Read/Write (Always Write) |

**주의**: LCD는 4-bit 모드로 연결하며, RW는 GND에 고정합니다.

---

# System Operation Workflow

## 1. Phase 1: Assembly Programming (PC → Nano #1 → ROM)

사용자가 Python으로 어셈블리 프로그램을 작성하여 ROM에 기록하는 단계입니다.

### 1.1 Python에서 어셈블리 작성
```assembly
; program.asm
LOAD 10
ADD 5
MUL 2
OUT
HALT
```

### 1.2 Python 어셈블러가 시리얼로 전송
```
ASM 1 LOAD 10
ASM 1 ADD 5
ASM 1 MUL 2
ASM 1 OUT
ASM 1 HALT
COMPILE
LOAD 0
RUN
```

### 1.3 Nano #1이 바이트코드 변환 & ROM 기록
```
0x00: 0x1A  ; LOAD 10
0x01: 0x25  ; ADD 5
0x02: 0x42  ; MUL 2
0x03: 0x70  ; OUT
0x04: 0xF0  ; HALT
```

## 2. Phase 2: Core Execution (ROM → Cores)

두 코어가 각자의 뱅크에서 VM 바이트코드를 실행합니다.

### 2.1 Fetch-Decode-Execute Cycle
```cpp
while(!halted) {
    instruction = fetch_from_ROM(PC);
    execute(instruction);
    PC++;
}
```

### 2.2 결과 출력
```cpp
// 주소 0x20에 쓰기 → 74HC138 Y1/Y4 활성화
output_register(0x20, regA);
```

## 3. Phase 3: Display Monitoring (Cores → Nano #2 → LCD)

연산 결과를 LCD에 실시간 표시합니다.

### 3.1 신호 감지
```
74HC138 Y1 LOW → Core 1 결과 출력
74HC138 Y4 LOW → Core 2 결과 출력
```

### 3.2 LCD 표시
```
C1:015  C2:030
[====75%====]
```

## 4. Operation Modes Summary

| Mode | Nano #1 | Nano #2 | Cores | Bus Owner |
|------|---------|---------|-------|-----------|
| **Programming** | 어셈블러 동작 | 대기 | Halted | Nano #1 |
| **Execution** | 대기 | 모니터링 | Running | Cores |
| **Display** | 대기 | LCD 출력 | Running | Cores |

---

# I/O Mapped Address (74HC138 Decoder)

74HC138 디코더는 A5, A6만 사용하므로 I/O 주소는 다음과 같이 재매핑됩니다:

| 주소 (7비트) | A6 | A5 | 138 출력 | 기능 | 나노 #2 핀 |
|-------------|----|----|---------|------|-----------|
| **0x00** | 0 | 0 | Y0 | ROM CE | - |
| **0x20** | 0 | 1 | Y1 | Core 1 Reg A | D2 |
| **0x20** | 0 | 1 | Y4 | Core 2 Reg A | D3 |

**주의**: Core 1과 Core 2의 레지스터 주소가 동일(0x20)하지만, A14 뱅크로 물리적으로 분리되어 충돌하지 않습니다.

---

# Instruction Set Architecture

## 명령어 형식 (8-bit)
```
OOOO DDDD
│    │
│    └─ 데이터/피연산자 (4비트, 0~15)
└────── 명령 코드 (4비트)
```

## 명령어 목록

| 코드 | 니모닉 | 설명 | 예시 |
|------|--------|------|------|
| 0x0D | NOP | No Operation | 0x00 |
| 0x1D | LOAD D | A = D | 0x15 (A=5) |
| 0x2D | ADD D | A = A + D | 0x23 (A=A+3) |
| 0x3D | SUB D | A = A - D | 0x32 (A=A-2) |
| 0x4D | MUL D | A = A * D | 0x42 (A=A*2) |
| 0x5D | AND D | A = A & D | 0x5F (A=A&15) |
| 0x6D | OR D | A = A \| D | 0x61 (A=A\|1) |
| 0x70 | OUT | RegA 출력 | 0x70 |
| 0xF0 | HALT | 실행 정지 | 0xF0 |

---

# 시스템 최종 배선 및 사양 확정

## 핵심 설계 사항

- **Programmable VM**: 코어는 고정 펌웨어, 프로그램은 ROM에 저장
- **A14 Bank Division**: 물리적 뱅크 분할로 완벽한 메모리 격리
- **Full 15-bit Addressing**: 74HC595 2개로 A0~A13 완전 제어, D11로 A14 제어
- **Dual Arduino System**: 
  - Nano #1: 프로그래밍 전용
  - Nano #2: 모니터링 전용
- **LCD Real-time Display**: 실행 결과 즉시 표시
- **Python Development Environment**: PC에서 편리한 프로그래밍

## 주요 개선점

1. **74HC245 +1개**: 나노 #1 데이터 버퍼 추가로 버스 충돌 완전 방지
2. **74HC595 2개**: A0~A13 완전 제어로 바이트 단위 프로그래밍 가능
3. **A14 물리 분할**: 코어 간 메모리 충돌 원천 차단
4. **I/O 주소 재매핑**: 0x20으로 단순화
5. **VM 아키텍처**: 펌웨어 한 번 업로드, 프로그램은 ROM에서
6. **Python 개발 환경**: 어셈블리 작성 및 업로드 자동화

## 결론

본 설계는 ATmega328P를 진짜 프로그래머블 CPU로 사용하는 듀얼코어 컴퓨터 시스템입니다. 사용자는 ROM에 프로그램만 작성하면 되며, Python 인터페이스로 편리하게 개발할 수 있습니다.
