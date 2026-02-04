/*
 * ============================================================================
 * Arduino Nano #1 - SMU v4.0 (595 Paging Support)
 * ============================================================================
 * 핀 배치:
 * - D2~D7, A0~A1: 데이터 버스 (D0~D7)
 * - D9: SCK (74HC595-1, 74HC595-2)
 * - D10: RCK (74HC595-1, 74HC595-2)
 * - D11: SER (74HC595-1)
 * - D12: ROM OE
 * - D13: ROM WE
 * - A2: System RESET (Core 1, 2)
 * - A3: 74HC595-1,2 G# (Output Enable)
 * - A4: 28C256 CE# (Chip Enable)
 * 
 * 주요 변경사항:
 * - 코어가 74HC595로 A7~A13 직접 제어
 * - 나노#1은 A0~A13 전체 제어 (프로그램 로딩용)
 * - 페이지 단위: 128바이트 (A0~A6)
 * ============================================================================
 */

#include <avr/io.h>

// 74HC595 제어
const uint8_t HC595_SER  = 11;  // PB3
const uint8_t HC595_SCK  = 9;   // PB1
const uint8_t HC595_RCK  = 10;  // PB2
const uint8_t HC595_G    = A3;  // PC3

// 28C256 제어
const uint8_t ROM_CE  = A4;   // PC4
const uint8_t ROM_OE  = 12;   // PB4
const uint8_t ROM_WE  = 13;   // PB5

// 시스템 제어
const uint8_t SYS_RESET = A2;  // PC2

// 명령어 셋
#define OP_NOP     0x00
#define OP_LOAD    0x10
#define OP_ADD     0x20
#define OP_SUB     0x30
#define OP_MUL     0x40
#define OP_AND     0x50
#define OP_OR      0x60
#define OP_OUT     0x70
#define OP_FETCH   0x80
#define OP_SLOT    0x90
#define OP_PUSH    0xA0
#define OP_POP     0xB0
#define OP_SETPAGE 0xE0
#define OP_HALT    0xF0

#define MAX_PROG 2048
uint8_t prog_buf[MAX_PROG];
uint16_t prog_sz = 0;

uint8_t current_page = 0;

/*
 * ============================================================================
 * 데이터 버스 제어 (PORT 방식)
 * ============================================================================
 * D0~D5: PD2~7
 * D6~D7: PC0~1 (A0, A1)
 */
inline void set_data_output() {
    DDRD |= 0xFC;  // PD2~7 출력
    DDRC |= 0x03;  // PC0~1 출력
}

inline void set_data_input() {
    DDRD &= 0x03;  // PD2~7 입력
    PORTD &= 0x03; // 풀업 비활성화
    DDRC &= 0xFC;  // PC0~1 입력
    PORTC &= 0xFC; // 풀업 비활성화
}

inline void write_data_bus(uint8_t data) {
    PORTD = (PORTD & 0x03) | ((data << 2) & 0xFC);
    PORTC = (PORTC & 0xFC) | (data >> 6);
}

inline uint8_t read_data_bus() {
    return ((PIND & 0xFC) >> 2) | ((PINC & 0x03) << 6);
}

/*
 * ============================================================================
 * 74HC595 제어 매크로
 * ============================================================================
 */
#define HC595_G_ENABLE()    PORTC &= ~(1 << 3)  // A3 = LOW
#define HC595_G_DISABLE()   PORTC |=  (1 << 3)  // A3 = HIGH
#define HC595_RCK_LOW()     PORTB &= ~(1 << 2)  // D10 = LOW
#define HC595_RCK_HIGH()    PORTB |=  (1 << 2)  // D10 = HIGH
#define HC595_SCK_LOW()     PORTB &= ~(1 << 1)  // D9 = LOW
#define HC595_SCK_HIGH()    PORTB |=  (1 << 1)  // D9 = HIGH
#define HC595_SER_LOW()     PORTB &= ~(1 << 3)  // D11 = LOW
#define HC595_SER_HIGH()    PORTB |=  (1 << 3)  // D11 = HIGH

/*
 * ============================================================================
 * ROM 제어 매크로
 * ============================================================================
 */
#define ROM_CE_ENABLE()     PORTC &= ~(1 << 4)  // A4 = LOW
#define ROM_CE_DISABLE()    PORTC |=  (1 << 4)  // A4 = HIGH
#define ROM_OE_ENABLE()     PORTB &= ~(1 << 4)  // D12 = LOW
#define ROM_OE_DISABLE()    PORTB |=  (1 << 4)  // D12 = HIGH
#define ROM_WE_ENABLE()     PORTB &= ~(1 << 5)  // D13 = LOW
#define ROM_WE_DISABLE()    PORTB |=  (1 << 5)  // D13 = HIGH

