# 8-bit Dual Core System Hardware Specification (A14 Bank Switch Edition)

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

## 2. 컨트롤러 (ATmega328P)

| 구분 | 기능 | 핀 번호 | 연결 대상 |
|------|------|---------|-----------|
| **Core 1** | 데이터 버스 | 4 ~ 6, 11 ~ 15 (PD2-PD7, PB0~PB1) | Core 1 데이터 버퍼 (A단: 2~9번) |
| **Core 1** | 주소 버스(L) | 16~19, 23~25 (A0-A6) | Core 1 주소 버퍼 (A단: 2~8번) |
| **Core 1** | 통행권 제어 | 27 (PC4) | Core 1의 데이터 버퍼, 주소 버퍼(19번) & 74HC04(1번) |
| **Core 1** | 방향 제어 | 28 (PC5) | 모든 데이터 버퍼 (1번 DIR) |
| **Core 2** | 데이터 버스 | 4 ~ 6, 11 ~ 15 (PD2-PD7, PB0~PB1) | Core 2 데이터 버퍼 (A단: 2~9번) |
| **Core 2** | 주소 버스(L) | 16~19, 23~25 (A0-A6) | Core 2 주소 버퍼 (A단: 2~8번) |
| **Core 2** | 입력모드 | 27 (PC4) | 본인소유 데이터 버퍼, 주소버퍼의 19번핀, 74HC04의 2번핀 |

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
| 14 (Y1) | Arduino Nano D10 | Core 1 Reg A 감지 |
| 11 (Y4) | Arduino Nano A5 | Core 2 Reg A 감지 |

### 기타 연결

- 74HC04 (1번 Out) → core1 (27), core1 데이터 버퍼 (19), core1 주소 버퍼(19)
- 74HC04 (2번 In) → core1 (27)
- 74HC04 (3번 Out) → core2 (27), core2 데이터 버퍼 (19), core2 주소 버퍼(19)

## 4. 28C256 제어 핀 설정

| 핀 | 신호 | 연결 | 비고 |
|----|------|------|------|
| 20 | CE (Chip Enable) | 74HC138 Y0 (Pin 15) | Active LOW |
| 22 | OE (Output Enable) | Arduino Nano D12 | Active LOW (나노 제어) |
| 27 | WE (Write Enable) | Arduino Nano D13 | Active LOW (10kΩ 풀업) |

**주의**: 코어 동작 중에는 OE=GND 고정, WE=VCC 고정. 나노 프로그래밍 시에만 제어.

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
| **74HC245** | Octal Bus Transceiver | **4** | 데이터 및 주소 버퍼 (코어당 2개씩 사용) |
| **74HC138** | 3-to-8 Line Decoder | **1** | I/O 레지스터 선택 및 ROM CE 제어 |
| **74HC04** | Hex Inverter | **1** | Core 2 통행권 제어를 위한 신호 반전 |
| **28C256** | 256K (32K x 8) EEPROM | **1** | 프로그램 저장 및 데이터 로드용 ROM |
| **74HC595** | 8-bit Shift Register | **2** | 나노의 A0~A13 주소 확장 (14비트) |
| **74HC125** | Quad Buffer | **1** | 클럭 신호 버퍼링 |

## 2. 제어 및 통신용 보드

| 부품 명칭 | 수량 | 주요 역할 |
|----------|------|----------|
| **Arduino Nano** | **1** | ROM 프로그래밍 및 레지스터 모니터링 |

## 3. 수동 소자 및 기타 부품

| 부품 명칭 | 사양 | 수량 | 비고 |
|----------|------|------|------|
| **Crystal** | 16MHz | **1** | 두 코어의 클럭 동기화용 |
| **Ceramic Capacitor** | 22pF | **2** | 크리스탈 발진 안정화용 |
| **Ceramic Capacitor** | 0.1uF | **9** | 각 IC 전원 노이즈 제거용 (Bypass) |
| **Electrolytic Cap** | 10uF ~ 47uF | **1** | 전체 회로 전원 평활용 |
| **Resistor** | 10kΩ | **2** | 리셋 라인 풀업용, WE 풀업용 |
| **Push Button** | 2-pin / 4-pin | **1** | 시스템 하드웨어 리셋용 |
| **Breadboard / PCB** | - | **1** | 부품 실장 및 배선용 |

---

# Arduino Nano: I/O & Programmer Node Specification

