/*
 * ============================================================================
 * System-Management-Unit(SMU).
 * Arduino Nano - System Monitor & ROM Programmer (Dual 74HC595 Edition)
 * Only monitoring A Register.
 * ============================================================================
 * 
 * This code monitors the primary register (Register A) for both cores and 
 * provides ROM programming functionality with full 15-bit address control.
 * 
 * Hardware Connections:
 * ---------------------
 * Data Bus (D2~D9): 8-bit data monitoring/programming
 * 
 * Register Monitor Pins (74HC138 outputs):
 *   Core 1:
 *     D10 -> 74HC138 Y1 (Register A - 0x20) - Primary result
 *   
 *   Core 2:
 *     A5  -> 74HC138 Y4 (Register A - 0x20) - Primary result
 * 
 * ROM Control:
 *   D11 -> 28C256 A14 (Bank Select: 0=Core1, 1=Core2)
 *   D12 -> 28C256 OE (Output Enable)
 *   D13 -> 28C256 WE (Write Enable)
 * 
 * 74HC595 Dual Daisy-Chain (A0~A13):
 *   A1 -> DS (Serial Data Input to #1)
 *   A2 -> SHCP (Shift Clock, Common)
 *   A3 -> STCP (Latch Clock, Common)
 *   
 *   74HC595 #1 (A0~A6):
 *     Q0~Q6 -> 28C256 A0~A6
 *     Q7S -> 74HC595 #2 DS
 *   
 *   74HC595 #2 (A7~A13):
 *     Q0~Q6 -> 28C256 A7~A13
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

// Register Monitor Pins (Register A only)
const uint8_t CORE1_REG_A = 10;  // Y1 (0x20)
const uint8_t CORE2_REG_A = A5;  // Y4 (0x20)

// ROM Control
const uint8_t ROM_A14 = 11;  // Bank Select (0=Core1, 1=Core2)
const uint8_t ROM_OE = 12;
const uint8_t ROM_WE = 13;

// 74HC595 Control (Dual daisy-chain for A0~A13)
const uint8_t HC595_DS = A1;    // Serial Data
const uint8_t HC595_SHCP = A2;  // Shift Clock
const uint8_t HC595_STCP = A3;  // Latch Clock

// System Control
const uint8_t SYS_RESET = A0;

// ============================================================================
// Address Management
// ============================================================================
uint8_t current_bank = 0;       // 0 = Core 1 area, 1 = Core 2 area
uint16_t current_addr = 0x0000; // Current A0~A13 value

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println(F("==========================================="));
    Serial.println(F("8-bit Dual Core Monitor v3.0"));
    Serial.println(F("Dual 74HC595 - Full 15-bit Addressing"));
    Serial.println(F("==========================================="));
    
    // Configure data bus as INPUT (monitoring mode)
    for(int i = 0; i < 8; i++) {
        pinMode(DATA_PINS[i], INPUT);
    }
    
    // Configure register monitor pins as INPUT with pull-up
    pinMode(CORE1_REG_A, INPUT_PULLUP);
    pinMode(CORE2_REG_A, INPUT_PULLUP);
    
    // Configure ROM control pins
    pinMode(ROM_A14, OUTPUT);
    pinMode(ROM_OE, OUTPUT);
    pinMode(ROM_WE, OUTPUT);
    digitalWrite(ROM_A14, LOW);   // Default to bank 0 (Core 1)
    digitalWrite(ROM_OE, HIGH);   // Disable ROM output initially
    digitalWrite(ROM_WE, HIGH);   // Write disabled (10kΩ pull-up also present)
    
    // Configure 74HC595 control pins
    pinMode(HC595_DS, OUTPUT);
    pinMode(HC595_SHCP, OUTPUT);
    pinMode(HC595_STCP, OUTPUT);
    
    // Configure system reset
    pinMode(SYS_RESET, OUTPUT);
    digitalWrite(SYS_RESET, HIGH);  // Cores running
    
    // Initialize address to 0
    setAddress(0x0000);
    
    Serial.println(F("[INFO] Monitoring Register A only (primary results)"));
    Serial.println(F("[INFO] Bank 0 (Core1): 0x0000~0x3FFF"));
    Serial.println(F("[INFO] Bank 1 (Core2): 0x4000~0x7FFF"));
    Serial.println(F("[INFO] Full 15-bit address control enabled"));
    Serial.println(F("[READY] Monitoring started..."));
    Serial.println();
}

// ============================================================================
// Main Loop - Register Monitoring
// ============================================================================
void loop() {
    // Monitor Core 1 Register A
    if(digitalRead(CORE1_REG_A) == LOW) {  // Active LOW signal
        uint8_t data = readDataBus();
        
        Serial.print(F("[CORE1] A: 0x"));
        if(data < 0x10) Serial.print('0');
        Serial.print(data, HEX);
        Serial.print(F(" ("));
        Serial.print(data);
        Serial.println(F(")"));
        
        delay(5);  // Debounce
    }
    
    // Monitor Core 2 Register A
    if(digitalRead(CORE2_REG_A) == LOW) {  // Active LOW signal
        uint8_t data = readDataBus();
        
        Serial.print(F("[CORE2] A: 0x"));
        if(data < 0x10) Serial.print('0');
        Serial.print(data, HEX);
        Serial.print(F(" ("));
        Serial.print(data);
        Serial.println(F(")"));
        
        delay(5);  // Debounce
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
// Address Control Functions
// ============================================================================

/**
 * Set A0~A13 using dual 74HC595 daisy-chain (14 bits)
 * 
 * Data flow: Nano A1 -> 595#1 -> 595#2
 * 595#1 outputs A0~A6 (lower 7 bits)
 * 595#2 outputs A7~A13 (upper 7 bits)
 */
