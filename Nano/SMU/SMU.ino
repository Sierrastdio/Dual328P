/*
 * ============================================================================
 * Arduino Nano #1 - Assembler & ROM Programmer
 * System Management Unit (SMU)
 * ============================================================================
 * 
 * Role: Receive assembly code from PC, convert to bytecode, load into ROM
 * 
 * Assembly Language Format:
 * -------------------------
 * LOAD <value>    - Load immediate value into A
 * ADD <value>     - Add value to A
 * SUB <value>     - Subtract value from A
 * MUL <value>     - Multiply A by value
 * AND <value>     - Bitwise AND
 * OR <value>      - Bitwise OR
 * OUT             - Output A to RegA
 * HALT            - Stop execution
 * 
 * Example Program:
 * ----------------
 * LOAD 10
 * ADD 5
 * MUL 2
 * OUT
 * HALT
 * 
 * Serial Protocol:
 * ----------------
 * ASM <core> <line>    - Send assembly line (e.g., ASM 1 LOAD 10)
 * COMPILE              - Finish and compile to bytecode
 * LOAD <bank>          - Load compiled code into ROM bank
 * RUN                  - Start core execution
 * RESET                - Reset cores
 * 
 * ============================================================================
 */

// ============================================================================
// Pin Definitions
// ============================================================================
const uint8_t DATA_PINS[] = {2, 3, 4, 5, 6, 7, 8, 9};
const uint8_t ROM_A14 = 11;
const uint8_t ROM_OE = 12;
const uint8_t ROM_WE = 13;
const uint8_t HC595_DS = A1;
const uint8_t HC595_SHCP = A2;
const uint8_t HC595_STCP = A3;
const uint8_t SYS_RESET = A0;

// ============================================================================
// Instruction Set
// ============================================================================
#define OP_NOP   0x00
#define OP_LOAD  0x10
#define OP_ADD   0x20
#define OP_SUB   0x30
#define OP_MUL   0x40
#define OP_AND   0x50
#define OP_OR    0x60
#define OP_OUT   0x70
#define OP_HALT  0xF0

struct Opcode {
    const char* mnemonic;
    uint8_t code;
    bool has_operand;
};

const Opcode opcodes[] = {
    {"NOP",  OP_NOP,  false},
    {"LOAD", OP_LOAD, true},
    {"ADD",  OP_ADD,  true},
    {"SUB",  OP_SUB,  true},
    {"MUL",  OP_MUL,  true},
    {"AND",  OP_AND,  true},
    {"OR",   OP_OR,   true},
    {"OUT",  OP_OUT,  false},
    {"HALT", OP_HALT, false}
};

const int NUM_OPCODES = sizeof(opcodes) / sizeof(Opcode);

// ============================================================================
// Program Buffer
// ============================================================================
#define MAX_PROGRAM_SIZE 128

uint8_t program_buffer[MAX_PROGRAM_SIZE];
uint16_t program_size = 0;
String assembly_lines[MAX_PROGRAM_SIZE];
uint16_t line_count = 0;

// ============================================================================
// Hardware Control
// ============================================================================

void setAddress(uint16_t addr) {
    digitalWrite(HC595_STCP, LOW);
    shiftOut(HC595_DS, HC595_SHCP, MSBFIRST, (addr >> 7) & 0x7F);
    shiftOut(HC595_DS, HC595_SHCP, MSBFIRST, addr & 0x7F);
    digitalWrite(HC595_STCP, HIGH);
    digitalWrite(HC595_STCP, LOW);
}

void setBank(uint8_t bank) {
    digitalWrite(ROM_A14, bank & 0x01);
}

void haltCores() {
    digitalWrite(SYS_RESET, LOW);
    delay(10);
}

void resumeCores() {
    digitalWrite(SYS_RESET, HIGH);
    delay(50);
}

void writeROM(uint16_t addr, uint8_t data) {
    haltCores();
    
    setBank((addr >> 14) & 0x01);
    setAddress(addr & 0x3FFF);
    
    for(int i = 0; i < 8; i++) {
        pinMode(DATA_PINS[i], OUTPUT);
        digitalWrite(DATA_PINS[i], (data >> i) & 0x01);
    }
    
    digitalWrite(ROM_OE, HIGH);
    digitalWrite(ROM_WE, LOW);
    delayMicroseconds(1);
    digitalWrite(ROM_WE, HIGH);
    delay(10);
    
    for(int i = 0; i < 8; i++) {
        pinMode(DATA_PINS[i], INPUT);
    }
}

