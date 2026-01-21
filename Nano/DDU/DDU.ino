/*
 * ============================================================================
 * Arduino Nano #2 - LCD Display Monitor
 * Data Display Unit (DDU)
 * ============================================================================
 * 
 * Role: Monitor data bus and display Core results on LCD in real-time
 * 
 * Hardware Connections:
 * ---------------------
 * Data Bus (Read-Only):
 *   A0~A3 (14~17) -> 28C256 D0~D3
 *   D8~D11        -> 28C256 D4~D7
 * 
 * Core Output Detection:
 *   D2 -> 74HC138 Y1 (Core 1 Reg A - 0x20)
 *   D3 -> 74HC138 Y4 (Core 2 Reg A - 0x20)
 * 
 * LCD (16x2, 4-bit mode):
 *   D4 -> LCD D4
 *   D5 -> LCD D5
 *   D6 -> LCD D6
 *   D7 -> LCD D7
 *   D12 -> LCD RS
 *   D13 -> LCD EN
 *   GND -> LCD RW
 * 
 * Display Format:
 * ---------------
 * C1:123  C2:045
 * [====75%====]
 * 
 * ============================================================================
 */

#include <LiquidCrystal.h>

// ============================================================================
// Pin Definitions
// ============================================================================

// Data Bus Input (8-bit)
const uint8_t DATA_D0 = A0;  // D0
const uint8_t DATA_D1 = A1;  // D1
const uint8_t DATA_D2 = A2;  // D2
const uint8_t DATA_D3 = A3;  // D3
const uint8_t DATA_D4 = 8;   // D4
const uint8_t DATA_D5 = 9;   // D5
const uint8_t DATA_D6 = 10;  // D6
const uint8_t DATA_D7 = 11;  // D7

// Core Output Detection
const uint8_t CORE1_SIGNAL = 2;  // Y1 (Active LOW)
const uint8_t CORE2_SIGNAL = 3;  // Y4 (Active LOW)

// LCD Pins (4-bit mode)
const uint8_t LCD_RS = 12;
const uint8_t LCD_EN = 13;
const uint8_t LCD_D4 = 4;
const uint8_t LCD_D5 = 5;
const uint8_t LCD_D6 = 6;
const uint8_t LCD_D7 = 7;

// ============================================================================
// LCD Object
// ============================================================================
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// ============================================================================
// Display State
// ============================================================================
uint8_t last_core1_value = 0;
uint8_t last_core2_value = 0;
uint16_t core1_count = 0;
uint16_t core2_count = 0;
unsigned long last_update = 0;
bool display_needs_update = false;

// Progress tracking
uint16_t expected_total = 256;  // Expected number of results
uint8_t progress_percent = 0;

// ============================================================================
// Data Bus Reading
// ============================================================================

uint8_t readDataBus() {
    uint8_t data = 0;
    
    // Read lower 4 bits (A0~A3)
    if(digitalRead(DATA_D0) == HIGH) data |= 0x01;
    if(digitalRead(DATA_D1) == HIGH) data |= 0x02;
    if(digitalRead(DATA_D2) == HIGH) data |= 0x04;
    if(digitalRead(DATA_D3) == HIGH) data |= 0x08;
    
    // Read upper 4 bits (D8~D11)
    if(digitalRead(DATA_D4) == HIGH) data |= 0x10;
    if(digitalRead(DATA_D5) == HIGH) data |= 0x20;
    if(digitalRead(DATA_D6) == HIGH) data |= 0x40;
    if(digitalRead(DATA_D7) == HIGH) data |= 0x80;
    
    return data;
}

// ============================================================================
// LCD Display Functions
// ============================================================================