void setAddress(uint16_t addr) {
    current_addr = addr & 0x3FFF;  // Mask to 14 bits (A0~A13)
    
    digitalWrite(HC595_STCP, LOW);  // Prepare to latch
    
    // Shift out 14 bits total (MSB first into chain)
    // First: Upper 7 bits (A13~A7) go to 595#2
    shiftOut(HC595_DS, HC595_SHCP, MSBFIRST, (current_addr >> 7) & 0x7F);
    
    // Second: Lower 7 bits (A6~A0) go to 595#1
    shiftOut(HC595_DS, HC595_SHCP, MSBFIRST, current_addr & 0x7F);
    
    digitalWrite(HC595_STCP, HIGH);  // Latch data to output
    digitalWrite(HC595_STCP, LOW);
}

/**
 * Set bank (A14 pin)
 * 0 = Core 1 area (0x0000~0x3FFF)
 * 1 = Core 2 area (0x4000~0x7FFF)
 */
void setBank(uint8_t bank) {
    current_bank = bank & 0x01;
    digitalWrite(ROM_A14, current_bank);
}

/**
 * Set full 15-bit address (for ROM programming)
 * addr[14]: A14 (bank select)
 * addr[13:0]: A13~A0 (dual 74HC595)
 */
void setFullAddress(uint16_t addr) {
    // Extract A14 (bank)
    uint8_t bank = (addr >> 14) & 0x01;
    setBank(bank);
    
    // Extract A13~A0
    uint16_t addr_0_13 = addr & 0x3FFF;
    setAddress(addr_0_13);
}

// ============================================================================
// ROM Programming Functions
// ============================================================================

/**
 * Write data to ROM at specified address
 * Now supports full 15-bit addressing!
 */
void writeROM(uint16_t addr, uint8_t data) {
    // Halt cores
    digitalWrite(SYS_RESET, LOW);
    delay(10);
    
    // Set full address (A0~A14)
    setFullAddress(addr);
    
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
    Serial.print(F(" to 0x"));
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
    
    // Set full address (A0~A14)
    setFullAddress(addr);
    
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

/**
 * Switch bank and restart cores
 * This allows cores to access different 128-byte blocks
 */
void switchBank(uint8_t bank, uint16_t addr_0_13) {
    Serial.print(F("[BANK] Switching to Bank "));
    Serial.print(bank);
    Serial.print(F(", Address 0x"));
    Serial.println(addr_0_13, HEX);
    
    // Halt cores
    digitalWrite(SYS_RESET, LOW);
    delay(10);
    
    // Set new bank and address
    setBank(bank);
    setAddress(addr_0_13);
    
    delay(50);  // Stabilization time
    
    // Resume cores
    digitalWrite(SYS_RESET, HIGH);
    
    Serial.println(F("[BANK] Cores restarted with new address space"));
}

/**
 * Dump memory range in hex format
 */
void dumpMemory(uint16_t start, uint16_t end) {
    digitalWrite(SYS_RESET, LOW);
    delay(10);
    
    Serial.println(F("\n--- Memory Dump ---"));
    
    for(uint16_t base = start; base <= end; base += 16) {
        if(base + 16 > end + 1) break;
        
        byte data[16];
        for(int offset = 0; offset < 16; offset++) {
            setFullAddress(base + offset);
            digitalWrite(ROM_OE, LOW);
            delayMicroseconds(1);
            data[offset] = readDataBus();
            digitalWrite(ROM_OE, HIGH);
        }
        
        char buf[80];
        sprintf(buf, "%04x:  %02x %02x %02x %02x %02x %02x %02x %02x   %02x %02x %02x %02x %02x %02x %02x %02x",
                base, 
                data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
                data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]);
        
        Serial.println(buf);
    }
    
    Serial.println(F("--- End Dump ---\n"));
    
    digitalWrite(SYS_RESET, HIGH);
}

