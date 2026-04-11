/*
 * ============================================================================
 * BPU Nano v1.0 - Binary Programmer Unit for Arduino Nano/Uno
 * ============================================================================
 * SMU Nano v5.0과 동일한 핀 배치 사용 (74HC595 주소 버스)
 *
 * 핀 배치:
 * - D2~D7, A0~A1: 데이터 버스 (D0~D7)
 * - D9:  SCK (74HC595-SMU1, SMU2)
 * - D10: RCK (74HC595-SMU1, SMU2)
 * - D11: SER (74HC595-SMU1)
 * - D12: RAM OE
 * - D13: RAM WE
 * - A2:  System RESET (Core 1, 2)
 * - A3:  74HC595 G# (Output Enable)
 * - A4:  62256 CE#
 * - A5:  62256 A14 (Bank Select)
 *
 * 프로토콜:
 *   :wb <bank> <page> <size>\n  → 이후 <size>바이트 바이너리 수신 → "OK"
 *   :run                        → Core 해제 → "RUN"
 *   :rst                        → Core 리셋 → "RST"
 * ============================================================================
 */

#include <avr/io.h>

// ── 핀 정의 ──────────────────────────────────────────────────────────────────
const uint8_t HC595_SER = 11;   // PB3
const uint8_t HC595_SCK = 9;    // PB1
const uint8_t HC595_RCK = 10;   // PB2
const uint8_t HC595_G   = A3;   // PC3

const uint8_t RAM_CE  = A4;     // PC4
const uint8_t RAM_OE  = 12;     // PB4
const uint8_t RAM_WE  = 13;     // PB5
const uint8_t RAM_A14 = A5;     // PC5

const uint8_t SYS_RESET = A2;   // PC2

// ── 설정 ─────────────────────────────────────────────────────────────────────
#define MAX_PROG      256
#define RX_TIMEOUT_MS 3000

uint8_t prog_buf[MAX_PROG];

// ── 매크로 ───────────────────────────────────────────────────────────────────
#define HC595_G_ENABLE()    PORTC &= ~(1 << 3)
#define HC595_G_DISABLE()   PORTC |=  (1 << 3)
#define HC595_RCK_LOW()     PORTB &= ~(1 << 2)
#define HC595_RCK_HIGH()    PORTB |=  (1 << 2)
#define HC595_SCK_LOW()     PORTB &= ~(1 << 1)
#define HC595_SCK_HIGH()    PORTB |=  (1 << 1)
#define HC595_SER_LOW()     PORTB &= ~(1 << 3)
#define HC595_SER_HIGH()    PORTB |=  (1 << 3)

#define RAM_CE_ENABLE()     PORTC &= ~(1 << 4)
#define RAM_CE_DISABLE()    PORTC |=  (1 << 4)
#define RAM_OE_ENABLE()     PORTB &= ~(1 << 4)
#define RAM_OE_DISABLE()    PORTB |=  (1 << 4)
#define RAM_WE_ENABLE()     PORTB &= ~(1 << 5)
#define RAM_WE_DISABLE()    PORTB |=  (1 << 5)
#define RAM_A14_LOW()       PORTC &= ~(1 << 5)
#define RAM_A14_HIGH()      PORTC |=  (1 << 5)

#define RESET_CORES()       PORTC &= ~(1 << 2)
#define RELEASE_CORES()     PORTC |=  (1 << 2)

// ── 데이터 버스 ──────────────────────────────────────────────────────────────
inline void set_data_output() {
    DDRD |= 0b11111100;
    DDRC |= 0b00000011;
}

inline void set_data_input() {
    DDRD  &= 0b00000011;
    PORTD &= 0b00000011;
    DDRC  &= 0b11111100;
    PORTC &= ~(1 << 0);
    PORTC &= ~(1 << 1);
}

inline void write_data_bus(uint8_t data) {
    PORTD = (PORTD & 0b00000011) | ((data << 2) & 0b11111100);
    PORTC = (PORTC & ~0b00000011) | (data >> 6);
}

