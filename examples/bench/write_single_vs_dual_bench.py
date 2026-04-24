import pathlib


ROOT = pathlib.Path(__file__).resolve().parent

# VM address model:
# - 7-bit page register (A7~A13): 128 pages
# - 128 bytes per page
# => executable space per core = 16 KiB
PAGE_SIZE = 128
PAGE_COUNT = 128
TARGET_INSTR = PAGE_SIZE * PAGE_COUNT - 1  # compiler.py appends final HALT

# Requested single benchmark image size.
SINGLE_INSTR = 32_768 - 1
DUAL_INSTR = TARGET_INSTR


DUAL_0_BLOCK = [
    "LOAD 15",
    "SLOT 0",
    "LOAD 7",
    "SLOT 1",
    "FETCH 0",
    "ADD 1",
    "SLOT 0",
    "FETCH 0",
    "SUB 1",
    "SLOT 0",
    "FETCH 0",
    "MUL 2",
    "SLOT 0",
    "FETCH 0",
    "AND 14",
    "SLOT 0",
    "FETCH 0",
    "OR 1",
    "SLOT 0",
    "FETCH 1",
    "ADD 3",
    "SLOT 1",
    "FETCH 1",
    "SUB 2",
    "SLOT 1",
    "FETCH 1",
    "MUL 2",
    "SLOT 1",
    "FETCH 1",
    "AND 13",
    "SLOT 1",
    "FETCH 1",
    "OR 2",
    "SLOT 1",
    "FETCH 0",
    "ADD 4",
    "SLOT 0",
    "FETCH 0",
    "SUB 3",
    "SLOT 0",
    "FETCH 1",
    "ADD 5",
    "SLOT 1",
    "FETCH 1",
    "SUB 4",
    "SLOT 1",
]

DUAL_1_BLOCK = [
    "LOAD 12",
    "SLOT 0",
    "LOAD 5",
    "SLOT 1",
    "FETCH 0",
    "ADD 2",
    "SLOT 0",
    "FETCH 0",
    "SUB 1",
    "SLOT 0",
    "FETCH 0",
    "MUL 2",
    "SLOT 0",
    "FETCH 0",
    "AND 11",
    "SLOT 0",
    "FETCH 0",
    "OR 3",
    "SLOT 0",
    "FETCH 1",
    "ADD 4",
    "SLOT 1",
    "FETCH 1",
    "SUB 2",
    "SLOT 1",
    "FETCH 1",
    "MUL 2",
    "SLOT 1",
    "FETCH 1",
    "AND 14",
    "SLOT 1",
    "FETCH 1",
    "OR 1",
    "SLOT 1",
    "FETCH 0",
    "ADD 6",
    "SLOT 0",
    "FETCH 0",
    "SUB 5",
    "SLOT 0",
    "FETCH 1",
    "ADD 3",
    "SLOT 1",
    "FETCH 1",
    "SUB 1",
    "SLOT 1",
]


def _fill_workload(length: int, block: list[str], seed: int) -> list[str]:
    out: list[str] = []
    idx = seed % len(block)
    while len(out) < length:
        out.append(block[idx])
        idx = (idx + 1) % len(block)
    return out


def build_paged_program(block: list[str], instruction_count: int) -> list[str]:
    if instruction_count != TARGET_INSTR:
        raise ValueError(f"instruction_count must be {TARGET_INSTR}")

    program: list[str] = []

    for page in range(PAGE_COUNT):
        is_last = page == PAGE_COUNT - 1
        next_page = (page + 1) & 0x7F

        # Each page starts with a NOP because SETPAGE in VM sets PC=0,
        # then loop() increments PC, so next page effectively starts from offset 1.
        page_body: list[str] = ["NOP"]

        if is_last:
            # Final page: compiler-appended HALT sits at page127:offset127.
            # We generate up to offset126 (127 instructions total in this page).
            page_body.extend(_fill_workload(PAGE_SIZE - 1 - len(page_body), block, page))
        else:
            # Non-final page: reserve tail for explicit page hop.
            page_body.extend(_fill_workload(PAGE_SIZE - 4 - len(page_body), block, page))
            page_body.extend(
                [
                    f"LOAD {next_page}",
                    "SLOT 15",
                    "SETPAGE",
                    "NOP",
                ]
            )

        program.extend(page_body)

    # Remove the very last instruction slot: compiler.py will append HALT there.
    return program[:instruction_count]


def build_linear_program(parts: list[list[str]], instruction_count: int) -> list[str]:
    program: list[str] = []
    for part in parts:
        program.extend(part)
    if len(program) > instruction_count:
        return program[:instruction_count]
    while len(program) < instruction_count:
        program.append("NOP")
    return program


def write_asm(path: pathlib.Path, header: str, program: list[str]) -> None:
    lines = [header, ""] + program + [""]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"{path.name}: {len(program)} instructions (+ compiler HALT)")


def main() -> None:
    dual_0_program = build_paged_program(DUAL_0_BLOCK, DUAL_INSTR)
    dual_1_program = build_paged_program(DUAL_1_BLOCK, DUAL_INSTR)
    # single image is 32 KiB: bank0-style + bank1-style payload concatenated.
    single_program = build_linear_program([dual_0_program, dual_1_program], SINGLE_INSTR)

    write_asm(
        ROOT / "dual_0.asm",
        "; dual_0.asm - 16 KiB executable benchmark for bank 0 (Core 1)",
        dual_0_program,
    )
    write_asm(
        ROOT / "dual_1.asm",
        "; dual_1.asm - 16 KiB executable benchmark for bank 1 (Core 2)",
        dual_1_program,
    )
    write_asm(
        ROOT / "single.asm",
        "; single.asm - 32 KiB benchmark payload for single-core comparison",
        single_program,
    )


if __name__ == "__main__":
    main()