/*
 * ============================================================================
 * 시스템 제어 매크로
 * ============================================================================
 */
#define RESET_CORES()       PORTC &= ~(1 << 2)  // A2 = LOW
#define RELEASE_CORES()     PORTC |=  (1 << 2)  // A2 = HIGH

/*
 * ============================================================================
 * 74HC595 비트 시프트 (하드웨어 방식)
 * ============================================================================
 */
void shiftOut_fast(uint8_t data) {
    for(uint8_t i = 0; i < 8; i++) {
        if(data & 0x80) {
            HC595_SER_HIGH();
        } else {
            HC595_SER_LOW();
        }
        
        HC595_SCK_HIGH();
        HC595_SCK_LOW();
        
        data <<= 1;
    }
}

/*
 * ============================================================================
 * 74HC595 주소 설정 (A0~A13, 14비트)
 * ============================================================================
 * 595-1: A0~A7 (QA~QH)
 * 595-2: A8~A13 (QB~QG, QA는 N/C)
 */
void setAddr(uint16_t addr) {
    addr &= 0x3FFF;  // 14비트 마스크
    
    HC595_RCK_LOW();
    
    // 상위 바이트 먼저 (A8~A13) - 595-2로 전송
    // A13~A8 순서로 전송, QB~QG 사용
    uint8_t high_byte = (addr >> 7) & 0x7F;  // A8~A13 + 1비트 패딩
    shiftOut_fast(high_byte);
    
    // 하위 바이트 (A0~A7) - 595-1로 전송
    uint8_t low_byte = addr & 0xFF;
    shiftOut_fast(low_byte);
    
    HC595_RCK_HIGH();
    asm volatile("nop\n\t");
    HC595_RCK_LOW();
}

/*
 * ============================================================================
 * ROM 쓰기
 * ============================================================================
 */
void writeROM(uint16_t addr, uint8_t data) {
    // 595 출력 활성화
    HC595_G_ENABLE();
    
    // 주소 설정 (A0~A13)
    setAddr(addr);
    
    // 데이터 버스 출력 모드
    set_data_output();
    write_data_bus(data);
    
    // ROM 쓰기 시퀀스
    ROM_CE_ENABLE();
    ROM_OE_DISABLE();
    ROM_WE_ENABLE();
    
    delayMicroseconds(1);
    
    ROM_WE_DISABLE();
    ROM_CE_DISABLE();
    
    delay(10);  // tWC (Write Cycle Time)
    
    // 데이터 버스 입력 모드로 복귀
    set_data_input();
}

/*
 * ============================================================================
 * 물리 주소 계산
 * ============================================================================
 * 페이지 단위: 128바이트 (코어의 페이지 크기와 동일)
 * 오프셋: 0~127
 */
uint16_t calcPhysicalAddr(uint8_t page, uint8_t offset) {
    // 페이지는 128바이트 단위
    offset &= 0x7F;  // 0~127
    uint16_t addr = ((uint16_t)page << 7) | offset;
    
    // 16KB 초과 체크
    if(addr >= 0x4000) return 0xFFFF;
    return addr;
}

/*
 * ============================================================================
 * 어셈블리 파싱
 * ============================================================================
 */
void processLine(String line) {
    if (prog_sz >= MAX_PROG) {
        Serial.println(F("ERR:OVERFLOW"));
        return;
    }
    
    line.trim();
    line.toUpperCase();
    
    if(line.length() == 0 || line.startsWith(";")) return;
    
    int space = line.indexOf(' ');
    String inst = (space == -1) ? line : line.substring(0, space);
    String operand = (space == -1) ? "" : line.substring(space + 1);
    
    inst.trim();
    operand.trim();

    uint8_t opcode = 0;
    bool hasOperand = false;

    if      (inst == "LOAD")    { opcode = OP_LOAD;    hasOperand = true; }
    else if (inst == "ADD")     { opcode = OP_ADD;     hasOperand = true; }
    else if (inst == "SUB")     { opcode = OP_SUB;     hasOperand = true; }
    else if (inst == "MUL")     { opcode = OP_MUL;     hasOperand = true; }
    else if (inst == "AND")     { opcode = OP_AND;     hasOperand = true; }
    else if (inst == "OR")      { opcode = OP_OR;      hasOperand = true; }
    else if (inst == "OUT")     { opcode = OP_OUT; }
    else if (inst == "FETCH")   { opcode = OP_FETCH;   hasOperand = true; }
    else if (inst == "SLOT")    { opcode = OP_SLOT;    hasOperand = true; }
    else if (inst == "PUSH")    { opcode = OP_PUSH; }
    else if (inst == "POP")     { opcode = OP_POP; }
    else if (inst == "SETPAGE") { opcode = OP_SETPAGE; hasOperand = true; }
    else if (inst == "HALT")    { opcode = OP_HALT; }
    else if (inst == "NOP")     { opcode = OP_NOP; }
    else {
        Serial.print(F("ERR:UNKNOWN:"));
        Serial.println(inst);
        return;
    }

    if (hasOperand) {
        int val = operand.toInt();
        if(val < 0 || val > 15) {
            Serial.println(F("ERR:RANGE"));
            return;
        }
        prog_buf[prog_sz++] = opcode | (val & 0x0F);
    } else {
        prog_buf[prog_sz++] = opcode;
    }
}

