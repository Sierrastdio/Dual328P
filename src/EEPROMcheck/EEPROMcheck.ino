#include <avr/eeprom.h>

// Core 1 EEPROM layout
#define EEPROM_ADDR_US    ((uint32_t*)0x00)
#define EEPROM_ADDR_INSTR ((uint32_t*)0x04)
#define EEPROM_ADDR_FLAG  ((uint8_t*) 0x08)
#define EEPROM_ADDR_REGA  ((uint8_t*) 0x09)
#define EEPROM_FLAG_VALID 0xAA

void setup() {
    Serial.begin(9600);
    delay(500);

    Serial.println("=== Core 1 EEPROM Dump ===");

    uint8_t flag = eeprom_read_byte(EEPROM_ADDR_FLAG);
    Serial.print("FLAG: 0x");
    Serial.print(flag, HEX);
    Serial.println(flag == EEPROM_FLAG_VALID ? " (VALID)" : " (INVALID — no data)");

    if (flag == EEPROM_FLAG_VALID) {
        uint32_t us    = eeprom_read_dword(EEPROM_ADDR_US);
        uint32_t instr = eeprom_read_dword(EEPROM_ADDR_INSTR);
        uint8_t  rega  = eeprom_read_byte (EEPROM_ADDR_REGA);

        Serial.print("timing_result_us:    "); Serial.println(us);
        Serial.print("timing_result_instr: "); Serial.println(instr);
        if (instr > 0) {
            Serial.print("ns/instr:            ");
            Serial.println((us * 1000UL) / instr);
        }
        Serial.print("regA:                0x");
        Serial.println(rega, HEX);
    }

    // 원시 EEPROM 전체 덤프 (처음 32바이트)
    Serial.println("\n=== Raw EEPROM (0x00~0x1F) ===");
    for (uint8_t i = 0; i < 32; i++) {
        uint8_t val = eeprom_read_byte((uint8_t*)i);
        if (i % 8 == 0) {
            Serial.print("0x");
            if (i < 16) Serial.print("0");
            Serial.print(i, HEX);
            Serial.print(": ");
        }
        if (val < 0x10) Serial.print("0");
        Serial.print(val, HEX);
        Serial.print(" ");
        if (i % 8 == 7) Serial.println();
    }
}

void loop() {}
