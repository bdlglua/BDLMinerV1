#!/usr/bin/env python3
"""Generates src/hw_sha_fill.S: the fast hardware SHA loop of hw_sha_classic.S
with a software SHA-256d running in the cycles it spends waiting.

The hardware loop is rebuilt instruction by instruction exactly as the
PL_MINE macro expands with the "fast" schedule; every `nop` it waits with is a
slot. Software instructions only replace slots, one for one, so the hardware
timing can only get longer, never shorter, which the loop tolerates (its
delays are minimums). They use registers a9..a15 and SAR only (the loop uses
a2..a8), never touch the SHA peripheral, and never store to memory in the 3
instructions before a store to the peripheral (so the store buffer cannot
bunch peripheral stores together).

The software hash (see src/core/fill.h) moves on 2 rounds per hardware nonce,
a nonce takes 62 + 62 rounds plus 2 transition iterations (64 hardware nonces).
At the top of each iteration, two branches pick a transition copy of the
iteration body instead of the main one:
  A  end of the first hash: second-hash message words from its result
  B  end of the second hash: candidate check, next nonce, first-hash setup

    python3 tools/hw_fill/gen_hw_fill.py src/hw_sha_fill.S
Checked by test/host/test_core.cpp (testHwFill), which runs the software
instructions of the generated file.
"""
import sys

# Fast schedule of hw_sha_classic.S (PL_MINE hwsha_classic_fast ...).
P1, D2, P2, PL1, D3, P3, PL2, LPOS, WGAP = 4, 16, 40, 10, 14, 27, 10, 2, 1

R_START, R_CONT, R_LOAD = 0x90, 0x94, 0x98
# Ctx offsets, see struct Ctx in src/hw_sha.cpp.
CTX = dict(HDR16=64, NONCE=80, BUDGET=84, RUN=88, PAD=92, CHK=96, FILLP=100, Q=104, HITQ=108,
           HITF=112, END1=116, END2=120, B1=124, B2=128, B1_61=132, IV7=136, C18=140, MID=144)
RB = 20  # bytes per record: A, E, W, KW, K
A, E, W, KW, K = 0, 4, 8, 12, 16


# ---------------------------------------------------------------------------
# Hardware loop, in execution order from the top of an iteration (.Lhead,
# just after START of block 1) to the START of the next one.
# ---------------------------------------------------------------------------

SLOT = ("slot",)
JW2B = ("jw2b",)  # `j .Lw2b`: the head part jumps to the w2b part


def hw(text, sha=False):
    return ("hw", text, sha)


def nops(n):
    return [SLOT] * n


def copy(src, dst):
    return [hw(f"l32i    a7, a3, {src}"), hw(f"s32i.n  a7, a2, {dst}", True)] + nops(WGAP)


def zero(dst):
    return [hw(f"s32i.n  a8, a2, {dst}", True)] + nops(1) + nops(WGAP)


def hw_iteration():
    head = nops(5) + [
        hw("beqz    a6, .Lhit"),
        hw("beqz    a5, .Lstop"),
        hw(f"l32i    a7, a3, {CTX['RUN']}"),
        hw("beqz    a7, .Lstop"),
        hw("addi.n  a5, a5, -1"),
        hw("movi.n  a6, 1"),
    ]
    head += copy(CTX["HDR16"], 0) + copy(CTX["HDR16"] + 4, 4) + copy(CTX["HDR16"] + 8, 8)
    head += [hw(f"l32i    a7, a3, {CTX['PAD']}"), hw("s32i.n  a4, a2, 12", True), hw("addi.n  a4, a4, 1")]
    head += nops(WGAP) + [hw("s32i.n  a7, a2, 16", True)] + nops(1) + nops(WGAP)
    for d in range(20, 60, 4):
        head += zero(d)
    head += [hw("movi    a7, 640"), hw("s32i.n  a7, a2, 60", True)] + nops(P1)
    head += [hw(f"s32i    a6, a2, {R_CONT}", True)] + nops(D2)
    head += copy(CTX["PAD"], 32) + [hw("movi    a7, 256"), hw("s32i.n  a7, a2, 60", True)]

    w2b = nops(P2) + [hw(f"s32i    a6, a2, {R_LOAD}", True)] + nops(PL1)
    w2b += [hw(f"s32i    a6, a2, {R_START}", True)] + nops(D3)
    for o in range(32, 64, 4):
        w2b += copy(o, o)
    w2b += nops(P3) + [hw(f"s32i    a6, a2, {R_LOAD}", True)] + nops(PL2)
    for k in range(8):
        w2b += copy(4 * k, 4 * k)
        if LPOS == k + 1:
            w2b += [hw("l16ui   a6, a2, 28")]
    w2b += [hw("movi.n  a7, 1"), hw(f"s32i    a7, a2, {R_START}", True)]
    return head + [JW2B] + w2b


