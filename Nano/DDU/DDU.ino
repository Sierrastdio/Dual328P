/* 
 * ============================================================================
 * Arduino Nano #2 - GLCD Display Monitor with 74HC165
 * ============================================================================
 * 
 * Role: Monitor address/data bus via 74HC165 and display on GLCD
 * 
 * Hardware Connections:
 * ---------------------
 * 74HC165 Chain (2 chips daisy-chained):
 *   74HC165 #1 (Data Bus D0~D7):
 *     D0~D7 -> 28C256 D0~D7 (parallel input)
 *     Q7 -> 74HC165 #2 DS (serial cascade)
 *   
 *   74HC165 #2 (Address Bus A0~A6):
 *     A0~A6 -> 28C256 A0~A6 (parallel input)
 *     A7 -> GND (unused)
 * 
 * Control Pins:
 *   A0 -> SH/LD (Shift/Load) - both chips
 *   A1 -> CLK (Clock) - both chips
 *   A2 -> DS (Serial Data from #2 Q7)
 *   
 * Core Output Detection:
 *   D2 -> 74HC138 Y1 (Core 1 Reg A)
 *   D3 -> 74HC138 Y4 (Core 2 Reg A)
 * 
 * GLCD ST7920 128x64 (Software SPI):
 *   D13 -> CLK
 *   D11 -> Data
 *   D10 -> CS
 *   D8  -> Reset
 * 
 * Display Layout:
 * ---------------
 * ┌────────────────────────┐
 * │ Dual-Core Monitor v2.0 │
 * ├────────────────────────┤
 * │ Core 1:                │
 * │   Addr: 0x1A           │
 * │   Data: 0x08 (8)       │
 * │                        │
 * │ Core 2:                │
 * │   Addr: 0x05           │
 * │   Data: 0x10 (16)      │
 * │                        │
 * │ Total: C1:42 C2:38     │
 * └────────────────────────┘
 * 
 * ============================================================================
 */

#include <U8g2lib.h>

// ============================================================================
// GLCD Setup (270° rotation, Software SPI)
// ============================================================================
// CLK=13, Data=11, CS=10, Reset=8
U8G2_ST7920_128X64_1_SW_SPI u8g2(U8G2_R3, 13, 11, 10, 8);

// ============================================================================
// Pin Definitions
// ============================================================================

// 74HC165 Control Pins
const uint8_t HC165_LOAD = A0;   // SH/LD (Shift/Load, Active LOW)
const uint8_t HC165_CLK  = A1;   // Clock
const uint8_t HC165_DATA = A2;   // Serial Data Input (from #2 Q7)

// Core Output Detection
const uint8_t CORE1_SIGNAL = 2;  // 74HC138 Y1 (Active LOW)
const uint8_t CORE2_SIGNAL = 3;  // 74HC138 Y4 (Active LOW)

// ============================================================================
// Display State
// ============================================================================
uint8_t last_core1_addr = 0;
uint8_t last_core1_data = 0;
uint8_t last_core2_addr = 0;
uint8_t last_core2_data = 0;

uint16_t core1_count = 0;
uint16_t core2_count = 0;

unsigned long last_update = 0;
bool display_needs_update = false;
bool system_running = false;

// ============================================================================
// 74HC165 Chain Reading (16-bit: 7-bit Address + 8-bit Data + 1 unused)
// ============================================================================

/**
 * Read 16-bit from daisy-chained 74HC165 chips
 * Returns: [15:9] = Address A0~A6, [7:0] = Data D0~D7
 */
uint16_t read74HC165Chain() {
    uint16_t result = 0;
    
    // 1. Load parallel data (Active LOW)
    digitalWrite(HC165_LOAD, LOW);
    delayMicroseconds(5);
    digitalWrite(HC165_LOAD, HIGH);
    delayMicroseconds(5);
    
    // 2. Shift out 16 bits
    for(uint8_t i = 0; i < 16; i++) {
        // Read bit
        uint8_t bit = digitalRead(HC165_DATA);
        result = (result << 1) | bit;
        
        // Clock pulse
        digitalWrite(HC165_CLK, HIGH);
        delayMicroseconds(2);
        digitalWrite(HC165_CLK, LOW);
        delayMicroseconds(2);
    }
    
    return result;
}

/**
 * Extract address from 16-bit chain data
 */
uint8_t extractAddress(uint16_t chain_data) {
    return (chain_data >> 9) & 0x7F;  // bits [15:9]
}