// ============================================================================
// Command Handler
// ============================================================================
void handleCommand() {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    
    if(cmd.startsWith("W ")) {
        // Write command: W 0x1234 0xFF
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
        // Read command: R 0x1234
        String addr_str = cmd.substring(2);
        uint16_t addr = strtol(addr_str.c_str(), NULL, 16);
        
        uint8_t data = readROM(addr);
        
        Serial.print(F("[ROM] 0x"));
        if(addr < 0x1000) Serial.print('0');
        if(addr < 0x100) Serial.print('0');
        if(addr < 0x10) Serial.print('0');
        Serial.print(addr, HEX);
        Serial.print(F(" = 0x"));
        if(data < 0x10) Serial.print('0');
        Serial.println(data, HEX);
    }
    else if(cmd.startsWith("D ")) {
        // Dump command: D 0x0000 0x00FF
        int end_idx = cmd.indexOf(' ', 2);
        if(end_idx > 0) {
            String start_str = cmd.substring(2, end_idx);
            String end_str = cmd.substring(end_idx + 1);
            
            uint16_t start = strtol(start_str.c_str(), NULL, 16);
            uint16_t end = strtol(end_str.c_str(), NULL, 16);
            
            dumpMemory(start, end);
        }
    }
    else if(cmd.startsWith("BANK ")) {
        // Bank switch command: BANK 0 0x0080
        int addr_idx = cmd.indexOf(' ', 5);
        if(addr_idx > 0) {
            String bank_str = cmd.substring(5, addr_idx);
            String addr_str = cmd.substring(addr_idx + 1);
            
            uint8_t bank = strtol(bank_str.c_str(), NULL, 10);
            uint16_t addr = strtol(addr_str.c_str(), NULL, 16);
            
            switchBank(bank, addr);
        }
    }
    else if(cmd == "STATUS") {
        Serial.println(F("\n=== System Status ==="));
        Serial.print(F("Current Bank (A14): "));
        Serial.println(current_bank);
        Serial.print(F("Current Address (A0~A13): 0x"));
        if(current_addr < 0x1000) Serial.print('0');
        if(current_addr < 0x100) Serial.print('0');
        if(current_addr < 0x10) Serial.print('0');
        Serial.println(current_addr, HEX);
        Serial.print(F("Physical Address: 0x"));
        uint16_t phys = (current_bank << 14) | current_addr;
        if(phys < 0x1000) Serial.print('0');
        if(phys < 0x100) Serial.print('0');
        if(phys < 0x10) Serial.print('0');
        Serial.println(phys, HEX);
        Serial.println();
    }
    else if(cmd == "HELP") {
        Serial.println(F("\n=== Commands ==="));
        Serial.println(F("W <addr> <data>    - Write byte (e.g., W 0x1234 0xFF)"));
        Serial.println(F("R <addr>           - Read byte (e.g., R 0x1234)"));
        Serial.println(F("D <start> <end>    - Dump memory (e.g., D 0x0 0xFF)"));
        Serial.println(F("BANK <b> <addr>    - Switch bank (e.g., BANK 0 0x0080)"));
        Serial.println(F("                     b: 0=Core1, 1=Core2"));
        Serial.println(F("                     addr: A0~A13 value (0x0000~0x3FFF)"));
        Serial.println(F("STATUS             - Show current configuration"));
        Serial.println(F("HELP               - Show this message"));
        Serial.println();
    }
    else {
        Serial.println(F("[ERROR] Unknown command. Type HELP for usage."));
    }
}
