/*
 * ============================================================================
 * SMU v6.0 - Arduino Mega 2560 (62256 SRAM Programmer)
 * ============================================================================
 * 변경사항:
 * - Arduino Nano → Arduino Mega 2560
 * - 74HC595 제거 (핀이 충분함)
 * - 간단한 회로, 빠른 속도
 * 
 * Arduino Mega 2560 핀 배치:
 * 
 * Bank 시스템:
 * - Bank 0 (A14=0): Core 1 영역 (0x0000~0x3FFF)
 * - Bank 1 (A14=1): Core 2 영역 (0x4000~0x7FFF)

    ## 핀 배치도
    Arduino Mega 2560:

    데이터 버스:
    D22 ─ RAM D0
    D23 ─ RAM D1
    D24 ─ RAM D2
    D25 ─ RAM D3
    D26 ─ RAM D4
    D27 ─ RAM D5
    D28 ─ RAM D6
    D29 ─ RAM D7

    주소 버스 (하위):
    D30 ─ RAM A0
    D31 ─ RAM A1
    D32 ─ RAM A2
    D33 ─ RAM A3
    D34 ─ RAM A4
    D35 ─ RAM A5
    D36 ─ RAM A6
    D37 ─ RAM A7

    주소 버스 (상위):
    D42 ─ RAM A8
    D43 ─ RAM A9
    D44 ─ RAM A10
    D45 ─ RAM A11
    D46 ─ RAM A12
    D47 ─ RAM A13

 * 제어 신호:
 * - D48 (PL1): RAM A14 (Bank Select)
 * - D49 (PL0): RAM CE#
 * - D50 (PB3): RAM OE#
 * - D51 (PB2): RAM WE#
 * - D52 (PB1): System RESET (Core 1, 2)
 */

#include <avr/io.h>

// ── 명령어 셋 ────────────────────────────────────────────────────────────────
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

// ── 프로그램 버퍼 ────────────────────────────────────────────────────────────
#define MAX_PROG 256
uint8_t  prog_buf[MAX_PROG];
uint16_t prog_sz = 0;

uint8_t target_bank  = 0;
uint8_t current_page = 0;
bool    cores_reset  = false;

// ── 데이터 버스 (PORTA: D22~D29) ─────────────────────────────────────────────
inline void set_data_output() {
    DDRA = 0xFF;  // 전체 출력
}

inline void set_data_input() {
    DDRA = 0x00;   // 전체 입력
    PORTA = 0x00;  // 풀업 비활성화
}

inline void write_data_bus(uint8_t data) {
    PORTA = data;
}

inline uint8_t read_data_bus() {
    return PINA;
}

// ── 주소 버스 설정 ────────────────────────────────────────────────────────────
// PORTC: A0~A7 (D30~D37)
// PORTL: A8~A13 (D42~D47 = PL7~PL2, 상위 6비트)
inline void set_addr_bus(uint16_t addr) {
    addr &= 0x3FFF;  // 14비트만 사용
    
    // A0~A7: PORTC
    PORTC = addr & 0xFF;
    
    // A8~A13: PORTL (PL7~PL2, bit 2~7)
    // PORTL의 하위 2비트(PL0, PL1)는 제어 신호용이므로 보존
    PORTL = (PORTL & 0b00000011) | ((addr >> 6) & 0b11111100);
}

// ── RAM 제어 매크로 (AVR 직접 제어) ──────────────────────────────────────────
// PL1: A14 (Bank Select)
// PL0: CE#
// PB3: OE#
// PB2: WE#
// PB1: RESET

#define RAM_A14_LOW()       PORTL &= ~(1 << 1)  // PL1 = 0
#define RAM_A14_HIGH()      PORTL |=  (1 << 1)  // PL1 = 1

#define RAM_CE_ENABLE()     PORTL &= ~(1 << 0)  // PL0 = 0
#define RAM_CE_DISABLE()    PORTL |=  (1 << 0)  // PL0 = 1

#define RAM_OE_ENABLE()     PORTB &= ~(1 << 3)  // PB3 = 0
#define RAM_OE_DISABLE()    PORTB |=  (1 << 3)  // PB3 = 1

#define RAM_WE_ENABLE()     PORTB &= ~(1 << 2)  // PB2 = 0
#define RAM_WE_DISABLE()    PORTB |=  (1 << 2)  // PB2 = 1

#define RESET_CORES()       PORTB &= ~(1 << 1)  // PB1 = 0
#define RELEASE_CORES()     PORTB |=  (1 << 1)  // PB1 = 1

