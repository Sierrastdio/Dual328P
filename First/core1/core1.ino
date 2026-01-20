/*
 * ============================================================================
 * Core 1 (Master) - System Controller
 * ============================================================================
 * 
 * Hardware Pinout:
 * ----------------
 * Data Bus (8-bit):
 *   - D0~D5: PD2~PD7 (6 bits on PORTD)
 *   - D6~D7: PB0~PB1 (2 bits on PORTB)
 * 
 * Address Bus (7-bit):
 *   - A0~A3: PB2~PB5 (4 bits on PORTB)
 *   - A4~A6: PC0~PC2 (3 bits on PORTC)
 * 
 * Control Signals:
 *   - PC3: Device Select (LOW=ROM, HIGH=Monitor)
 *   - PC4: Bus Request/Grant (HIGH=Master owns, LOW=Slave owns via inverter)
 *   - PC5: Direction Control (LOW=Read, HIGH=Write)
 * 
 * Operation Flow:
 * ---------------
 * 1. Master takes bus and reads from ROM
 * 2. Master releases bus for Slave to read (overlaps with calculation)
 * 3. Master takes bus and writes result to Monitor
 * 4. Master releases bus for Slave to write
 * 
 * ============================================================================
 */

#include <avr/io.h>
#include <util/delay.h>

// ============================================================================
// Configuration Constants
// ============================================================================
const uint16_t ROM_START = 0x0000;
const uint8_t DATA_SET[] = {0x00, 0x01, 0x02, 0x03, 0x04};
const uint8_t LEN = 5;

// ============================================================================
// Hardware Control Macros (Optimized for speed)
// ============================================================================

// Bus Ownership Control
#define TAKE_BUS()      PORTC |= (1 << 4)   // PC4 HIGH -> Master Active
#define GIVE_BUS()      PORTC &= ~(1 << 4)  // PC4 LOW  -> Slave Active via Inverter

// Data Direction Control
#define SET_READ()      PORTC &= ~(1 << 5)  // PC5 LOW  = Read Mode
#define SET_WRITE()     PORTC |= (1 << 5)   // PC5 HIGH = Write Mode

// Device Selection (74HC138 input)
#define SEL_ROM()       PORTC &= ~(1 << 3)  // PC3 LOW  = ROM selected
#define SEL_MON()       PORTC |= (1 << 3)   // PC3 HIGH = Monitor selected

// Timing: 3-cycle delay (187.5ns @ 16MHz for signal stabilization)
#define SYNC_DELAY()    asm volatile("nop\n\t nop\n\t nop\n\t")

// ============================================================================
// Hardware Abstraction Layer
// ============================================================================

/**
 * Set 7-bit address on bus
 * A0~A3 -> PB2~PB5, A4~A6 -> PC0~PC2
 */
inline void set_addr(uint16_t a) {
    PORTB = (PORTB & 0xC3) | ((a & 0x0F) << 2); // Preserve other bits, set A0-A3
    PORTC = (PORTC & 0xF8) | ((a >> 4) & 0x07); // Preserve other bits, set A4-A6
}

/**
 * Configure data bus pins as OUTPUT (for writing)
 */
inline void set_data_out() { 
    DDRD |= 0xFC; // PD2~PD7 as OUTPUT
    DDRB |= 0x03; // PB0~PB1 as OUTPUT
}

/**
 * Configure data bus pins as INPUT (for reading)
 */
inline void set_data_in() { 
    DDRD &= 0x03; // PD2~PD7 as INPUT (keep PD0, PD1 unchanged)
    DDRB &= 0xFC; // PB0~PB1 as INPUT (keep PB2~PB7 unchanged)
}

/**
 * Set data bus to High-Z (tristate) to avoid conflicts
 * Note: Master usually drives Address constantly or floats when Slave active
 */
inline void set_high_z() {
    DDRD &= 0x03; PORTD &= 0x03; // Data Hi-Z (no pull-ups)
    DDRB &= 0x03; PORTB &= 0x03; // Data & Addr(Lower) Hi-Z
    // Addr(Upper) Hi-Z handled by maintaining PORTC input on unused bits if needed
}

/**
 * Write 8-bit data to bus
 * D0~D5 -> PD2~PD7, D6~D7 -> PB0~PB1
 */
inline void write_bus(uint8_t d) {
    PORTD = (PORTD & 0x03) | ((d << 2) & 0xFC);
    PORTB = (PORTB & 0xFC) | ((d >> 6) & 0x03);
}

/**
 * Read 8-bit data from bus
 * Returns: Combined value from PD2~PD7 (D0~D5) and PB0~PB1 (D6~D7)
 */
inline uint8_t read_bus() {
    return ((PIND & 0xFC) >> 2) | ((PINB & 0x03) << 6);
}

// ============================================================================
// Main Program
// ============================================================================

void setup() {
    // Configure control pins as OUTPUT
    DDRC |= 0x38; // PC3, PC4, PC5 Output
    
    // Initial state: Master owns the bus
    TAKE_BUS();
    SET_READ();
}

void loop() {
    for(uint8_t i=0; i<LEN; i++) {
        uint8_t val = 0;
        
        // ====================================================================
        // PHASE 1: Master Read from ROM
        // ====================================================================
        TAKE_BUS();         // Claim bus ownership
        set_data_in();      // Data pins as input
        SET_READ();         // Set direction to READ
        SEL_ROM();          // Select ROM device
        set_addr(ROM_START + DATA_SET[i]);
        SYNC_DELAY();       // Wait for bus stabilization
        val = read_bus();   // Read data from ROM
        
        // ====================================================================
        // PHASE 2: Release Bus for Slave + Master Calculation (Parallel)
        // ====================================================================
        set_high_z();       // Release data bus (Hi-Z)
        GIVE_BUS();         // Transfer bus to Slave (PC4 LOW)
        
        // ** PIPELINING ** : Calculating while Slave uses Bus
        val = val + 1;      // Master performs calculation
        
        _delay_us(5);       // Wait for Slave to finish reading
        
        // ====================================================================
        // PHASE 3: Master Write to Monitor
        // ====================================================================
        TAKE_BUS();         // Reclaim bus ownership
        set_data_out();     // Data pins as output
        SET_WRITE();        // Set direction to WRITE
        SEL_MON();          // Select Monitor device
        write_bus(val);     // Send data to monitor
        SYNC_DELAY();       // Ensure stable write
        
        // ====================================================================
        // PHASE 4: Release Bus for Slave Write
        // ====================================================================
        set_high_z();       // Release data bus
        GIVE_BUS();         // Transfer bus to Slave
        _delay_us(5);       // Wait for Slave to finish writing
    }
    _delay_ms(1000);
}
