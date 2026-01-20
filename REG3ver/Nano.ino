/*
 * ============================================================================
 * Arduino Nano - System Monitor & ROM Programmer
 * ============================================================================
 * 
 * This code monitors the 6 registers (3 per core) and provides ROM programming
 * functionality for the dual-core system.
 * 
 * Hardware Connections:
 * ---------------------
 * Data Bus (D2~D9): 8-bit data monitoring/programming
 * 
 * Register Monitor Pins:
 *   Core 1:
 *     D10 -> 74HC138 Y1 (Register A - 0x70)
 *     D11 -> 74HC138 Y2 (Register B - 0x71)
 *     A4  -> 74HC138 Y3 (Register C - 0x72)
 *   
 *   Core 2:
 *     A5  -> 74HC138 Y4 (Register A - 0x73)
 *     A6  -> 74HC138 Y5 (Register B - 0x74)
 *     A7  -> 74HC138 Y6 (Register C - 0x75)
 * 
 * ROM Control:
 *   D12 -> 28C256 OE (Output Enable)
 *   D13 -> 28C256 WE (Write Enable)
 * 
 * 74HC595 (Address Control):
 *   A1 -> DS (Serial Data)
 *   A2 -> SHCP (Shift Clock)
 *   A3 -> STCP (Latch Clock)
 * 
 * System Control:
 *   A0 -> RESET (Both Cores)
 * 
 * ============================================================================
 */

// ============================================================================
// Pin Definitions
// ============================================================================

// Data Bus
const uint8_t DATA_PINS[] = {2, 3, 4, 5, 6, 7, 8, 9};

// Core 1 Register Monitor Pins
const uint8_t CORE1_REG_A = 10;  // Y1
const uint8_t CORE1_REG_B = 11;  // Y2
const uint8_t CORE1_REG_C = A4;  // Y3

// Core 2 Register Monitor Pins
const uint8_t CORE2_REG_A = A5;  // Y4
const uint8_t CORE2_REG_B = A6;  // Y5
const uint8_t CORE2_REG_C = A7;  // Y6

// ROM Control
const uint8_t ROM_OE = 12;
const uint8_t ROM_WE = 13;

// 74HC595 Control (Address Generator)
const uint8_t HC595_DS = A1;    // Serial Data
const uint8_t HC595_SHCP = A2;  // Shift Clock
const uint8_t HC595_STCP = A3;  // Latch Clock

// System Control
const uint8_t SYS_RESET = A0;

// ============================================================================
// Register Monitor Arrays
// ============================================================================
const uint8_t core1_pins[] = {CORE1_REG_A, CORE1_REG_B, CORE1_REG_C};
const uint8_t core2_pins[] = {CORE2_REG_A, CORE2_REG_B, CORE2_REG_C};
const char* reg_names[] = {"A", "B", "C"};

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println(F("==========================================="));
    Serial.println(F("8-bit Dual Core Monitor v1.0"));
    Serial.println(F("3 Registers per Core (A, B, C)"));
    Serial.println(F("==========================================="));
    
    // Configure data bus as INPUT (monitoring mode)
    for(int i = 0; i < 8; i++) {
        pinMode(DATA_PINS[i], INPUT);
    }
    
    // Configure register monitor pins as INPUT with pull-up
    for(int i = 0; i < 3; i++) {
        pinMode(core1_pins[i], INPUT_PULLUP);
        pinMode(core2_pins[i], INPUT_PULLUP);
    }
    
    // Configure ROM control pins
    pinMode(ROM_OE, OUTPUT);
    pinMode(ROM_WE, OUTPUT);
    digitalWrite(ROM_OE, HIGH);  // Disable ROM output initially
    digitalWrite(ROM_WE, HIGH);  // Write disabled (10kΩ pull-up also present)
    
    // Configure 74HC595 control pins
    pinMode(HC595_DS, OUTPUT);
    pinMode(HC595_SHCP, OUTPUT);
    pinMode(HC595_STCP, OUTPUT);
    
    // Configure system reset
    pinMode(SYS_RESET, OUTPUT);
    digitalWrite(SYS_RESET, HIGH);  // Cores running
    
    Serial.println(F("[READY] Monitoring started..."));
    Serial.println();
}

// ============================================================================
// Main Loop - Register Monitoring
// ============================================================================
void loop() {
    // Monitor Core 1 Registers
    for(int i = 0; i < 3; i++) {
        if(digitalRead(core1_pins[i]) == LOW) {  // Active LOW signal
            uint8_t data = readDataBus();
            
            Serial.print(F("[CORE1] REG "));
            Serial.print(reg_names[i]);
            Serial.print(F(": 0x"));
            if(data < 0x10) Serial.print('0');
            Serial.print(data, HEX);
            Serial.print(F(" ("));
            Serial.print(data);
            Serial.println(F(")"));
            
            delay(5);  // Debounce
        }
    }
    
    // Monitor Core 2 Registers
    for(int i = 0; i < 3; i++) {
        if(digitalRead(core2_pins[i]) == LOW) {  // Active LOW signal
            uint8_t data = readDataBus();
            
            Serial.print(F("[CORE2] REG "));
            Serial.print(reg_names[i]);
            Serial.print(F(": 0x"));
            if(data < 0x10) Serial.print('0');
            Serial.print(data, HEX);
            Serial.print(F(" ("));
            Serial.print(data);
            Serial.println(F(")"));
            
            delay(5);  // Debounce
        }
    }
    
    // Check for serial commands
    if(Serial.available()) {
        handleCommand();
    }
}

