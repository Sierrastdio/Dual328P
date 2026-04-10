/*
 * ============================================================================
 * BPU v1.1 - Binary Programmer Unit for Arduino Mega 2560
 * ============================================================================
 * :wb <bank> <page> <size>
 * 이 명령어를 통해 PC로부터 바이너리 데이터를 직접 수신하여 SRAM에 기록.
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

#define MAX_PROG 1024
#define RX_TIMEOUT_MS 3000   // 수신 타임아웃 (ms)

uint8_t prog_buf[MAX_PROG];

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

uint16_t calcPhysicalAddr(uint8_t page, uint8_t offset) {
    return ((uint16_t)(page & 0x7F) << 7) | (offset & 0x7F);
}

void setup() {
    Serial.begin(115200);
    DDRA = 0x00;
    DDRC = 0xFF;
    DDRL = 0xFF;
    DDRB |= 0b00001110;
    PORTL |= 0b00000001;
    PORTB |= 0b00001110;
    RESET_CORES();
    Serial.println(F("BPU v1.1 Ready"));
}

void loop() {
    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();

        if (cmd.startsWith(":wb ")) {
            int bank, page, size;
            if (sscanf(cmd.c_str(), ":wb %d %d %d", &bank, &page, &size) == 3) {
                if (size <= 0 || size > MAX_PROG) {
                    Serial.println(F("ERR: SIZE"));
                    return;
                }

                // 타임아웃 있는 수신 루프
                size_t received = 0;
                unsigned long deadline = millis() + RX_TIMEOUT_MS;
                while (received < (size_t)size) {
                    if (Serial.available()) {
                        prog_buf[received++] = Serial.read();
                        deadline = millis() + RX_TIMEOUT_MS; // 수신될 때마다 갱신
                    } else if (millis() > deadline) {
                        Serial.print(F("ERR: TIMEOUT received="));
                        Serial.println(received);
                        return;
                    }
                }

                if (bank == 0) RAM_A14_LOW(); else RAM_A14_HIGH();
                for (int i = 0; i < (int)received; i++) {
                    writeRAM(calcPhysicalAddr(page, i & 0x7F), prog_buf[i]);
                }
                Serial.println(F("OK"));

            } else {
                Serial.println(F("ERR: PARSE"));
            }

        } else if (cmd == ":run") {
            RELEASE_CORES();
            Serial.println(F("RUN"));
        } else if (cmd == ":rst") {
            RESET_CORES();
            Serial.println(F("RST"));
        } else {
            Serial.println(F("ERR: CMD"));
        }
    }
}