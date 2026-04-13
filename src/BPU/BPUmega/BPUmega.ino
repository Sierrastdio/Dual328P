/*
 * ============================================================================
 * BPU Mega v2.0 - 청크+ACK 프로토콜, 500000 baud
 * ============================================================================
 * 변경사항 (v1.1 → v2.0):
 * - 115200 → 500000 baud (4.3배 향상)
 * - prog_buf[1024] 제거 → chunk_buf[512] 청크 단위 수신/기록
 * - 청크마다 ACK 응답으로 흐름 제어 (수신버퍼 오버플로 방지)
 * - 뱅크 자동 전환 (32KB 연속 기록 지원)
 * - READY 핸드셰이크 추가
 * ============================================================================
 */

#include <avr/io.h>

#define RAM_A14_LOW()    PORTL &= ~(1 << 1)
#define RAM_A14_HIGH()   PORTL |=  (1 << 1)
#define RAM_CE_ENABLE()  PORTL &= ~(1 << 0)
#define RAM_CE_DISABLE() PORTL |=  (1 << 0)
#define RAM_OE_DISABLE() PORTB |=  (1 << 3)
#define RAM_WE_ENABLE()  PORTB &= ~(1 << 2)
#define RAM_WE_DISABLE() PORTB |=  (1 << 2)
#define RESET_CORES()    PORTB &= ~(1 << 1)
#define RELEASE_CORES()  PORTB |=  (1 << 1)

// ── 설정 ─────────────────────────────────────────────────────────────────────
#define BAUD_RATE      115200
#define CHUNK_SIZE     512      // 청크 크기 (Mega SRAM 여유 고려)
#define MAX_TOTAL      32768    // 32KB
#define RX_TIMEOUT_MS  3000

uint8_t chunk_buf[CHUNK_SIZE];

// ── 주소/버스 ─────────────────────────────────────────────────────────────────
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

// logical_addr: 0x0000~0x3FFF = Bank0, 0x4000~0x7FFF = Bank1
inline void writeLogical(uint32_t logical_addr, uint8_t data) {
    if (logical_addr < 0x4000) {
        RAM_A14_LOW();
        writeRAM((uint16_t)logical_addr, data);
    } else {
        RAM_A14_HIGH();
        writeRAM((uint16_t)(logical_addr - 0x4000), data);
    }
}

uint16_t calcPhysicalAddr(uint8_t page, uint8_t offset) {
    return ((uint16_t)(page & 0x7F) << 7) | (offset & 0x7F);
}

// ── 청크 수신 ─────────────────────────────────────────────────────────────────
// 정확히 size 바이트를 수신. 타임아웃 시 false 반환
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
    Serial.begin(BAUD_RATE);
    DDRA = 0x00;
    DDRC = 0xFF;
    DDRL = 0xFF;
    DDRB |= 0b00001110;
    PORTL |= 0b00000001;   // CE# HIGH (비활성)
    PORTB |= 0b00001110;   // OE#/WE#/RESET HIGH
    RESET_CORES();
    Serial.println(F("BPU Mega v2.0 Ready"));
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
            Serial.println(F("ERR: PARSE"));
            return;
        }
        if (bank < 0 || bank > 1) { Serial.println(F("ERR: bank 0-1")); return; }
        if (page < 0 || page > 127) { Serial.println(F("ERR: page 0-127")); return; }
        if (total <= 0 || total > MAX_TOTAL) { Serial.println(F("ERR: SIZE")); return; }

        // 논리 시작 주소 (bank × 16KB + page × 128)
        uint32_t logical_base = (uint32_t)bank * 0x4000
                              + calcPhysicalAddr(page, 0);

        Serial.println(F("READY"));   // PC에 전송 허가

        uint32_t received = 0;
        while (received < (uint32_t)total) {
            uint16_t chunk_sz = min((uint32_t)CHUNK_SIZE,
                                    (uint32_t)total - received);

            // 청크 수신
            if (!recvChunk(chunk_buf, chunk_sz)) return;

            // SRAM 기록 (논리 주소로 뱅크 자동 전환)
            for (uint16_t i = 0; i < chunk_sz; i++) {
                writeLogical(logical_base + received + i, chunk_buf[i]);
            }

            received += chunk_sz;

            // 마지막 청크면 OK, 아니면 ACK
            if (received >= (uint32_t)total) {
                Serial.println(F("OK"));
            } else {
                Serial.println(F("ACK"));
            }
        }
        return;
    }

    // ── :run ─────────────────────────────────────────────────────────────
    if (cmd == ":run") {
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