# ---------------------------------------------------------------------------
# Software instructions. P = a9 points at record i-3 during round i.
# ---------------------------------------------------------------------------

P = "a9"
T = ["a10", "a11", "a12", "a13", "a14", "a15"]


def ins(text, store=False):
    return ("f", text, store)


def l32i(r, base, off):
    return ins(f"l32i    {r}, {base}, {off}")


def s32i(r, base, off):
    return ins(f"s32i    {r}, {base}, {off}", True)


def rec(i, field):
    """Offset of a field of record i+k relative to P (record i-3): rec(k)."""
    return (i + 3) * RB + field


def step():
    """W[i+16] expansion and round i, then P to the next record."""
    t0, t1, t2, t3, t4, t5 = T
    out = [
        # W[i+16] = s1(W[i+14]) + W[i+9] + s0(W[i+1]) + W[i], KW[i+16] = W + K
        l32i(t0, P, rec(14, W)),
        l32i(t2, P, rec(1, W)),
        ins("ssai    17"), ins(f"src     {t1}, {t0}, {t0}"),
        ins("ssai    19"), ins(f"src     {t3}, {t0}, {t0}"), ins(f"xor     {t1}, {t1}, {t3}"),
        ins(f"srli    {t3}, {t0}, 10"), ins(f"xor     {t1}, {t1}, {t3}"),
        ins("ssai    7"), ins(f"src     {t3}, {t2}, {t2}"),
        ins("ssai    18"), ins(f"src     {t4}, {t2}, {t2}"), ins(f"xor     {t3}, {t3}, {t4}"),
        ins(f"srli    {t4}, {t2}, 3"), ins(f"xor     {t3}, {t3}, {t4}"),
        l32i(t4, P, rec(9, W)),
        ins(f"add     {t1}, {t1}, {t3}"),
        l32i(t5, P, rec(0, W)),
        ins(f"add     {t1}, {t1}, {t4}"),
        l32i(t3, P, rec(16, K)),
        ins(f"add     {t1}, {t1}, {t5}"),
        s32i(t1, P, rec(16, W)),
        ins(f"add     {t1}, {t1}, {t3}"),
        s32i(t1, P, rec(16, KW)),
        # T1 = h + S1(e) + ch(e, f, g) + KW[i];  e' = d + T1
        l32i(t0, P, rec(0, E)),
        l32i(t2, P, rec(-1, E)),
        l32i(t3, P, rec(-2, E)),
        ins("ssai    6"), ins(f"src     {t1}, {t0}, {t0}"),
        ins("ssai    11"), ins(f"src     {t4}, {t0}, {t0}"), ins(f"xor     {t1}, {t1}, {t4}"),
        ins("ssai    25"), ins(f"src     {t4}, {t0}, {t0}"), ins(f"xor     {t1}, {t1}, {t4}"),
        ins(f"xor     {t4}, {t2}, {t3}"), ins(f"and     {t4}, {t4}, {t0}"), ins(f"xor     {t4}, {t4}, {t3}"),
        l32i(t5, P, rec(-3, E)),
        ins(f"add     {t1}, {t1}, {t4}"),
        l32i(t4, P, rec(0, KW)),
        ins(f"add     {t1}, {t1}, {t5}"),
        l32i(t5, P, rec(-3, A)),
        ins(f"add     {t1}, {t1}, {t4}"),
        l32i(t0, P, rec(0, A)),
        ins(f"add     {t5}, {t5}, {t1}"),
        s32i(t5, P, rec(1, E)),
        # a' = T1 + S0(a) + maj(a, b, c)
        l32i(t2, P, rec(-1, A)),
        ins("ssai    2"), ins(f"src     {t3}, {t0}, {t0}"),
        ins("ssai    13"), ins(f"src     {t4}, {t0}, {t0}"), ins(f"xor     {t3}, {t3}, {t4}"),
        ins("ssai    22"), ins(f"src     {t4}, {t0}, {t0}"), ins(f"xor     {t3}, {t3}, {t4}"),
        l32i(t4, P, rec(-2, A)),
        ins(f"or      {t5}, {t0}, {t2}"),
        ins(f"and     {t0}, {t0}, {t2}"),
        ins(f"and     {t5}, {t5}, {t4}"),
        ins(f"or      {t5}, {t5}, {t0}"),
        ins(f"add     {t3}, {t3}, {t5}"),
        ins(f"add     {t3}, {t3}, {t1}"),
        s32i(t3, P, rec(1, A)),
        ins(f"addi    {P}, {P}, {RB}"),
    ]
    return out