아래 부분은 시스템 상태 모니터링 및 74HC595 시프트 레지스터를 이용한 28C256 ROM 라이팅 기능을 담당하는 Arduino Nano의 핀 맵을 정의합니다.

## 1. Arduino Nano Pin Assignment

| Function | Pin | Signal | Target | Description |
|----------|-----|--------|--------|-------------|
| **Serial COM** | D0 | RX | PC/Laptop | USB Serial Communication |
| | D1 | TX | PC/Laptop | USB Serial Communication |
| **Data Bus** | D2 | D0 | System Data Bus D0 | Bus Sniffing & Programming |
| | D3 | D1 | System Data Bus D1 | Bus Sniffing & Programming |
| | D4 | D2 | System Data Bus D2 | Bus Sniffing & Programming |
| | D5 | D3 | System Data Bus D3 | Bus Sniffing & Programming |
| | D6 | D4 | System Data Bus D4 | Bus Sniffing & Programming |
| | D7 | D5 | System Data Bus D5 | Bus Sniffing & Programming |
| | D8 | D6 | System Data Bus D6 | Bus Sniffing & Programming |
| | D9 | D7 | System Data Bus D7 | Bus Sniffing & Programming |
| **ROM Control** | D11 | A14 | 28C256 Pin 27 | Bank Selection (0=Core1, 1=Core2) |
| | D12 | OE | 28C256 Pin 22 | Output Enable (Active LOW) |
| | D13 | WE | 28C256 Pin 27 | Write Enable (Active LOW, 10kΩ Pull-up) |
| **Shift Reg.** | A1 | DS | 74HC595 #1 Pin 14 | Serial Data Input |
| (A0~A13) | A2 | SHCP | 74HC595 #1,2 Pin 11 | Shift Clock (Common) |
| | A3 | STCP | 74HC595 #1,2 Pin 12 | Storage Clock / Latch (Common) |
| **System Ctrl** | A0 | RESET | Core 1, 2 Pin 1 | System Halt for Bus Ownership |
| **Status Mon.** | D10 | Y1 | 74HC138 Pin 14 | Core 1 Reg A (0x20) |
| | A5 | Y4 | 74HC138 Pin 11 | Core 2 Reg A (0x20) |

## 2. 74HC595 Daisy-Chain Configuration (14-bit Address A0~A13)

나노의 핀 3개를 사용하여 28C256 ROM의 하위 주소선(A0~A13)을 제어합니다.

### 2.1 First 74HC595 (#1: Lower Address A0 ~ A6)

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
| 11 | SHCP | Nano A2 | Shift Clock |
| 12 | STCP | Nano A3 | Latch Clock |
| 14 | DS | **Nano A1** | Serial Data Input |
| 10 | MR | VCC | Master Reset (disabled) |
| 13 | OE | GND | Output Enable (always on) |

### 2.2 Second 74HC595 (#2: Upper Address A7 ~ A13)

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
| 11 | SHCP | Nano A2 | Shift Clock |
| 12 | STCP | Nano A3 | Latch Clock |
| 14 | DS | **74HC595 #1 Pin 9** | **Serial Data In (From Prev Chip)** |
| 10 | MR | VCC | Master Reset (disabled) |
| 13 | OE | GND | Output Enable (always on) |

**주의**: A14는 74HC595가 아닌 나노의 D11 핀으로 직접 제어합니다.

## 3. Implementation Notes

1. **Hardware Safety**: 28C256의 WE(27번) 핀에는 반드시 **10kΩ 풀업 저항**을 연결하여 전원 인가 시 의도치 않은 쓰기 동작을 방지합니다.

2. **Daisy-Chain**: 데이터는 `#2(MSB)`에서 `#1(LSB)` 순서로 밀어넣거나, 코드 상에서 `shiftOut` 순서를 조정하여 주소 정렬을 맞춥니다.

3. **Address Generation**: 나노가 ROM 프로그래밍 시:
   - A0~A6: 74HC595 #1로 제어
   - A7~A13: 74HC595 #2로 제어
   - A14: D11 핀으로 직접 제어

4. **Bus Conflict**: 나노가 롬에 데이터를 쓸 때는 반드시:
   - **A0(RESET)**를 LOW로 떨어뜨려 코어들을 정지
   - **D12(OE)**를 HIGH로 올려 롬의 출력을 차단