void updateDisplay() {
    // Line 1: Core values
    lcd.setCursor(0, 0);
    lcd.print("C1:");
    if(last_core1_value < 100) lcd.print(" ");
    if(last_core1_value < 10) lcd.print(" ");
    lcd.print(last_core1_value);
    
    lcd.print("  C2:");
    if(last_core2_value < 100) lcd.print(" ");
    if(last_core2_value < 10) lcd.print(" ");
    lcd.print(last_core2_value);
    
    // Line 2: Progress bar
    lcd.setCursor(0, 1);
    lcd.print("[");
    
    uint16_t total_results = core1_count + core2_count;
    progress_percent = (total_results * 100) / expected_total;
    if(progress_percent > 100) progress_percent = 100;
    
    // Draw progress bar (14 characters)
    uint8_t filled = (progress_percent * 14) / 100;
    for(uint8_t i = 0; i < 14; i++) {
        lcd.print(i < filled ? "=" : " ");
    }
    lcd.print("]");
}

void showWelcome() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Dual-Core");
    lcd.setCursor(0, 1);
    lcd.print("Monitor Ready");
    delay(2000);
    lcd.clear();
}

void showStats() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Total: ");
    lcd.print(core1_count + core2_count);
    lcd.setCursor(0, 1);
    lcd.print("C1:");
    lcd.print(core1_count);
    lcd.print(" C2:");
    lcd.print(core2_count);
    delay(3000);
    lcd.clear();
    display_needs_update = true;
}

// ============================================================================
// Core Monitoring
// ============================================================================

void monitorCores() {
    bool updated = false;
    
    // Check Core 1 output
    if(digitalRead(CORE1_SIGNAL) == LOW) {
        uint8_t value = readDataBus();
        last_core1_value = value;
        core1_count++;
        updated = true;
        
        // Also send to Serial for logging
        Serial.print(F("[C1] "));
        Serial.println(value);
        
        delay(5);  // Debounce
    }
    
    // Check Core 2 output
    if(digitalRead(CORE2_SIGNAL) == LOW) {
        uint8_t value = readDataBus();
        last_core2_value = value;
        core2_count++;
        updated = true;
        
        // Also send to Serial for logging
        Serial.print(F("[C2] "));
        Serial.println(value);
        
        delay(5);  // Debounce
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
    Serial.println(F("\n=== LCD Display Monitor Started ===\n"));
    
    // Initialize LCD
    lcd.begin(16, 2);
    lcd.clear();
    
    // Configure data bus pins as INPUT
    pinMode(DATA_D0, INPUT);
    pinMode(DATA_D1, INPUT);
    pinMode(DATA_D2, INPUT);
    pinMode(DATA_D3, INPUT);
    pinMode(DATA_D4, INPUT);
    pinMode(DATA_D5, INPUT);
    pinMode(DATA_D6, INPUT);
    pinMode(DATA_D7, INPUT);
    
    // Configure core signal pins as INPUT with pull-up
    pinMode(CORE1_SIGNAL, INPUT_PULLUP);
    pinMode(CORE2_SIGNAL, INPUT_PULLUP);
    
    // Show welcome message
    showWelcome();
    
    // Initial display
    updateDisplay();
}

void loop() {
    // Monitor core outputs
    monitorCores();
    
    // Update display if needed (throttle to 100ms)
    if(display_needs_update && (millis() - last_update > 100)) {
        updateDisplay();
        display_needs_update = false;
        last_update = millis();
    }
    
    // Show stats every 10 seconds
    static unsigned long last_stats = 0;
    if(millis() - last_stats > 10000) {
        if(core1_count + core2_count > 0) {
            showStats();
        }
        last_stats = millis();
    }
    
    // Check for serial commands
    if(Serial.available()) {
        char cmd = Serial.read();
        if(cmd == 'S' || cmd == 's') {
            showStats();
        } else if(cmd == 'R' || cmd == 'r') {
            // Reset counters
            core1_count = 0;
            core2_count = 0;
            last_core1_value = 0;
            last_core2_value = 0;
            Serial.println(F("[RESET] Counters cleared"));
            updateDisplay();
        }
    }
}