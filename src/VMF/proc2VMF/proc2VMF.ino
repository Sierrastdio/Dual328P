#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
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
#define BURST_SIZE  4

// ─── Timing measurement ───────────────────────────────────────────────────────
//
//  Enable:  uncomment #define ENABLE_TIMING
//  Disable: comment it out → zero runtime overhead
//
//  Result flow:
//    HALT → eeprom_update saves 9 bytes (~30 ms, after execution)
//    Next power-on → setup() reads EEPROM, prints via UART (595 not yet driven)
//    → flag cleared → normal execution begins
//
#define ENABLE_TIMING
#define TIMING_INTERVAL  5120UL

// ─── EEPROM layout (9 bytes total) ───────────────────────────────────────────
//  Core 2 uses offset 0x10 to avoid collision with Core 1 (0x00~0x08)
//  0x10~0x13 : timing_result_us    (uint32_t, little-endian)
//  0x14~0x17 : timing_result_instr (uint32_t, little-endian)
//  0x18      : flag  0xAA = valid data present
#define EEPROM_ADDR_US    ((uint32_t*)0x10)
#define EEPROM_ADDR_INSTR ((uint32_t*)0x14)
#define EEPROM_ADDR_FLAG  ((uint8_t*) 0x18)
#define EEPROM_FLAG_VALID 0xAA

// ─── VM state ─────────────────────────────────────────────────────────────────
volatile uint8_t regA         = 0x00;
volatile uint8_t slot[16];
volatile uint8_t stack[8];
volatile uint8_t stack_ptr    = 0;
volatile uint8_t PC           = 0;
volatile uint8_t current_page = 0;
volatile bool    halted       = false;
volatile uint8_t cached_page  = 0xFF;

#define PAGE_REG slot[15]

// ─── Hardware macros ─────────────────────────────────────────────────────────
#define SIGNAL_BUSY()       PORTC &= ~(1 << 5)  // PC5 = LOW  (working)
#define SIGNAL_DONE()       PORTC |=  (1 << 5)  // PC5 = HIGH (done)

#define HC595_SER_HIGH()    PORTD |=  (1 << 0)
#define HC595_SER_LOW()     PORTD &= ~(1 << 0)
#define HC595_SCK_HIGH()    PORTD |=  (1 << 1)
#define HC595_SCK_LOW()     PORTD &= ~(1 << 1)
#define HC595_RCK_HIGH()    PORTC |=  (1 << 3)
#define HC595_RCK_LOW()     PORTC &= ~(1 << 3)

#define SYNC_DELAY()        asm volatile("nop\n\t nop\n\t nop\n\t nop\n\t")


// ============================================================================
//  TIMING SUBSYSTEM
// ============================================================================
#ifdef ENABLE_TIMING

volatile uint32_t _t1_overflows = 0;

ISR(TIMER1_OVF_vect) {
    _t1_overflows++;
}

static inline uint32_t _get_ticks() {
    uint8_t  sreg = SREG;
    cli();
    uint32_t ov = _t1_overflows;
    uint16_t t  = TCNT1;
    if ((TIFR1 & (1 << TOV1)) && t < 0x8000U) ov++;
    SREG = sreg;
    return (ov << 16) | t;
}

volatile uint32_t timing_result_us    = 0;
volatile uint32_t timing_result_instr = 0;

static uint32_t _timing_start   = 0;
static uint32_t _timing_counted = 0;
static bool     _timing_active  = false;

void timing_init() {
    TCCR1A = 0;
    TCCR1B = (1 << CS11);
    TIMSK1 = (1 << TOIE1);
    TCNT1  = 0;
    _t1_overflows = 0;
    _timing_active = false;
}

static inline void timing_start_window() {
    _timing_counted = 0;
    _timing_start   = _get_ticks();
    _timing_active  = true;
}

static inline bool timing_tick(uint8_t actual_count) {
    if (!_timing_active) return false;
    _timing_counted += actual_count;
    if (_timing_counted >= TIMING_INTERVAL) {
        uint32_t elapsed    = _get_ticks() - _timing_start;
        timing_result_us    = elapsed >> 1;
        timing_result_instr = _timing_counted;
        _timing_active = false;
        return true;
    }
    return false;
}

static void _uart_init() {
    UBRR0H = 0;
    UBRR0L = 103;
    UCSR0B = (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
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
    while (i--) _uart_putc(buf[i]);
}

void timing_save_eeprom() {
    eeprom_update_dword(EEPROM_ADDR_US,    timing_result_us);
    eeprom_update_dword(EEPROM_ADDR_INSTR, timing_result_instr);
    eeprom_update_byte (EEPROM_ADDR_FLAG,  EEPROM_FLAG_VALID);
}

void timing_load_and_print_eeprom() {
    if (eeprom_read_byte(EEPROM_ADDR_FLAG) != EEPROM_FLAG_VALID) return;

    uint32_t us    = eeprom_read_dword(EEPROM_ADDR_US);
    uint32_t instr = eeprom_read_dword(EEPROM_ADDR_INSTR);

    _uart_init();
    _uart_puts("[TIMING] ");
    _uart_putu32(instr);
    _uart_puts(" instr in ");
    _uart_putu32(us);
    _uart_puts(" us (");
    if (instr > 0) _uart_putu32((us * 1000UL) / instr);
    else           _uart_putc('?');
    _uart_puts(" ns/instr)\r\n");

    eeprom_update_byte(EEPROM_ADDR_FLAG, 0x00);
}

#else
#define timing_init()                   do {} while(0)
#define timing_start_window()           do {} while(0)
#define timing_tick(n)                  (false)
#define timing_save_eeprom()            do {} while(0)
#define timing_load_and_print_eeprom()  do {} while(0)
#endif  // ENABLE_TIMING
// ============================================================================


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
    DDRD  &= 0b00000011;  PORTD &= 0b00000011;
    DDRB  &= 0b11111100;  PORTB &= 0b11111100;
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
    DDRB |= 0b00111100;
    DDRC |= 0b00000111;
    DDRD |= 0b00000011;
    DDRC |= 0b00001000;
    DDRC |= 0b00100000;  // PC5 output (done signal)

    regA = 0x00; PC = 0; stack_ptr = 0;
    current_page = 0; cached_page = 0xFF; halted = false;
    for (uint8_t i = 0; i < 16; i++) slot[i]  = 0;
    for (uint8_t i = 0; i < 8;  i++) stack[i] = 0;
    PAGE_REG = 0;

    // ★ 595가 구동되기 전 — UART 안전 구간 ★
    timing_load_and_print_eeprom();

    set_page_595(0);    // ← 여기서부터 595 구동 시작

    SIGNAL_DONE();   // initial state: ready

    timing_init();
    timing_start_window();

    _delay_ms(100);
}


// ============================================================================
//  Loop
// ============================================================================
void loop() {
    if (halted) {
        set_high_z();
        SIGNAL_DONE();
        timing_save_eeprom();   // ~30 ms, 실행 종료 후라 측정값 오염 없음
        while (1);
    }

    SIGNAL_BUSY();

    uint8_t actual = 0;
    for (uint8_t i = 0; i < BURST_SIZE; i++) {
        execute(fetch());
        PC++;
        if (PC >= 128) PC = 0;
        actual++;
        if (halted) break;
    }

    timing_tick(actual);

    set_high_z();
    SIGNAL_DONE();

    _delay_us(20);
}