5. **Bank Switching**: 나노가 코어 실행 중 A0~A13을 변경하려면:
   - RESET=LOW (코어 정지)
   - 74HC595로 새로운 A0~A13 값 설정
   - RESET=HIGH (코어 재시작)
   - 코어는 새로운 128바이트 블록에 접근

## 4. ROM Data & Memory Map (Updated)

### 4.1 Memory Segmentation (A14 Bank Division)

32KB 주소 공간을 A14 핀으로 물리적으로 분할합니다.

| 물리 주소 | Bank | Core | 접근 방식 |
|----------|------|------|----------|
| **0x0000 ~ 0x3FFF** | 0 (A14=GND) | Core 1 전용 | 코어: A0~A6 제어, 나노: A0~A13 완전 제어 |
| **0x4000 ~ 0x7FFF** | 1 (A14=VCC) | Core 2 전용 | 코어: A0~A6 제어, 나노: A0~A13 완전 제어 |

### 4.2 코어의 주소 접근 방식

각 코어는 7비트 주소(A0~A6)만 제어하므로, 한 번에 128바이트 블록만 접근 가능합니다.

**예시**: Core 1이 더 많은 데이터에 접근하려면

```
나노 동작:
1. RESET=LOW (Core 1 정지)
2. 74HC595로 A0~A13 = 0x0000 (0x0000~0x007F 블록 선택)
3. RESET=HIGH (Core 1 시작)
4. Core 1은 0x00~0x7F 주소로 0x0000~0x007F 접근

...작업 완료 후...

5. RESET=LOW
6. 74HC595로 A0~A13 = 0x0080 (0x0080~0x00FF 블록 선택)
7. RESET=HIGH
8. Core 1은 0x00~0x7F 주소로 0x0080~0x00FF 접근
```

이 방식으로 전체 16KB에 순차 접근 가능합니다.

### 4.3 I/O Mapped Address (74HC138 Decoder)

74HC138 디코더는 A5, A6만 사용하므로 I/O 주소는 다음과 같이 재매핑됩니다:

| 주소 (7비트) | A6 | A5 | 138 출력 | 기능 | 나노 핀 |
|-------------|----|----|---------|------|---------|
| **0x00** | 0 | 0 | Y0 | ROM CE | - |
| **0x20** | 0 | 1 | Y1 | Core 1 Reg A | D10 |
| **0x20** | 0 | 1 | Y4 | Core 2 Reg A | A5 |

**주의**: Core 1과 Core 2의 레지스터 주소가 동일하지만, A14 뱅크로 물리적으로 분리되어 충돌하지 않습니다.

### 4.4 Test Data Configuration

1. **Core 1 (Master)**: 
   - 0x0000~0x007F: 선형 증가 데이터 [0x00, 0x01, 0x02, ...]
   - 나노가 전체 16KB에 자유롭게 데이터 기록 가능

2. **Core 2 (Slave)**: 
   - 0x4000~0x407F: 제곱 값 LUT [0x00, 0x01, 0x04, 0x09, ...]
   - 나노가 전체 16KB에 자유롭게 데이터 기록 가능

---

# System Operation Workflow

본 문서는 노트북 명령 전달부터 ROM 기록, 듀얼 코어 병렬 연산 및 결과 리포팅까지의 전체 데이터 흐름을 정의합니다.

## 1. Phase 1: ROM Programming (Laptop → Nano → ROM)

사용자가 작성한 바이너리 데이터를 ROM(28C256)의 특정 주소에 기록하는 단계입니다.

1. **Command Input**: 사용자가 노트북 시리얼 모니터에 쓰기 명령을 입력합니다.
   - 예: `W 0x1234 0xFF`

2. **System Halt**: 나노가 **A0(RESET)**를 LOW로 유지하여 Core 1, 2의 동작을 멈추고 버스 점유권을 획득합니다.

3. **Address Setup**:
   - **A0~A13**: 74HC595 #1, #2로 14비트 완전 제어
   - **A14**: D11 핀으로 뱅크 선택 (0=Core1 영역, 1=Core2 영역)

4. **Data Bus Setup**: 나노가 데이터 버스(**D2~D9**)에 기록할 값(0xFF)을 출력합니다.

5. **Write Pulse**: 
   - **D12(OE)**를 HIGH로 올림 (ROM 출력 차단)
   - **D13(WE)** 핀에 LOW 펄스를 인가하여 롬에 데이터를 기록 (약 10ms 대기)

6. **Verification**: 
   - **D12(OE)**를 LOW로 활성화하여 기록된 데이터를 검증
   - 노트북으로 완료 메시지 전송

