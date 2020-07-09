#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static void panic(const char *plaint) {
    fprintf(stderr, "%s\n", plaint);
    exit(1);
}

static void *allot(size_t n) {
    void *result = malloc(n);
    if (!result && 0 < n) panic("Out of memory");
    return result;
}

typedef uint8_t  u8;
typedef uint32_t u32;
typedef int32_t  i32;
typedef uint64_t u64;

static u32 field(u32 value, u32 offset, u32 width) {
    return (value >> offset) & ((1u << width) - 1);
}

// Extend a 16-bit value to a 32-bit one.
static u32 sign_extend(u32 sign, u32 value) {
    assert(sign == (sign & 1u));
    assert((value >> 16u) == 0);
    return sign ? 0xFFFF0000 | value : value; // TODO branchlessly
}

typedef struct M M;
struct M {
    u32 r[16]; // registers
    u32 pc;    // program counter
    u8 *mem;   // memory; must be of size `cap`
    u32 cap;   // in bytes; must be a whole # of words
    u32 rh;    // special H register for the remainder from division
    u8  flags; // just 4 bits: see the enum below
};

// Offsets of the flag bits within the flags field.
// N: negative; Z: zero; C: carry; V: overflow.
// TODO what is Wirth's endianness for these bits? Is this backwards?
enum { FN, FZ, FC, FV, };

// Opcodes for register instructions.
enum {
    MOV,
    LSL,
    ASR,
    ROR,
    AND,
    ANN,
    IOR,
    XOR,
    ADD,
    SUB,
    MUL,
    DIV,
    // N.B. floating-point instructions omitted
};

// Opcodes for load/store instructions.
enum {
    LDW = 8,
    LDB,
    STW,
    STB,
};

static u8 fetch8(M *m, u32 addr) {
    if (m->cap <= addr) panic("Out of bounds");
    return m->mem[addr];
}

static void store8(M *m, u32 addr, u8 value) {
    if (m->cap <= addr) panic("Out of bounds");
    m->mem[addr] = value;
}

// TODO little-endian? or big?
static u32 fetch32(M *m, u32 addr) {
    // TODO tune this for word access, not for byte access
    // TODO unaligned is unallowed, right?
    if ((addr & 3) != 0) panic("Unaligned fetch");
    u8 v0 = fetch8(m, addr);   // TODO I pick little-endian
    u8 v1 = fetch8(m, addr+1);
    u8 v2 = fetch8(m, addr+2);
    u8 v3 = fetch8(m, addr+3);
    return v0 | (v1<<8) | (v2<<16) | (v3<<24);
}

static void store32(M *m, u32 addr, u32 value) {
    if ((addr & 3) != 0) panic("Unaligned store");
    store8(m, addr,   field(value, 0,8));
    store8(m, addr+1, field(value, 8,8));
    store8(m, addr+2, field(value,16,8));
    store8(m, addr+3, field(value,24,8));
}

// Extend a 24-bit value to a 32-bit one.
static u32 sign_extend24(u32 value) {
    assert((value >> 8u) == 0);
    enum { sign_bit = 1u << 23 };
    return (value ^ sign_bit) - sign_bit;
}

static u32 sign_extend20(u32 value) {
    assert((value >> 12u) == 0);
    enum { sign_bit = 1u << 19 };
    return (value ^ sign_bit) - sign_bit;
}

static i32 shift_left(u32 v, u32 n) {
    n = n & 31u; // p. 10 of "Design of a RISC Arch. & its Impl. with an FPGA"
    return v << n;
}

static i32 signed_shift_right(i32 v, u32 n) {
    n = n & 31u; // p. 10 of "Design of a RISC Arch. & its Impl. with an FPGA"
    return v >> n; // XXX how in portable C?
}

static u32 rotate_right(u32 v, u32 n) {
    n = n & 31u;
    return (v >> n) | (v << (32u-n));  // TODO correct?
}

static u32 add(u32 v, u32 n) {
    return v + n;  // TODO portable with overflow?
}

#define CASE    break; case
#define DEFAULT break; default

static void register_ins(M *m, u32 f01, u32 u, u32 v, u32 a, u32 b, u32 op, u32 n) {
    assert(a < 16 && b < 16);
    u32 va, nflag, zflag, cflag=0, vflag=0; // result value and flags
    switch (op) {
        CASE MOV: {
            // TODO this is the only place using the f01 and v arguments
            //  so it seems especially inefficient to always pass them.
            if      (!u)  va = n;
            else if (f01) va = n << 16u;
            else if (v)   va = m->flags;
            else          va = m->rh;
        }
        CASE LSL: va = shift_left(m->r[b], n);
        CASE ASR: va = signed_shift_right(m->r[b], n);
        CASE ROR: va = rotate_right(m->r[b], n);
        CASE AND: va = m->r[b] & n;
        CASE ANN: va = m->r[b] & ~n;
        CASE IOR: va = m->r[b] | n;
        CASE XOR: va = m->r[b] ^ n;
        CASE ADD: {
            u32 carry_in = u ? field(m->flags,FC,1) : 0;
            u64 sum = (u64)m->r[b] + (u64)n + (u64)carry_in;
            va = (u32)sum;
            cflag = 1u & (sum >> 32u); // TODO portable?
            vflag = (sum != ((u64)va + ((u64)cflag << 32u)));; // XXX how exactly is this defined? is this it?
        }
        CASE SUB: {
            u32 carry_in = u ? field(m->flags,FC,1) : 0;
            u64 sum = (u64)m->r[b] - (u64)n + (u64)carry_in; // TODO right? i64?
            va = (u32)sum;
            cflag = 1u & (sum >> 32u); // TODO portable?
            vflag = (sum != ((u64)va + ((u64)cflag << 32u)));; // XXX how exactly is this defined? is this it?
        }
        CASE MUL: {
            if (u) va = m->r[b] * n; // TODO portable?
            else   va = (u32) ((i32)m->r[b] * (i32)n); // TODO portable?
        }
        CASE DIV: {
            va = m->r[b] / n; // TODO portable rounding; what on zero divide? should it be signed?
            m->rh = m->r[b] % n; // XXX check Wirth's definition for this
        }
        DEFAULT:
            panic("Unknown register instruction");
            va = 0; // (just for the C compiler to think va is always set)
    }
    nflag = (0 != (va & (1u << 31u)));
    zflag = (0 != va);
    m->flags = (nflag << FN) | (zflag << FZ) | (cflag << FC) | (vflag << FV);
    m->r[a] = va;
}

