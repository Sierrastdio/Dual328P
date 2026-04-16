/*
 * ============================================================================
 * BPU Nano v2.0 - READY/ACK 청크 프로토콜
 * ============================================================================
 * 변경사항 (v1.0 → v2.0):
 * - READY/ACK 핸드셰이크 프로토콜 추가 (컴파일러 v2.0 호환)
 * - prog_buf 제거 → chunk_buf[64]로 교체 (Uno SRAM 2KB 고려)
 * - 청크 단위 수신 즉시 SRAM 기록
 * ============================================================================
 */

#include <avr/io.h>

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

#define CHUNK_SIZE     64     // Uno SRAM(2KB) 고려한 청크 크기
#define MAX_TOTAL      16384  // Nano/Uno는 Bank 하나(16KB)까지
#define RX_TIMEOUT_MS  3000

uint8_t chunk_buf[CHUNK_SIZE];

// ── 74HC595 / 주소 버스 ───────────────────────────────────────────────────────
void shiftOut_fast(uint8_t data) {
    for (uint8_t i = 0; i < 8; i++) {
        if (data & 0x80) HC595_SER_HIGH(); else HC595_SER_LOW();
        HC595_SCK_HIGH();
        HC595_SCK_LOW();
        data <<= 1;
    }
}

void setAddr(uint16_t addr) {
    addr &= 0x3FFF;
    HC595_RCK_LOW();
    shiftOut_fast(((addr >> 8) & 0x3F) << 1);
    shiftOut_fast(addr & 0xFF);
    HC595_RCK_HIGH();
    asm volatile("nop\n\t");
    HC595_RCK_LOW();
}

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

uint16_t calcPhysicalAddr(uint8_t page, uint8_t offset) {
    return ((uint16_t)(page & 0x7F) << 7) | (offset & 0x7F);
}

// ── 청크 수신 ─────────────────────────────────────────────────────────────────
bool recvChunk(uint8_t* buf, uint16_t size) {
    uint16_t got = 0;
    unsigned long deadline = millis() + RX_TIMEOUT_MS;
    while (got < size) {
        if (Serial.available()) {
            buf[got++] = Serial.read();
            deadline = millis() + RX_TIMEOUT_MS;
        } else if (millis() > deadline) {
            Serial.print(F("ERR: TIMEOUT got="));
            Serial.println(got);
            return false;
        }
    }
    return true;
}

// ── setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    DDRB |= 0b00001110;
    DDRB |= 0b00110000;
    DDRC |= 0b00000100;
    DDRC |= 0b00001000;
    DDRC |= 0b00010000;
    DDRC |= 0b00100000;

    set_data_input();
    HC595_G_DISABLE();
    RAM_CE_DISABLE();
    RAM_OE_DISABLE();
    RAM_WE_DISABLE();
    RAM_A14_LOW();
    RESET_CORES();

    Serial.println(F("BPU Nano v2.0 Ready"));
}

// ── loop ─────────────────────────────────────────────────────────────────────
void loop() {
    if (!Serial.available()) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    // ── :wb <bank> <page> <total> ─────────────────────────────────────────
    if (cmd.startsWith(":wb ")) {
        int bank, page, total;
        if (sscanf(cmd.c_str(), ":wb %d %d %d", &bank, &page, &total) != 3) {
            Serial.println(F("ERR: PARSE")); return;
        }
        if (bank < 0 || bank > 1)   { Serial.println(F("ERR: bank 0-1")); return; }
        if (page < 0 || page > 127) { Serial.println(F("ERR: page 0-127")); return; }
        if (total <= 0 || total > MAX_TOTAL) { Serial.println(F("ERR: SIZE")); return; }

        if (bank == 0) RAM_A14_LOW(); else RAM_A14_HIGH();

        Serial.println(F("READY"));   // ← 핸드셰이크

        uint32_t received = 0;
        while (received < (uint32_t)total) {
            uint16_t chunk_sz = min((uint32_t)CHUNK_SIZE,
                                    (uint32_t)total - received);

            if (!recvChunk(chunk_buf, chunk_sz)) return;

            for (uint16_t i = 0; i < chunk_sz; i++) {
                uint32_t offset = received + i;
                uint8_t  pg     = page + (offset >> 7);
                uint8_t  off    = offset & 0x7F;
                writeRAM(calcPhysicalAddr(pg, off), chunk_buf[i]);
            }

            received += chunk_sz;

            if (received >= (uint32_t)total) {
                Serial.println(F("OK"));
            } else {
                Serial.println(F("ACK"));   // ← 다음 청크 요청
            }
        }
        return;
    }

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

    if (cmd == ":rst") {
        RESET_CORES();
        Serial.println(F("RST"));
        return;
    }

    Serial.println(F("ERR: CMD"));
}