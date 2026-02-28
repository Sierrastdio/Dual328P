/*
 * ============================================================================
 * Arduino Nano #1 - SMU v4.1 (멀티 페이지 에디터 호환)
 * ============================================================================
 * 핀 배치:
 * - D2~D7, A0~A1: 데이터 버스 (D0~D7)
 * - D9: SCK (74HC595-SMU1, 74HC595-SMU2)
 * - D10: RCK (74HC595-SMU1, 74HC595-SMU2)
 * - D11: SER (74HC595-SMU1)
 * - D12: RAM OE
 * - D13: RAM WE
 * - A2: System RESET (Core 1, 2)
 * - A3: 74HC595 G# (Output Enable)
 * - A4: 62256 CE#
 * - A5: 62256 A14 (Bank Select)
 *
 * 595 체인:
 * - 595-SMU1: A0~A7 (QA~QH)
 * - 595-SMU2: A8~A13 (QB~QG), QA는 N/C
 *
 * Bank 시스템:
 * - Bank 0 (A14=0): Core 1 영역 (0x0000~0x3FFF)
 * - Bank 1 (A14=1): Core 2 영역 (0x4000~0x7FFF)
 *
 * 에디터 업로드 프로토콜 (페이지당 반복):
 *   :clear          → 버퍼 초기화 (CLR)
 *   INST [operand]  → 명령어 버퍼에 추가 (인라인 ; 주석 자동 제거)
 *   :w <bank> <pg>  → 해당 bank/page 에 쓰기 (OK / ERR)
 * 전체 완료 후:
 *   :run            → Core 해제 (RUN)
 * ============================================================================
 */

#include <avr/io.h>

// ── 핀 정의 ──────────────────────────────────────────────────────────────────
const uint8_t HC595_SER  = 11;  // PB3
const uint8_t HC595_SCK  = 9;   // PB1
const uint8_t HC595_RCK  = 10;  // PB2
const uint8_t HC595_G    = A3;  // PC3

const uint8_t RAM_CE   = A4;    // PC4
const uint8_t RAM_OE   = 12;    // PB4
const uint8_t RAM_WE   = 13;    // PB5
const uint8_t RAM_A14  = A5;    // PC5

const uint8_t SYS_RESET = A2;   // PC2

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
// 페이지 한 장 = 최대 128 명령어. 여유있게 256 확보.
#define MAX_PROG 256
uint8_t  prog_buf[MAX_PROG];
uint16_t prog_sz = 0;

uint8_t target_bank  = 0;
uint8_t current_page = 0;
bool    cores_reset  = false;   // 업로드 중 RESET 상태 추적

// ── 데이터 버스 ──────────────────────────────────────────────────────────────
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

// ── 74HC595 매크로 ───────────────────────────────────────────────────────────
#define HC595_G_ENABLE()    PORTC &= ~(1 << 3)
#define HC595_G_DISABLE()   PORTC |=  (1 << 3)
#define HC595_RCK_LOW()     PORTB &= ~(1 << 2)
#define HC595_RCK_HIGH()    PORTB |=  (1 << 2)
#define HC595_SCK_LOW()     PORTB &= ~(1 << 1)
#define HC595_SCK_HIGH()    PORTB |=  (1 << 1)
#define HC595_SER_LOW()     PORTB &= ~(1 << 3)
#define HC595_SER_HIGH()    PORTB |=  (1 << 3)

// ── RAM 매크로 ───────────────────────────────────────────────────────────────
#define RAM_CE_ENABLE()     PORTC &= ~(1 << 4)
#define RAM_CE_DISABLE()    PORTC |=  (1 << 4)
#define RAM_OE_ENABLE()     PORTB &= ~(1 << 4)
#define RAM_OE_DISABLE()    PORTB |=  (1 << 4)
#define RAM_WE_ENABLE()     PORTB &= ~(1 << 5)
#define RAM_WE_DISABLE()    PORTB |=  (1 << 5)
#define RAM_A14_LOW()       PORTC &= ~(1 << 5)
#define RAM_A14_HIGH()      PORTC |=  (1 << 5)

// ── 시스템 제어 ──────────────────────────────────────────────────────────────
#define RESET_CORES()       PORTC &= ~(1 << 2)
#define RELEASE_CORES()     PORTC |=  (1 << 2)

// ── 595 시리얼 전송 ──────────────────────────────────────────────────────────
void shiftOut_fast(uint8_t data) {
    for (uint8_t i = 0; i < 8; i++) {
        if (data & 0x80) HC595_SER_HIGH(); else HC595_SER_LOW();
        HC595_SCK_HIGH();
        HC595_SCK_LOW();
        data <<= 1;
    }
}

// ── 주소 설정 ────────────────────────────────────────────────────────────────
void setAddr(uint16_t addr) {
    addr &= 0x3FFF;  // 14비트
    HC595_RCK_LOW();
    shiftOut_fast((addr >> 7) & 0x7F);  // A8~A13 (595-SMU2, QA=N/C)
    shiftOut_fast(addr & 0xFF);          // A0~A7  (595-SMU1)
    HC595_RCK_HIGH();
    asm volatile("nop\n\t");
    HC595_RCK_LOW();
}

