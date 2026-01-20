/**
 * 8-bit Dual Core System - EEPROM Programmer
 * Hardware: Arduino Nano + 28C256 EEPROM + 74HC595 x2
 * 
 * Based on Ben Eater's EEPROM programmer, adapted for:
 * - 32KB address space (15-bit addressing via dual 74HC595)
 * - Dual ATmega328P core system control
 * - Direct OE/WE control
 * - Serial command interface
 */

// ==================== PIN DEFINITIONS ====================
// Data Bus (28C256 D0~D7)
#define DATA_D0 2
#define DATA_D7 9

// 74HC595 Shift Register Control (Address Expansion)
#define SHIFT_DS    A1    // Serial Data Input
#define SHIFT_SHCP  A2    // Shift Clock
#define SHIFT_STCP  A3    // Storage Clock (Latch)

// ROM Control Signals
#define ROM_OE      12    // Output Enable (Active LOW)
#define ROM_WE      13    // Write Enable (Active LOW)

// System Control
#define CORE_RESET  A0    // ATmega328P Core 1 & 2 Reset

// Status Monitor (Optional - for future use)
#define STATUS_Y1   10    // 74HC138 Y1 output
#define STATUS_Y2   11    // 74HC138 Y2 output

// ==================== TIMING CONSTANTS ====================
#define WRITE_PULSE_US   1      // WE pulse width (microseconds)
#define WRITE_DELAY_MS   10     // Post-write stabilization delay
#define READ_SETUP_US    1      // Address setup time before read

// ==================== MEMORY MAP ====================
#define ROM_SIZE        0x8000  // 32KB (28C256)
#define CORE1_START     0x0000  // Core 1 Program Area
#define CORE1_END       0x1FFF  // 8KB
#define CORE2_START     0x2000  // Core 2 Program Area
#define CORE2_END       0x3FFF  // 8KB
#define SHARED_START    0x4000  // Shared Data Area
#define SHARED_END      0x7FFF  // 16KB


/**
 * Set 15-bit address via dual 74HC595 daisy-chain
 * Address bits: A14~A8 (Chip #2) -> A7~A0 (Chip #1)
 */
void setAddress(uint16_t address) {
  // Mask to 15 bits (28C256 max address)
  address &= 0x7FFF;
  
  // Shift out MSB first: upper 7 bits (A14~A8) then lower 8 bits (A7~A0)
  shiftOut(SHIFT_DS, SHIFT_SHCP, MSBFIRST, (address >> 8) & 0x7F);  // A14~A8
  shiftOut(SHIFT_DS, SHIFT_SHCP, MSBFIRST, address & 0xFF);         // A7~A0
  
  // Latch the address to output pins
  digitalWrite(SHIFT_STCP, LOW);
  digitalWrite(SHIFT_STCP, HIGH);
  digitalWrite(SHIFT_STCP, LOW);
}


/**
 * Configure data bus as INPUT (for reading)
 */
void setDataBusInput() {
  for (int pin = DATA_D0; pin <= DATA_D7; pin++) {
    pinMode(pin, INPUT);
  }
}


/**
 * Configure data bus as OUTPUT (for writing)
 */
void setDataBusOutput() {
  for (int pin = DATA_D0; pin <= DATA_D7; pin++) {
    pinMode(pin, OUTPUT);
  }
}


/**
 * Read byte from data bus (D7~D0)
 */
byte readDataBus() {
  byte data = 0;
  for (int pin = DATA_D7; pin >= DATA_D0; pin--) {
    data = (data << 1) | digitalRead(pin);
  }
  return data;
}


/**
 * Write byte to data bus (D0~D7)
 */
void writeDataBus(byte data) {
  for (int pin = DATA_D0; pin <= DATA_D7; pin++) {
    digitalWrite(pin, data & 1);
    data >>= 1;
  }
}


/**
 * Read a byte from EEPROM at specified address
 */
byte readEEPROM(uint16_t address) {
  setDataBusInput();
  setAddress(address);
  
  digitalWrite(ROM_OE, LOW);   // Enable output
  delayMicroseconds(READ_SETUP_US);
  
  byte data = readDataBus();
  
  digitalWrite(ROM_OE, HIGH);  // Disable output
  return data;
}


/**
 * Write a byte to EEPROM at specified address
 * Includes automatic write verification
 */
bool writeEEPROM(uint16_t address, byte data) {
  setAddress(address);
  digitalWrite(ROM_OE, HIGH);  // Disable output during write
  
  setDataBusOutput();
  writeDataBus(data);
  
  // Write pulse (active LOW)
  digitalWrite(ROM_WE, LOW);
  delayMicroseconds(WRITE_PULSE_US);
  digitalWrite(ROM_WE, HIGH);
  
  delay(WRITE_DELAY_MS);  // Wait for EEPROM internal write cycle
  
  // Verify write
  byte readback = readEEPROM(address);
  return (readback == data);
}


/**
 * Halt both cores by pulling RESET LOW
 */