// ── 74HC595 시리얼 전송 ──────────────────────────────────────────────────────
void shiftOut_fast(uint8_t data) {
    for (uint8_t i = 0; i < 8; i++) {
        if (data & 0x80) HC595_SER_HIGH(); else HC595_SER_LOW();
        HC595_SCK_HIGH();
        HC595_SCK_LOW();
        data <<= 1;
    }
}

// ── 주소 설정 ────────────────────────────────────────────────────────────────
// SMU1: A0~A7 (QA~QH), SMU2: A8~A13 (QB~QG)
void setAddr(uint16_t addr) {
    addr &= 0x3FFF;
    HC595_RCK_LOW();
    shiftOut_fast(((addr >> 8) & 0x3F) << 1);  // → SMU2 (A8~A13)
    shiftOut_fast(addr & 0xFF);                  // → SMU1 (A0~A7)
    HC595_RCK_HIGH();
    asm volatile("nop\n\t");
    HC595_RCK_LOW();
}

// ── 물리 주소 계산 ───────────────────────────────────────────────────────────
uint16_t calcPhysicalAddr(uint8_t page, uint8_t offset) {
    return ((uint16_t)(page & 0x7F) << 7) | (offset & 0x7F);
}

// ── RAM 쓰기 ─────────────────────────────────────────────────────────────────
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

// ── setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // 출력 핀 설정
    DDRB |= 0b00001110;   // D9(SCK), D10(RCK), D11(SER)
    DDRB |= 0b00110000;   // D12(OE), D13(WE)
    DDRC |= 0b00000100;   // A2(RESET)
    DDRC |= 0b00001000;   // A3(595 G#)
    DDRC |= 0b00010000;   // A4(CE#)
    DDRC |= 0b00100000;   // A5(A14)

    set_data_input();

    HC595_G_DISABLE();
    RAM_CE_DISABLE();
    RAM_OE_DISABLE();
    RAM_WE_DISABLE();
    RAM_A14_LOW();

    RESET_CORES();

    Serial.println(F("BPU Nano v1.0 Ready"));
}

// ── loop ─────────────────────────────────────────────────────────────────────
void loop() {
    if (!Serial.available()) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    // ── :wb <bank> <page> <size> ─────────────────────────────────────────
    if (cmd.startsWith(":wb ")) {
        int bank, page, size;
        if (sscanf(cmd.c_str(), ":wb %d %d %d", &bank, &page, &size) != 3) {
            Serial.println(F("ERR: PARSE"));
            return;
        }
        if (bank < 0 || bank > 1) {
            Serial.println(F("ERR: bank 0-1"));
            return;
        }
        if (page < 0 || page > 127) {
            Serial.println(F("ERR: page 0-127"));
            return;
        }
        if (size <= 0 || size > MAX_PROG) {
            Serial.println(F("ERR: SIZE"));
            return;
        }

        // 바이너리 수신
        size_t received = 0;
        unsigned long deadline = millis() + RX_TIMEOUT_MS;
        while (received < (size_t)size) {
            if (Serial.available()) {
                prog_buf[received++] = Serial.read();
                deadline = millis() + RX_TIMEOUT_MS;
            } else if (millis() > deadline) {
                Serial.print(F("ERR: TIMEOUT received="));
                Serial.println(received);
                return;
            }
        }

        // SRAM 기록
        if (bank == 0) RAM_A14_LOW(); else RAM_A14_HIGH();
        for (int i = 0; i < (int)received; i++) {
            writeRAM(calcPhysicalAddr(page, i & 0x7F), prog_buf[i]);
        }

        Serial.println(F("OK"));
        return;
    }

    // ── :run ─────────────────────────────────────────────────────────────
    if (cmd == ":run") {
        set_data_input();
        HC595_G_DISABLE();
        RAM_CE_DISABLE();
        RAM_OE_DISABLE();
        RAM_WE_DISABLE();
        RELEASE_CORES();
        Serial.println(F("RUN"));
        return;
    }

    // ── :rst ─────────────────────────────────────────────────────────────
    if (cmd == ":rst") {
        RESET_CORES();
        Serial.println(F("RST"));
        return;
    }

    Serial.println(F("ERR: CMD"));
}