def dispatch():
    t0, t1 = T[0], T[1]
    return [
        l32i(t0, "a3", CTX["END1"]),
        l32i(t1, "a3", CTX["END2"]),
        ins(f"sub     {t0}, {P}, {t0}"),
        ins(f"sub     {t1}, {P}, {t1}"),
        ins(f"beqz    {t0}, .LTA"),
        ins(f"beqz    {t1}, .LTB"),
    ]


def transition_a():
    """Second-hash words 0..7 = mid + state after the first hash; P = second hash."""
    t0, t1, t2, t3, t4, t5 = T
    out = [l32i(t0, "a3", CTX["B1_61"]), l32i(t1, "a3", CTX["B2"])]
    # state after round 63: a..d = A[64..61], e..h = E[64..61]
    src = [(64, A), (63, A), (62, A), (61, A), (64, E), (63, E), (62, E), (61, E)]
    for k in range(0, 8, 2):
        (r0, f0), (r1, f1) = src[k], src[k + 1]
        w0, w1 = (k + 3) * RB, (k + 4) * RB
        out += [
            l32i(t2, t0, (r0 - 61) * RB + f0),
            l32i(t3, "a3", CTX["MID"] + 4 * k),
            l32i(t4, t0, (r1 - 61) * RB + f1),
            l32i(t5, "a3", CTX["MID"] + 4 * k + 4),
            ins(f"add     {t2}, {t2}, {t3}"),
            s32i(t2, t1, w0 + W),
            l32i(t3, t1, w0 + K),
            ins(f"add     {t4}, {t4}, {t5}"),
            s32i(t4, t1, w1 + W),
            l32i(t5, t1, w1 + K),
            ins(f"add     {t2}, {t2}, {t3}"),
            s32i(t2, t1, w0 + KW),
            ins(f"add     {t4}, {t4}, {t5}"),
            s32i(t4, t1, w1 + KW),
        ]
    out.append(ins(f"mov     {P}, {t1}"))
    return out


def transition_b():
    """Candidate check of the second hash, then the next nonce's first hash."""
    t0, t1, t2, t3, t4, t5 = T
    out = [
        # e after round 60 (record 61) + IV[7]: low 16 bits zero = candidate
        l32i(t0, P, 2 * RB + E),  # P is record 59 of the second hash
        l32i(t1, "a3", CTX["IV7"]),
        l32i(t3, "a3", CTX["Q"]),
        ins(f"add     {t0}, {t0}, {t1}"),
        ins(f"extui   {t0}, {t0}, 0, 16"),
        ("group",),
        ins(f"bnez    {t0}, .Lnohit"),
        s32i(t3, "a3", CTX["HITQ"]),
        ins(f"movi.n  {t2}, 1"),
        s32i(t2, "a3", CTX["HITF"]),
        ins(".Lnohit:"),
        ("endgroup",),
        # next nonce: W[3] = q, KW[3], W[18] = c18 + s0(q), KW[18]
        ins(f"addi    {t3}, {t3}, 1"),
        l32i(t4, "a3", CTX["B1"]),
        s32i(t3, "a3", CTX["Q"]),
        ins("ssai    7"), ins(f"src     {t0}, {t3}, {t3}"),
        ins("ssai    18"), ins(f"src     {t1}, {t3}, {t3}"), ins(f"xor     {t0}, {t0}, {t1}"),
        ins(f"srli    {t1}, {t3}, 3"), ins(f"xor     {t0}, {t0}, {t1}"),
        l32i(t1, "a3", CTX["C18"]),
        l32i(t5, t4, 3 * RB + K),
        s32i(t3, t4, 3 * RB + W),
        ins(f"add     {t0}, {t0}, {t1}"),
        ins(f"add     {t5}, {t5}, {t3}"),
        s32i(t0, t4, 18 * RB + W),
        l32i(t1, t4, 18 * RB + K),
        s32i(t5, t4, 3 * RB + KW),
        ins(f"add     {t0}, {t0}, {t1}"),
        s32i(t0, t4, 18 * RB + KW),
        ins(f"mov     {P}, {t4}"),
    ]
    return out


