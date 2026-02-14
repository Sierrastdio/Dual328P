/*
 * ============================================================================
 * Arduino Nano #1 - SMU v4.0 (595 Paging + Bank Control)
 * ============================================================================
 * 핀 배치:
 * - D2~D7, A0~A1: 데이터 버스 (D0~D7)
 * - D9: SCK (74HC595-SMU1, 74HC595-SMU2)
 * - D10: RCK (74HC595-SMU1, 74HC595-SMU2)
 * - D11: SER (74HC595-SMU1)
 * - D12: ROM OE
 * - D13: ROM WE
 * - A2: System RESET (Core 1, 2)
 * - A3: 74HC595 G# (Output Enable)
 * - A4: 28C256 CE#
 * - A5: 28C256 A14 (Bank Select) ← 추가!
 * 
 * 595 체인:
 * - 595-SMU1: A0~A7 (QA~QH)
 * - 595-SMU2: A8~A13 (QB~QG), QA는 N/C
 * 
 * Bank 시스템:
 * - Bank 0 (A14=0): Core 1 영역 (0x0000~0x3FFF)
 * - Bank 1 (A14=1): Core 2 영역 (0x4000~0x7FFF)
 * 
 * 사용법:
 * - :w <bank> <page>
 *   :w 0 0 → Core 1, Page 0
 *   :w 1 0 → Core 2, Page 0
 * ============================================================================
 */

#include <avr/io.h>

// 핀 정의
const uint8_t HC595_SER  = 11;  // PB3
const uint8_t HC595_SCK  = 9;   // PB1
const uint8_t HC595_RCK  = 10;  // PB2
const uint8_t HC595_G    = A3;  // PC3

const uint8_t ROM_CE   = A4;    // PC4
const uint8_t ROM_OE   = 12;    // PB4
const uint8_t ROM_WE   = 13;    // PB5
const uint8_t ROM_A14  = A5;    // PC5 ← Bank Select

const uint8_t SYS_RESET = A2;   // PC2

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

#define MAX_PROG 512
uint8_t prog_buf[MAX_PROG];
uint16_t prog_sz = 0;

uint8_t target_bank = 0;
uint8_t current_page = 0;

/*
 * ============================================================================
 * 데이터 버스 제어
 * ============================================================================
 */
inline void set_data_output() {
    DDRD |= 0b11111100;
    DDRC |= 0b00000011;
}

inline void set_data_input() {
    DDRD &= 0b00000011;
    PORTD &= 0b00000011;
    DDRC &= 0b11111100;
    PORTC &= ~(1 << 0);
    PORTC &= ~(1 << 1);
}

inline void write_data_bus(uint8_t data) {
    PORTD = (PORTD & 0b00000011) | ((data << 2) & 0b11111100);
    PORTC = (PORTC & ~0b00000011) | (data >> 6);
}

/*
 * ============================================================================
 * 74HC595 제어
 * ============================================================================
 */
#define HC595_G_ENABLE()    PORTC &= ~(1 << 3)
#define HC595_G_DISABLE()   PORTC |=  (1 << 3)
#define HC595_RCK_LOW()     PORTB &= ~(1 << 2)
#define HC595_RCK_HIGH()    PORTB |=  (1 << 2)
#define HC595_SCK_LOW()     PORTB &= ~(1 << 1)
#define HC595_SCK_HIGH()    PORTB |=  (1 << 1)
#define HC595_SER_LOW()     PORTB &= ~(1 << 3)
#define HC595_SER_HIGH()    PORTB |=  (1 << 3)

/*
 * ============================================================================
 * ROM 제어
 * ============================================================================
 */
#define ROM_CE_ENABLE()     PORTC &= ~(1 << 4)
#define ROM_CE_DISABLE()    PORTC |=  (1 << 4)
#define ROM_OE_ENABLE()     PORTB &= ~(1 << 4)
#define ROM_OE_DISABLE()    PORTB |=  (1 << 4)
#define ROM_WE_ENABLE()     PORTB &= ~(1 << 5)
#define ROM_WE_DISABLE()    PORTB |=  (1 << 5)
#define ROM_A14_LOW()       PORTC &= ~(1 << 5)
#define ROM_A14_HIGH()      PORTC |=  (1 << 5)

/*
 * ============================================================================
 * 시스템 제어
 * ============================================================================
 */
#define RESET_CORES()       PORTC &= ~(1 << 2)
#define RELEASE_CORES()     PORTC |=  (1 << 2)

/*
 * ============================================================================
 * 74HC595 시리얼 전송
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
 * 주소 설정 (A0~A13, 14비트)
 * ============================================================================
 * 595-SMU1: A0~A7
 * 595-SMU2: A8~A13 (QA는 N/C, QB~QG 사용)
 */
