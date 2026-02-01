/*
 * ============================================================================
 * Arduino Nano #1 - SMU v2.2 (Final)
 * ============================================================================
 * 
 * Instruction Set:
 * - Arithmetic: LOAD, ADD, SUB, MUL, AND, OR
 * - Slot (16 vars): FETCH, SLOT
 * - Stack (8 deep): PUSH, POP
 * - Control: OUT, SETPAGE, HALT, NOP
 * 
 * ============================================================================
 */

const uint8_t DATA_PINS[] = {2, 3, 4, 5, 6, 7, 8, 9};
const uint8_t BUFFER_DIR = 10;
const uint8_t ROM_A14 = 11, ROM_OE = 12, ROM_WE = 13;
const uint8_t HC595_DS = A1, HC595_SHCP = A2, HC595_STCP = A3;
const uint8_t SYS_RESET = A0;

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

#define MAX_PROG 256
uint8_t prog_buf[MAX_PROG];
uint16_t prog_sz = 0;

uint8_t current_page = 0;
uint8_t target_bank = 0;

// --- 하드웨어 제어 ---
void setAddr(uint16_t addr) {
    digitalWrite(HC595_STCP, LOW);
    shiftOut(HC595_DS, HC595_SHCP, MSBFIRST, (addr >> 7) & 0x7F);
    shiftOut(HC595_DS, HC595_SHCP, MSBFIRST, addr & 0x7F);
    digitalWrite(HC595_STCP, HIGH);
    digitalWrite(HC595_STCP, LOW);
}

void writeROM(uint16_t addr, uint8_t data) {
    digitalWrite(ROM_A14, (addr >> 14) & 0x01);
    setAddr(addr & 0x3FFF);
    
    for(uint8_t i=0; i<8; i++) {
        pinMode(DATA_PINS[i], OUTPUT);
        digitalWrite(DATA_PINS[i], (data>>i)&0x01);
    }
    
    digitalWrite(ROM_OE, HIGH);
    digitalWrite(ROM_WE, LOW);
    delayMicroseconds(1);
    digitalWrite(ROM_WE, HIGH);
    delay(10);
    
    for(uint8_t i=0; i<8; i++) pinMode(DATA_PINS[i], INPUT);
}

uint16_t calcPhysicalAddr(uint8_t bank, uint8_t page, uint8_t offset) {
    uint16_t base = (bank == 0) ? 0x0000 : 0x4000;
    uint16_t page_addr = (uint16_t)page * 128 + offset;
    if(page_addr >= 0x4000) return 0xFFFF;
    return base + page_addr;
}

// --- 어셈블리 파싱 ---
void processLine(String line) {
    if (prog_sz >= MAX_PROG) return;
    
    line.trim();
    line.toUpperCase();
    if(line.length() == 0) return;
    
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
        
        int fs = cmd.indexOf(' ');
        int ss = cmd.indexOf(' ', fs + 1);
        
        target_bank = cmd.substring(fs + 1, ss).toInt();
        current_page = cmd.substring(ss + 1).toInt();
        
        if(target_bank > 1) target_bank = 0;
        if(current_page > 127) current_page = 0;
        
        digitalWrite(SYS_RESET, LOW);
        delay(10);
        
        uint8_t wp = current_page;
        for(uint16_t i=0; i<prog_sz; i++) {
            uint8_t po = i % 128;
            if(i > 0 && po == 0) wp++;
            
            uint16_t pa = calcPhysicalAddr(target_bank, wp, po);
            if(pa != 0xFFFF) writeROM(pa, prog_buf[i]);
        }
        
        Serial.println(F("OK"));
    }
    else if (cmd == ":run") {
        digitalWrite(SYS_RESET, HIGH);
        Serial.println(F("OK"));
    }
    else if (cmd == ":r") {
        digitalWrite(SYS_RESET, LOW);
        Serial.println(F("OK"));
    }
    else if (cmd == ":clear") {
        prog_sz = 0;
        Serial.println(F("OK"));
    }
    else {
        processLine(cmd);
    }
}

void setup() {
    Serial.begin(115200);
    
    for(uint8_t i=0; i<8; i++) pinMode(DATA_PINS[i], INPUT);
    
    pinMode(BUFFER_DIR, OUTPUT);
    pinMode(ROM_A14, OUTPUT);
    pinMode(ROM_OE, OUTPUT);
    pinMode(ROM_WE, OUTPUT);
    pinMode(HC595_DS, OUTPUT);
    pinMode(HC595_SHCP, OUTPUT);
    pinMode(HC595_STCP, OUTPUT);
    pinMode(SYS_RESET, OUTPUT);
    
    digitalWrite(BUFFER_DIR, LOW);
    digitalWrite(ROM_WE, HIGH);
    digitalWrite(ROM_OE, HIGH);
    digitalWrite(SYS_RESET, LOW);
    
    current_page = 0;
    target_bank = 0;
    prog_sz = 0;
}

void loop() {
    if(Serial.available()) handleCommand();
}