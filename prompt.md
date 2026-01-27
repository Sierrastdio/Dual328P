# 8-bit Dual Core System Hardware Specification (Final Edition)

## 시스템 개요

본 시스템은 2개의 ATmega328P 코어가 ROM의 바이트코드를 병렬 실행하는 프로그래머블 듀얼코어 컴퓨터입니다.

### 주요 구성
- **Core 1, 2**: VM 펌웨어 실행 (ROM 바이트코드 해석)
- **Nano #1**: 어셈블러 & ROM 프로그래머
- **Nano #2**: 주소/데이터 버스 모니터 & LCD 디스플레이
- **PC**: Python 어셈블러 인터페이스

---

## 1. 부품 구성 (Bill of Materials)

### 주요 IC 칩

| 부품 번호 | 수량 | 역할 |
|----------|------|------|
| **ATmega328P-PU** | 2 | 듀얼 코어 프로세서 |
| **28C256 EEPROM** | 1 | 32KB 프로그램 저장소 |
| **74HC245** | 5 | 데이터/주소 버퍼 (코어1: 2개, 코어2: 2개, 나노1: 1개) |
| **74HC595** | 2 | 주소 확장 (A0~A13, 데이지체인) |
| **74HC165** | 2 | 버스 모니터링 (나노2 전용) |
| **74HC138** | 1 | 3-to-8 주소 디코더 (I/O 매핑) |
| **74HC04** | 1 | 인버터 (통행권 신호 반전) |
| **74HC125** | 1 | 클럭 버퍼 (듀얼 코어 동기화) |
| **Arduino Nano** | 2 | 프로그래머(#1), 모니터(#2) |

### 수동 소자

| 부품 | 사양 | 수량 | 용도 |
|------|------|------|------|
| **크리스탈** | 16MHz | 1 | 코어 클럭 |
| **세라믹 커패시터** | 22pF | 2 | 크리스탈 안정화 |
| **세라믹 커패시터** | 0.1uF | 12 | 바이패스 (각 IC) |
| **전해 커패시터** | 10~47uF | 1 | 전원 평활 |
| **저항** | 10kΩ | 2 | 풀업 (RESET, WE) |
| **LCD 16x2** | I2C/4bit | 1 | 실시간 모니터 |
| **푸시 버튼** | - | 1 | 하드웨어 리셋 |

---

## 2. 시스템 아키텍처

### 듀얼 코어 구조

**Core 1 (Master)**:
- 버스 통행권 제어 (PC4)
- 방향 신호 제어 (PC5)
- 메모리: 0x0000~0x3FFF (16KB)
- 역할: VM 실행 + 버스 중재

**Core 2 (Slave)**:
- 통행권 수신 (PC4 입력)
- 메모리: 0x4000~0x7FFF (16KB)
- 역할: VM 실행만

### 메모리 뱅크 분할

**28C256 A14 하드와이어**:
- Core 1: A14 = GND → 0x0000~0x3FFF
- Core 2: A14 = VCC → 0x4000~0x7FFF
- **효과**: 물리적 메모리 격리, 충돌 없음

### 클럭 동기화

**74HC125 버퍼**:
```
16MHz 크리스탈 → 74HC125 → Core 1, 2 XTAL1
```
- 두 코어가 동일한 16MHz로 동작
- 양쪽 22pF 커패시터로 발진 안정화

---

## 3. 버스 시스템

### 데이터 버스 (8-bit)

**74HC245 버퍼 5개 사용**:
1. Core 1 전용 데이터 버퍼
2. Core 1 전용 주소 버퍼
3. Core 2 전용 데이터 버퍼
4. Core 2 전용 주소 버퍼
5. 나노 #1 프로그래밍 버퍼

**방향 제어 (DIR)**:
- Core 1 PC5 → 모든 데이터 버퍼 DIR
- LOW = 코어→ROM, HIGH = ROM→코어

**통행권 제어 (OE)**:
```
Core 1 PC4 → Core 1 버퍼 OE (직접)
            → 74HC04 → Core 2 버퍼 OE (반전)
```

### 주소 버스 (15-bit)

**구성**:
- A0~A6: 코어 직접 제어 (74HC245 통과)
- A7~A13: 74HC595 확장 (나노 전용)
- A14: 하드와이어 뱅크 분할

**74HC595 데이지체인**:
```
나노 A1 → 595#1 (A0~A6) → 595#2 (A7~A13)
         SHCP: 나노 A2
         STCP: 나노 A3
```

---

## 4. I/O 시스템

### 74HC138 주소 디코더

**입력**:
- A, B: 주소 A5, A6
- C: 코어 PC3 (확장용)

**출력 매핑**:

| PC3 | A6 | A5 | 출력 | 주소 | 기능 |
|-----|----|----|------|------|------|
| 0 | 0 | 0 | Y0 | 0x00 | ROM CE |
| 0 | 0 | 1 | Y1 | 0x20 | Core 1 RegA |
| 1 | 0 | 0 | Y4 | 0x08 | Core 2 RegA |

**확장성**: Y2, Y3, Y5, Y6, Y7 예비

---

## 5. 시스템 동작 방식

### Phase 1: 프로그램 작성 (PC → Nano #1)

**사용자 입력**:
```python
# Python에서 어셈블리 작성
program = """
LOAD 10
ADD 5
OUT
HALT
"""

# 시리얼로 전송
send_to_nano(program)
```

**나노 #1 처리**:
1. 시리얼 수신: 어셈블리 코드
2. 바이트코드 변환:
   - LOAD 10 → 0x1A
   - ADD 5 → 0x25
   - OUT → 0x70
   - HALT → 0xF0
3. 코어 리셋 (A0 = LOW)
4. ROM 쓰기:
   - 74HC595로 주소 설정
   - WE 펄스로 기록

### Phase 2: VM 실행 (ROM → Cores)

**Fetch-Decode-Execute Cycle**:
```cpp
void loop() {
    // 1. FETCH
    TAKE_BUS();        // PC4 = HIGH
    set_addr(PC);      // 주소 설정
    instruction = read_bus();
    
    // 2. DECODE
    opcode = instruction & 0xF0;
    operand = instruction & 0x0F;
    
    // 3. EXECUTE
    switch(opcode) {
        case OP_LOAD: regA = operand; break;
        case OP_ADD:  regA += operand; break;
        case OP_OUT:  output_register(0x20, regA); break;
        case OP_HALT: halted = true; break;
    }
    
    PC++;
    GIVE_BUS();       // PC4 = LOW
}
```

**버스 통행권 교환**:
- Core 1이 GIVE_BUS() 호출
- PC4 = LOW → 74HC04 → Core 2 버퍼 활성화
- Core 2가 버스 사용 가능
- 타임슬라이싱으로 공정한 실행

### Phase 3: 모니터링 (Cores → Nano #2)

**74HC165 병렬 입력**:
```
데이터 버스 D0~D7 → 74HC165 #1
주소 버스 A0~A6  → 74HC165 #2
```

**나노 #2 동작**:
```cpp
void loop() {
    // 코어 감지
    if (digitalRead(Y1_PIN) == LOW) {
        // Core 1 출력 중
        uint8_t data = read_74HC165_chain();
        uint8_t addr = read_address();
        
        lcd.setCursor(0, 0);
        lcd.print("C1:");
        lcd.print(addr, HEX);
        lcd.print("=");
        lcd.print(data);
    }
    
    if (digitalRead(Y4_PIN) == LOW) {
        // Core 2 출력 중
        // 동일 처리
    }
}
```

**LCD 표시**:
```
C1:1A=08  C2:05=10
[===Running===]
```

---

## 6. 명령어 세트 (ISA)

### 형식 (8-bit)
```
OOOO DDDD
│    └─ 피연산자 (4비트, 0~15)
└────── 명령코드 (4비트)
```

### 명령어 목록

| 코드 | 니모닉 | 동작 | 예시 |
|------|--------|------|------|
| 0x0D | NOP | 아무것도 안함 | 0x00 |
| 0x1D | LOAD D | A = D | 0x15 (A=5) |
| 0x2D | ADD D | A += D | 0x23 (A+=3) |
| 0x3D | SUB D | A -= D | 0x32 (A-=2) |
| 0x4D | MUL D | A *= D | 0x42 (A*=2) |
| 0x5D | AND D | A &= D | 0x5F |
| 0x6D | OR D | A \|= D | 0x61 |
| 0x70 | OUT | RegA 출력 | 0x70 |
| 0x80 | LOADM | 메모리 읽기 | 0x80 |
| 0xF0 | HALT | 정지 | 0xF0 |

### 프로그램 예시
```assembly
; 주소: 명령어  ; 설명
0x00: 0x15     ; LOAD 5
0x01: 0x23     ; ADD 3
0x02: 0x42     ; MUL 2
0x03: 0x70     ; OUT
0x04: 0xF0     ; HALT

; 결과: (5 + 3) * 2 = 16
```

---

## 7. 시스템 특징

### 완전한 메모리 격리
- A14 하드와이어로 물리적 분리
- 코어 간 메모리 충돌 불가능

### 협력적 멀티태스킹
- GIVE_BUS()로 자발적 양보
- 공정한 CPU 시간 분배

### 실시간 디버깅
- 74HC165로 버스 탭핑
- LCD에 즉시 표시
- 회로 동작에 영향 없음

### 프로그래머블 VM
- 코어는 펌웨어 한 번만 업로드
- 프로그램은 ROM에만 작성
- Python 개발 환경 제공

### 확장 가능한 I/O
- 74HC138로 8개 I/O 공간
- Y2~Y7 예비 출력 활용 가능
- 추가 장치 연결 용이

---

## 8. 동작 시나리오

### 시나리오 1: 단일 코어 프로그램
```python
# Core 1만 사용
core1_program = """
LOAD 10
ADD 5
MUL 2
OUT
HALT
"""
upload(core1_program, bank=1)
```

**결과**: Core 1만 실행, Core 2는 대기

### 시나리오 2: 병렬 연산
```python
# Core 1: 피보나치
core1 = """
LOAD 1
ADD 1
OUT
...
"""

# Core 2: 소수 판정
core2 = """
LOAD 7
...
"""

upload(core1, bank=1)
upload(core2, bank=2)
```

**결과**: 두 코어가 독립 실행, LCD에 동시 표시

---

## 9. 시스템 검증

### 전원 체크
- [ ] 모든 IC VCC = +5V
- [ ] 각 IC에 0.1uF 바이패스

### 클럭 체크
- [ ] 크리스탈 양쪽 22pF
- [ ] 74HC125 출력 확인

### 버스 체크
- [ ] 74HC245 DIR 신호 동작
- [ ] PC4 통행권 전환 확인

### ROM 체크
- [ ] A14 뱅크 분할 확인
- [ ] WE 풀업 저항 확인

### 모니터 체크
- [ ] 74HC165 데이터 수신
- [ ] LCD 표시 정상

---

## 10. 결론

본 시스템은 ATmega328P를 진짜 프로그래머블 CPU로 사용하는 듀얼코어 컴퓨터입니다. 사용자는 Python으로 편리하게 프로그래밍하고, LCD로 실시간 디버깅하며, 두 코어의 병렬 실행을 직접 관찰할 수 있습니다.