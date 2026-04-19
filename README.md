<!-- markdownlint-disable -->
# Dual-processor System with Virtual Machine Firmware
### main component:
* `Arduino Mega 2560`
* `shared RAM`
* `Processor1`
* `Processor2`
* `Arduino Nano(DDU)`

### parts list:
| parts type | name | number |
|-------|-------|-------|
| MCU   | `Arduino Mega 2560`   | 1 |
| MCU   | `Arduino Nano`   | 1 |
| MCU   | `Atmega328P (DIP)`    | 2 |
| Memory| `62256 SRAM`     | 1 |
| IC logic | `74HC245`     | 4 |
| IC logic | `74HC595`     | 2 |
| IC logic | `74HC04`      | 1 |
| IC logic | `74HC125`     | 1 |
| IC logic | `74HC165`     | 2 |
| Oscillator | `Active Crystal 16MHz`   | 1 |
********
### DST feature:

* **Processor-IEE**(processor Isolated Execution Environment)

* **VMF**(Virtual Machine Firmware on processor)

-------------

# Main Component

## 1. BPU (Binary Programmer Unit)

The `BPU` is based on `Arduino Mega 2560`. BPU receives binary and program(load) to RAM(`62256 SRAM`)

## 2. Processor 1

`Processor 1` is the first processor based on `Atmega328P`. It fetches and executes instructions from shared RAM, controlling data access through bus mastery.

## 3. Processor 2

`Processor 2` is the second processor based on `Atmega328P`. It alternates with Processor 1 to execute instructions using shared RAM.

## 4. DDU (Data Display Unit)

The DDU is the data display unit, using Arduino Nano to display system status or outputs.

********

********

# How it works

- Processor1,2 `RESET Pin` LOW (`:rst` from `BPU`)
- Load Binary(8bit) in RAM (`python compiler.py <filepath> <port> <bank> <page> -b`)
- Processor1,2 `RESET Pin` HIGH (`:run` from `BPU`)
  - -> Processor starts.
- 1. `Processor 1` Acquire `Bus Mastery` (`Processor 1 PC4` `LOW`; also RAM `A14` on that net)
- 2. `Processor 1` Read Data from `62256 RAM` and processing Instruction Fetch & Execute
- 3. `Processor 1` release `Bus Mastery` to `Processor 2` (`Processor 1 PC4` `HIGH`)
- 4. `Processor 2` Read Data from `62256 RAM` and processing Instruction Fetch & Execute


# Fuse Bit Configuration
When you use `Active Crystal(Oscillator)`, You need to configure the Fuse Bits to **enable clock output** on `Processor 1` and `Processor 2`.
If you don't have an active crystal, you can use this method(both methods need to configure fuse bits unless you use `Passive Crystal`.). This section explains how to configure fuse bits using only Arduino without a separate oscillator or active crystal to set up 16MHz generator and read mode.

1. Connect the Uno (A, programmer) to the PC via USB.

2. In Arduino IDE, select File > Examples > 11.ArduinoISP > ArduinoISP and upload it to the Uno to be used as programmer.

3. Connect the programmer Uno (A) and the target ex)Nano (B) as per the table below:

| Programmer Uno (A) | Target (B) |
|--------------------|-----------------|
| D10               | RESET          |
| D11               | D11            |
| D12               | D12            |
| D13               | D13            |
| 5V                | 5V             |
| GND               | GND            |

4. Connect the computer and the programmer Arduino (A) with the upload cable.

5. Windows Environment (PowerShell based)

   `Change to the avrdude directory:`
   ```powershell
   cd "C:\Users\user\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17\bin"
   ```

   `Verify connection:`
   ```powershell
   .\avrdude.exe -C ..\etc\avrdude.conf -c avrisp -p m328p -P COM3 -b 19200 -v
   ```
   Note: Ensure the COM port (COM3) and baud rate (19200) are consistent with your setup.

   `Set 16MHz CKOUT:`
   ```powershell
   .\avrdude.exe -C ..\etc\avrdude.conf -c avrisp -p m328p -P COM3 -b 19200 -U lfuse:w:0xBF:m
   ```

   `Set clock read mode:`
   ```powershell
   .\avrdude.exe -C ..\etc\avrdude.conf -c avrisp -p m328p -P COM3 -b 19200 -U lfuse:w:0xE0:m
   ```

   `Set default:`
   ```powershell
   .\avrdude.exe -C ..\etc\avrdude.conf -c avrisp -p m328p -P COM3 -b 19200 -U lfuse:w:0xFF:m -U hfuse:w:0xDA:m -U efuse:w:0xFD:m
   ```

# Usage
1. Connect the BPU to the PC.
2. Write code in your PC, compile to binary and send to BPU.
3. BPU will load the binary to RAM.
4. when you use `:run` to BPU, Processors will start to work.

## Repository layout

Overview of this repository (Arduino sketch folders follow the usual `FolderName/FolderName.ino` layout):

```
Dual328P/
├── LICENSE
|
├── PinManual.md
├── README.md
|
├── .vscode/
│   └── settings.json
|
├── examples/
│   ├── test1.asm
│   ├── test1.bas
|   |
│   ├── test2.asm
│   ├── test2.bas
|   |
|   ├── bench32K.asm
|   ├── bench16K.asm
|   └── write_bench.py
|
├── modifiedISP/
│   └── modifiedISP.ino
|
└── src/
    ├── BPU/
    │   ├── compiler.py
    │   ├── BPUmega/
    │   │   └── BPUmega.ino
    │   └── BPUnano/
    │       └── BPUnano.ino
    |
    ├── DDU/
    │   ├── DDU128x64/
    │   │   └── DDU128x64.ino
    │   └── DDU16x2/
    │       └── DDU16x2.ino
    |
    ├── SMU/
    │   ├── SMUeditor.py
    │   ├── SMUmega/
    │   │   └── SMUmega.ino
    │   └── SMUnano/
    │       └── SMUnano.ino
    |
    └── VMF/
        ├── proc1VMF/
        │   └── proc1VMF.ino
        └── proc2VMF/
            └── proc2VMF.ino
```

# Contributing

# License
This project is under the GNU General Public License v3.0 license. See the `LICENSE` file for details.

Third-Party Credits:
Modified ArduinoISP: This project includes a modified version of the ArduinoISP sketch.

Original Author: Copyright (c) 2008-2011 Randall Bohn (BSD License).

Modifications: include 8MHz clock output on D9.

The original copyright notices are preserved in the source code as per the BSD license requirements.