// ── RAM 쓰기 (완전 최적화) ───────────────────────────────────────────────────
void writeRAM(uint16_t addr, uint8_t data) {
    set_addr_bus(addr);
    
    DDRA = 0xFF;    // 데이터 출력
    PORTA = data;   // 데이터 쓰기
    
    PORTL &= ~(1 << 0);  // CE = 0
    PORTB |=  (1 << 3);  // OE = 1
    PORTB &= ~(1 << 2);  // WE = 0
    
    asm volatile("nop\n\t nop\n\t");  // 최소 대기
    
    PORTB |=  (1 << 2);  // WE = 1
    PORTL |=  (1 << 0);  // CE = 1
    
    DDRA = 0x00;    // 데이터 입력으로
    PORTA = 0x00;
}

// ── RAM 읽기 (디버그용) ──────────────────────────────────────────────────────
uint8_t readRAM(uint16_t addr) {
    set_addr_bus(addr);
    
    DDRA = 0x00;    // 데이터 입력
    PORTA = 0x00;
    
    PORTL &= ~(1 << 0);  // CE = 0
    PORTB &= ~(1 << 3);  // OE = 0
    
    asm volatile("nop\n\t nop\n\t");
    
    uint8_t data = PINA;
    
    PORTB |= (1 << 3);  // OE = 1
    PORTL |= (1 << 0);  // CE = 1
    
    return data;
}

// ── 물리 주소 계산 ──────────────────────────────────────────────────────────
uint16_t calcPhysicalAddr(uint8_t page, uint8_t offset) {
    offset &= 0x7F;
    uint16_t addr = ((uint16_t)page << 7) | offset;
    if (addr >= 0x4000) return 0xFFFF;
    return addr;
}

// ── 인라인 주석 제거 ─────────────────────────────────────────────────────────
String stripComment(String line) {
    int ci = line.indexOf(';');
    if (ci != -1) line = line.substring(0, ci);
    line.trim();
    return line;
}


// ── 어셈블리 파싱 (해시, char* 버전 - 최고속) ─────────────────────────────────────
void processLine_fast(const char* line_raw) {
    if (prog_sz >= MAX_PROG) {
        Serial.println(F("FULL"));
        return;
    }

    // 임시 버퍼
    char line[64];
    strncpy(line, line_raw, 63);
    line[63] = '\0';

    // 주석 제거
    char* comment = strchr(line, ';');
    if (comment) *comment = '\0';

    // 앞뒤 공백 제거 (간단 버전)
    char* start = line;
    while (*start == ' ' || *start == '\t') start++;
    if (*start == '\0') return;

    // 대문자 변환
    for (char* p = start; *p; p++) {
        if (*p >= 'a' && *p <= 'z') {
            *p = *p - 32;  // 'a' → 'A'
        }
    }

    // 명령어와 피연산자 분리
    char* space = strchr(start, ' ');
    char* inst = start;
    char* operand = NULL;
    
    if (space) {
        *space = '\0';
        operand = space + 1;
        while (*operand == ' ') operand++;  // 앞 공백 제거
    }

    // ★ 해시 계산 ★
    if (inst[0] == '\0' || inst[1] == '\0') return;
    
    uint16_t hash = ((uint16_t)inst[0] << 8) | inst[1];

    uint8_t opcode = 0;
    bool hasOperand = false;

    // ★ switch-case ★
    switch(hash) {
        case 0x4C4F:  // LOAD
            opcode = OP_LOAD;
            hasOperand = true;
            break;

        case 0x4144:  // ADD
            opcode = OP_ADD;
            hasOperand = true;
            break;

        case 0x5355:  // SUB
            opcode = OP_SUB;
            hasOperand = true;
            break;

        case 0x4D55:  // MUL
            opcode = OP_MUL;
            hasOperand = true;
            break;

        case 0x414E:  // AND
            opcode = OP_AND;
            hasOperand = true;
            break;

        case 0x4F52:  // OR
            opcode = OP_OR;
            hasOperand = true;
            break;

        case 0x4F55:  // OUT
            opcode = OP_OUT;
            break;

        case 0x4645:  // FETCH
            opcode = OP_FETCH;
            hasOperand = true;
            break;

        case 0x534C:  // SLOT
            opcode = OP_SLOT;
            hasOperand = true;
            break;

        case 0x5055:  // PUSH
            opcode = OP_PUSH;
            break;

        case 0x504F:  // POP
            opcode = OP_POP;
            break;

        case 0x5345:  // SETPAGE
            opcode = OP_SETPAGE;
            break;

        case 0x4841:  // HALT
            opcode = OP_HALT;
            break;

        case 0x4E4F:  // NOP
            opcode = OP_NOP;
            break;

        default:
            return;
    }

    if (hasOperand) {
        if (!operand) {
            Serial.print(F("NO_OP: "));
            Serial.println(inst);
            return;
        }
        
        // atoi 대신 직접 파싱 (더 빠름)
        int val = 0;
        while (*operand >= '0' && *operand <= '9') {
            val = val * 10 + (*operand - '0');
            operand++;
        }
        
        if (val < 0 || val > 15) {
            Serial.print(F("RANGE: "));
            Serial.println(inst);
            return;
        }
        prog_buf[prog_sz++] = opcode | (val & 0x0F);
    } else {
        prog_buf[prog_sz++] = opcode;
    }
}



