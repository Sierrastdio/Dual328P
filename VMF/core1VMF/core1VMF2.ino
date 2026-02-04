/*
 * ============================================================================
 * Core 1 - Virtual Machine Firmware v3.0 (Pin-Map Compliant)
 * ============================================================================
 * 핀 배치:
 * - PD2~7, PB0~1: 데이터 버스 (D0~D7) → 74HC245 데이터버퍼#1
 * - PB2~5, PC0~2: 주소 버스 (A0~A6) → 74HC245 주소버퍼#1
 * - PC3: A7 주소선 → 74HC245 주소버퍼#1
 * - PC4: Core Select (0=Core1 Active) + EEPROM A14 → 74HC04, 28C256
 * - PC5: DIR (모든 74HC245 방향 제어)
 * 
 * 주요 변경사항:
 * - 각 코어는 A0~A7 (8비트, 256바이트) 인식
 * - PC4=0: Core1 활성화, EEPROM Bank1 (A14=0)
 * - PC4=1: Core2 활성화, EEPROM Bank2 (A14=1)
 * ============================================================================
 */

#include <avr/io.h>
#include <util/delay.h>

// Instruction Set
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

// VM State
volatile uint8_t regA = 0x00;
volatile uint8_t slots[16];        // 16 variable slots
volatile uint8_t stack[8];         // 8-deep stack
volatile uint8_t SP = 0;           // Stack pointer
volatile uint8_t PC = 0;           // Program Counter (0-255)
volatile bool halted = false;

// Hardware Control Macros
// PC4: 0 = Core1 Active (EEPROM Bank1), 1 = Core2 Active (EEPROM Bank2)
#define ACTIVATE_CORE1()    PORTC &= ~(1 << 4)
#define RELEASE_TO_CORE2()  PORTC |=  (1 << 4)

// PC5: DIR Control (0 = Read from EEPROM, 1 = Write to Monitor/Bus)
#define SET_BUS_READ()      PORTC &= ~(1 << 5)
#define SET_BUS_WRITE()     PORTC |=  (1 << 5)

#define SYNC_DELAY()        asm volatile("nop\n\t nop\n\t nop\n\t nop\n\t")

/*
 * ============================================================================
 * 주소 버스 설정: A0~A7 (8-bit, 256바이트)
 * ============================================================================
 * A0~A3: PB2~5
 * A4~A6: PC0~2
 * A7: PC3
 */
inline void set_addr_bus(uint8_t addr) {
    // A0~A3 (PB2~5)
    PORTB = (PORTB & 0xC3) | ((addr & 0x0F) << 2);
    
    // A4~A6 (PC0~2)
    PORTC = (PORTC & 0xF8) | ((addr >> 4) & 0x07);
    
    // A7 (PC3)
    if (addr & 0x80) {
        PORTC |= (1 << 3);
    } else {
        PORTC &= ~(1 << 3);
    }
}

/*
 * ============================================================================
 * 데이터 버스 입출력
 * ============================================================================
 * D0~D5: PD2~7
 * D6~D7: PB0~1
 */
inline void set_data_output() {
    DDRD |= 0xFC;  // PD2~7 출력
    DDRB |= 0x03;  // PB0~1 출력
}

inline void set_data_input() {
    DDRD &= 0x03;  // PD2~7 입력
    PORTD &= 0x03; // 풀업 비활성화
    DDRB &= 0xFC;  // PB0~1 입력
    PORTB &= 0xFC; // 풀업 비활성화
}

inline void write_data_bus(uint8_t data) {
    PORTD = (PORTD & 0x03) | ((data << 2) & 0xFC);
    PORTB = (PORTB & 0xFC) | ((data >> 6) & 0x03);
}

inline uint8_t read_data_bus() {
    return ((PIND & 0xFC) >> 2) | ((PINB & 0x03) << 6);
}

/*
 * ============================================================================
 * High-Z 상태: 버스에서 완전히 분리
 * ============================================================================
 */
inline void set_high_z() {
    // 데이터 버스 High-Z
    DDRD &= 0x03; 
    PORTD &= 0x03;
    DDRB &= 0xFC; 
    PORTB &= 0xFC;
    
    // 주소 버스 High-Z (PB2~5, PC0~3)
    DDRB &= 0xC3;
    PORTB &= 0xC3;
    DDRC &= 0xF0;
    PORTC &= 0xF0;
}

/*
 * ============================================================================
 * Fetch: ROM에서 명령어 읽기
 * ============================================================================
 */
uint8_t fetch() {
    ACTIVATE_CORE1();
    SET_BUS_READ();
    set_data_input();
    
    // 주소 버스 출력 모드
    DDRB |= 0x3C;  // PB2~5 출력
    DDRC |= 0x0F;  // PC0~3 출력
    
    set_addr_bus(PC);
    
    SYNC_DELAY();
    _delay_us(5);  // EEPROM 액세스 타임
    
    uint8_t instruction = read_data_bus();
    
    return instruction;
}

/*
 * ============================================================================
 * Output: 나노#2가 데이터 버스를 읽을 수 있도록 출력
 * ============================================================================
 */