static u32 bit(u32 value, u32 offset) {
    return field(value, offset, 1);
}

static void branch_ins(M *m, u32 u, u32 v, u32 cond, u32 off_or_dest) {
    assert(u || off_or_dest < 16);
    int taken;
    switch (cond) {
        CASE 000: taken =  bit(m->flags,FN);
        CASE 010: taken = !bit(m->flags,FN);

        CASE 001: taken =  bit(m->flags,FZ);
        CASE 011: taken = !bit(m->flags,FZ);

        CASE 002: taken =  bit(m->flags,FC);
        CASE 012: taken = !bit(m->flags,FC);

        CASE 003: taken =  bit(m->flags,FV);
        CASE 013: taken = !bit(m->flags,FV);
        
        CASE 004: taken =   !bit(m->flags,FC) || bit(m->flags,FZ);
        CASE 014: taken = !(!bit(m->flags,FC) || bit(m->flags,FZ));
        
        CASE 005: taken =    bit(m->flags,FN) ^ bit(m->flags,FV);
        CASE 015: taken = !( bit(m->flags,FN) ^ bit(m->flags,FV));
        
        CASE 006: taken =   ((bit(m->flags,FN) ^ bit(m->flags,FV))
                             | bit(m->flags,FZ));
        CASE 016: taken = !(((bit(m->flags,FN) ^ bit(m->flags,FV))
                            | bit(m->flags,FZ)));

        CASE 007: taken =  1;
        CASE 017: taken = !1;
        
        DEFAULT: assert(0);
    }
    if (taken) {
        // N.B. Wirth says pc+1 instead of pc for each of the next two lines,
        //  but we already added the 1 in step().
        if (v) m->r[15] = m->pc << 2;
        m->pc = (u ? m->pc + off_or_dest // TODO portable overflow?
                   : m->r[off_or_dest] >> 2);
    }
}

#define F01  field(ir,30, 1)
#define U    field(ir,29, 1)
#define V    field(ir,28, 1)
#define COND field(ir,24, 4)
#define A    field(ir,24, 4)
#define B    field(ir,20, 4)
#define OP   field(ir,16, 4)
#define C    field(ir, 0, 4)
#define IM   field(ir, 0,16)
#define OFF20  sign_extend20(field(ir, 0,20)) /* TODO sure it's signed? */
#define SOFF24 sign_extend24(field(ir, 0,24))

// TODO optional tracing
static void step(M *m) {
    u32 ir = fetch32(m, (m->pc++) << 2); // TODO portable overflow back to 0?
    // We get away with incrementing m->pc here because only
    // branch_ins() uses m->pc, and that code is written to expect
    // m->pc has already been incremented.
    switch (ir >> 28) {
        CASE 0: case 2: // TODO well this CASEcase thing is ugly
            if (field(ir,4,12) != 0) panic("Crap in register instruction");
            register_ins(m, F01, U, V, A, B, OP, m->r[C]);
        CASE 4: case 5: case 6: case 7:
            register_ins(m, F01, U, V, A, B, OP, sign_extend(V, IM));
        CASE LDW:
            m->r[A] = fetch32(m, add(m->r[B], OFF20));
        CASE LDB:
            m->r[A] = fetch8(m, add(m->r[B], OFF20));
        CASE STW:
            store32(m, add(m->r[B], OFF20), m->r[A]);
        CASE STB:
            store8(m, add(m->r[B], OFF20), m->r[A] & 0xFF);
        CASE 0xC: case 0xD:
            if (field(ir,4,20) != 0) panic("Crap in branch instruction");
            branch_ins(m, U, V, COND, m->r[C]);
        CASE 0xE: case 0xF:
            branch_ins(m, U, V, COND, SOFF24);
        DEFAULT:
            panic("Unknown instruction");
    }
}

#undef F01
#undef U
#undef V
#undef COND
#undef A
#undef B
#undef OP
#undef C
#undef IM
#undef OFF20
#undef SOFF24

static void run(M *m) {
    for (int i = 0; i < 1; ++i) {  // TODO timeslices or something; and a way to HALT
        step(m);
    }
}

static void dump(M *m) {
    for (int i = 0; i < 16; ++i) {
        printf("%2d: %4x\n", i, m->r[i]);
    }
}

static void install_example(M *m) {
    m->pc = 0;
    // TODO install an example program
}

enum { mem_cap = 64*1024 };

int main(int argc, char **argv) {
    M m = {.mem = allot(mem_cap), .cap = mem_cap};
    install_example(&m);
    run(&m);
    dump(&m);
    return 0;
}
