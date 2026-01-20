/*
 * ============================================================================
 * Core 2 (Slave) - 3 Register Architecture + same ver of CLI added Nano.
 * ============================================================================
 * 
 * Hardware Pinout: (Identical to Core 1)
 * ----------------
 * Data Bus (8-bit):
 *   - D0~D5: PD2~PD7
 *   - D6~D7: PB0~PB1
 * 
 * Address Bus (7-bit):
 *   - A0~A3: PB2~PB5
 *   - A4~A6: PC0~PC2
 * 
 * Control Signals (INPUT mode - monitoring Master):
 *   - PC4: Bus Grant Signal (via 74HC04 inverter)
 *          Master HIGH -> Inverter -> Slave sees LOW (Bus NOT granted)
 *          Master LOW  -> Inverter -> Slave sees HIGH (Bus granted)
 *   - PC5: Direction Monitor (LOW=Read, HIGH=Write)
 * 
 * Registers (Updated addresses for 6-register system):
 *   - Register A (0x73): Accumulator (Calculation results)
 *   - Register B (0x74): Operand (Temporary storage)
 *   - Register C (0x75): Counter/Index (Loop control)
 * 
 * Monitor Connection:
 *   - 74HC138 Y4 (0x73) -> Arduino Nano A5 (Core 2 Reg A)
 *   - 74HC138 Y5 (0x74) -> Arduino Nano A6 (Core 2 Reg B)
 *   - 74HC138 Y6 (0x75) -> Arduino Nano A7 (Core 2 Reg C)
 * 
 * Operation:
 *   1. Wait for bus grant from Master (PC4 HIGH via 74HC04 inverter)
 *   2. Read data from ROM into Register B
 *   3. Perform calculation (A = B * B)
 *   4. Output Register A to Monitor at address 0x73
 *   5. Update counter in Register C
 *   6. Release bus and wait for next grant
 * 
 * ============================================================================
 */

#include <avr/io.h>
#include <util/delay.h>

// ============================================================================
// Configuration Constants
// ============================================================================
const uint16_t SLAVE_OFFSET = 0x0040;
const uint8_t DATA_SET[] = {0x02, 0x03, 0x04, 0x05, 0x06};
const uint8_t LEN = 5;

// Register I/O Addresses (74HC138 Y outputs)
const uint8_t REG_A_ADDR = 0x73;  // Y4 -> Nano A5
const uint8_t REG_B_ADDR = 0x74;  // Y5 -> Nano A6
const uint8_t REG_C_ADDR = 0x75;  // Y6 -> Nano A7

// ============================================================================
// Software Registers
// ============================================================================
volatile uint8_t regA = 0x00;  // Accumulator
volatile uint8_t regB = 0x00;  // Operand
volatile uint8_t regC = 0x00;  // Counter

// ============================================================================
// Hardware Monitoring Macros
// ============================================================================

// Bus Grant Detection (inverted by 74HC04)
#define BUS_GRANTED()   (PINC & (1 << 4))    // HIGH = Bus granted to Slave

// Direction Detection
#define IS_READ()       (!(PINC & (1 << 5))) // LOW = Read phase
#define IS_WRITE()      (PINC & (1 << 5))    // HIGH = Write phase

// Timing
#define SYNC_DELAY()    asm volatile("nop\n\t nop\n\t nop\n\t")

// ============================================================================
// Hardware Abstraction Layer
// ============================================================================

/**
 * Set ALL bus pins to High-Z
 */
inline void set_high_z_all() {
    DDRD &= 0x03; PORTD &= 0x03; // Data bus Hi-Z
    DDRB &= 0x03; PORTB &= 0x03; // Data + Addr(Low) Hi-Z
    DDRC &= 0xF8; PORTC &= 0xF8; // Addr(High) Hi-Z
}

/**
 * Set 7-bit address on bus
 */
inline void set_addr(uint16_t a) {
    DDRB |= 0x3C; DDRC |= 0x07; // Set Addr pins to OUTPUT
    PORTB = (PORTB & 0xC3) | ((a & 0x0F) << 2);
    PORTC = (PORTC & 0xF8) | ((a >> 4) & 0x07);
}

/**
 * Configure data bus as OUTPUT
 */
inline void set_data_out() { 
    DDRD |= 0xFC; 
    DDRB |= 0x03; 
}

/**
 * Configure data bus as INPUT
 */
inline void set_data_in() { 
    DDRD &= 0x03; 
    DDRB &= 0xFC; 
}

/**
 * Read 8-bit data from bus
 */
inline uint8_t read_bus() {
    return ((PIND & 0xFC) >> 2) | ((PINB & 0x03) << 6);
}

/**
 * Write 8-bit data to bus
 */
inline void write_bus(uint8_t d) {
    PORTD = (PORTD & 0x03) | ((d << 2) & 0xFC);
    PORTB = (PORTB & 0xFC) | ((d >> 6) & 0x03);
}

// ============================================================================
// Register Output Function
// ============================================================================

/**
 * Output register value to monitor when bus is granted
 * Triggers 74HC138 Y4/Y5/Y6 outputs monitored by Arduino Nano
 */
void output_register(uint8_t reg_addr, uint8_t value) {
    if(!BUS_GRANTED()) return;  // Safety check
    
    set_data_out();             // Data pins as output
    set_addr(reg_addr);         // Set register address (triggers 74HC138)
    write_bus(value);           // Write value to bus
    
    SYNC_DELAY();               // Hold signal
    _delay_us(50);              // Extended hold for Arduino Nano to sample
}

// ============================================================================
// Main Program
// ============================================================================

void setup() {
    // Configure control pins as INPUT (monitoring Master's signals)
    DDRC &= ~0x30; // PC4, PC5 as INPUT
    PORTC &= ~0x30; // No pull-up (signals driven by Master/74HC04)
    
    // Initial state: Release all bus lines
    set_high_z_all();
    
    // Initialize registers
    regA = 0x00;
    regB = 0x00;
    regC = 0x00;
}

void loop() {
    static uint8_t idx = 0;
    
    // ========================================================================
    // Wait for Bus Grant from Master
    // ========================================================================
    
    while(!BUS_GRANTED()) {
        // Idle: Master owns the bus
        set_high_z_all();
        _delay_us(1);
    }
    
    // ========================================================================
    // Bus granted - Execute Core 2 operation
    // ========================================================================
    
    // ====================================================================
    // PHASE 1: Read from ROM into Register B
    // ====================================================================
    set_data_in();
    set_addr(SLAVE_OFFSET + DATA_SET[idx]);
    SYNC_DELAY();
    _delay_us(5);       // Address setup time
    
    regB = read_bus();  // Load data into Register B
    regC = idx;         // Update counter
    
    // ====================================================================
    // PHASE 2: Calculation (A = B * B)
    // ====================================================================
    regA = regB * regB;  // Accumulator = Operand squared
    
    // ====================================================================
    // PHASE 3: Output Registers to Monitor
    // ====================================================================
    
    // Output Register A (primary result)
    output_register(REG_A_ADDR, regA);
    _delay_us(10);  // Gap between outputs
    
    // Output Register B (operand - optional)
    output_register(REG_B_ADDR, regB);
    _delay_us(10);
    
    // Output Register C (counter - optional)
    output_register(REG_C_ADDR, regC);
    _delay_us(10);
    
    // ====================================================================
    // PHASE 4: Release bus and update index
    // ====================================================================
    set_high_z_all();
    
    idx = (idx + 1) % LEN;  // Increment index (wrap around)
    
    // Wait for bus to be taken back by Master
    while(BUS_GRANTED()) {
        _delay_us(1);
    }
}
