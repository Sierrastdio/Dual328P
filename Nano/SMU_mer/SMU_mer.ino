/*
 * ============================================================================
 * Arduino Nano #1 - SMU v4.0 (RESET 핀 완전 수정)
 * ============================================================================
 */

#include <avr/io.h>

const uint8_t HC595_SER  = 11;
const uint8_t HC595_SCK  = 9;
const uint8_t HC595_RCK  = 10;
const uint8_t HC595_G    = A3;

const uint8_t ROM_CE  = A4;
const uint8_t ROM_OE  = 12;
const uint8_t ROM_WE  = 13;

const uint8_t SYS_RESET = A2;

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
uint8_t current_page = 0;

/*
 * ============================================================================
 * 데이터 버스 제어 - PC2 완전 보호
 * ============================================================================
 */
inline void set_data_output() {
    DDRD |= 0xFC;
    DDRC |= 0x03;
}

inline void set_data_input() {
    DDRD &= 0x03;
    PORTD &= 0x03;
    DDRC &= 0xFC;  // PC0, PC1만 입력으로
    // PORTC 비트 개별 클리어 (PC2, PC3, PC4 보호)
    PORTC &= ~(1 << 0);  // PC0 = 0
    PORTC &= ~(1 << 1);  // PC1 = 0
    // PC2, PC3, PC4는 건드리지 않음
}

inline void write_data_bus(uint8_t data) {
    PORTD = (PORTD & 0x03) | ((data << 2) & 0xFC);
    // PC0, PC1만 변경
    PORTC = (PORTC & ~0x03) | (data >> 6);
}

/*
 * ============================================================================
 * 74HC595 제어 매크로
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
 * ROM 제어 매크로
 * ============================================================================
 */
#define ROM_CE_ENABLE()     PORTC &= ~(1 << 4)
#define ROM_CE_DISABLE()    PORTC |=  (1 << 4)
#define ROM_OE_ENABLE()     PORTB &= ~(1 << 4)
#define ROM_OE_DISABLE()    PORTB |=  (1 << 4)
#define ROM_WE_ENABLE()     PORTB &= ~(1 << 5)
#define ROM_WE_DISABLE()    PORTB |=  (1 << 5)

/*
 * ============================================================================
 * 시스템 제어 매크로
 * ============================================================================
 */
#define RESET_CORES()       PORTC &= ~(1 << 2)
#define RELEASE_CORES()     PORTC |=  (1 << 2)

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

void setAddr(uint16_t addr) {
    addr &= 0x3FFF;
    
    HC595_RCK_LOW();
    
    uint8_t high_byte = (addr >> 7) & 0x7F;
    shiftOut_fast(high_byte);
    
    uint8_t low_byte = addr & 0xFF;
    shiftOut_fast(low_byte);
    
    HC595_RCK_HIGH();
    asm volatile("nop\n\t");
    HC595_RCK_LOW();
}

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

uint16_t calcPhysicalAddr(uint8_t page, uint8_t offset) {
    offset &= 0x7F;
    uint16_t addr = ((uint16_t)page << 7) | offset;
    
    if(addr >= 0x4000) return 0xFFFF;
    return addr;
}

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

void handleCommand() {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd.startsWith(":w ")) {
        if(prog_sz == 0) {
            Serial.println(F("EMPTY"));
            return;
        }
        
        current_page = cmd.substring(3).toInt();
        
        if(current_page > 127) {
            Serial.println(F("PAGE!"));
            return;
        }
        
        RESET_CORES();
        delay(10);
        
        Serial.print(prog_sz);
        Serial.print(F("B->P"));
        Serial.println(current_page);
        
        for(uint16_t i = 0; i < prog_sz; i++) {
            uint8_t page_offset = i & 0x7F;
            uint8_t write_page = current_page + (i >> 7);
            
            uint16_t phys_addr = calcPhysicalAddr(write_page, page_offset);
            if(phys_addr == 0xFFFF) {
                Serial.println(F("OVER!"));
                break;
            }
            
            writeROM(phys_addr, prog_buf[i]);
            
            if((i & 0x3F) == 0x3F) Serial.print('.');
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
    DDRB |= 0x0E;  // PB1~3 (595)
    DDRB |= 0x30;  // PB4~5 (ROM)
    DDRC |= 0x04;  // PC2 (RESET) - 출력
    DDRC |= 0x08;  // PC3 (595 G#)
    DDRC |= 0x10;  // PC4 (ROM CE#)
    
    // 데이터 버스 입력 (이때 PC2 보호됨)
    set_data_input();
    
    // 초기 상태
    HC595_G_DISABLE();
    ROM_CE_DISABLE();
    ROM_OE_DISABLE();
    ROM_WE_DISABLE();
    
    // 리셋 상태로 시작
    RESET_CORES();
    
    current_page = 0;
    prog_sz = 0;
    
    Serial.println(F("SMU v4.0"));
    Serial.println(F(":w<p>,:run,:r,:clear"));
}

void loop() {
    if(Serial.available()) {
        handleCommand();
    }
}