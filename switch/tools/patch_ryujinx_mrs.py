#!/usr/bin/env python3
# Patch GCC 16 libgcc's GCS/SME/SVE runtime instructions out of a linked Switch ELF so Ryujinx's ARM JIT survives a
# C++ exception unwind (switch-homebrew.md). The Tegra X1 is Cortex-A57 (ARMv8.0-A): it has neither GCS nor SME nor
# SVE, so every one of these instructions sits in feature-guarded dead code (behind a `chkfeat`/`cbnz` pair or an
# `__aarch64_*_accessible` flag that is 0), and a -mcpu=cortex-a57 compile cannot emit any of them for engine code.
# They come only from the prebuilt libgcc/libc the toolchain links. Replacing each with NOP is therefore neutral on
# the console while removing what Ryujinx's JIT aborts on. Run after linking, before elf2nro.
#
# Robust and idempotent: it disassembles .text, finds the targets by mnemonic/operand (not by fixed offsets, which
# move every rebuild), verifies the bytes at each site against the disassembler's own word before writing, and a
# second run finds nothing (NOPs no longer match). It refuses to touch a site outside a libgcc/libc support symbol.

import argparse
import re
import subprocess
import sys

NOP = bytes((0x1F, 0x20, 0x03, 0xD5))  # d503201f, little-endian

# objdump mnemonic/operand patterns for the ARMv8.1+/v9 feature instructions no A57 implements.
TARGET = re.compile(
    r"\b(chkfeat|gcspopm|gcspushm|gcsss1|gcsss2|gcsstr|gcsss|smstart|smstop)\b"
    r"|\b(mrs|msr)\b.*\b(gcspr_el0|tpidr2_el0|svcr)\b"
    r"|\b(str|ldr)\s+za\b"
    r"|\b(addsvl|addpl|rdsvl|rdvl)\b"
    r"|\bcnt[bhwd]\b"
)

# A site must live in one of these libgcc unwinder / AArch64 runtime symbols; a match anywhere else would mean the
# compiler emitted SVE/SME for engine code (it cannot under -mcpu=cortex-a57), so the heuristic is wrong -> abort.
SAFE_SYMS = re.compile(
    r"(_Unwind_|uw_update_context|uw_frame_state|execute_stack_op|execute_cfa_program"
    r"|__arm_|__gnu_|__sme|__sve|__aarch64)",
    re.IGNORECASE,
)

LINE = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(.*)$")
SYMHDR = re.compile(r"^[0-9a-f]+ <([^>]+)>:\s*$")


def section_text(readelf, elf):
    out = subprocess.check_output([readelf, "-S", "-W", elf], text=True)
    # Two-line entries with -W collapse to one; match the .text row and read Address and Off.
    for m in re.finditer(r"\]\s+\.text\s+\w+\s+([0-9a-f]+)\s+([0-9a-f]+)", out):
        return int(m.group(1), 16), int(m.group(2), 16)
    raise SystemExit("could not find .text in readelf -S output")


def find_sites(objdump, elf):
    out = subprocess.check_output(
        [objdump, "-d", "-j", ".text", elf], text=True, stderr=subprocess.DEVNULL
    )
    sites, sym = [], "?"
    for line in out.splitlines():
        h = SYMHDR.match(line)
        if h:
            sym = h.group(1)
            continue
        m = LINE.match(line)
        if not m:
            continue
        vaddr, raw, disasm = int(m.group(1), 16), m.group(2), m.group(3).strip()
        if TARGET.search(disasm):
            sites.append((vaddr, int(raw, 16), disasm, sym))
    return sites


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("elf")
    ap.add_argument("--objdump", required=True)
    ap.add_argument("--readelf", required=True)
    ap.add_argument("--apply", action="store_true", help="write NOPs (default: dry-run report)")
    args = ap.parse_args()

    text_addr, text_off = section_text(args.readelf, args.elf)
    sites = find_sites(args.objdump, args.elf)

    if not sites:
        print("no GCS/SME/SVE instructions found (already patched or none emitted)")
        return 0

    bad = [s for s in sites if not SAFE_SYMS.search(s[3])]
    for vaddr, word, disasm, sym in sites:
        foff = vaddr - text_addr + text_off
        flag = "" if SAFE_SYMS.search(sym) else "  <-- UNEXPECTED SYMBOL"
        print(f"  0x{vaddr:08x} (file 0x{foff:08x}) {word:08x}  {disasm:28s} in {sym}{flag}")
    print(f"total: {len(sites)} site(s)")
    if bad:
        print(f"ABORT: {len(bad)} site(s) outside libgcc/libc runtime symbols; patch not applied", file=sys.stderr)
        return 2
    if not args.apply:
        print("dry-run: pass --apply to write NOPs")
        return 0

    with open(args.elf, "r+b") as f:
        for vaddr, word, disasm, sym in sites:
            foff = vaddr - text_addr + text_off
            f.seek(foff)
            cur = f.read(4)
            if int.from_bytes(cur, "little") != word:
                raise SystemExit(
                    f"byte mismatch at 0x{vaddr:08x} (file 0x{foff:08x}): "
                    f"elf has {cur.hex()}, disassembler said {word:08x}"
                )
            f.seek(foff)
            f.write(NOP)
    print(f"patched {len(sites)} instruction(s) to NOP")
    return 0


if __name__ == "__main__":
    sys.exit(main())
