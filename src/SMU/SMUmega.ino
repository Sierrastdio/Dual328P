/*
 * ============================================================================
 * SMU v6.1 - Arduino Mega 2560 (62256 SRAM Programmer)
 * ============================================================================
 * 변경사항:
 * - Arduino Nano → Arduino Mega 2560
 * - 74HC595 제거 (핀이 충분함)
 * - AVR 레지스터 직접 제어
 * - 해시 기반 파싱 (40배 고속화)
 * 
 * Bank 시스템:
 * - Bank 0 (A14=0): Core 1 영역 (0x0000~0x3FFF)
 * - Bank 1 (A14=1): Core 2 영역 (0x4000~0x7FFF)
 *
 * 핀 배치:
 * 데이터: D22~D29 (PORTA) → RAM D0~D7
 * 주소: D30~D37 (PORTC) → RAM A0~A7
 *       D42~D47 (PORTL) → RAM A8~A13
 * 제어: D48 (PL1) → RAM A14 (Bank)
 *       D49 (PL0) → RAM CE#
 *       D50 (PB3) → RAM OE#
 *       D51 (PB2) → RAM WE#
 *       D52 (PB1) → System RESET
 * ============================================================================
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
    DDRA = 0xFF;
    PORTA = 0x00;
}

inline void set_data_input() {
    DDRA = 0x00;
    PORTA = 0x00;
}

inline void write_data_bus(uint8_t data) {
    PORTA = data;
}

inline uint8_t read_data_bus() {
    return PINA;
}

// ── 주소 버스 설정 ────────────────────────────────────────────────────────────
inline void set_addr_bus(uint16_t addr) {
    addr &= 0x3FFF;
    
    PORTC = addr & 0xFF;
    PORTL = (PORTL & 0b00000011) | ((addr >> 6) & 0b11111100);
}

// ── RAM 제어 매크로 ──────────────────────────────────────────────────────────
#define RAM_A14_LOW()       PORTL &= ~(1 << 1)
#define RAM_A14_HIGH()      PORTL |=  (1 << 1)

#define RAM_CE_ENABLE()     PORTL &= ~(1 << 0)
#define RAM_CE_DISABLE()    PORTL |=  (1 << 0)

#define RAM_OE_ENABLE()     PORTB &= ~(1 << 3)
#define RAM_OE_DISABLE()    PORTB |=  (1 << 3)

#define RAM_WE_ENABLE()     PORTB &= ~(1 << 2)
#define RAM_WE_DISABLE()    PORTB |=  (1 << 2)

#define RESET_CORES()       PORTB &= ~(1 << 1)
#define RELEASE_CORES()     PORTB |=  (1 << 1)

// ── RAM 쓰기 ─────────────────────────────────────────────────────────────────
void writeRAM(uint16_t addr, uint8_t data) {
    set_addr_bus(addr);
    
    DDRA = 0xFF;
    PORTA = data;
    
    PORTL &= ~(1 << 0);  // CE = 0
    PORTB |=  (1 << 3);  // OE = 1
    PORTB &= ~(1 << 2);  // WE = 0
    
    asm volatile("nop\n\t nop\n\t");
    
    PORTB |=  (1 << 2);  // WE = 1
    PORTL |=  (1 << 0);  // CE = 1
    
    DDRA = 0x00;
    PORTA = 0x00;
}

// ── 물리 주소 계산 ──────────────────────────────────────────────────────────
uint16_t calcPhysicalAddr(uint8_t page, uint8_t offset) {
    offset &= 0x7F;
    uint16_t addr = ((uint16_t)page << 7) | offset;
    if (addr >= 0x4000) return 0xFFFF;
    return addr;
}

// ── 어셈블리 파싱 (해시 기반) ────────────────────────────────────────────────
void processLine_fast(const char* line_raw) {
    if (prog_sz >= MAX_PROG) {
        Serial.println(F("FULL"));
        return;
    }

    char line[64];
    strncpy(line, line_raw, 63);
    line[63] = '\0';

    // 주석 제거
    char* comment = strchr(line, ';');
    if (comment) *comment = '\0';

    // 앞 공백 제거
    char* start = line;
    while (*start == ' ' || *start == '\t') start++;
    if (*start == '\0') return;

    // 대문자 변환
    for (char* p = start; *p; p++) {
        if (*p >= 'a' && *p <= 'z') {
            *p = *p - 32;
        }
    }

    // 명령어와 피연산자 분리
    char* space = strchr(start, ' ');
    char* inst = start;
    char* operand = NULL;
    
    if (space) {
        *space = '\0';
        operand = space + 1;
        while (*operand == ' ') operand++;
    }

    // 해시 계산
    if (inst[0] == '\0' || inst[1] == '\0') return;
    uint16_t hash = ((uint16_t)inst[0] << 8) | inst[1];

    uint8_t opcode = 0;
    bool hasOperand = false;

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
        if (!operand) return;
        
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

// ── 명령 처리 ────────────────────────────────────────────────────────────────
void handleCommand() {
    if (!Serial.available()) return;
    
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    // ========================================================================
    // SMU 명령어
    // ========================================================================
    if (cmd[0] == ':') {
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
            
            target_bank = (uint8_t)args.substring(0, sp).toInt();
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
        
        Serial.println(F("ERR: Unknown command"));
        return;
    }
    
    // ========================================================================
    // 어셈블리 명령어
    // ========================================================================
    else {
        processLine_fast(cmd.c_str());
    }
}

// ── setup / loop ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // 데이터 버스 (PORTA)
    DDRA = 0x00;
    PORTA = 0x00;

    // 주소 버스 A0~A7 (PORTC)
    DDRC = 0xFF;
    PORTC = 0x00;

    // 주소 버스 A8~A13 + 제어 (PORTL)
    DDRL = 0xFF;
    PORTL = 0b00000001;  // CE = 1

    // 제어 신호 (PORTB)
    DDRB |= 0b00001110;
    PORTB |= 0b00001110;

    RAM_CE_DISABLE();
    RAM_OE_DISABLE();
    RAM_WE_DISABLE();
    RAM_A14_LOW();

    RESET_CORES();
    cores_reset = true;

    target_bank  = 0;
    current_page = 0;
    prog_sz      = 0;

    Serial.println(F("SMU v6.1 - Mega 2560"));
    Serial.println(F(":clear | :w <bank> <page> | :run | :rst"));
}

void loop() {
    if (Serial.available()) {
        handleCommand();
    }
}

/*
 * ============================================================================
 * 사용법
 * ============================================================================
 * 
 * 1. 명령어 입력:
 *    LOAD 5
 *    ADD 3
 *    OUT
 *    HALT
 * 
 * 2. 메모리에 쓰기:
 *    :w 0 0        (Bank 0, Page 0에 쓰기)
 * 
 * 3. 실행:
 *    :run          (Core 해제)
 * 
 * 4. 초기화:
 *    :rst          (Core 리셋)
 *    :clear        (버퍼 클리어)
 * 
 * ============================================================================
 * 명령어 해시 테이블
 * ============================================================================
 * NOP     → 0x4E4F    LOAD    → 0x4C4F
 * ADD     → 0x4144    SUB     → 0x5355
 * MUL     → 0x4D55    AND     → 0x414E
 * OR      → 0x4F52    OUT     → 0x4F55
 * FETCH   → 0x4645    SLOT    → 0x534C
 * PUSH    → 0x5055    POP     → 0x504F
 * SETPAGE → 0x5345    HALT    → 0x4841
 * ============================================================================
 */