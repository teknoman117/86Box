#ifndef EMU_X86_MUL_TIMING_H
#define EMU_X86_MUL_TIMING_H

/* The 386's MUL and IMUL use an early-out multiply: the time depends on the
   position of the most significant bit of the optimizing multiplier m (the
   r/m operand, or the immediate in the immediate forms), positive or
   negative: max(ceiling(log2(|m|)), 3) + 6 clocks, or 9 clocks if m is 0,
   plus 3 clocks for a memory operand. Other CPUs keep the fixed timing the
   caller passes. */

/* |v| for a sign-extended IMUL multiplier. */
static __inline uint32_t
mul_magnitude(int32_t v)
{
    return (v < 0) ? (0U - (uint32_t) v) : (uint32_t) v;
}

static __inline int
mul_cycles(uint32_t m, int fixed)
{
    uint32_t v;
    int      bits = 0;

    if ((cpu_s->cpu_type != CPU_386SX) && (cpu_s->cpu_type != CPU_386DX))
        return fixed;

    if (m == 0)
        return (cpu_mod == 3) ? 9 : 12;

    /* ceiling(log2(m)) is the bit length of m - 1. */
    v = m - 1;
    if (v >= 0x10000) {
        bits += 16;
        v >>= 16;
    }
    if (v >= 0x100) {
        bits += 8;
        v >>= 8;
    }
    if (v >= 0x10) {
        bits += 4;
        v >>= 4;
    }
    if (v >= 0x4) {
        bits += 2;
        v >>= 2;
    }
    if (v >= 0x2) {
        bits += 1;
        v >>= 1;
    }
    bits += v;

    return ((bits < 3) ? 3 : bits) + 6 + ((cpu_mod == 3) ? 0 : 3);
}

#endif /* EMU_X86_MUL_TIMING_H */