void haltCores() {
  digitalWrite(CORE_RESET, LOW);
  delay(10);
}


/**
 * Resume both cores by releasing RESET
 */
void resumeCores() {
  digitalWrite(CORE_RESET, HIGH);
  delay(50);
  Serial.println(F("[GO]"));
}


/**
 * Dump EEPROM contents in hex format
 */
void dumpEEPROM(uint16_t startAddr, uint16_t endAddr) {
  for (uint16_t base = startAddr; base <= endAddr; base += 16) {
    if (base + 16 > endAddr) break;
    
    byte data[16];
    for (int offset = 0; offset < 16; offset++) {
      data[offset] = readEEPROM(base + offset);
    }
    
    char buf[80];
    sprintf(buf, "%04x:  %02x %02x %02x %02x %02x %02x %02x %02x   %02x %02x %02x %02x %02x %02x %02x %02x",
            base, 
            data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
            data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]);
    
    Serial.println(buf);
  }
}


/**
 * Fill EEPROM range with specific byte pattern
 */
void fillEEPROM(uint16_t startAddr, uint16_t endAddr, byte fillByte) {
  for (uint16_t addr = startAddr; addr <= endAddr; addr++) {
    writeEEPROM(addr, fillByte);
    if (addr % 256 == 0) Serial.print(F("."));
  }
  Serial.println();
}


/**
 * Parse and execute serial commands
 * Commands:
 *   W <addr> <data>  - Write byte to address
 *   D <start> <end>  - Dump memory range
 *   G                - Go (resume cores)
 */
void processCommand() {
  if (!Serial.available()) return;
  
  char cmd = Serial.read();
  
  // Consume whitespace
  while (Serial.available() && Serial.peek() == ' ') {
    Serial.read();
  }
  
  switch (cmd) {
    case 'W': case 'w': {  // Write
      uint16_t addr = Serial.parseInt();
      byte data = Serial.parseInt();
      
      haltCores();
      bool success = writeEEPROM(addr, data);
      
      Serial.print(F("[WRITE] 0x"));
      Serial.print(addr, HEX);
      Serial.print(F(" = 0x"));
      Serial.print(data, HEX);
      Serial.println(success ? F(" OK") : F(" FAILED!"));
      
      resumeCores();
      break;
    }
    
    case 'D': case 'd': {  // Dump
      uint16_t start = Serial.parseInt();
      uint16_t end = Serial.parseInt();
      
      haltCores();
      dumpEEPROM(start, end);
      resumeCores();
      break;
    }
    
    case 'G': case 'g': {  // Go
      resumeCores();
      break;
    }
    
    default:
      if (cmd != '\n' && cmd != '\r') {
        Serial.println(F("[ERROR] Unknown command"));
        Serial.println(F("W <addr> <data> - Write"));
        Serial.println(F("D <start> <end> - Dump"));
        Serial.println(F("G - Resume cores"));
      }
      break;
  }
  
  // Clear input buffer
  while (Serial.available()) {
    Serial.read();
  }
}


// ==================== SETUP ====================
void setup() {
  // Initialize shift register pins
  pinMode(SHIFT_DS, OUTPUT);
  pinMode(SHIFT_SHCP, OUTPUT);
  pinMode(SHIFT_STCP, OUTPUT);
  
  // Initialize ROM control pins
  pinMode(ROM_OE, OUTPUT);
  pinMode(ROM_WE, OUTPUT);
  digitalWrite(ROM_OE, HIGH);   // Disabled by default
  digitalWrite(ROM_WE, HIGH);   // Disabled by default (active LOW)
  
  // Initialize core reset (HIGH = running)
  pinMode(CORE_RESET, OUTPUT);
  digitalWrite(CORE_RESET, HIGH);
  
  // Initialize status monitor pins (input with pullup)
  pinMode(STATUS_Y1, INPUT_PULLUP);
  pinMode(STATUS_Y2, INPUT_PULLUP);
  
  // Serial communication
  Serial.begin(57600);
  delay(100);
  
  Serial.println(F("8-Bit Dual Core Monitor"));
  Serial.println(F("W <addr> <data> - Write"));
  Serial.println(F("D <start> <end> - Dump"));
  Serial.println(F("G - Resume\n"));
}


// ==================== MAIN LOOP ====================
void loop() {
  processCommand();
  
  // Optional: Monitor system status
  // if (digitalRead(STATUS_Y1) == LOW) {
  //   Serial.println(F("[MONITOR] Register A output detected"));
  // }
}
/* 
  8-Bit Dual Core Monitor
  W <addr> <data> - Write
  D <start> <end> - Dump
  G - Resume
  
  > W 0x0 0x01
  [WRITE] 0x0 = 0x1 OK
  
  > W 0x1 0x02
  [WRITE] 0x1 = 0x2 OK
  
  > D 0x0 0xF
  0000:  01 02 00 00 00 00 00 00   00 00 00 00 00 00 00 00
  
  > G
  [GO]
*/