/**
 * Extract data from 16-bit chain data
 */
uint8_t extractData(uint16_t chain_data) {
    return chain_data & 0xFF;  // bits [7:0]
}

// ============================================================================
// GLCD Display Functions
// ============================================================================

void drawDisplay() {
    u8g2.firstPage();
    do {
        // Title bar
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(0, 8, "Dual-Core Monitor");
        u8g2.drawHLine(0, 10, 128);
        
        // Core 1 Section
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(0, 20, "Core 1:");
        
        u8g2.setFont(u8g2_font_5x7_tf);
        char buf[20];
        
        sprintf(buf, "Addr: 0x%02X", last_core1_addr);
        u8g2.drawStr(8, 28, buf);
        
        sprintf(buf, "Data: 0x%02X (%d)", last_core1_data, last_core1_data);
        u8g2.drawStr(8, 36, buf);
        
        // Core 2 Section
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(0, 46, "Core 2:");
        
        u8g2.setFont(u8g2_font_5x7_tf);
        
        sprintf(buf, "Addr: 0x%02X", last_core2_addr);
        u8g2.drawStr(8, 54, buf);
        
        sprintf(buf, "Data: 0x%02X (%d)", last_core2_data, last_core2_data);
        u8g2.drawStr(8, 62, buf);
        
        // Bottom status bar
        u8g2.drawHLine(0, 11, 128);
        
    } while(u8g2.nextPage());
}

void drawWelcome() {
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_9x15_tf);
        u8g2.drawStr(10, 25, "Dual-Core");
        u8g2.drawStr(8, 45, "System v2.0");
    } while(u8g2.nextPage());
}

void drawStats() {
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_9x15_tf);
        u8g2.drawStr(20, 15, "Statistics");
        
        u8g2.setFont(u8g2_font_6x10_tf);
        char buf[30];
        
        sprintf(buf, "Total: %d", core1_count + core2_count);
        u8g2.drawStr(10, 35, buf);
        
        sprintf(buf, "Core 1: %d", core1_count);
        u8g2.drawStr(10, 48, buf);
        
        sprintf(buf, "Core 2: %d", core2_count);
        u8g2.drawStr(10, 61, buf);
        
    } while(u8g2.nextPage());
}

void drawIdleScreen() {
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(15, 30, "Waiting for");
        u8g2.drawStr(10, 45, "Program Start...");
    } while(u8g2.nextPage());
}

// ============================================================================
// Core Monitoring
// ============================================================================

void monitorCores() {
    bool updated = false;
    
    // Check Core 1 output (Y1 Active LOW)
    if(digitalRead(CORE1_SIGNAL) == LOW) {
        // Read bus data via 74HC165 chain
        uint16_t bus_data = read74HC165Chain();
        
        last_core1_addr = extractAddress(bus_data);
        last_core1_data = extractData(bus_data);
        core1_count++;
        updated = true;
        system_running = true;
        
        // Serial logging
        Serial.print(F("[C1] Addr:0x"));
        if(last_core1_addr < 0x10) Serial.print("0");
        Serial.print(last_core1_addr, HEX);
        Serial.print(F(" Data:0x"));
        if(last_core1_data < 0x10) Serial.print("0");
        Serial.print(last_core1_data, HEX);
        Serial.print(F(" ("));
        Serial.print(last_core1_data);
        Serial.println(F(")"));
        
        delay(10);  // Debounce
    }
    
    // Check Core 2 output (Y4 Active LOW)
    if(digitalRead(CORE2_SIGNAL) == LOW) {
        // Read bus data via 74HC165 chain
        uint16_t bus_data = read74HC165Chain();
        
        last_core2_addr = extractAddress(bus_data);
        last_core2_data = extractData(bus_data);
        core2_count++;
        updated = true;
        system_running = true;
        
        // Serial logging
        Serial.print(F("[C2] Addr:0x"));
        if(last_core2_addr < 0x10) Serial.print("0");
        Serial.print(last_core2_addr, HEX);
        Serial.print(F(" Data:0x"));
        if(last_core2_data < 0x10) Serial.print("0");
        Serial.print(last_core2_data, HEX);
        Serial.print(F(" ("));
        Serial.print(last_core2_data);
        Serial.println(F(")"));
        
        delay(10);  // Debounce
    }
    
    if(updated) {
        display_needs_update = true;
    }
}