// ============================================================================
// Assembler Functions
// ============================================================================

/**
 * Parse assembly line and return bytecode
 */
int assembleLine(String line, uint8_t* bytecode) {
    line.trim();
    line.toUpperCase();
    
    if(line.length() == 0 || line.startsWith(";")) {
        return 0;  // Empty or comment
    }
    
    // Split into tokens
    int space_idx = line.indexOf(' ');
    String mnemonic = (space_idx > 0) ? line.substring(0, space_idx) : line;
    String operand_str = (space_idx > 0) ? line.substring(space_idx + 1) : "";
    
    mnemonic.trim();
    operand_str.trim();
    
    // Find opcode
    for(int i = 0; i < NUM_OPCODES; i++) {
        if(mnemonic.equals(opcodes[i].mnemonic)) {
            if(opcodes[i].has_operand) {
                if(operand_str.length() == 0) {
                    Serial.println(F("[ERROR] Missing operand"));
                    return -1;
                }
                
                int operand = operand_str.toInt();
                if(operand < 0 || operand > 15) {
                    Serial.print(F("[ERROR] Operand out of range (0-15): "));
                    Serial.println(operand);
                    return -1;
                }
                
                *bytecode = opcodes[i].code | (operand & 0x0F);
                return 1;
            } else {
                *bytecode = opcodes[i].code;
                return 1;
            }
        }
    }
    
    Serial.print(F("[ERROR] Unknown instruction: "));
    Serial.println(mnemonic);
    return -1;
}

/**
 * Compile all assembly lines to bytecode
 */
bool compileProgram() {
    program_size = 0;
    
    Serial.println(F("\n=== Compiling Program ==="));
    
    for(uint16_t i = 0; i < line_count; i++) {
        if(program_size >= MAX_PROGRAM_SIZE) {
            Serial.println(F("[ERROR] Program too large!"));
            return false;
        }
        
        uint8_t bytecode;
        int result = assembleLine(assembly_lines[i], &bytecode);
        
        if(result < 0) {
            Serial.print(F("Line "));
            Serial.print(i + 1);
            Serial.print(F(": "));
            Serial.println(assembly_lines[i]);
            return false;
        } else if(result > 0) {
            program_buffer[program_size++] = bytecode;
            
            Serial.print(F("0x"));
            if(program_size - 1 < 0x10) Serial.print('0');
            Serial.print(program_size - 1, HEX);
            Serial.print(F(": 0x"));
            if(bytecode < 0x10) Serial.print('0');
            Serial.print(bytecode, HEX);
            Serial.print(F("  ; "));
            Serial.println(assembly_lines[i]);
        }
    }
    
    Serial.println(F("\n[OK] Compilation successful!"));
    Serial.print(F("Program size: "));
    Serial.print(program_size);
    Serial.println(F(" bytes\n"));
    
    return true;
}

/**
 * Load compiled program into ROM
 */
void loadProgram(uint8_t bank) {
    Serial.print(F("\n[LOAD] Loading program into Bank "));
    Serial.println(bank);
    
    haltCores();
    
    uint16_t base_addr = (bank == 0) ? 0x0000 : 0x4000;
    
    for(uint16_t i = 0; i < program_size; i++) {
        writeROM(base_addr + i, program_buffer[i]);
        
        if(i % 16 == 0) Serial.print(F("."));
    }
    
    Serial.println(F("\n[OK] Program loaded!\n"));
}

// ============================================================================
// Command Handler
// ============================================================================

