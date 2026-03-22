"""
vma_infer.trace_reader – Parse ChampSim trace files (binary & text).

Binary format
=============
The default layout mirrors the ChampSim ``input_instr`` struct (64 bytes,
little-endian):

    Offset  Size  Field
    ------  ----  -----
     0       8    ip                       (uint64)
     8       1    is_branch                (uint8)
     9       1    branch_taken             (uint8)
    10       2    destination_registers[2]  (2 × uint8)
    14       4    source_registers[4]       (4 × uint8)
    18      16    destination_memory[2]     (2 × uint64)  ← WRITES
    34      32    source_memory[4]          (4 × uint64)  ← READS
    ----
    Total = 64 bytes

**To change the binary layout** edit the three constants below:
``RECORD_STRUCT``, ``RECORD_SIZE``, and the ``_parse_binary_record()``
helper.  Everything else adapts automatically.

Text format
===========
Each line is split on whitespace.  We try several column orders
(see ``_TEXT_PARSERS``) and pick the first one whose heuristic succeeds.
"""

from __future__ import annotations

import gzip
import io
import os
import struct
import sys
from typing import (
    BinaryIO,
    Callable,
    Generator,
    List,
    NamedTuple,
    Optional,
    TextIO,
    Tuple,
)

# ---------------------------------------------------------------------------
# Binary record layout – **EDIT HERE** to support a different struct.
# ---------------------------------------------------------------------------

#: ``struct.Struct`` describing one binary record.
#: Fields: ip(Q) is_branch(B) branch_taken(B) dest_regs(2B) src_regs(4B)
#:         dest_mem(2Q) src_mem(4Q)
RECORD_STRUCT = struct.Struct("<Q BB 2B 4B 2Q 4Q")

#: Convenience alias – number of bytes per record.
RECORD_SIZE: int = RECORD_STRUCT.size  # 64


class MemAccess(NamedTuple):
    """One memory access extracted from a trace record."""
    ip: int         # instruction pointer / PC
    addr: int       # virtual address accessed
    is_write: bool  # True → store, False → load
    rec_idx: int    # sequential record index (proxy for time)


# ---------------------------------------------------------------------------
# Binary record parsing
# ---------------------------------------------------------------------------

def _parse_binary_record(buf: bytes, rec_idx: int) -> List[MemAccess]:
    """Unpack *one* binary record and return a list of ``MemAccess``es.

    A single instruction can touch up to 6 addresses (2 dest + 4 src).
    Only non-zero addresses are emitted.

    **Edit this function** if you change ``RECORD_STRUCT``.
    """
    fields = RECORD_STRUCT.unpack(buf)
    ip = fields[0]
    # fields[1] = is_branch, fields[2] = branch_taken
    # fields[3:5] = dest_regs, fields[5:9] = src_regs
    dest_mem: Tuple[int, ...] = fields[9:11]   # 2 destination memory addrs (writes)
    src_mem: Tuple[int, ...] = fields[11:15]    # 4 source memory addrs (reads)

    accesses: List[MemAccess] = []
    for addr in dest_mem:
        if addr != 0:
            accesses.append(MemAccess(ip=ip, addr=addr, is_write=True, rec_idx=rec_idx))
    for addr in src_mem:
        if addr != 0:
            accesses.append(MemAccess(ip=ip, addr=addr, is_write=False, rec_idx=rec_idx))
    return accesses


def iter_binary(stream: BinaryIO, *, limit: int = 0) -> Generator[MemAccess, None, None]:
    """Yield ``MemAccess`` tuples from a binary ChampSim trace stream.

    Parameters
    ----------
    stream : readable binary stream (file, gzip, …)
    limit  : stop after this many *records* (0 = unlimited).
    """
    rec_idx = 0
    while True:
        buf = stream.read(RECORD_SIZE)
        if len(buf) < RECORD_SIZE:
            break
        for acc in _parse_binary_record(buf, rec_idx):
            yield acc
        rec_idx += 1
        if limit and rec_idx >= limit:
            break


# ---------------------------------------------------------------------------
# Text record parsing — liberal multi-format support
# ---------------------------------------------------------------------------

