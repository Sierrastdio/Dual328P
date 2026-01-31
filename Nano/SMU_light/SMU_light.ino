/*
 * ============================================================================
 * Arduino Nano #1 - SMU (JIT Assembler with Paging System)
 * ============================================================================
 * 
 * Paging System:
 * - Each core has 16KB bank (16,384 bytes)
 * - Core can directly access 128 bytes (A0~A6)
 * - Paging divides each bank into 128 pages of 128 bytes each
 * - Core 1: Pages 0~127 (0x0000~0x3FFF)
 * - Core 2: Pages 0~127 (0x4000~0x7FFF)
 * 
 * New Instructions:
 * - 0xE0~0xEF: SETPAGE 0~15 (set page register, limited to 0-15 for demo)
 * 
 * ============================================================================
 */

const uint8_t DATA_PINS[] = {2, 3, 4, 5, 6, 7, 8, 9};
const uint8_t BUFFER_DIR = 10;  // 74HC245 방향 제어
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
#define OP_SETPAGE 0xE0  // NEW: Page switching
#define OP_HALT    0xF0

#define MAX_PROG 2048  // 증가: 페이징으로 더 큰 프로그램 가능
uint8_t prog_buf[MAX_PROG];
uint16_t prog_sz = 0;

// 페이징 상태 추적
uint8_t current_page = 0;      // 현재 작성 중인 페이지
uint8_t target_bank = 0;       // 0=Core1, 1=Core2

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
    
    for(int i=0; i<8; i++) {
        pinMode(DATA_PINS[i], OUTPUT);
        digitalWrite(DATA_PINS[i], (data>>i)&0x01);
    }
    
    digitalWrite(ROM_OE, HIGH);
    digitalWrite(ROM_WE, LOW);
    delayMicroseconds(1);
    digitalWrite(ROM_WE, HIGH);
    delay(10);
    
    for(int i=0; i<8; i++) pinMode(DATA_PINS[i], INPUT);
}

// --- 물리 주소 계산 (페이징 적용) ---
uint16_t calcPhysicalAddr(uint8_t bank, uint8_t page, uint8_t offset) {
    // bank: 0=Core1(0x0000~), 1=Core2(0x4000~)
    // page: 0~127 (128 pages per bank)
    // offset: 0~127 (128 bytes per page)
    
    uint16_t base = (bank == 0) ? 0x0000 : 0x4000;
    uint16_t page_addr = (uint16_t)page * 128 + offset;
    
    // 뱅크 범위 체크
    if(page_addr >= 0x4000) {
        Serial.println(F("[ERROR] Page overflow!"));
        return 0xFFFF;  // 에러
    }
    
    return base + page_addr;
}

// --- JIT 어셈블러 ---
void processASM(String line) {
    if (prog_sz >= MAX_PROG) {
        Serial.println(F("[ERROR] Buffer Full"));
        return;
    }
    
    line.trim();
    line.toUpperCase();
    
    int firstSpace = line.indexOf(' ');
    int secondSpace = line.indexOf(' ', firstSpace + 1);
    int thirdSpace = line.indexOf(' ', secondSpace + 1);

    if (firstSpace == -1 || secondSpace == -1) {
        Serial.println(F("[ERROR] Syntax: ASM <core> <INST> [VAL]"));
        return;
    }

    String mnemonic = (thirdSpace == -1) 
        ? line.substring(secondSpace + 1) 
        : line.substring(secondSpace + 1, thirdSpace);
    String operandStr = (thirdSpace == -1) ? "" : line.substring(thirdSpace + 1);
    
    mnemonic.trim();
    operandStr.trim();

    uint8_t opcode = 0;
    bool hasOperand = false;

    if      (mnemonic == "LOAD")    { opcode = OP_LOAD;    hasOperand = true; }
    else if (mnemonic == "ADD")     { opcode = OP_ADD;     hasOperand = true; }
    else if (mnemonic == "SUB")     { opcode = OP_SUB;     hasOperand = true; }
    else if (mnemonic == "MUL")     { opcode = OP_MUL;     hasOperand = true; }
    else if (mnemonic == "AND")     { opcode = OP_AND;     hasOperand = true; }
    else if (mnemonic == "OR")      { opcode = OP_OR;      hasOperand = true; }
    else if (mnemonic == "OUT")     { opcode = OP_OUT; }
    else if (mnemonic == "SETPAGE") { opcode = OP_SETPAGE; hasOperand = true; }
    else if (mnemonic == "HALT")    { opcode = OP_HALT; }
    else if (mnemonic == "NOP")     { opcode = OP_NOP; }
    else {
        Serial.print(F("[ERROR] Unknown: "));
        Serial.println(mnemonic);
        return;
    }

    if (hasOperand) {
        if (operandStr == "") {
            Serial.println(F("[ERROR] Missing Operand"));
            return;
        }
        int val = operandStr.toInt();
        
        // SETPAGE는 0~127까지 가능하지만, 4비트 제약으로 0~15만 허용
        if(mnemonic == "SETPAGE") {
            if(val < 0 || val > 15) {
                Serial.println(F("[ERROR] Page must be 0-15 (4-bit limit)"));
                return;
            }
        } else {
            if(val < 0 || val > 15) {
                Serial.println(F("[ERROR] Operand must be 0-15"));
                return;
            }
        }
        prog_buf[prog_sz++] = opcode | (val & 0x0F);
    } else {
        prog_buf[prog_sz++] = opcode;
    }

    Serial.print(F("[ASM] Line "));
    Serial.print(prog_sz);
    Serial.print(F(": "));
    Serial.print(mnemonic);
    if(hasOperand) {
        Serial.print(F(" "));
        Serial.print(operandStr);
    }
    Serial.print(F(" -> 0x"));
    if(prog_buf[prog_sz-1] < 16) Serial.print("0");
    Serial.println(prog_buf[prog_sz-1], HEX);
}