void output_register(uint8_t value) {
    SET_BUS_WRITE();
    set_data_output();
    
    write_data_bus(value);
    
    SYNC_DELAY();
    _delay_us(50);  // 나노#2가 74HC165로 읽을 시간 확보
    
    SET_BUS_READ();
    set_data_input();
}

/*
 * ============================================================================
 * Execute: 명령어 실행
 * ============================================================================
 */
void execute(uint8_t instruction) {
    uint8_t opcode = instruction & 0xF0;
    uint8_t operand = instruction & 0x0F;
    
    switch(opcode) {
        case OP_NOP:
            break;
            
        case OP_LOAD:
            regA = operand;
            break;
            
        case OP_ADD:
            regA += operand;
            break;
            
        case OP_SUB:
            regA -= operand;
            break;
            
        case OP_MUL:
            regA *= operand;
            break;
            
        case OP_AND:
            regA &= operand;
            break;
            
        case OP_OR:
            regA |= operand;
            break;
            
        case OP_OUT:
            output_register(regA);
            break;
            
        case OP_FETCH:
            // Load from slot to regA
            regA = slots[operand & 0x0F];
            break;
            
        case OP_SLOT:
            // Store regA to slot
            slots[operand & 0x0F] = regA;
            break;
            
        case OP_PUSH:
            // Push regA to stack
            if(SP < 8) {
                stack[SP++] = regA;
            }
            break;
            
        case OP_POP:
            // Pop from stack to regA
            if(SP > 0) {
                regA = stack[--SP];
            }
            break;
            
        case OP_SETPAGE:
            // 페이지 전환 (실제로는 PC 상위 비트 조작)
            // operand는 0~15 범위의 페이지 번호
            // 현재 설계에서는 A0~A7만 제어 가능하므로
            // 페이지는 논리적 개념으로만 존재
            PC = (operand & 0x0F) << 4;  // 페이지*16 위치로 점프
            break;
            
        case OP_HALT:
            halted = true;
            break;
            
        default:
            break;
    }
}

void setup() {
    // 포트 방향 설정
    DDRB |= 0x3C;  // PB2~5 (주소 A0~3) 출력
    DDRC |= 0x3F;  // PC0~5 (주소 A4~7, Core Select, DIR) 출력
    
    // 초기 상태
    ACTIVATE_CORE1();
    SET_BUS_READ();
    set_data_input();
    
    // VM 상태 초기화
    regA = 0x00;
    PC = 0;
    SP = 0;
    halted = false;
    
    // Slots, Stack 초기화
    for(uint8_t i = 0; i < 16; i++) slots[i] = 0;
    for(uint8_t i = 0; i < 8; i++) stack[i] = 0;
    
    _delay_ms(100);
}

void loop() {
    if(halted) {
        set_high_z();
        RELEASE_TO_CORE2();
        _delay_ms(100);
        return;
    }
    
    // Fetch-Decode-Execute
    uint8_t instruction = fetch();
    execute(instruction);
    
    // PC 증가 (256바이트 순환)
    PC++;
    
    // Core 간 전환 (인터리빙)
    set_high_z();
    RELEASE_TO_CORE2();
    _delay_us(50);  // Core 2가 작업할 시간
    ACTIVATE_CORE1();
    
    _delay_us(10);
}

/*
 * ============================================================================
 * Example Programs
 * ============================================================================
 * 
 * Example 1 - 슬롯과 스택 사용:
 * -------------------------------------
 * LOAD 10      ; A = 10
 * SLOT 0       ; slots[0] = 10
 * LOAD 5       ; A = 5
 * SLOT 1       ; slots[1] = 5
 * FETCH 0      ; A = slots[0] = 10
 * PUSH         ; stack = [10]
 * FETCH 1      ; A = slots[1] = 5
 * POP          ; A = 10
 * OUT          ; 출력
 * HALT
 * 
 * Example 2 - 페이지 전환 (논리적):
 * ----------------------------
 * LOAD 5
 * SLOT 0
 * SETPAGE 1    ; PC = 16 (페이지 1의 시작)
 * ; 이후 명령어는 16번지부터 실행
 * 
 * Example 3 - 복합 연산:
 * --------------------------------
 * LOAD 3       ; A = 3
 * SLOT 0       ; x = 3
 * LOAD 4       ; A = 4
 * SLOT 1       ; y = 4
 * FETCH 0      ; A = 3
 * PUSH         ; stack = [3]
 * FETCH 1      ; A = 4
 * PUSH         ; stack = [3, 4]
 * POP          ; A = 4
 * SLOT 2       ; temp = 4
 * POP          ; A = 3
 * FETCH 2      ; A = 4
 * MUL 3        ; A = 12
 * OUT
 * HALT
 * 
 * ============================================================================
 * 설계 제약사항
 * ============================================================================
 * 1. 각 코어는 256바이트만 인식 (A0~A7)
 * 2. A8~A13은 74HC595로 나노#1이 제어
 * 3. A14는 PC4로 Bank 선택 (Core1=0, Core2=1)
 * 4. SETPAGE는 논리적 페이지 점프로만 동작
 * 5. 물리적 전체 메모리 접근은 나노#1이 프로그램 로드 시 처리
 * ============================================================================
 */