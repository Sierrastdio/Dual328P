/*
 * ============================================================================
 * BPU Mega v3.0 - Core serial monitoring
 * ============================================================================
 * Changes (v2.0 → v3.0):
 * - Serial1 (pin 19) ← Core 1 TX : receives boot-time timing result
 * - Serial2 (pin 17) ← Core 2 TX : receives boot-time timing result
 * - Core UART output auto-forwarded to Serial (USB→laptop) with prefix
 * - :timing  prints last cached result from both cores
 * - :timing1 / :timing2  prints result from individual core
 * - Removed SRAM timing read (no longer used)
 *
 * Wiring (RX only — TX not needed from Mega side):
 *   Mega pin 19 (Serial1 RX) ← Core 1 PD1 (TX)
 *   Mega pin 17 (Serial2 RX) ← Core 2 PD1 (TX)
 *   GND common
 *
 * Core UART is active ONLY during boot (before 595 is driven).
 * After that, PD1 becomes 595 SCK — noise on Serial1/2 is expected.
 * Mega filters by line-complete '\n' so partial noise bytes are discarded.
 * ============================================================================
 */

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
#define BAUD_RATE       115200
#define CORE_BAUD       9600    // must match _uart_init() in Core firmware
#define CHUNK_SIZE      512
#define MAX_TOTAL       32768
#define RX_TIMEOUT_MS   3000

// ─── Core serial line buffers ─────────────────────────────────────────────────
// Stores the last complete '[TIMING] ...' line received from each core.
// Size 80 is enough for "TIMING: 5120 instr in 20000 us (3906 ns/instr)\r\n"
#define TIMING_LINE_LEN 80

char core1_timing[TIMING_LINE_LEN] = "";   // "" = not yet received
char core2_timing[TIMING_LINE_LEN] = "";

// Incremental line assemblers (filled byte-by-byte in loop())
static char _c1_buf[TIMING_LINE_LEN];
static char _c2_buf[TIMING_LINE_LEN];
static uint8_t _c1_pos = 0;
static uint8_t _c2_pos = 0;

// ─── Chunk buffer ─────────────────────────────────────────────────────────────
uint8_t chunk_buf[CHUNK_SIZE];

// ─── RAM helpers ─────────────────────────────────────────────────────────────
void set_addr_bus(uint16_t addr) {
    addr &= 0x3FFF;
    PORTC = addr & 0xFF;
    PORTL = (PORTL & 0b00000011) | ((addr >> 6) & 0b11111100);
}

void writeRAM(uint16_t addr, uint8_t data) {
    set_addr_bus(addr);
    DDRA = 0xFF;
    PORTA = data;
    RAM_CE_ENABLE();
    RAM_OE_DISABLE();
    RAM_WE_ENABLE();
    asm volatile("nop\n\t nop\n\t");
    RAM_WE_DISABLE();
    RAM_CE_DISABLE();
    DDRA = 0x00;
}

uint8_t readRAM(uint16_t addr) {
    set_addr_bus(addr);
    DDRA = 0x00;
    PORTA = 0x00;
    RAM_CE_ENABLE();
    RAM_OE_DISABLE();
    asm volatile("nop\n\t nop\n\t");
    uint8_t data = PINA;
    RAM_CE_DISABLE();
    return data;
}

inline void writeLogical(uint32_t addr, uint8_t data) {
    if (addr < 0x4000) { RAM_A14_LOW();  writeRAM((uint16_t)addr, data); }
    else               { RAM_A14_HIGH(); writeRAM((uint16_t)(addr - 0x4000), data); }
}

inline uint8_t readLogical(uint32_t addr) {
    if (addr < 0x4000) { RAM_A14_LOW();  return readRAM((uint16_t)addr); }
    else               { RAM_A14_HIGH(); return readRAM((uint16_t)(addr - 0x4000)); }
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
            Serial.print(F("ERR: TIMEOUT got="));
            Serial.println(got);
            return false;
        }
    }
    return true;
}