// ── 명령 처리 (완전 최적화) ──────────────────────────────────────────────────
void handleCommand() {
    // char 버퍼로 직접 읽기 (String보다 빠름)
    static char buffer[128];
    uint8_t idx = 0;
    
    while (Serial.available() && idx < 127) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') break;
        buffer[idx++] = c;
    }
    buffer[idx] = '\0';
    
    if (idx == 0) return;

    // ========================================================================
    // SMU 명령어 (':' 시작)
    // ========================================================================
    if (buffer[0] == ':') {
        String cmd = String(buffer);  // String으로 변환 (명령어 처리용)
        cmd.trim();

        // ── :clear ────────────────────────────────────────────────────────
        if (cmd == ":clear") {
            prog_sz = 0;
            Serial.println(F("CLR"));
            return;
        }

        // ── :w <bank> <page> ─────────────────────────────────────────────
        if (cmd.startsWith(":w ")) {
            if (prog_sz == 0) {
                Serial.println(F("EMPTY"));
                return;
            }

            String args = cmd.substring(3);
            args.trim();
            int sp = args.indexOf(' ');
            if (sp == -1) {
                Serial.println(F("USAGE: :w <bank> <page>"));
                return;
            }
            target_bank  = (uint8_t)args.substring(0, sp).toInt();
            current_page = (uint8_t)args.substring(sp + 1).toInt();

            if (target_bank > 1) {
                Serial.println(F("ERR: bank 0-1"));
                return;
            }
            if (current_page > 127) {
                Serial.println(F("ERR: page 0-127"));
                return;
            }
            if (prog_sz > 128) {
                Serial.println(F("ERR: >128 inst"));
                return;
            }

            if (!cores_reset) {
                RESET_CORES();
                delay(5);
                cores_reset = true;
            }

            if (target_bank == 0) RAM_A14_LOW(); else RAM_A14_HIGH();

            Serial.print(F("B"));
            Serial.print(target_bank);
            Serial.print(F(":P"));
            Serial.print(current_page);
            Serial.print(F(" "));
            Serial.print(prog_sz);
            Serial.println(F("B"));

            for (uint16_t i = 0; i < prog_sz; i++) {
                uint8_t  pg_offset = i & 0x7F;
                uint8_t  write_pg  = current_page + (i >> 7);
                uint16_t phys      = calcPhysicalAddr(write_pg, pg_offset);

                if (phys == 0xFFFF) {
                    Serial.println(F("OVER"));
                    break;
                }
                writeRAM(phys, prog_buf[i]);

                if ((i & 0x3F) == 0x3F) Serial.print('.');
            }

            prog_sz = 0;
            Serial.println(F("\nOK"));
            return;
        }

        // ── :run ─────────────────────────────────────────────────────────
        if (cmd == ":run") {
            set_data_input();
            RAM_CE_DISABLE();
            RAM_OE_DISABLE();
            RAM_WE_DISABLE();

            cores_reset = false;
            RELEASE_CORES();
            Serial.println(F("RUN"));
            return;
        }

        // ── :rst ─────────────────────────────────────────────────────────
        if (cmd == ":rst") {
            cores_reset = true;
            RESET_CORES();
            Serial.println(F("RST"));
            return;
        }

        // ── :read <bank> <page> ──────────────────────────────────────────
        if (cmd.startsWith(":read ")) {
            String args = cmd.substring(6);
            args.trim();
            int sp = args.indexOf(' ');
            if (sp == -1) {
                Serial.println(F("USAGE: :read <bank> <page>"));
                return;
            }
            
            uint8_t bank = (uint8_t)args.substring(0, sp).toInt();
            uint8_t page = (uint8_t)args.substring(sp + 1).toInt();
            
            if (bank > 1 || page > 127) {
                Serial.println(F("ERR: bank 0-1, page 0-127"));
                return;
            }
            
            if (bank == 0) RAM_A14_LOW(); else RAM_A14_HIGH();
            
            Serial.print(F("B"));
            Serial.print(bank);
            Serial.print(F(":P"));
            Serial.println(page);
            
            for (uint8_t i = 0; i < 128; i++) {
                uint16_t addr = calcPhysicalAddr(page, i);
                uint8_t data = readRAM(addr);
                
                Serial.print(F("0x"));
                if (data < 16) Serial.print('0');
                Serial.print(data, HEX);
                Serial.print(' ');
                
                if ((i & 0x0F) == 0x0F) Serial.println();
            }
            Serial.println(F("OK"));
            return;
        }

        // ── :dump ────────────────────────────────────────────────────────
        if (cmd == ":dump") {
            Serial.println(F("=== RAM DUMP ==="));
            for (uint8_t bank = 0; bank <= 1; bank++) {
                if (bank == 0) RAM_A14_LOW(); else RAM_A14_HIGH();
                
                Serial.print(F("BANK "));
                Serial.println(bank);
                
                for (uint8_t page = 0; page < 4; page++) {
                    Serial.print(F("  Page "));
                    Serial.print(page);
                    Serial.print(F(": "));
                    
                    for (uint8_t i = 0; i < 16; i++) {
                        uint16_t addr = calcPhysicalAddr(page, i);
                        uint8_t data = readRAM(addr);
                        
                        if (data < 16) Serial.print('0');
                        Serial.print(data, HEX);
                        Serial.print(' ');
                    }
                    Serial.println();
                }
            }
            Serial.println(F("OK"));
            return;
        }

        // 알 수 없는 명령어
        Serial.println(F("ERR: Unknown command"));
        return;
    }

    // ========================================================================
    // 어셈블리 명령어 (최적화된 파싱)
    // ========================================================================
    else {
        processLine_fast(buffer);
    }
}

