/*
 * ============================================================================
 * Core 1 (Master) - A14 Bank Switch Edition
 * ============================================================================
 * 
 * Hardware Pinout:
 * ----------------
 * Data Bus (8-bit):
 *   - D0~D5: PD2~PD7 (6 bits on PORTD)
 *   - D6~D7: PB0~PB1 (2 bits on PORTB)
 * 
 * Address Bus (7-bit):
 *   - A0~A6: PB2~PB5 (4 bits), PC0~PC2 (3 bits)
 *   - A14: Hardwired to GND (Bank 0: 0x0000~0x3FFF)
 * 
 * Control Signals:
 *   - PC4: Bus Grant (HIGH=Master owns, LOW=Slave owns via 74HC04)
 *   - PC5: Direction Control (LOW=Read, HIGH=Write)
 * 
 * Registers:
 *   - Register A (0x20): Accumulator (Main calculation results)
 * 
 * Monitor Connection:
 *   - 74HC138 Y1 (0x20) -> Arduino Nano D10 (Core 1 Reg A)
 * 
 * Operation:
 *   1. Read data from ROM (A14=0 bank)
 *   2. Store in Register B
 *   3. Perform calculation (A = B + 1)
 *   4. Output Register A to Monitor at address 0x20
 *   5. Release bus for Core 2
 * 
 * ============================================================================
 */

#include <avr/io.h>
#include <util/delay.h>

// ============================================================================
// Configuration Constants
// ============================================================================
const uint16_t ROM_START = 0x00;  // Starting address in 128-byte block
const uint8_t DATA_SET[] = {0x00, 0x01, 0x02, 0x03, 0x04};
const uint8_t LEN = 5;

// Register I/O Addresses (74HC138 decoding A5, A6 only)
const uint8_t REG_A_ADDR = 0x20;  // 0b0100000 -> A6=0, A5=1 -> Y1

// ============================================================================
// Software Registers
// ============================================================================
volatile uint8_t regA = 0x00;  // Accumulator
volatile uint8_t regB = 0x00;  // Operand (internal only)
volatile uint8_t regC = 0x00;  // Counter (internal only)

// ============================================================================
// Hardware Control Macros
// ============================================================================

// Bus Ownership Control
#define TAKE_BUS()      PORTC |= (1 << 4)   // PC4 HIGH -> Master Active
#define GIVE_BUS()      PORTC &= ~(1 << 4)  // PC4 LOW  -> Slave Active

// Data Direction Control
#define SET_READ()      PORTC &= ~(1 << 5)  // PC5 LOW  = Read Mode
#define SET_WRITE()     PORTC |= (1 << 5)   // PC5 HIGH = Write Mode

// Timing
#define SYNC_DELAY()    asm volatile("nop\n\t nop\n\t nop\n\t")

// ============================================================================
// Hardware Abstraction Layer
// ============================================================================

/**
 * Set 7-bit address on bus (A0~A6 only)
 */
inline void set_addr(uint8_t a) {
    // Set address pins to OUTPUT
    DDRB |= 0x3C;  // PB2~PB5 (A0~A3)
    DDRC |= 0x07;  // PC0~PC2 (A4~A6)
    
    // Write address value (only lower 7 bits)
    a &= 0x7F;
    PORTB = (PORTB & 0xC3) | ((a & 0x0F) << 2);
    PORTC = (PORTC & 0xF8) | ((a >> 4) & 0x07);
}

/**
 * Configure data bus as OUTPUT
 */
inline void set_data_out() { 
    DDRD |= 0xFC;  // PD2~PD7
    DDRB |= 0x03;  // PB0~PB1
}

/**
 * Configure data bus as INPUT
 */
inline void set_data_in() { 
    DDRD &= 0x03;  // PD2~PD7 as INPUT
    DDRB &= 0xFC;  // PB0~PB1 as INPUT
}

/**
 * Set all bus lines to High-Z
 */
inline void set_high_z() {
    DDRD &= 0x03; PORTD &= 0x03;
    DDRB &= 0x03; PORTB &= 0x03;
    DDRC &= 0xF8; PORTC &= 0xF8;
}

/**
 * Write 8-bit data to bus
 */
inline void write_bus(uint8_t d) {
    PORTD = (PORTD & 0x03) | ((d << 2) & 0xFC);
    PORTB = (PORTB & 0xFC) | ((d >> 6) & 0x03);
}

/**
 * Read 8-bit data from bus
 */
inline uint8_t read_bus() {
    return ((PIND & 0xFC) >> 2) | ((PINB & 0x03) << 6);
}

// ============================================================================
// Register Output Functions
// ============================================================================

/**
 * Output register value to monitor
 * Address 0x20 triggers 74HC138 Y1 output (monitored by Nano D10)
 */
void output_register(uint8_t reg_addr, uint8_t value) {
    TAKE_BUS();         // Claim bus
    set_data_out();     // Data pins as output
    SET_WRITE();        // Set write mode (PC5 HIGH)
    
    set_addr(reg_addr); // Set register address (triggers 74HC138)
    write_bus(value);   // Write register value to data bus
    
    SYNC_DELAY();       // Hold for stability
    _delay_us(50);      // Extended hold time for Arduino Nano to sample
    
    set_high_z();       // Release bus
}

// ============================================================================
// Main Program
// ============================================================================

void setup() {
    // Configure control pins as OUTPUT
    DDRC |= 0x30;  // PC4 (Bus Grant), PC5 (Direction)
    
    // Initial state: Master owns bus, read mode
    TAKE_BUS();
    SET_READ();
    
    // Initialize registers
    regA = 0x00;
    regB = 0x00;
    regC = 0x00;
}

void loop() {
    // ========================================================================
    // Main Execution Loop - Process all data elements
    // ========================================================================
    
    for(uint8_t i = 0; i < LEN; i++) {
        // ====================================================================
        // PHASE 1: Read from ROM into Register B
        // ====================================================================
        TAKE_BUS();
        set_data_in();
        SET_READ();
        
        set_addr(ROM_START + DATA_SET[i]);
        SYNC_DELAY();
        _delay_us(5);   // Address setup time
        
        regB = read_bus();  // Load data into Register B
        
        // ====================================================================
        // PHASE 2: Calculation (A = B + 1)
        // ====================================================================
        regA = regB + 1;    // Accumulator = Operand + 1
        regC = i;           // Update counter
        
        // ====================================================================
        // PHASE 3: Output Register A to Monitor (0x20 -> Y1)
        // ====================================================================
        output_register(REG_A_ADDR, regA);
        _delay_us(10);
        
        // ====================================================================
        // PHASE 4: Release Bus for Core 2
        // ====================================================================
        set_high_z();
        GIVE_BUS();         // PC4 LOW -> 74HC04 -> Core 2 PC4 HIGH
        _delay_us(100);     // Wait for Core 2 to complete its operation
    }
    
    // End of cycle - pause before next iteration
    _delay_ms(500);
}
