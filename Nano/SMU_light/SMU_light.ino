/*
 * ============================================================================
 * Arduino Nano #1 - SMU (JIT Assembler, Original Syntax Kept)
 * 데이터는 주소에 순차적으로 저장됨.
 * ============================================================================
 */

const uint8_t DATA_PINS[] = {2, 3, 4, 5, 6, 7, 8, 9};
const uint8_t ROM_A14 = 11, ROM_OE = 12, ROM_WE = 13;
const uint8_t HC595_DS = A1, HC595_SHCP = A2, HC595_STCP = A3;
const uint8_t SYS_RESET = A0;

// 명령어 셋 정의 (처음 주신 값 그대로)
#define OP_NOP  0x00
#define OP_LOAD 0x10
#define OP_ADD  0x20
#define OP_SUB  0x30
#define OP_MUL  0x40
#define OP_AND  0x50
#define OP_OR   0x60
#define OP_OUT  0x70
#define OP_HALT 0xF0

#define MAX_PROG 128
uint8_t prog_buf[MAX_PROG];
uint16_t prog_sz = 0;

// --- 하드웨어 제어 루틴 ---
void setAddr(uint16_t addr) {
    digitalWrite(HC595_STCP, LOW);
    shiftOut(HC595_DS, HC595_SHCP, MSBFIRST, (addr >> 7) & 0x7F);
    shiftOut(HC595_DS, HC595_SHCP, MSBFIRST, addr & 0x7F);
    digitalWrite(HC595_STCP, HIGH);
}

void writeROM(uint16_t addr, uint8_t data) {
    digitalWrite(ROM_A14, (addr >> 14) & 0x01);
    setAddr(addr & 0x3FFF);
    for(int i=0; i<8; i++) { pinMode(DATA_PINS[i], OUTPUT); digitalWrite(DATA_PINS[i], (data>>i)&0x01); }
    digitalWrite(ROM_OE, HIGH); digitalWrite(ROM_WE, LOW);
    delayMicroseconds(1);
    digitalWrite(ROM_WE, HIGH); delay(10); 
    for(int i=0; i<8; i++) pinMode(DATA_PINS[i], INPUT);
}

// --- 원래 문법을 유지하는 즉시 컴파일러 ---
void processASM(String line) {
    if (prog_sz >= MAX_PROG) { Serial.println(F("[ERROR] Buffer Full")); return; }
    
    line.trim();
    line.toUpperCase();
    
    // "ASM <core> <instruction> <operand>" 구조 분해
    int firstSpace = line.indexOf(' ');
    int secondSpace = line.indexOf(' ', firstSpace + 1);
    int thirdSpace = line.indexOf(' ', secondSpace + 1);

    if (firstSpace == -1 || secondSpace == -1) {
        Serial.println(F("[ERROR] Invalid Syntax: ASM <core> <INST> [VAL]"));
        return;
    }

    String mnemonic = (thirdSpace == -1) ? line.substring(secondSpace + 1) : line.substring(secondSpace + 1, thirdSpace);
    String operandStr = (thirdSpace == -1) ? "" : line.substring(thirdSpace + 1);
    mnemonic.trim();
    operandStr.trim();

    uint8_t opcode = 0;
    bool hasOperand = false;

    if      (mnemonic == "LOAD") { opcode = OP_LOAD; hasOperand = true; }
    else if (mnemonic == "ADD")  { opcode = OP_ADD;  hasOperand = true; }
    else if (mnemonic == "SUB")  { opcode = OP_SUB;  hasOperand = true; }
    else if (mnemonic == "MUL")  { opcode = OP_MUL;  hasOperand = true; }
    else if (mnemonic == "AND")  { opcode = OP_AND;  hasOperand = true; }
    else if (mnemonic == "OR")   { opcode = OP_OR;   hasOperand = true; }
    else if (mnemonic == "OUT")  { opcode = OP_OUT; }
    else if (mnemonic == "HALT") { opcode = OP_HALT; }
    else if (mnemonic == "NOP")  { opcode = OP_NOP; }
    else { Serial.print(F("[ERROR] Unknown: ")); Serial.println(mnemonic); return; }

    if (hasOperand) {
        if (operandStr == "") { Serial.println(F("[ERROR] Missing Operand")); return; }
        int val = operandStr.toInt();
        prog_buf[prog_sz++] = opcode | (val & 0x0F);
    } else {
        prog_buf[prog_sz++] = opcode;
    }

    Serial.print(F("[ASM] Line ")); Serial.print(prog_sz);
    Serial.print(F(": ")); Serial.print(mnemonic);
    if(hasOperand) { Serial.print(F(" ")); Serial.print(operandStr); }
    Serial.print(F(" -> 0x")); Serial.println(prog_buf[prog_sz-1], HEX);
}

void handleCommand() {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd.startsWith("ASM ")) {
        processASM(cmd);
    } 
    else if (cmd.startsWith("LOAD ")) {
        uint8_t bank = cmd.substring(5).toInt();
        uint16_t base = (bank == 0) ? 0x0000 : 0x4000;
        digitalWrite(SYS_RESET, LOW);
        for(int i=0; i<prog_sz; i++) writeROM(base + i, prog_buf[i]);
        Serial.println(F("[OK] LOAD COMPLETE"));
    }
    else if (cmd == "RUN")   { digitalWrite(SYS_RESET, HIGH); Serial.println(F(">> SYSTEM START")); }
    else if (cmd == "RESET") { digitalWrite(SYS_RESET, LOW);  Serial.println(F(">> SYSTEM HALTED")); }
    else if (cmd == "CLEAR") { prog_sz = 0; Serial.println(F("Buffer Cleared.")); }
    else if (cmd == "LIST")  {
        Serial.println(F("--- Compiled Bytes ---"));
        for(int i=0; i<prog_sz; i++) { 
            Serial.print(i); Serial.print(F(": 0x")); 
            if(prog_buf[i]<16) Serial.print("0"); Serial.println(prog_buf[i], HEX); 
        }
    }
    else if (cmd == "HELP") {
        Serial.println(F("\n==== Dual-Core SMU HELP ===="));
        Serial.println(F("ASM <core> <INST> <VAL> : Add Code"));
        Serial.println(F("  Ex: ASM 1 LOAD 10"));
        Serial.println(F("LIST        : Show compiled bytes"));
        Serial.println(F("LOAD <bank> : Write to ROM (0=C1, 1=C2)"));
        Serial.println(F("RUN / RESET : Control System"));
        Serial.println(F("CLEAR       : Empty buffer"));
        Serial.println(F("----------------------------"));
        Serial.println(F("INST: LOAD, ADD, SUB, MUL, AND, OR (0-15)"));
        Serial.println(F("INST: OUT, HALT, NOP (No value)"));
    }
}

void setup() {
    Serial.begin(115200);///////////////////////////////////////////////////////////////////////////////////////////////
    for(int i=0; i<8; i++) pinMode(DATA_PINS[i], INPUT);
    pinMode(ROM_A14, OUTPUT); pinMode(ROM_OE, OUTPUT); pinMode(ROM_WE, OUTPUT);
    pinMode(HC595_DS, OUTPUT); pinMode(HC595_SHCP, OUTPUT); pinMode(HC595_STCP, OUTPUT);
    pinMode(SYS_RESET, OUTPUT);
    digitalWrite(ROM_WE, HIGH); digitalWrite(ROM_OE, HIGH); digitalWrite(SYS_RESET, LOW);
    Serial.println(F("Dual-Core SMU Ready. Type HELP."));
}

void loop() { if(Serial.available()) handleCommand(); }