_WRITE_TOKENS = frozenset({"W", "1", "WRITE", "ST", "STORE", "S"})
_READ_TOKENS = frozenset({"R", "0", "READ", "LD", "LOAD", "L"})


def _tok_is_type(tok: str) -> Optional[bool]:
    """Return True (write), False (read), or None if *tok* isn't a type."""
    up = tok.upper()
    if up in _WRITE_TOKENS:
        return True
    if up in _READ_TOKENS:
        return False
    return None


def _tok_is_hex_addr(tok: str) -> Optional[int]:
    """Try to parse *tok* as a hex integer (with or without ``0x`` prefix)."""
    try:
        return int(tok, 16)
    except ValueError:
        return None


def _tok_is_dec_or_hex(tok: str) -> Optional[int]:
    """Parse *tok* as decimal or hex (if it starts with ``0x``)."""
    try:
        if tok.startswith("0x") or tok.startswith("0X"):
            return int(tok, 16)
        return int(tok)
    except ValueError:
        return None


# Each text parser is a callable(fields: List[str]) -> Optional[Tuple[ip, addr, is_write]]
# We try them in order and use the first that returns non-None for every line
# of a small sample.

def _parse_pc_addr_type(fields: List[str]) -> Optional[Tuple[int, int, bool]]:
    """``PC ADDR TYPE`` — e.g. ``0x4005a0 0x7ffc12340 R``"""
    if len(fields) < 3:
        return None
    ip = _tok_is_hex_addr(fields[0])
    addr = _tok_is_hex_addr(fields[1])
    is_w = _tok_is_type(fields[2])
    if ip is not None and addr is not None and is_w is not None:
        return ip, addr, is_w
    return None


def _parse_pc_type_addr(fields: List[str]) -> Optional[Tuple[int, int, bool]]:
    """``PC TYPE ADDR``"""
    if len(fields) < 3:
        return None
    ip = _tok_is_hex_addr(fields[0])
    is_w = _tok_is_type(fields[1])
    addr = _tok_is_hex_addr(fields[2])
    if ip is not None and addr is not None and is_w is not None:
        return ip, addr, is_w
    return None


def _parse_addr_type(fields: List[str]) -> Optional[Tuple[int, int, bool]]:
    """``ADDR TYPE`` (no IP)"""
    if len(fields) < 2:
        return None
    addr = _tok_is_hex_addr(fields[0])
    is_w = _tok_is_type(fields[1])
    if addr is not None and is_w is not None:
        return 0, addr, is_w
    return None


def _parse_type_addr(fields: List[str]) -> Optional[Tuple[int, int, bool]]:
    """``TYPE ADDR`` (no IP)"""
    if len(fields) < 2:
        return None
    is_w = _tok_is_type(fields[0])
    addr = _tok_is_hex_addr(fields[1])
    if addr is not None and is_w is not None:
        return 0, addr, is_w
    return None


# Ordered by preference (most specific first).
_TEXT_PARSERS: List[Callable[[List[str]], Optional[Tuple[int, int, bool]]]] = [
    _parse_pc_addr_type,
    _parse_pc_type_addr,
    _parse_addr_type,
    _parse_type_addr,
]


def _detect_text_parser(
    sample_lines: List[str],
) -> Optional[Callable[[List[str]], Optional[Tuple[int, int, bool]]]]:
    """Pick the best text parser by testing against *sample_lines*."""
    for parser in _TEXT_PARSERS:
        ok = 0
        for line in sample_lines:
            line = line.strip()
            if not line or line.startswith("#"):
                ok += 1
                continue
            fields = line.split()
            if parser(fields) is not None:
                ok += 1
        if ok == len(sample_lines):
            return parser
    return None


