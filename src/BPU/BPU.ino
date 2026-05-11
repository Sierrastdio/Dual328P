/*
 * ============================================================================
 * BPU Mega v3.1 - EEPROM-based timing (boot window removed)
 * ============================================================================
 * Timing data is read directly from Core EEPROM via Uno after chip extraction.
 * Serial1/Serial2 monitoring retained for passive noise filtering only.
 *
 * Wiring:
 *   Mega pin 19 (Serial1 RX) ← Core 1 PD1 (TX)
 *   Mega pin 17 (Serial2 RX) ← Core 2 PD1 (TX)
 *   GND common
 * ============================================================================
 */
#include <Arduino.h>
#include <avr/io.h>

// ─── RAM control ─────────────────────────────────────────────────────────────
#define RAM_A14_LOW()    PORTL &= ~(1 << 1)
#define RAM_A14_HIGH()   PORTL |=  (1 << 1)
#define RAM_CE_ENABLE()  PORTL &= ~(1 << 0)
#define RAM_CE_DISABLE() PORTL |=  (1 << 0)
#define RAM_OE_DISABLE() PORTB |=  (1 << 3)
#define RAM_WE_ENABLE()  PORTB &= ~(1 << 2)
#define RAM_WE_DISABLE() PORTB |=  (1 << 2)
#define RESET_CORES()    PORTB &= ~(1 << 1)
#define RELEASE_CORES()  PORTB |=  (1 << 1)

// ─── Config ───────────────────────────────────────────────────────────────────
#define BAUD_RATE      115200
#define CORE_BAUD      9600
#define CHUNK_SIZE     512
#define MAX_TOTAL      32768
#define RX_TIMEOUT_MS  3000

// ─── Core serial buffers (passive — noise filtered, not relied upon) ──────────
#define TIMING_LINE_LEN 80

char core1_timing[TIMING_LINE_LEN] = "";
char core2_timing[TIMING_LINE_LEN] = "";

static char    _c1_buf[TIMING_LINE_LEN];
static char    _c2_buf[TIMING_LINE_LEN];
static uint8_t _c1_pos = 0;
static uint8_t _c2_pos = 0;

uint8_t chunk_buf[CHUNK_SIZE];

// ─── RAM helpers ─────────────────────────────────────────────────────────────
void set_addr_bus(uint16_t addr) {
    addr &= 0x3FFF;
    PORTC = addr & 0xFF;
    PORTL = (PORTL & 0b00000011) | ((addr >> 6) & 0b11111100);
}

void writeRAM(uint16_t addr, uint8_t data) {
    set_addr_bus(addr);
    DDRA = 0xFF; PORTA = data;
    RAM_CE_ENABLE(); RAM_OE_DISABLE(); RAM_WE_ENABLE();
    asm volatile("nop\n\t nop\n\t");
    RAM_WE_DISABLE(); RAM_CE_DISABLE();
    DDRA = 0x00;
}

uint8_t readRAM(uint16_t addr) {
    set_addr_bus(addr);
    DDRA = 0x00; PORTA = 0x00;
    RAM_CE_ENABLE(); RAM_OE_DISABLE();
    asm volatile("nop\n\t nop\n\t");
    uint8_t data = PINA;
    RAM_CE_DISABLE();
    return data;
}

inline void writeLogical(uint32_t addr, uint8_t data) {
    if (addr < 0x4000) { RAM_A14_LOW();  writeRAM((uint16_t)addr, data); }
    else               { RAM_A14_HIGH(); writeRAM((uint16_t)(addr - 0x4000), data); }
}

uint16_t calcPhysicalAddr(uint8_t page, uint8_t offset) {
    return ((uint16_t)(page & 0x7F) << 7) | (offset & 0x7F);
}

// ─── Chunk receive ────────────────────────────────────────────────────────────
bool recvChunk(uint8_t* buf, uint16_t size) {
    uint16_t got = 0;
    unsigned long deadline = millis() + RX_TIMEOUT_MS;
    while (got < size) {
        if (Serial.available()) {
            buf[got++] = Serial.read();
            deadline = millis() + RX_TIMEOUT_MS;
        } else if (millis() > deadline) {
            Serial.print(F("ERR: TIMEOUT got=")); Serial.println(got);
            return false;
        }
    }
    return true;
}