// ============================================================================
// Setup & Loop
// ============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println(F("\n=== 74HC165 GLCD Monitor Started ==="));
    Serial.println(F("Commands: S=Stats, R=Reset, T=Test\n"));
    
    // Initialize GLCD
    u8g2.begin();
    u8g2.setContrast(128);  // Adjust as needed (0-255)
    
    // Configure 74HC165 control pins
    pinMode(HC165_LOAD, OUTPUT);
    pinMode(HC165_CLK, OUTPUT);
    pinMode(HC165_DATA, INPUT);
    
    digitalWrite(HC165_LOAD, HIGH);  // Idle state
    digitalWrite(HC165_CLK, LOW);    // Idle state
    
    // Configure core signal pins as INPUT with pull-up
    pinMode(CORE1_SIGNAL, INPUT_PULLUP);
    pinMode(CORE2_SIGNAL, INPUT_PULLUP);
    
    // Show welcome message
    drawWelcome();
    delay(2000);
    
    // Initial display
    drawIdleScreen();
    
    Serial.println(F("[READY] Monitoring bus activity...\n"));
}

void loop() {
    // Monitor core outputs
    monitorCores();
    
    // Update display if needed (throttle to 200ms for GLCD)
    if(display_needs_update && (millis() - last_update > 200)) {
        if(system_running) {
            drawDisplay();
        } else {
            drawIdleScreen();
        }
        display_needs_update = false;
        last_update = millis();
    }
    
    // Periodic refresh even if no updates (every 500ms)
    if(millis() - last_update > 500) {
        if(system_running) {
            drawDisplay();
        }
        last_update = millis();
    }
    
    // Show stats every 15 seconds
    static unsigned long last_stats = 0;
    if(millis() - last_stats > 15000) {
        if(core1_count + core2_count > 0) {
            drawStats();
            delay(3000);
            display_needs_update = true;
        }
        last_stats = millis();
    }
    
    // Check for serial commands
    if(Serial.available()) {
        char cmd = Serial.read();
        
        if(cmd == 'S' || cmd == 's') {
            drawStats();
            delay(3000);
            display_needs_update = true;
        } 
        else if(cmd == 'R' || cmd == 'r') {
            // Reset counters
            core1_count = 0;
            core2_count = 0;
            last_core1_addr = 0;
            last_core1_data = 0;
            last_core2_addr = 0;
            last_core2_data = 0;
            system_running = false;
            Serial.println(F("\n[RESET] All counters cleared\n"));
            drawIdleScreen();
        }
        else if(cmd == 'T' || cmd == 't') {
            // Test 74HC165 reading
            Serial.println(F("\n[TEST] Reading 74HC165 chain..."));
            uint16_t test_data = read74HC165Chain();
            Serial.print(F("Raw 16-bit: 0b"));
            Serial.println(test_data, BIN);
            Serial.print(F("Address (A0~A6): 0x"));
            Serial.println(extractAddress(test_data), HEX);
            Serial.print(F("Data (D0~D7): 0x"));
            Serial.println(extractData(test_data), HEX);
            Serial.println();
        }
    }
}

/*
 * ============================================================================
 * GLCD ST7920 Pinout (Software SPI)
 * ============================================================================
 * 
 * Arduino Nano -> ST7920
 * ----------------------------
 * D13 (SCK)   -> CLK (E)
 * D11 (MOSI)  -> Data (R/W)
 * D10 (CS)    -> CS (RS)
 * D8          -> Reset (RST)
 * GND         -> GND, PSB (parallel/serial select = 0 for serial)
 * VCC         -> VCC, BLA (backlight anode)
 * 
 * Note: PSB pin MUST be connected to GND for serial mode!
 * 
 * ============================================================================
 * 74HC165 Wiring (Same as before)
 * ============================================================================
 * 
 * 74HC165 #1 (Data Bus D0~D7):
 *   Pin 1  (SH/LD)  -> Nano A0
 *   Pin 2  (CLK)    -> Nano A1
 *   Pin 9  (Q7)     -> 74HC165 #2 Pin 10 (DS)
 *   Pin 11-14, 3-6  -> 28C256 D0~D7
 * 
 * 74HC165 #2 (Address Bus A0~A6):
 *   Pin 1  (SH/LD)  -> Nano A0
 *   Pin 2  (CLK)    -> Nano A1
 *   Pin 9  (Q7)     -> Nano A2
 *   Pin 10 (DS)     -> 74HC165 #1 Pin 9
 *   Pin 11-14, 3-6  -> 28C256 A0~A6
 * 
 * ============================================================================
 */