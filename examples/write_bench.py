import pathlib

ROOT = pathlib.Path(__file__).resolve().parent

ASM_16K = """; bench.asm - 16 KiB VM binary for BPU Nano / Uno (BPUnano MAX_TOTAL=16384)
; 16383 opcodes here + compiler-appended HALT = 16384 bytes in one :wb transfer.
; NOP = 0x00 each line. (32768-byte image is Mega-only; see bench_mega_32k.asm)
;
;   python src/BPU/compiler.py examples/bench.asm COM3 0 0 -b
;
"""

ASM_32K = """; bench_mega_32k.asm - 32 KiB VM binary for BPU Mega only (MAX_TOTAL=32768)
; 32767 opcodes + compiler HALT = 32768 bytes. BPUnano rejects this with ERR: SIZE.
;
;   python src/BPU/compiler.py examples/bench_mega_32k.asm COM3 0 0 -b
;
"""


def write(path: pathlib.Path, nops: int, header: str) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
        f.write("\n".join(["NOP"] * nops))
        f.write("\n")
    print(path.name, nops, "NOPs")


def main() -> None:
    write(ROOT / "bench.asm", 16383, ASM_16K)
    write(ROOT / "bench_mega_32k.asm", 32767, ASM_32K)


if __name__ == "__main__":
    main()
