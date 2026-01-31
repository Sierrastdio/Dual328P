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
/* 아두이노 나노 #1 - 인터페이스 연동용 최적화 */
// (핀 정의 및 변수 부분은 주신 코드와 동일하므로 생략)

void processASM(String line) {
    if (prog_sz >= MAX_PROG) return;
    
    line.trim();
    line.toUpperCase();
    
    int firstSpace = line.indexOf(' ');
    int secondSpace = line.indexOf(' ', firstSpace + 1);
    int thirdSpace = line.indexOf(' ', secondSpace + 1);

    if (firstSpace == -1 || secondSpace == -1) return;

    String mnemonic = (thirdSpace == -1) ? line.substring(secondSpace + 1) : line.substring(secondSpace + 1, thirdSpace);
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
    else return;

    if (hasOperand) {
        int val = operandStr.toInt();
        prog_buf[prog_sz++] = opcode | (val & 0x0F);
    } else {
        prog_buf[prog_sz++] = opcode;
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
    }
    else if (cmd.startsWith("LOAD ")) {
        target_bank = cmd.substring(5).toInt();
        digitalWrite(SYS_RESET, LOW);
        
        uint8_t write_page = current_page;
        for(uint16_t i=0; i<prog_sz; i++) {
            uint8_t page_offset = i % 128;
            if(i > 0 && page_offset == 0) write_page++;
            
            uint16_t phys_addr = calcPhysicalAddr(target_bank, write_page, page_offset);
            if(phys_addr != 0xFFFF) writeROM(phys_addr, prog_buf[i]);
        }
        Serial.println("OK"); // 전송 완료 신호만 보냄
    }
    else if (cmd == "RUN") { digitalWrite(SYS_RESET, HIGH); }
    else if (cmd == "RESET") { digitalWrite(SYS_RESET, LOW); }
    else if (cmd == "CLEAR") { prog_sz = 0; }
}

// setup, loop 로직 유지
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