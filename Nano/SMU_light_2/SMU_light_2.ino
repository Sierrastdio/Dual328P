/*
 * ============================================================================
 * Arduino Nano #1 - SMU (JIT Assembler with Paging System)
 * ============================================================================
 * 
 * Memory Optimized Version for Arduino Nano (2KB SRAM)
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
#define OP_SETPAGE 0xE0
#define OP_HALT    0xF0

// 메모리 최적화: 256바이트로 축소 (2페이지분)
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

// --- 물리 주소 계산 ---
uint16_t calcPhysicalAddr(uint8_t bank, uint8_t page, uint8_t offset) {
    uint16_t base = (bank == 0) ? 0x0000 : 0x4000;
    uint16_t page_addr = (uint16_t)page * 128 + offset;
    
    if(page_addr >= 0x4000) return 0xFFFF;
    
    return base + page_addr;
}

// --- JIT 어셈블러 (메모리 최적화) ---
void processASM(String line) {
    if (prog_sz >= MAX_PROG) return;
    
    line.trim();
    line.toUpperCase();
    
    int fs = line.indexOf(' ');
    int ss = line.indexOf(' ', fs + 1);
    int ts = line.indexOf(' ', ss + 1);

    if (fs == -1 || ss == -1) return;

    String mn = (ts == -1) ? line.substring(ss + 1) : line.substring(ss + 1, ts);
    String op = (ts == -1) ? "" : line.substring(ts + 1);
    
    mn.trim();
    op.trim();

    uint8_t opc = 0;
    bool hasOp = false;

    // Switch-case 대신 if-else (메모리 절약)
    if      (mn == "LOAD")    { opc = OP_LOAD;    hasOp = true; }
    else if (mn == "ADD")     { opc = OP_ADD;     hasOp = true; }
    else if (mn == "SUB")     { opc = OP_SUB;     hasOp = true; }
    else if (mn == "MUL")     { opc = OP_MUL;     hasOp = true; }
    else if (mn == "AND")     { opc = OP_AND;     hasOp = true; }
    else if (mn == "OR")      { opc = OP_OR;      hasOp = true; }
    else if (mn == "OUT")     { opc = OP_OUT; }
    else if (mn == "SETPAGE") { opc = OP_SETPAGE; hasOp = true; }
    else if (mn == "HALT")    { opc = OP_HALT; }
    else if (mn == "NOP")     { opc = OP_NOP; }
    else return;

    if (hasOp) {
        int val = op.toInt();
        prog_buf[prog_sz++] = opc | (val & 0x0F);
    } else {
        prog_buf[prog_sz++] = opc;
    }
}

void handleCommand() {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd.startsWith("ASM ")) {
        processASM(cmd);
    }
    else if (cmd.startsWith("PAGE ")) {
        current_page = cmd.substring(5).toInt();
        if(current_page > 127) current_page = 0;
    }
    else if (cmd.startsWith("LOAD ")) {
        if(prog_sz == 0) return;
        
        target_bank = cmd.substring(5).toInt();
        if(target_bank > 1) target_bank = 0;
        
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
    else if (cmd == "RUN") {
        digitalWrite(SYS_RESET, HIGH);
        Serial.println(F("RUN"));
    }
    else if (cmd == "RESET") {
        digitalWrite(SYS_RESET, LOW);
        Serial.println(F("HALT"));
    }
    else if (cmd == "CLEAR") {
        prog_sz = 0;
        Serial.println(F("CLR"));
    }
    else if (cmd == "LIST") {
        Serial.print(F("SZ:"));
        Serial.println(prog_sz);
        for(uint16_t i=0; i<prog_sz; i++) {
            if(prog_buf[i] < 16) Serial.print("0");
            Serial.print(prog_buf[i], HEX);
            if(i < prog_sz-1) Serial.print(" ");
        }
        Serial.println();
    }
    else if (cmd == "INFO") {
        Serial.print(F("PG:"));
        Serial.print(current_page);
        Serial.print(F(" BK:"));
        Serial.print(target_bank);
        Serial.print(F(" SZ:"));
        Serial.print(prog_sz);
        Serial.print(F("/"));
        Serial.println(MAX_PROG);
    }
    else if (cmd == "HELP") {
        Serial.println(F("\n=== SMU v2.1 ==="));
        Serial.println(F("ASM <c> <op> [v]"));
        Serial.println(F("PAGE <0-127>"));
        Serial.println(F("LOAD <0-1>"));
        Serial.println(F("RUN / RESET"));
        Serial.println(F("CLEAR / LIST"));
        Serial.println(F("INFO / HELP"));
        Serial.println(F("\nOPS: LOAD ADD"));
        Serial.println(F("SUB MUL AND OR"));
        Serial.println(F("OUT HALT NOP"));
        Serial.println(F("SETPAGE 0-15\n"));
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
    
    Serial.println(F("\nSMU v2.1 Ready"));
    Serial.println(F("Type HELP\n"));
}

void loop() {
    if(Serial.available()) handleCommand();
}

/*
 * ============================================================================
 * 메모리 최적화 내역:
 * - 버퍼 크기: 2048 -> 256 바이트 (8배 감소)
 * - 변수명 단축: mnemonic -> mn, operand -> op
 * - 불필요한 Serial.print 제거
 * - F() 매크로로 문자열 SRAM 절약
 * - 디버그 메시지 최소화
 * 
 * 사용 가능 메모리: 약 1.5KB 여유
 * ============================================================================
 */