## 2. Phase 2: Bank Switching for Extended Access

코어가 128바이트 이상의 데이터에 접근해야 할 때, 나노가 뱅크를 전환합니다.

1. **Initial Setup**:
   - 나노가 74HC595로 A0~A13 = 0x0000 설정
   - RESET=HIGH로 코어 시작
   - 코어는 0x0000~0x007F (Core1) 또는 0x4000~0x407F (Core2) 블록 접근

2. **Bank Switch**:
   - 코어가 현재 블록 처리 완료
   - 나노가 RESET=LOW (코어 정지)
   - 74HC595로 A0~A13 = 0x0080 설정 (다음 블록)
   - RESET=HIGH (코어 재시작)
   - 코어는 0x0080~0x00FF (Core1) 또는 0x4080~0x40FF (Core2) 블록 접근

3. **Repeat**: 필요한 만큼 뱅크 전환 반복하여 전체 16KB 접근

## 3. Phase 3: Parallel Execution (ROM → Cores)

두 코어가 각자의 뱅크에서 병렬로 데이터를 읽고 연산을 수행합니다.

1. **Bus Release**: 나노가 **A0(RESET)**를 HIGH로 복구합니다.

2. **Concurrent Access**:
   - **Core 1 (Master)**: A14=0 뱅크(0x0000~)에서 데이터 읽기
   - **Core 2 (Slave)**: A14=1 뱅크(0x4000~)에서 데이터 읽기
   - 물리적으로 분리된 뱅크이므로 **동시 접근 가능**

3. **Bus Arbitration**: 
   - 74HC04와 PC4(Control) 신호로 버퍼 방향 제어
   - Core 1이 마스터로 버스 타이밍 주도

## 4. Phase 4: Status Reporting (Core → Nano → Laptop)

연산 결과를 나노가 감지하여 노트북으로 출력합니다.

1. **I/O Mapped Write**: 
   - Core 1이 결과 출력: 주소 **0x20** (Reg A)
   - Core 2가 결과 출력: 주소 **0x20** (Reg A, 다른 뱅크)

2. **Device Selection**: 
   - **74HC138** 디코더가 A5=1, A6=0을 감지
   - Core 1: **Y1(Pin 14)** 신호를 LOW로 활성화
   - Core 2: **Y4(Pin 11)** 신호를 LOW로 활성화

3. **Bus Sniffing**: 
   - 나노가 **D10(Y1)** 또는 **A5(Y4)**의 LOW 신호 감지
   - 즉시 데이터 버스(D2~D9)의 값을 읽음

4. **Serial Reporting**: 
   - 나노가 읽은 값을 시리얼로 출력
   - 예: `[CORE1] REG A: 0xFD`

## 5. Operation Modes Summary

| Mode | Nano Status | Core Status | Bus Owner | A0~A13 Control |
|------|-------------|-------------|-----------|----------------|
| **Programmer** | Output (WE/Address/Data) | Halted (Hi-Z) | Arduino Nano | 74HC595 x2 |
| **Monitor** | Input (Data Sniffing) | Running | Core 1 & 2 | 고정값 유지 |
| **Bank Switch** | RESET Control | Halted | Arduino Nano | 74HC595 x2 재설정 |

---

# 시스템 최종 배선 및 사양 확정

## 핵심 설계 사항

- **A14 Bank Division**: 물리적 뱅크 분할로 완벽한 메모리 격리
- **Full 15-bit Addressing**: 74HC595 2개로 A0~A13 완전 제어, D11로 A14 제어
- **Data Integrity**: PORTD, PORTB를 데이터 버스로 할당하여 고속 병렬 처리 보장
- **I/O Offloading**: 제3의 모니터 아두이노를 통한 시스템 상태 관제로 코어 부하 제로화
- **Conflict Prevention**: 
  - A14 하드와이어로 물리적 뱅크 격리
  - 74HC04 인버터 기반의 배타적 버스 점유 시스템
- **Dynamic Bank Switching**: 나노가 RESET 제어로 코어의 접근 블록 동적 변경 가능

## 주요 개선점

1. **74HC595 1개 → 2개**: A0~A13 완전 제어로 바이트 단위 프로그래밍 가능
2. **A14 물리 분할**: 코어 간 메모리 충돌 원천 차단
3. **I/O 주소 재매핑**: 0x20으로 단순화