# ---------------------------------------------------------------------------
# Placement
# ---------------------------------------------------------------------------


def store_ok(body, pos):
    """No store to the peripheral in the 3 instructions after `pos` (the
    iteration ends with one, and the next starts with slots)."""
    for j in range(pos + 1, min(pos + 4, len(body))):
        if body[j][0] == "hw" and body[j][2]:
            return False
    return True


def place(body, start, filler, tag):
    """Puts `filler` into the slots of body[start:], in order. Returns
    {pos: [(line, tag)]} and the positions of the .LTA/.LTB branches."""
    out = {}
    marks = []
    n = 0
    pos = start

    def put(at, line):
        nonlocal n
        out.setdefault(at, []).append((line, f"F{tag}{n:03d}"))
        n += 1

    fi = 0
    while fi < len(filler):
        it = filler[fi]
        if it == ("group",):
            # A conditional skip: all of it in one run of slots, so that the
            # branch only ever skips software instructions.
            end = filler.index(("endgroup",), fi)
            group = [g for g in filler[fi + 1:end] if not g[1].endswith(":")]
            label = [g for g in filler[fi + 1:end] if g[1].endswith(":")]
            while True:
                if pos >= len(body):
                    raise SystemExit(f"{tag}: no room for a conditional group")
                run = 0
                while pos + run < len(body) and body[pos + run] == SLOT:
                    run += 1
                if run >= len(group) and all(not g[2] or store_ok(body, pos + j) for j, g in enumerate(group)):
                    break
                pos += max(run, 1)
            for g in group:
                put(pos, g[1])
                pos += 1
            for g in label:
                put(pos - 1, g[1])  # after the last skipped instruction
            fi = end + 1
            continue
        while pos < len(body) and (body[pos] != SLOT or (it[2] and not store_ok(body, pos))):
            pos += 1
        if pos >= len(body):
            raise SystemExit(f"{tag}: {len(filler) - fi} instructions do not fit")
        put(pos, it[1])
        if ".LT" in it[1]:
            marks.append(pos)
        pos += 1
        fi += 1
    return out, marks


def operands(line):
    parts = line.split(None, 1)
    return parts[0], [o.strip() for o in parts[1].split(",")] if len(parts) > 1 else []


def load_use_stalls(body, placed):
    """Software instructions reading a register that the instruction executed
    just before loaded (one stall cycle each, which lengthens a delay)."""
    seq = []
    for pos, item in enumerate(body):
        if pos in placed:
            seq += [line for line, _ in placed[pos] if not line.endswith(":")]
        elif item[0] == "hw":
            seq.append(item[1])
        else:
            seq.append("nop.n")
    stalls = 0
    for prev, cur in zip(seq, seq[1:]):
        op, args = operands(prev)
        if op != "l32i" or args[0] not in [P] + T:
            continue
        cop, cargs = operands(cur)
        reads = cargs if cop in ("s32i", "beqz", "bnez") else cargs[1:]
        if args[0] in reads:
            stalls += 1
    return stalls


def emit_part(lines, body, placed, lo, hi, label_jw2b):
    for pos in range(lo, hi):
        item = body[pos]
        if pos in placed:
            for line, tag in placed[pos]:
                if line.endswith(":"):
                    lines.append(f"{line:<38}// {tag}")
                else:
                    lines.append(f"    {line:<34}// {tag}")
        elif item[0] == "hw":
            lines.append(f"    {item[1]}")
        elif item == JW2B:
            lines.append(f"    j       {label_jw2b}")
        else:
            lines.append("    nop.n")