void handleCommand() {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if(cmd.startsWith("ASM ")) {
        // Add assembly line: ASM 1 LOAD 10
        int first_space = cmd.indexOf(' ');
        int second_space = cmd.indexOf(' ', first_space + 1);
        
        if(second_space < 0) {
            Serial.println(F("[ERROR] Format: ASM <core> <instruction>"));
            return;
        }
        
        String core_str = cmd.substring(4, second_space);
        String asm_line = cmd.substring(second_space + 1);
        
        if(line_count >= MAX_PROGRAM_SIZE) {
            Serial.println(F("[ERROR] Program buffer full!"));
            return;
        }
        
        assembly_lines[line_count++] = asm_line;
        
        Serial.print(F("[ASM] Line "));
        Serial.print(line_count);
        Serial.print(F(": "));
        Serial.println(asm_line);
    }
    else if(cmd == "COMPILE") {
        compileProgram();
    }
    else if(cmd.startsWith("LOAD ")) {
        uint8_t bank = cmd.substring(5).toInt();
        
        if(program_size == 0) {
            Serial.println(F("[ERROR] No compiled program! Use COMPILE first."));
            return;
        }
        
        loadProgram(bank);
    }
    else if(cmd == "RUN") {
        Serial.println(F("\n[RUN] Starting cores...\n"));
        resumeCores();
    }
    else if(cmd == "RESET") {
        haltCores();
        Serial.println(F("\n[RESET] Cores halted\n"));
    }
    else if(cmd == "CLEAR") {
        line_count = 0;
        program_size = 0;
        Serial.println(F("\n[CLEAR] Program buffer cleared\n"));
    }
    else if(cmd == "LIST") {
        Serial.println(F("\n=== Assembly Listing ==="));
        for(uint16_t i = 0; i < line_count; i++) {
            Serial.print(i + 1);
            Serial.print(F(": "));
            Serial.println(assembly_lines[i]);
        }
        Serial.println();
    }
    else if(cmd == "HELP") {
        Serial.println(F("\n=== Assembler Commands ==="));
        Serial.println(F("ASM <core> <line>   - Add assembly line"));
        Serial.println(F("COMPILE             - Compile to bytecode"));
        Serial.println(F("LOAD <bank>         - Load into ROM (0=Core1, 1=Core2)"));
        Serial.println(F("RUN                 - Start execution"));
        Serial.println(F("RESET               - Halt cores"));
        Serial.println(F("CLEAR               - Clear program buffer"));
        Serial.println(F("LIST                - Show assembly listing"));
        Serial.println(F("\n=== Instruction Set ==="));
        Serial.println(F("LOAD <0-15>  - Load immediate"));
        Serial.println(F("ADD <0-15>   - Add"));
        Serial.println(F("SUB <0-15>   - Subtract"));
        Serial.println(F("MUL <0-15>   - Multiply"));
        Serial.println(F("AND <0-15>   - Bitwise AND"));
        Serial.println(F("OR <0-15>    - Bitwise OR"));
        Serial.println(F("OUT          - Output to RegA"));
        Serial.println(F("HALT         - Stop"));
        Serial.println(F("\n=== Example ==="));
        Serial.println(F("ASM 1 LOAD 5"));
        Serial.println(F("ASM 1 ADD 3"));
        Serial.println(F("ASM 1 OUT"));
        Serial.println(F("ASM 1 HALT"));
        Serial.println(F("COMPILE"));
        Serial.println(F("LOAD 0"));
        Serial.println(F("RUN\n"));
    }
    else {
        Serial.println(F("[ERROR] Unknown command. Type HELP"));
    }
}

// ============================================================================
// Setup & Loop
// ============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println(F("\n========================================"));
    Serial.println(F("  Dual-Core Assembler & ROM Loader"));
    Serial.println(F("========================================\n"));
    
    // Initialize pins
    for(int i = 0; i < 8; i++) {
        pinMode(DATA_PINS[i], INPUT);
    }
    pinMode(ROM_A14, OUTPUT);
    pinMode(ROM_OE, OUTPUT);
    pinMode(ROM_WE, OUTPUT);
    pinMode(HC595_DS, OUTPUT);
    pinMode(HC595_SHCP, OUTPUT);
    pinMode(HC595_STCP, OUTPUT);
    pinMode(SYS_RESET, OUTPUT);
    
    digitalWrite(ROM_A14, LOW);
    digitalWrite(ROM_OE, HIGH);
    digitalWrite(ROM_WE, HIGH);
    digitalWrite(SYS_RESET, LOW);  // Start with cores halted
    
    Serial.println(F("Type HELP for command list\n"));
}

void loop() {
    if(Serial.available()) {
        handleCommand();
    }
}