void handleCommand() {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd.startsWith("ASM ")) {
        processASM(cmd);
    }
    else if (cmd.startsWith("PAGE ")) {
        // 현재 작성 페이지 설정
        int page_num = cmd.substring(5).toInt();
        if(page_num < 0 || page_num > 127) {
            Serial.println(F("[ERROR] Page must be 0-127"));
            return;
        }
        current_page = page_num;
        Serial.print(F("[PAGE] Now writing to page "));
        Serial.println(current_page);
    }
    else if (cmd.startsWith("LOAD ")) {
        if(prog_sz == 0) {
            Serial.println(F("[ERROR] No program! Use ASM first."));
            return;
        }
        
        target_bank = cmd.substring(5).toInt();
        if(target_bank > 1) {
            Serial.println(F("[ERROR] Bank must be 0 or 1"));
            return;
        }
        
        Serial.print(F("[LOAD] Writing "));
        Serial.print(prog_sz);
        Serial.print(F(" bytes to Bank "));
        Serial.print(target_bank);
        Serial.print(F(" (Core "));
        Serial.print(target_bank + 1);
        Serial.print(F("), Page "));
        Serial.println(current_page);
        
        // 페이지 크기 체크
        if(prog_sz > 128) {
            Serial.print(F("[WARNING] Program size "));
            Serial.print(prog_sz);
            Serial.println(F(" exceeds single page (128 bytes)"));
            Serial.println(F("[INFO] Will span multiple pages"));
        }
        
        digitalWrite(SYS_RESET, LOW);
        delay(10);
        
        // 프로그램을 페이지 단위로 쓰기
        uint8_t write_page = current_page;
        for(uint16_t i=0; i<prog_sz; i++) {
            uint8_t page_offset = i % 128;
            
            // 페이지 경계 넘으면 다음 페이지로
            if(i > 0 && page_offset == 0) {
                write_page++;
                Serial.print(F("\n[INFO] Moving to page "));
                Serial.println(write_page);
            }
            
            uint16_t phys_addr = calcPhysicalAddr(target_bank, write_page, page_offset);
            if(phys_addr == 0xFFFF) {
                Serial.println(F("\n[ERROR] Address calculation failed"));
                return;
            }
            
            writeROM(phys_addr, prog_buf[i]);
            
            if(i % 16 == 0) Serial.print(F("."));
        }
        
        Serial.println(F("\n[OK] LOAD COMPLETE"));
    }
    else if (cmd == "RUN") {
        digitalWrite(SYS_RESET, HIGH);
        Serial.println(F("\n>> SYSTEM RUNNING"));
    }
    else if (cmd == "RESET") {
        digitalWrite(SYS_RESET, LOW);
        Serial.println(F("\n>> SYSTEM HALTED"));
    }
    else if (cmd == "CLEAR") {
        prog_sz = 0;
        Serial.println(F("[OK] Buffer cleared"));
    }
    else if (cmd == "LIST") {
        if(prog_sz == 0) {
            Serial.println(F("[INFO] Buffer is empty"));
            return;
        }
        Serial.println(F("\n=== Compiled Bytes ==="));
        for(uint16_t i=0; i<prog_sz; i++) {
            if(i % 128 == 0) {
                Serial.print(F("\n--- Page "));
                Serial.print(current_page + (i/128));
                Serial.println(F(" ---"));
            }
            Serial.print(F("0x"));
            if(i < 16) Serial.print("0");
            Serial.print(i, HEX);
            Serial.print(F(": 0x"));
            if(prog_buf[i] < 16) Serial.print("0");
            Serial.print(prog_buf[i], HEX);
            
            // 디코드 정보 추가
            uint8_t op = prog_buf[i] & 0xF0;
            uint8_t val = prog_buf[i] & 0x0F;
            Serial.print(F("  ; "));
            switch(op) {
                case OP_LOAD:    Serial.print(F("LOAD ")); Serial.print(val); break;
                case OP_ADD:     Serial.print(F("ADD ")); Serial.print(val); break;
                case OP_SUB:     Serial.print(F("SUB ")); Serial.print(val); break;
                case OP_MUL:     Serial.print(F("MUL ")); Serial.print(val); break;
                case OP_AND:     Serial.print(F("AND ")); Serial.print(val); break;
                case OP_OR:      Serial.print(F("OR ")); Serial.print(val); break;
                case OP_OUT:     Serial.print(F("OUT")); break;
                case OP_SETPAGE: Serial.print(F("SETPAGE ")); Serial.print(val); break;
                case OP_HALT:    Serial.print(F("HALT")); break;
                case OP_NOP:     Serial.print(F("NOP")); break;
                default:         Serial.print(F("???")); break;
            }
            Serial.println();
        }
        Serial.println();
    }
    else if (cmd == "INFO") {
        Serial.println(F("\n=== System Info ==="));
        Serial.print(F("Current Page: "));
        Serial.println(current_page);
        Serial.print(F("Target Bank: "));
        Serial.print(target_bank);
        Serial.print(F(" (Core "));
        Serial.print(target_bank + 1);
        Serial.println(F(")"));
        Serial.print(F("Buffer Size: "));
        Serial.print(prog_sz);
        Serial.print(F(" / "));
        Serial.print(MAX_PROG);
        Serial.println(F(" bytes"));
        Serial.print(F("Pages Used: "));
        Serial.println((prog_sz + 127) / 128);
        Serial.println();
    }
    else if (cmd == "HELP") {
        Serial.println(F("\n==== Dual-Core SMU with Paging ===="));
        Serial.println(F("ASM <core> <INST> <VAL>"));
        Serial.println(F("  Ex: ASM 1 LOAD 10"));
        Serial.println(F("PAGE <0-127>  - Set write page"));
        Serial.println(F("LIST          - Show program"));
        Serial.println(F("LOAD 0        - Write to Core 1"));
        Serial.println(F("LOAD 1        - Write to Core 2"));
        Serial.println(F("RUN           - Start execution"));
        Serial.println(F("RESET         - Halt system"));
        Serial.println(F("CLEAR         - Empty buffer"));
        Serial.println(F("INFO          - Show system info"));
        Serial.println(F("\n=== Instructions ==="));
        Serial.println(F("LOAD/ADD/SUB/MUL 0-15"));
        Serial.println(F("AND/OR 0-15"));
        Serial.println(F("OUT, HALT, NOP"));
        Serial.println(F("SETPAGE 0-15  - Switch to page"));
        Serial.println(F("\n=== Paging System ==="));
        Serial.println(F("- Each bank: 16KB (128 pages)"));
        Serial.println(F("- Each page: 128 bytes"));
        Serial.println(F("- Core 1: Pages 0-127"));
        Serial.println(F("- Core 2: Pages 0-127"));
        Serial.println(F("- Use SETPAGE in code to switch\n"));
    }
    else {
        Serial.println(F("[ERROR] Unknown. Type HELP"));
    }
}

