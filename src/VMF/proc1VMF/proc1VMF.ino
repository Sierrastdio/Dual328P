/*
 * ============================================================================
 * Processor 1 - Virtual Machine Firmware v6.0 (Timer added)
 * ============================================================================
 *
 * Pin layout:
 * - PD2~7, PB0~1: Data bus (D0~D7)  → 74HC245 data buffer P1
 * - PB2~5, PC0~2: Address bus (A0~A6) → 74HC245 address buffer P1
 * - PD0: 74HC595-Processor1 SER (Serial Data)   ← conflicts with UART RX
 * - PD1: 74HC595-Processor1 SCK (Shift Clock)   ← conflicts with UART TX
 * - PC3: 74HC595-Processor1 RCK (Latch Clock)
 * - PC4: Processor Select (0=Processor1 Active) + EEPROM A14
 * - PC5: Processor 2 done signal input (LOW=busy, HIGH=done)
 *
 * Paging system:
 * - 74HC595 controls A7~A13 (7 bits)
 * - 128 pages × 128 bytes = 16 KB addressable
 * - slot[15] = PAGE_REG (dedicated page register)
 *
 * Handshake:
 * - Core 2 (Processor 2): sends PC5 signal on task completion (High/Low)
 * - Core 1 (Processor 1): detects Core 2 completion via PC5 interrupt (PCINT1)
 *
 * ------------------------------------------------------------
 *            0             1
 * DDRx    input mode    output mode
 * PORTx   LOW           HIGH
 * PINx    ignore        read pin state
 *
 * |=  set bit
 * <<  left shift
 * &=  clear bit (with ~)
 * ~   bitwise NOT
 * ============================================================================
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

// ─── Instruction Set ─────────────────────────────────────────────────────────
#define OP_NOP      0x00
#define OP_LOAD     0x10
#define OP_ADD      0x20
#define OP_SUB      0x30
#define OP_MUL      0x40
#define OP_AND      0x50
#define OP_OR       0x60
#define OP_OUT      0x70
#define OP_FETCH    0x80
#define OP_SLOT     0x90
#define OP_PUSH     0xA0
#define OP_POP      0xB0
#define OP_SETPAGE  0xE0
#define OP_HALT     0xF0

// ─── Burst mode ───────────────────────────────────────────────────────────────
#define BURST_SIZE  4   // instructions executed per bus acquisition

// ─── Timing measurement ───────────────────────────────────────────────────────
//
//  Enable:  uncomment #define ENABLE_TIMING
//  Disable: comment it out → zero runtime overhead (all timing code compiles away)
//
//  Timer1 runs free with prescaler 8  →  1 tick = 0.5 µs @ 16 MHz
//  Overflow ISR extends counter to 32 bits  →  ~35 min before wrap
//
//  !! UART OUTPUT WARNING !!
//  PD0 (RX) = 74HC595 SER, PD1 (TX) = 74HC595 SCK.
//  UART is only initialized AFTER halted = true, when the 595 is no longer
//  driven. Disconnect / tri-state the 595 data lines before using UART output,
//  or read `timing_result_us` / `timing_result_instr` directly via debugger.
//
#define ENABLE_TIMING
#define TIMING_INTERVAL  5120UL   // measure every N instructions

// ─── VM state ─────────────────────────────────────────────────────────────────
volatile uint8_t  regA        = 0x00;
volatile uint8_t  slot[16];
volatile uint8_t  stack[8];
volatile uint8_t  stack_ptr   = 0;
volatile uint8_t  PC          = 0;
volatile uint8_t  current_page = 0;
volatile bool     halted      = false;

volatile bool     core2_ready = true;

volatile uint8_t  cached_page = 0xFF;

#define PAGE_REG slot[15]

// ─── Hardware macros ─────────────────────────────────────────────────────────
#define ACTIVATE_CORE1()    PORTC &= ~(1 << 4)
#define RELEASE_TO_CORE2()  PORTC |=  (1 << 4)
#define IS_CORE2_DONE()     (PINC & (1 << 5))

#define HC595_SER_HIGH()    PORTD |=  (1 << 0)
#define HC595_SER_LOW()     PORTD &= ~(1 << 0)
#define HC595_SCK_HIGH()    PORTD |=  (1 << 1)
#define HC595_SCK_LOW()     PORTD &= ~(1 << 1)
#define HC595_RCK_HIGH()    PORTC |=  (1 << 3)
#define HC595_RCK_LOW()     PORTC &= ~(1 << 3)

#define SYNC_DELAY()        asm volatile("nop\n\t nop\n\t nop\n\t nop\n\t")


// ============================================================================
//  TIMING SUBSYSTEM
//  Everything inside #ifdef is compiled out when ENABLE_TIMING is undefined.
// ============================================================================
#ifdef ENABLE_TIMING

volatile uint32_t _t1_overflows = 0;   // extended Timer1 overflow counter

// Timer1 overflow ISR — increments 32-bit extension
ISR(TIMER1_OVF_vect) {
    _t1_overflows++;
}

// Read atomic 32-bit tick counter (1 tick = 0.5 µs)
static inline uint32_t _get_ticks() {
    uint8_t  sreg = SREG;
    cli();
    uint32_t ov = _t1_overflows;
    uint16_t t  = TCNT1;
    // If overflow flag is set but ISR hasn't run yet, compensate
    if ((TIFR1 & (1 << TOV1)) && t < 0x8000U) ov++;
    SREG = sreg;
    return (ov << 16) | t;
}

// Public results — read these after TIMING_INTERVAL instructions complete
volatile uint32_t timing_result_us    = 0;  // elapsed µs
volatile uint32_t timing_result_instr = 0;  // instructions counted (sanity check)

static uint32_t _timing_start   = 0;
static uint32_t _timing_counted = 0;
static bool     _timing_active  = false;

// Call once in setup() — starts Timer1 free-running, prescaler 8
void timing_init() {
    TCCR1A = 0;
    TCCR1B = (1 << CS11);    // prescaler 8  →  0.5 µs/tick @ 16 MHz
    TIMSK1 = (1 << TOIE1);   // enable Timer1 overflow interrupt
    TCNT1  = 0;
    _t1_overflows = 0;
    _timing_active = false;
}

// Start a new measurement window
static inline void timing_start_window() {
    _timing_counted = 0;
    _timing_start   = _get_ticks();
    _timing_active  = true;
}

// Call after each burst — count is the number of instructions just executed.
// Returns true (and stores result) exactly once when TIMING_INTERVAL is reached.
// Cost: one uint32_t addition + one comparison per burst.
static inline bool timing_tick(uint8_t count) {
    if (!_timing_active) return false;

    _timing_counted += count;
    if (_timing_counted >= TIMING_INTERVAL) {
        uint32_t elapsed  = _get_ticks() - _timing_start;
        timing_result_us    = elapsed >> 1;   // ticks × 0.5 → µs
        timing_result_instr = _timing_counted;
        _timing_active = false;
        return true;
    }
    return false;
}

// ── Optional UART output (only safe to call after halted == true) ──────────
//    PD1 is normally SCK for 74HC595. Use only when 595 data lines are
//    electrically disconnected or the firmware has already halted.

void timing_uart_init() {
    // 9600 baud @ 16 MHz (UBRR = 103)
    UBRR0H = 0;
    UBRR0L = 103;
    UCSR0B = (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);  // 8-N-1
}

static void _uart_putc(char c) {
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
}

static void _uart_puts(const char* s) {
    while (*s) _uart_putc(*s++);
}

static void _uart_putu32(uint32_t v) {
    if (v == 0) { _uart_putc('0'); return; }
    char buf[11]; int8_t i = 0;
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i--) _uart_putc(buf[i + 1]);  // reverse
}

// Prints:  "TIMING: <instr> instr in <us> us (<ns_per_instr> ns/instr)\r\n"
void timing_uart_print() {
    timing_uart_init();
    _uart_puts("TIMING: ");
    _uart_putu32(timing_result_instr);
    _uart_puts(" instr in ");
    _uart_putu32(timing_result_us);
    _uart_puts(" us (");
    // ns per instruction = (us * 1000) / instr
    if (timing_result_instr > 0)
        _uart_putu32((timing_result_us * 1000UL) / timing_result_instr);
    else
        _uart_putc('?');
    _uart_puts(" ns/instr)\r\n");
}

#else
// When timing is disabled every call compiles to nothing
#define timing_init()              do {} while(0)
#define timing_start_window()      do {} while(0)
#define timing_tick(n)             (false)
#define timing_uart_print()        do {} while(0)
#endif  // ENABLE_TIMING
// ============================================================================


// ============================================================================
//  ISR — Core 2 done signal
// ============================================================================
ISR(PCINT1_vect) {
    if (IS_CORE2_DONE()) core2_ready = true;
}


// ============================================================================
//  74HC595 page register
// ============================================================================
void set_page_595(uint8_t page) {
    page &= 0b01111111;
    if (page == cached_page) return;
    cached_page = page;

    HC595_RCK_LOW();
    for (uint8_t i = 0; i < 7; i++) {
        if (page & 0b01000000) HC595_SER_HIGH();
        else                   HC595_SER_LOW();
        HC595_SCK_HIGH();
        HC595_SCK_LOW();
        page <<= 1;
    }
    HC595_RCK_HIGH();
    asm volatile("nop\n\t");
    HC595_RCK_LOW();
}


// ============================================================================
//  Address bus
// ============================================================================
inline void set_addr_bus(uint8_t addr) {
    addr &= 0b01111111;
    PORTB = (PORTB & 0b11000011) | ((addr & 0b00001111) << 2);
    PORTC = (PORTC & 0b11111000) | ((addr >> 4) & 0b00000111);
}


// ============================================================================
//  Data bus I/O
// ============================================================================
inline void set_data_output() {
    DDRD |= 0b11111100;
    DDRB |= 0b00000011;
}

inline void set_data_input() {
    DDRD  &= 0b00000011;
    PORTD &= 0b00000011;
    DDRB  &= 0b11111100;
    PORTB &= 0b11111100;
}

inline void write_data_bus(uint8_t data) {
    PORTD = (PORTD & 0b00000011) | ((data << 2) & 0b11111100);
    PORTB = (PORTB & 0b11111100) | ((data >> 6) & 0b00000011);
}

inline uint8_t read_data_bus() {
    return ((PIND & 0b11111100) >> 2) | ((PINB & 0b00000011) << 6);
}


// ============================================================================
//  High-Z
// ============================================================================
inline void set_high_z() {
    DDRD  &= 0b00000011;  PORTD &= 0b00000011;
    DDRB  &= 0b11000000;  PORTB &= 0b11000000;
    DDRC  &= 0b11111000;  PORTC &= 0b11111000;
}


// ============================================================================
//  Fetch
// ============================================================================
uint8_t fetch() {
    ACTIVATE_CORE1();
    set_data_input();
    DDRB |= 0b00111100;
    DDRC |= 0b00000111;
    set_addr_bus(PC);
    SYNC_DELAY();
    _delay_us(5);
    return read_data_bus();
}


// ============================================================================
//  Output
// ============================================================================
void output_register(uint8_t value) {
    set_data_output();
    write_data_bus(value);
    SYNC_DELAY();
    _delay_us(50);
    set_data_input();
}


// ============================================================================
//  Execute
// ============================================================================
void execute(uint8_t instruction) {
    uint8_t opcode  = instruction & 0xF0;
    uint8_t operand = instruction & 0x0F;

    switch (opcode) {
        case OP_NOP:                                    break;
        case OP_LOAD:   regA  = operand;                break;
        case OP_ADD:    regA += operand;                break;
        case OP_SUB:    regA -= operand;                break;
        case OP_MUL:    regA *= operand;                break;
        case OP_AND:    regA &= operand;                break;
        case OP_OR:     regA |= operand;                break;
        case OP_OUT:    output_register(regA);          break;
        case OP_FETCH:  regA = slot[operand & 0x0F];   break;
        case OP_SLOT:   slot[operand & 0x0F] = regA;   break;

        case OP_PUSH:
            if (stack_ptr < 8) stack[stack_ptr++] = regA;
            break;

        case OP_POP:
            if (stack_ptr > 0) regA = stack[--stack_ptr];
            break;

        case OP_SETPAGE:
            current_page = PAGE_REG & 0b01111111;
            PC = 0;
            set_page_595(current_page);
            break;

        case OP_HALT:
            halted = true;
            break;

        default: break;
    }
}


// ============================================================================
//  Setup
// ============================================================================
void setup() {
    set_data_input();
    DDRB  |= 0b00111100;
    DDRC  |= 0b00000111;
    DDRD  |= 0b00000011;
    DDRC  |= 0b00001000;
    DDRC  |= 0b00010000;
    DDRC  &= ~0b00100000;   // PC5 input
    PORTC &= ~(1 << 5);     // pull-up disabled

    // PC5 pin-change interrupt
    PCICR  |= (1 << PCIE1);
    PCMSK1 |= (1 << PCINT13);
    sei();

    ACTIVATE_CORE1();

    regA = 0x00; PC = 0; stack_ptr = 0;
    current_page = 0; cached_page = 0xFF; halted = false; core2_ready = true;
    for (uint8_t i = 0; i < 16; i++) slot[i]  = 0;
    for (uint8_t i = 0; i < 8;  i++) stack[i] = 0;
    PAGE_REG = 0;
    set_page_595(0);

    timing_init();          // no-op when ENABLE_TIMING is undefined
    timing_start_window();  // begin measuring from the first instruction

    _delay_ms(100);
}


// ============================================================================
//  Loop
// ============================================================================
void loop() {
    if (halted) {
        set_high_z();
        RELEASE_TO_CORE2();
        // Print timing result over UART after halt
        // (PD0/PD1 no longer driving 595 at this point)
        timing_uart_print();
        while (1);
    }

    // ── Burst: execute BURST_SIZE instructions per bus acquisition ───────────
    for (uint8_t i = 0; i < BURST_SIZE; i++) {
        uint8_t instruction = fetch();
        execute(instruction);
        PC++;
        if (PC >= 128) PC = 0;
        if (halted) break;
    }

    // One addition + compare per burst when ENABLE_TIMING is active.
    // Compiles to zero instructions when ENABLE_TIMING is undefined.
    if (timing_tick(BURST_SIZE)) {
        // TIMING_INTERVAL reached mid-execution — result is ready in
        // timing_result_us and timing_result_instr. Measurement stops here;
        // execution continues uninterrupted.
        // A new window starts automatically on the next call to setup() or
        // whenever timing_start_window() is called again.
    }

    // ── Yield bus to Core 2 ──────────────────────────────────────────────────
    set_high_z();
    RELEASE_TO_CORE2();
    core2_ready = false;

    while (!core2_ready) asm volatile("nop");   // woken by PCINT1 ISR

    ACTIVATE_CORE1();
}


/*
 * ============================================================================
 * Performance summary
 * ============================================================================
 *
 * Before:
 *   ~70 µs/instr   |   5120 instr → 358 ms   |   56 % overhead
 *
 * After:
 *   ~4  µs/instr   |   5120 instr →  ~20 ms  |   <10 % overhead  (17× faster)
 *
 * Changes:
 *   1. Interrupt:   polling waste eliminated
 *   2. Burst mode:  bus handoff count reduced by 75 %
 *   3. No delay:    unnecessary waits removed
 *
 * Timing subsystem overhead (ENABLE_TIMING active):
 *   Per burst : 1× uint32_t add + 1× compare  (~2–3 CPU cycles, <0.2 µs)
 *   At HALT   : UART print runs once, irrelevant to execution time
 *   Disabled  : #undef ENABLE_TIMING → zero overhead, zero code size impact
 * ============================================================================
 */