/*
 * ============================================================================
 * 명령어 처리
 * ============================================================================
 */
void handleCommand() {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd.startsWith(":w ")) {
        if(prog_sz == 0) {
            Serial.println(F("ERR:EMPTY"));
            return;
        }
        
        int space_idx = cmd.indexOf(' ');
        current_page = cmd.substring(space_idx + 1).toInt();
        
        if(current_page > 127) {  // 128페이지 (0~127)
            Serial.println(F("ERR:PAGE"));
            return;
        }
        
        // 코어 리셋
        RESET_CORES();
        delay(10);
        
        Serial.print(F("WRITE:"));
        Serial.print(prog_sz);
        Serial.print(F("B->P"));
        Serial.println(current_page);
        
        // 프로그램 쓰기 (128바이트 단위 페이지)
        for(uint16_t i = 0; i < prog_sz; i++) {
            uint8_t page_offset = i % 128;  // 페이지 내 오프셋 (0~127)
            uint8_t write_page = current_page + (i / 128);
            
            uint16_t phys_addr = calcPhysicalAddr(write_page, page_offset);
            if(phys_addr == 0xFFFF) {
                Serial.println(F("ERR:OVERFLOW"));
                break;
            }
            
            writeROM(phys_addr, prog_buf[i]);
            
            if((i + 1) % 64 == 0) Serial.print(".");
        }
        
        Serial.println();
        Serial.println(F("OK"));
    }
    else if (cmd == ":run") {
        RELEASE_CORES();
        Serial.println(F("RUN"));
    }
    else if (cmd == ":r") {
        RESET_CORES();
        Serial.println(F("RESET"));
    }
    else if (cmd == ":clear") {
        prog_sz = 0;
        Serial.println(F("CLEARED"));
    }
    else {
        processLine(cmd);
    }
}

void setup() {
    Serial.begin(115200);
    
    // 데이터 버스 초기화
    set_data_input();
    
    // 74HC595 제어 핀 (PB1~3, PC3)
    DDRB |= 0x0E;   // PB1(SCK), PB2(RCK), PB3(SER)
    DDRC |= 0x08;   // PC3(G#)
    
    // ROM 제어 핀 (PB4~5, PC4)
    DDRB |= 0x30;   // PB4(OE), PB5(WE)
    DDRC |= 0x10;   // PC4(CE#)
    
    // 시스템 리셋 핀 (PC2)
    DDRC |= 0x04;   // PC2(RESET)
    
    // 초기 상태
    HC595_G_DISABLE();
    ROM_CE_DISABLE();
    ROM_OE_DISABLE();
    ROM_WE_DISABLE();
    RESET_CORES();
    
    current_page = 0;
    prog_sz = 0;
    
    Serial.println(F("SMU v4.0 (595 Paging)"));
    Serial.println(F(":w <page>, :run, :r, :clear"));
}

void loop() {
    if(Serial.available()) {
        handleCommand();
    }
}

/*
 * ============================================================================
 * 사용 예제
 * ============================================================================
 * 
 * ; 페이지 0에 초기화 코드
 * LOAD 5
 * SLOT 0
 * SETPAGE 1
 * 
 * ; 위 코드를 페이지 0에 쓰기
 * :w 0
 * 
 * ; 페이지 1에 메인 로직
 * FETCH 0
 * ADD 3
 * OUT
 * HALT
 * 
 * ; 위 코드를 페이지 1에 쓰기
 * :w 1
 * 
 * ; 실행
 * :run
 * 
 * ============================================================================
 * 페이지 시스템:
 * - 페이지 크기: 128바이트 (코어와 동일)
 * - 최대 페이지: 128개 (0~127)
 * - 총 용량: 16KB (Bank 0 기준)
 * - Bank 선택: Core1 PC4가 A14 제어
 * ============================================================================
 */