// ── RAM 쓰기 ────────────────────────────────────────────────────────────────
void writeRAM(uint16_t addr, uint8_t data) {
    HC595_G_ENABLE();
    setAddr(addr);
    set_data_output();
    write_data_bus(data);
    RAM_CE_ENABLE();
    RAM_OE_DISABLE();
    RAM_WE_ENABLE();
    delayMicroseconds(1);
    RAM_WE_DISABLE();
    RAM_CE_DISABLE();
    set_data_input();
}

// ── 물리 주소 계산 ──────────────────────────────────────────────────────────
uint16_t calcPhysicalAddr(uint8_t page, uint8_t offset) {
    offset &= 0x7F;
    uint16_t addr = ((uint16_t)page << 7) | offset;
    if (addr >= 0x4000) return 0xFFFF;  // 16KB 초과
    return addr;
}

// ── 인라인 주석 제거 ─────────────────────────────────────────────────────────
// "SLOT 15    ; PAGE_REG" → "SLOT 15"
String stripComment(String line) {
    int ci = line.indexOf(';');
    if (ci != -1) line = line.substring(0, ci);
    line.trim();
    return line;
}

// ── 어셈블리 파싱 ────────────────────────────────────────────────────────────
void processLine(String line) {
    if (prog_sz >= MAX_PROG) {
        Serial.println(F("FULL"));
        return;
    }

    line.trim();
    line = stripComment(line);   // 인라인 ; 주석 제거
    line.toUpperCase();

    if (line.length() == 0) return;    // 빈 줄 / 주석만 있던 줄
    if (line.startsWith(";")) return;  // (stripComment 후에도 안전망)

    int space = line.indexOf(' ');
    String inst    = (space == -1) ? line : line.substring(0, space);
    String operand = (space == -1) ? ""   : line.substring(space + 1);
    inst.trim();
    operand.trim();

    uint8_t opcode     = 0;
    bool    hasOperand = false;

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
    else if (inst == "SETPAGE") { opcode = OP_SETPAGE; }
    else if (inst == "HALT")    { opcode = OP_HALT; }
    else if (inst == "NOP")     { opcode = OP_NOP; }
    else {
        // 알 수 없는 명령어 무시 (에디터의 빈 줄·주석 잔여물 대응)
        return;
    }

    if (hasOperand) {
        int val = operand.toInt();
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
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();   // \r\n 모두 제거 (Windows 호환)
    if (cmd.length() == 0) return;

    // ── :clear ────────────────────────────────────────────────────────────
    if (cmd == ":clear") {
        prog_sz = 0;
        Serial.println(F("CLR"));
        return;
    }

    // ── :w <bank> <page> ─────────────────────────────────────────────────
    if (cmd.startsWith(":w ")) {
        if (prog_sz == 0) {
            Serial.println(F("EMPTY"));
            return;
        }

        // 파싱: ":w <bank> <page>"
        String args      = cmd.substring(3);
        args.trim();
        int sp           = args.indexOf(' ');
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

        // 첫 업로드이거나 bank 전환 시 RESET
        if (!cores_reset) {
            RESET_CORES();
            delay(5);
            cores_reset = true;
        }

        // Bank 설정
        if (target_bank == 0) RAM_A14_LOW(); else RAM_A14_HIGH();

        // 헤더 응답
        Serial.print(F("B"));
        Serial.print(target_bank);
        Serial.print(F(":P"));
        Serial.print(current_page);
        Serial.print(F(" "));
        Serial.print(prog_sz);
        Serial.println(F("B"));

        // RAM 쓰기
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

        prog_sz = 0;   // 다음 페이지를 위해 버퍼 초기화
        Serial.println(F("\nOK"));
        return;
    }

    // ── :run ─────────────────────────────────────────────────────────────
    if (cmd == ":run") {
        cores_reset = false;
        RELEASE_CORES();
        Serial.println(F("RUN"));
        return;
    }

    // ── :rst ─────────────────────────────────────────────────────────────
    if (cmd == ":rst") {
        cores_reset = true;
        RESET_CORES();
        Serial.println(F("RST"));
        return;
    }

    // ── 명령어 라인 (LOAD, ADD, ...) ─────────────────────────────────────
    processLine(cmd);
}

// ── setup / loop ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    DDRB |= 0b00001110;  // PB1~3 (595 SCK/RCK/SER)
    DDRB |= 0b00110000;  // PB4~5 (RAM OE, WE)
    DDRC |= 0b00000100;  // PC2   (RESET)
    DDRC |= 0b00001000;  // PC3   (595 G#)
    DDRC |= 0b00010000;  // PC4   (RAM CE#)
    DDRC |= 0b00100000;  // PC5   (RAM A14)

    set_data_input();

    HC595_G_DISABLE();
    RAM_CE_DISABLE();
    RAM_OE_DISABLE();
    RAM_WE_DISABLE();
    RAM_A14_LOW();

    RESET_CORES();
    cores_reset = true;

    target_bank  = 0;
    current_page = 0;
    prog_sz      = 0;

    Serial.println(F("SMU v4.1"));
    Serial.println(F(":w <bank> <page> | :run | :rst | :clear"));
}

void loop() {
    if (Serial.available()) {
        handleCommand();
    }
}