def iter_text(
    stream: TextIO,
    *,
    limit: int = 0,
    parser: Optional[Callable] = None,
) -> Generator[MemAccess, None, None]:
    """Yield ``MemAccess`` from a text trace.

    If *parser* is ``None`` we auto-detect from the first 20 non-blank lines.
    """
    # Buffer first lines for detection
    first_lines: List[str] = []
    if parser is None:
        for raw in stream:
            stripped = raw.strip()
            if stripped and not stripped.startswith("#"):
                first_lines.append(stripped)
            if len(first_lines) >= 20:
                break
        parser = _detect_text_parser(first_lines)
        if parser is None:
            print(
                "[WARNING] Could not auto-detect text column layout.  "
                "Try --format champsim or edit trace_reader._TEXT_PARSERS.",
                file=sys.stderr,
            )
            return

    rec_idx = 0
    # Process buffered lines first, then rest of stream.
    def _all_lines():
        yield from first_lines
        for raw in stream:
            yield raw.strip()

    for line in _all_lines():
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        result = parser(fields)
        if result is None:
            continue
        ip, addr, is_w = result
        if addr != 0:
            yield MemAccess(ip=ip, addr=addr, is_write=is_w, rec_idx=rec_idx)
        rec_idx += 1
        if limit and rec_idx >= limit:
            break


# ---------------------------------------------------------------------------
# Auto-detection & top-level entry point
# ---------------------------------------------------------------------------

def _is_likely_text(path: str) -> bool:
    """Heuristic: read up to 512 bytes and check for ASCII text markers."""
    opener: Callable
    if path.endswith(".gz"):
        opener = gzip.open
    else:
        opener = open

    try:
        with opener(path, "rb") as fh:
            head = fh.read(512)
    except Exception:
        return False

    if not head:
        return False

    # If most bytes are printable ASCII, newlines, or tabs → text.
    text_chars = sum(
        1 for b in head if 0x20 <= b <= 0x7E or b in (0x09, 0x0A, 0x0D)
    )
    return text_chars / len(head) > 0.85


def _open_stream(path: str, binary: bool = True):
    """Open *path* as binary or text; transparently handles ``.gz``."""
    if path.endswith(".gz"):
        if binary:
            return gzip.open(path, "rb")
        return io.TextIOWrapper(gzip.open(path, "rb"), encoding="utf-8", errors="replace")
    if binary:
        return open(path, "rb")
    return open(path, "r", encoding="utf-8", errors="replace")


def _validate_binary_sample(path: str, n: int = 200) -> bool:
    """Quick sanity check: parse *n* records, warn if addresses look wrong."""
    stream = _open_stream(path, binary=True)
    try:
        addrs = []
        for acc in iter_binary(stream, limit=n):
            addrs.append(acc.addr)
        if not addrs:
            return False  # no records at all
        # Check: are most addresses suspiciously small or zero?
        zeros = sum(1 for a in addrs if a < 4096)
        if zeros / len(addrs) > 0.5:
            return False
        # Check: is the address range pathologically tiny?
        if max(addrs) - min(addrs) < 4096:
            return False
        return True
    finally:
        stream.close()


def iter_trace(
    path: str,
    *,
    fmt: str = "auto",
    limit: int = 0,
) -> Generator[MemAccess, None, None]:
    """High-level entry: iterate over memory accesses in the trace at *path*.

    Parameters
    ----------
    path  : filesystem path (may be ``.gz`` compressed).
    fmt   : ``"auto"`` | ``"champsim"`` (binary) | ``"champsim_txt"`` (text).
    limit : stop after *limit* records (0 = unlimited).
    """
    if fmt == "auto":
        if _is_likely_text(path):
            fmt = "champsim_txt"
        else:
            # Try binary; validate first
            if not _validate_binary_sample(path):
                print(
                    "[WARNING] Binary parsing produced suspicious addresses.  "
                    "The file may be text or use a different binary struct.\n"
                    "  → Try: --format champsim_txt\n"
                    "  → Or edit RECORD_STRUCT in trace_reader.py",
                    file=sys.stderr,
                )
            fmt = "champsim"

    if fmt == "champsim":
        stream = _open_stream(path, binary=True)
        try:
            yield from iter_binary(stream, limit=limit)
        finally:
            stream.close()
    elif fmt == "champsim_txt":
        stream = _open_stream(path, binary=False)
        try:
            yield from iter_text(stream, limit=limit)
        finally:
            stream.close()
    else:
        raise ValueError(f"Unknown format: {fmt!r}")