// ============================================================================
// Data Bus Reading
// ============================================================================
uint8_t readDataBus() {
    uint8_t data = 0;
    for(int i = 0; i < 8; i++) {
        if(digitalRead(DATA_PINS[i]) == HIGH) {
            data |= (1 << i);
        }
    }
    return data;
}

// ============================================================================
// ROM Programming Functions
// ============================================================================

/**
 * Set 15-bit address using dual 74HC595
 */
void setAddress(uint16_t addr) {
    digitalWrite(HC595_STCP, LOW);
    
    // Shift out high byte first (A8~A14)
    shiftOut(HC595_DS, HC595_SHCP, MSBFIRST, (addr >> 8) & 0x7F);
    
    // Then shift out low byte (A0~A7)
    shiftOut(HC595_DS, HC595_SHCP, MSBFIRST, addr & 0xFF);
    
    digitalWrite(HC595_STCP, HIGH);  // Latch
}

/**
 * Write data to ROM at specified address
 */
void writeROM(uint16_t addr, uint8_t data) {
    // Halt cores
    digitalWrite(SYS_RESET, LOW);
    delay(10);
    
    // Set address
    setAddress(addr);
    
    // Configure data bus as OUTPUT
    for(int i = 0; i < 8; i++) {
        pinMode(DATA_PINS[i], OUTPUT);
        digitalWrite(DATA_PINS[i], (data >> i) & 0x01);
    }
    
    // Disable ROM output
    digitalWrite(ROM_OE, HIGH);
    
    // Write pulse (active LOW)
    digitalWrite(ROM_WE, LOW);
    delayMicroseconds(1);
    digitalWrite(ROM_WE, HIGH);
    delay(10);  // Write cycle time
    
    // Restore data bus to INPUT
    for(int i = 0; i < 8; i++) {
        pinMode(DATA_PINS[i], INPUT);
    }
    
    // Resume cores
    digitalWrite(SYS_RESET, HIGH);
    
    Serial.print(F("[ROM] Wrote 0x"));
    if(data < 0x10) Serial.print('0');
    Serial.print(data, HEX);
    Serial.print(F(" to address 0x"));
    if(addr < 0x1000) Serial.print('0');
    if(addr < 0x100) Serial.print('0');
    if(addr < 0x10) Serial.print('0');
    Serial.println(addr, HEX);
}

/**
 * Read data from ROM at specified address
 */
uint8_t readROM(uint16_t addr) {
    // Halt cores
    digitalWrite(SYS_RESET, LOW);
    delay(10);
    
    // Set address
    setAddress(addr);
    
    // Enable ROM output
    digitalWrite(ROM_OE, LOW);
    delayMicroseconds(1);
    
    // Read data
    uint8_t data = readDataBus();
    
    // Disable ROM output
    digitalWrite(ROM_OE, HIGH);
    
    // Resume cores
    digitalWrite(SYS_RESET, HIGH);
    
    return data;
}

// ============================================================================
// Command Handler
// ============================================================================
void handleCommand() {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    
    if(cmd.startsWith("W ")) {
        // Write command: W 0x1000 0xFF
        int addr_idx = cmd.indexOf(' ', 2);
        if(addr_idx > 0) {
            String addr_str = cmd.substring(2, addr_idx);
            String data_str = cmd.substring(addr_idx + 1);
            
            uint16_t addr = strtol(addr_str.c_str(), NULL, 16);
            uint8_t data = strtol(data_str.c_str(), NULL, 16);
            
            writeROM(addr, data);
        }
    }
    else if(cmd.startsWith("R ")) {
        // Read command: R 0x1000
        String addr_str = cmd.substring(2);
        uint16_t addr = strtol(addr_str.c_str(), NULL, 16);
        
        uint8_t data = readROM(addr);
        
        Serial.print(F("[ROM] Read 0x"));
        if(data < 0x10) Serial.print('0');
        Serial.print(data, HEX);
        Serial.print(F(" from address 0x"));
        if(addr < 0x1000) Serial.print('0');
        if(addr < 0x100) Serial.print('0');
        if(addr < 0x10) Serial.print('0');
        Serial.println(addr, HEX);
    }
    else if(cmd == "HELP") {
        Serial.println(F("\n=== Commands ==="));
        Serial.println(F("W <addr> <data> - Write to ROM (e.g., W 0x1000 0xFF)"));
        Serial.println(F("R <addr>        - Read from ROM (e.g., R 0x1000)"));
        Serial.println(F("HELP            - Show this message"));
        Serial.println();
    }
    else {
        Serial.println(F("[ERROR] Unknown command. Type HELP for usage."));
    }
}