// ─── Core serial handler (passive monitor only) ───────────────────────────────
void _handle_core_serial(HardwareSerial& src,
                          char* buf, uint8_t& pos,
                          char* dest, const __FlashStringHelper* tag) {
    while (src.available()) {
        char c = (char)src.read();
        if (c == '\n') {
            buf[pos] = '\0';
            if (pos > 0 && buf[pos - 1] == '\r') buf[--pos] = '\0';
            if (strncmp(buf, "[TIMING]", 8) == 0 ||
                strncmp(buf, "[DEBUG]",  7) == 0) {
                strncpy(dest, buf, TIMING_LINE_LEN - 1);
                dest[TIMING_LINE_LEN - 1] = '\0';
                Serial.print(tag);
                Serial.println(dest);
            }
            pos = 0;
        } else {
            if (pos < TIMING_LINE_LEN - 1) buf[pos++] = c;
            else                           pos = 0;
        }
    }
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(BAUD_RATE);
    Serial1.begin(CORE_BAUD);
    Serial2.begin(CORE_BAUD);

    DDRA  = 0x00;
    DDRC  = 0xFF;
    DDRL  = 0xFF;
    DDRB |= 0b00001110;
    PORTL |= 0b00000001;   // CE# HIGH (disabled)
    PORTB |= 0b00001110;   // OE#/WE#/RESET HIGH

    RESET_CORES();
    Serial.println(F("BPU Mega v3.1 Ready"));
    Serial.println(F("Commands: :run :rst :wb <bank> <page> <total> :timing :timing1 :timing2"));
    Serial.println(F("Timing data: extract Core chip, read EEPROM via Uno sketch"));
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    // 수동 모니터링 — [TIMING]/[DEBUG] 라인만 포워딩
    _handle_core_serial(Serial1, _c1_buf, _c1_pos, core1_timing, F("[CORE1] "));
    _handle_core_serial(Serial2, _c2_buf, _c2_pos, core2_timing, F("[CORE2] "));

    if (!Serial.available()) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    // ── :wb ──────────────────────────────────────────────────────────────────
    if (cmd.startsWith(":wb ")) {
        int bank, page; unsigned long total;
        if (sscanf(cmd.c_str(), ":wb %d %d %lu", &bank, &page, &total) != 3) {
            Serial.println(F("ERR: PARSE")); return;
        }
        if (bank < 0 || bank > 1)                             { Serial.println(F("ERR: bank 0-1")); return; }
        if (page < 0 || page > 127)                           { Serial.println(F("ERR: page 0-127")); return; }
        if (total == 0UL || total > (unsigned long)MAX_TOTAL) { Serial.println(F("ERR: SIZE")); return; }

        RESET_CORES();   // 쓰기 전 코어 강제 리셋

        uint32_t logical_base = (uint32_t)bank * 0x4000 + calcPhysicalAddr(page, 0);
        Serial.println(F("READY"));

        uint32_t received = 0;
        const uint32_t total_u = (uint32_t)total;
        while (received < total_u) {
            uint16_t chunk_sz = min((uint32_t)CHUNK_SIZE, total_u - received);
            if (!recvChunk(chunk_buf, chunk_sz)) return;
            for (uint16_t i = 0; i < chunk_sz; i++)
                writeLogical(logical_base + received + i, chunk_buf[i]);
            received += chunk_sz;
            Serial.println(received >= total_u ? F("OK") : F("ACK"));
        }
        return;
    }

    // ── :run ─────────────────────────────────────────────────────────────────
    if (cmd == ":run") {
        core1_timing[0] = '\0';
        core2_timing[0] = '\0';
        RELEASE_CORES();
        Serial.println(F("RUN"));
        return;
    }

    // ── :rst ─────────────────────────────────────────────────────────────────
    if (cmd == ":rst") {
        RESET_CORES();
        Serial.println(F("RST"));
        return;
    }

    // ── :timing ──────────────────────────────────────────────────────────────
    if (cmd == ":timing") {
        Serial.print(F("[CORE1] "));
        Serial.println(core1_timing[0] ? core1_timing : "no data (check EEPROM via Uno)");
        Serial.print(F("[CORE2] "));
        Serial.println(core2_timing[0] ? core2_timing : "no data (check EEPROM via Uno)");
        return;
    }

    if (cmd == ":timing1") {
        Serial.print(F("[CORE1] "));
        Serial.println(core1_timing[0] ? core1_timing : "no data (check EEPROM via Uno)");
        return;
    }

    if (cmd == ":timing2") {
        Serial.print(F("[CORE2] "));
        Serial.println(core2_timing[0] ? core2_timing : "no data (check EEPROM via Uno)");
        return;
    }

    Serial.println(F("ERR: CMD"));
}