void setAddr(uint16_t addr) {
    addr &= 0b0011111111111111;  // 14비트
    
    HC595_RCK_LOW();
    
    // 상위 바이트 (A8~A13, 6비트) - 595-SMU2
    // QA=N/C이므로 1비트 패딩 추가
    uint8_t high_byte = (addr >> 7) & 0b01111111;
    shiftOut_fast(high_byte);
    
    // 하위 바이트 (A0~A7) - 595-SMU1
    uint8_t low_byte = addr & 0b11111111;
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
    HC595_G_ENABLE();
    setAddr(addr);
    
    set_data_output();
    write_data_bus(data);
    
    ROM_CE_ENABLE();
    ROM_OE_DISABLE();
    ROM_WE_ENABLE();
    
    delayMicroseconds(1);
    
    ROM_WE_DISABLE();
    ROM_CE_DISABLE();
    
    delay(10);
    
    set_data_input();
}

/*
 * ============================================================================
 * 물리 주소 계산
 * ============================================================================
 */
uint16_t calcPhysicalAddr(uint8_t page, uint8_t offset) {
    offset &= 0b01111111;  // 0~127
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
        Serial.println(F("FULL"));
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
    else return;

    if (hasOperand) {
        int val = operand.toInt();
        if(val < 0 || val > 15) return;
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
            Serial.println(F("EMPTY"));
            return;
        }
        
        // :w <bank> <page> 파싱
        int first_space = cmd.indexOf(' ', 3);
        if(first_space == -1) {
            Serial.println(F("USAGE: :w <bank> <page>"));
            return;
        }
        
        target_bank = cmd.substring(3, first_space).toInt();
        current_page = cmd.substring(first_space + 1).toInt();
        
        if(target_bank > 1) {
            Serial.println(F("BANK! (0-1)"));
            return;
        }
        if(current_page > 127) {
            Serial.println(F("PAGE! (0-127)"));
            return;
        }
        
        // Bank 설정
        if(target_bank == 0) {
            ROM_A14_LOW();   // Bank 0
        } else {
            ROM_A14_HIGH();  // Bank 1
        }
        
        RESET_CORES();
        delay(10);
        
        Serial.print(F("B"));
        Serial.print(target_bank);
        Serial.print(F(":P"));
        Serial.print(current_page);
        Serial.print(F(" "));
        Serial.print(prog_sz);
        Serial.println(F("B"));
        
        // 프로그램 쓰기
        for(uint16_t i = 0; i < prog_sz; i++) {
            uint8_t page_offset = i & 0b01111111;
            uint8_t write_page = current_page + (i >> 7);
            
            uint16_t phys_addr = calcPhysicalAddr(write_page, page_offset);
            if(phys_addr == 0xFFFF) {
                Serial.println(F("\nOVER!"));
                break;
            }
            
            writeROM(phys_addr, prog_buf[i]);
            
            if((i & 0b00111111) == 0b00111111) Serial.print('.');
        }
        
        Serial.println(F("\nOK"));
    }
    else if (cmd == ":run") {
        RELEASE_CORES();
        Serial.println(F("RUN"));
    }
    else if (cmd == ":r") {
        RESET_CORES();
        Serial.println(F("RST"));
    }
    else if (cmd == ":clear") {
        prog_sz = 0;
        Serial.println(F("CLR"));
    }
    else {
        processLine(cmd);
    }
}

void setup() {
    Serial.begin(115200);
    
    // DDR 설정
    DDRB |= 0b00001110;  // PB1~3 (595)
    DDRB |= 0b00110000;  // PB4~5 (ROM OE, WE)
    DDRC |= 0b00000100;  // PC2 (RESET)
    DDRC |= 0b00001000;  // PC3 (595 G#)
    DDRC |= 0b00010000;  // PC4 (ROM CE#)
    DDRC |= 0b00100000;  // PC5 (ROM A14) ← 추가!
    
    set_data_input();
    
    // 초기 상태
    HC595_G_DISABLE();
    ROM_CE_DISABLE();
    ROM_OE_DISABLE();
    ROM_WE_DISABLE();
    ROM_A14_LOW();      // Bank 0 기본
    
    RESET_CORES();
    
    target_bank = 0;
    current_page = 0;
    prog_sz = 0;
    
    Serial.println(F("SMU v4.0 (595)"));
    Serial.println(F(":w <bank> <page>"));
    Serial.println(F(":run, :r, :clear"));
}

void loop() {
    if(Serial.available()) {
        handleCommand();
    }
}