void setup() {
    Serial.begin(115200);
    
    for(int i=0; i<8; i++) pinMode(DATA_PINS[i], INPUT);
    
    pinMode(BUFFER_DIR, OUTPUT);
    pinMode(ROM_A14, OUTPUT);
    pinMode(ROM_OE, OUTPUT);
    pinMode(ROM_WE, OUTPUT);
    pinMode(HC595_DS, OUTPUT);
    pinMode(HC595_SHCP, OUTPUT);
    pinMode(HC595_STCP, OUTPUT);
    pinMode(SYS_RESET, OUTPUT);
    
    digitalWrite(BUFFER_DIR, LOW);  // A→B (나노→ROM)
    digitalWrite(ROM_WE, HIGH);
    digitalWrite(ROM_OE, HIGH);
    digitalWrite(SYS_RESET, LOW);
    
    current_page = 0;
    target_bank = 0;
    
    Serial.println(F("\n================================"));
    Serial.println(F("  Dual-Core SMU v2.1"));
    Serial.println(F("  JIT Assembler + Paging"));
    Serial.println(F("================================"));
    Serial.println(F("Type HELP for commands\n"));
}

void loop() {
    if(Serial.available()) handleCommand();
}

/*
 * ============================================================================
 * 사용 예시
 * ============================================================================
 * 
 * # 간단한 프로그램 (한 페이지 내)
 * PAGE 0
 * ASM 1 LOAD 5
 * ASM 1 ADD 3
 * ASM 1 OUT
 * ASM 1 HALT
 * LOAD 0
 * RUN
 * 
 * # 페이지 전환 사용 (VM이 SETPAGE 지원 필요)
 * PAGE 0
 * ASM 1 LOAD 10
 * ASM 1 SETPAGE 1
 * ASM 1 HALT
 * LOAD 0
 * 
 * PAGE 1
 * ASM 1 LOAD 20
 * ASM 1 OUT
 * ASM 1 HALT
 * LOAD 0
 * RUN
 * 
 * ============================================================================
 */