def build():
    body = hw_iteration()
    jpos = body.index(JW2B)
    nslots = sum(1 for x in body if x == SLOT)

    main, marks = place(body, 0, dispatch() + step() + step(), "M")
    ta_at, tb_at = marks[0] + 1, marks[1] + 1
    ta, _ = place(body, ta_at, transition_a(), "A")
    tb, _ = place(body, tb_at, transition_b(), "B")

    used = sum(len([l for l, _ in v if not l.endswith(":")]) for v in main.values())
    stalls = [load_use_stalls(body, p) for p in (main, ta, tb)]

    def labels(name, text):
        return text.replace(".Lhit", ".Lhit_f").replace(".Lstop", ".Lstop_f").replace(".Lnohit", f".Lnohit_{name}")

    def part(lines, placed, lo, hi, name, w2b_label):
        sub = []
        emit_part(sub, body, placed, lo, hi, w2b_label)
        lines += [labels(name, l) for l in sub]

    L = []
    L.append(f"// GENERATED by tools/hw_fill/gen_hw_fill.py -- do not edit by hand.")
    L.append(f"// Slots per iteration: {nslots}, software instructions in the main body: {used}.")
    L.append(f"// Load-use stall cycles (main, A, B): {stalls[0]}, {stalls[1]}, {stalls[2]}.")
    L.append("//")
    L.append("// uint32_t hwsha_classic_fill(volatile uint32_t *sha_text_base, Ctx *ctx)")
    L.append("// Same contract as the loops of hw_sha_classic.S (1 = hardware candidate at")
    L.append("// ctx->nonce - 1, 0 = budget spent or ctx->run cleared), plus the software")
    L.append("// hash state in ctx (fillP, q, and hitQ/hitF for a software candidate).")
    L.append('#include "sdkconfig.h"')
    L.append("#if defined(CONFIG_IDF_TARGET_ESP32)")
    L.append("")
    L.append("    .section .iram1.hwsha_fill, \"ax\"")
    L.append("    .global hwsha_classic_fill")
    L.append("    .type   hwsha_classic_fill, @function")
    L.append("    .balign 4")
    L.append("hwsha_classic_fill:")
    L.append("    entry   a1, 32")
    L.append(f"    l32i    a4, a3, {CTX['NONCE']}")
    L.append(f"    l32i    a5, a3, {CTX['BUDGET']}")
    L.append("    movi.n  a8, 0")
    L.append(f"    l32i    a9, a3, {CTX['FILLP']}")
    L += ["    movi.n  a7, 28", "1:  addi.n  a7, a7, -1", "    bnez    a7, 1b"]
    for o in range(0, 64, 4):
        L += [f"    l32i    a7, a3, {o}", f"    s32i.n  a7, a2, {o}"] + ["    nop.n"] * WGAP
    L += ["    movi.n  a6, 1", f"    s32i    a6, a2, {R_START}", "    j       .Lhead_m", ""]
    # exit
    L += [".Lhit_f:", "    movi.n  a6, 1", "    j       .Lout_f", ".Lstop_f:", "    movi.n  a6, 0", ".Lout_f:"]
    L += ["    movi.n  a7, 28", "1:  addi.n  a7, a7, -1", "    bnez    a7, 1b"]
    L += [f"    s32i    a4, a3, {CTX['NONCE']}", f"    s32i    a5, a3, {CTX['BUDGET']}",
          f"    s32i    a9, a3, {CTX['FILLP']}", "    mov.n   a2, a6", "    retw.n", ""]
    # transition A: from its branch point to the end of the iteration
    L.append("    .balign 4")
    L.append(".LTA:")
    part(L, ta, ta_at, jpos + 1, "A", ".Lw2b_a")
    L.append("    .balign 4")
    L.append(".Lw2b_a:")
    part(L, ta, jpos + 1, len(body), "A", None)
    L += ["    j       .Lhead_m", ""]
    # main body: w2b part, then the top of the iteration
    L.append("    .balign 4")
    L.append(".Lw2b_m:")
    part(L, main, jpos + 1, len(body), "M", None)
    L.append(".Lhead_m:")
    part(L, main, 0, jpos, "M", ".Lw2b_m")
    L.append("    j       .Lw2b_m")
    L.append("")
    # transition B
    L.append("    .balign 4")
    L.append(".LTB:")
    part(L, tb, tb_at, jpos + 1, "B", ".Lw2b_b")
    L.append("    .balign 4")
    L.append(".Lw2b_b:")
    part(L, tb, jpos + 1, len(body), "B", None)
    L += ["    j       .Lhead_m", ""]
    L.append("    .size   hwsha_classic_fill, . - hwsha_classic_fill")
    L.append("")
    L.append("#endif")
    L.append("")
    return "\n".join(L), nslots, used, stalls


if __name__ == "__main__":
    text, nslots, used, stalls = build()
    open(sys.argv[1], "w").write(text)
    print(f"slots per iteration {nslots}, used by the main body {used}, load-use stalls {stalls}")