// ─── Core serial line handler ─────────────────────────────────────────────────
// Reads one byte from `src`, appends to `buf`/`pos`.
// On '\n': if line starts with "[TIMING]", saves to `dest` and prints to Serial.
// Noise bytes (from 595 SCK after boot) never form a complete '[TIMING]' line,
// so they are silently discarded when the buffer fills or '\n' arrives without match.
void _handle_core_serial(HardwareSerial& src,
                          char* buf, uint8_t& pos,
                          char* dest, const __FlashStringHelper* tag) {
    while (src.available()) {
        char c = (char)src.read();

        if (c == '\n') {
            buf[pos] = '\0';
            // strip trailing \r if present
            if (pos > 0 && buf[pos - 1] == '\r') buf[--pos] = '\0';

            if (strncmp(buf, "[TIMING]", 8) == 0) {
                strncpy(dest, buf, TIMING_LINE_LEN - 1);
                dest[TIMING_LINE_LEN - 1] = '\0';
                Serial.print(tag);
                Serial.println(dest);
            }
            // reset assembler regardless (noise lines also cleared)
            pos = 0;

        } else {
            if (pos < TIMING_LINE_LEN - 1) buf[pos++] = c;
            else                           pos = 0;  // overflow → discard
        }
    }
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(BAUD_RATE);     // laptop (USB)
    Serial1.begin(CORE_BAUD);   // Core 1 TX → Mega pin 19
    Serial2.begin(CORE_BAUD);   // Core 2 TX → Mega pin 17

    DDRA = 0x00;
    DDRC = 0xFF;
    DDRL = 0xFF;
    DDRB |= 0b00001110;
    PORTL |= 0b00000001;   // CE# HIGH
    PORTB |= 0b00001110;   // OE#/WE#/RESET HIGH

    RESET_CORES();
    Serial.println(F("BPU Mega v3.0 Ready"));
    Serial.println(F("  Serial1 (pin 19) listening for Core 1 timing"));
    Serial.println(F("  Serial2 (pin 17) listening for Core 2 timing"));
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    // ── Always forward Core 1/2 timing lines to laptop ───────────────────────
    _handle_core_serial(Serial1, _c1_buf, _c1_pos, core1_timing, F("[CORE1] "));
    _handle_core_serial(Serial2, _c2_buf, _c2_pos, core2_timing, F("[CORE2] "));

    // ── Commands from laptop ──────────────────────────────────────────────────
    if (!Serial.available()) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    // ── :wb <bank> <page> <total> ─────────────────────────────────────────────
    if (cmd.startsWith(":wb ")) {
        int bank, page;
        // Use 32-bit type: on AVR, int is 16-bit; total==32768 would overflow %d and trip ERR: SIZE.
        unsigned long total;
        if (sscanf(cmd.c_str(), ":wb %d %d %lu", &bank, &page, &total) != 3) {
            Serial.println(F("ERR: PARSE")); return;
        }
        if (bank < 0 || bank > 1)            { Serial.println(F("ERR: bank 0-1")); return; }
        if (page < 0 || page > 127)          { Serial.println(F("ERR: page 0-127")); return; }
        if (total == 0UL || total > (unsigned long)MAX_TOTAL) { Serial.println(F("ERR: SIZE")); return; }

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
        // Clear cached timing before new run so stale results aren't shown
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
    // Shows last received timing from both cores.
    // Results arrive automatically at next boot — no polling needed.
    if (cmd == ":timing") {
        Serial.print(F("[CORE1] "));
        Serial.println(core1_timing[0] ? core1_timing : "no result yet");
        Serial.print(F("[CORE2] "));
        Serial.println(core2_timing[0] ? core2_timing : "no result yet");
        return;
    }

    // ── :timing1 / :timing2 ───────────────────────────────────────────────────
    if (cmd == ":timing1") {
        Serial.print(F("[CORE1] "));
        Serial.println(core1_timing[0] ? core1_timing : "no result yet");
        return;
    }
    if (cmd == ":timing2") {
        Serial.print(F("[CORE2] "));
        Serial.println(core2_timing[0] ? core2_timing : "no result yet");
        return;
    }

    Serial.println(F("ERR: CMD"));
}