// ── setup / loop ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // 데이터 버스 (PORTA: D22~D29)
    DDRA = 0x00;
    PORTA = 0x00;

    // 주소 버스 A0~A7 (PORTC: D30~D37)
    DDRC = 0xFF;
    PORTC = 0x00;

    // 주소 버스 A8~A13 + 제어 (PORTL)
    // PL7~PL2: 주소 (출력)
    // PL1: A14 (출력)
    // PL0: CE (출력)
    DDRL = 0xFF;  // 전체 출력
    PORTL = 0b00000001;  // CE = 1 (비활성)

    // 제어 신호 (PORTB)
    // PB3: OE (출력)
    // PB2: WE (출력)
    // PB1: RESET (출력)
    DDRB |= 0b00001110;  // PB1~3 출력
    PORTB |= 0b00001110; // 전부 HIGH (비활성)

    RAM_CE_DISABLE();
    RAM_OE_DISABLE();
    RAM_WE_DISABLE();
    RAM_A14_LOW();

    RESET_CORES();
    cores_reset = true;

    target_bank  = 0;
    current_page = 0;
    prog_sz      = 0;

    Serial.println(F("SMU v6.1 - Mega 2560 (AVR)"));
    Serial.println(F(":w <bank> <page> | :run | :rst | :clear"));
    Serial.println(F(":read <bank> <page> | :dump"));
}

void loop() {
    if (Serial.available()) {
        handleCommand();
    }
}

/*
    NOP     → 'N','O' → 0x4E4F
    LOAD    → 'L','O' → 0x4C4F
    ADD     → 'A','D' → 0x4144
    SUB     → 'S','U' → 0x5355
    MUL     → 'M','U' → 0x4D55
    AND     → 'A','N' → 0x414E
    OR      → 'O','R' → 0x4F52
    OUT     → 'O','U' → 0x4F55
    FETCH   → 'F','E' → 0x4645
    SLOT    → 'S','L' → 0x534C
    PUSH    → 'P','U' → 0x5055
    POP     → 'P','O' → 0x504F
    SETPAGE → 'S','E' → 0x5345
    HALT    → 'H','A' → 0x4841 


        ## 전체 구조
    
    handleCommand()
    ├─ buffer로 시리얼 읽기 (char 배열)
    │
    ├─ buffer[0] == ':' ?
    │  ├─ YES → SMU 명령어
    │  │         ├─ :clear
    │  │         ├─ :w
    │  │         ├─ :run
    │  │         ├─ :rst
    │  │         ├─ :read
    │  │         └─ :dump
    │  │
    │  └─ NO  → 어셈블리 명령어
    │           └─ processLine_fast(buffer)
    │                 ├─ 주석 제거
    │                 ├─ 대문자 변환
    │                 ├─ 해시 계산
    │                 └─ switch-case 비교
 */