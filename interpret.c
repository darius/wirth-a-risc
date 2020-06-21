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

typedef struct M M;
struct M {
    u32 r[16]; // registers
    u32 pc;    // program counter
    u8 *mem;   // memory; must be of size `cap`
    u32 cap;   // in bytes; must be a whole # of words
    u8  flags; // just 4 bits, TODO
};

static u8 fetch8(M *m, u32 addr) {
    // XXX bounds check
    // XXX fetch it
    return 0; 
}

static void store8(M *m, u32 addr, u8 value) {
    // XXX bounds check
    // XXX store it
}

static u32 fetch32(M *m, u32 addr) {
    // XXX bounds check
    // XXX fetch it
    return 0; 
}

static void store32(M *m, u32 addr, u32 value) {
    // XXX
}

static u32 field(u32 value, u32 offset, u32 width) {
    return (value >> offset) & ((1u << width) - 1);
}

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

static i32 signed_shift_right(i32 v, u32 n) {
    return v >> n; // XXX how in portable C?
}

static u32 rotate_right(u32 v, u32 n) {
    n = n & 31;
    return (v >> n) | (v << (32-n));  // TODO correct?
}

static u32 add(u32 v, u32 n) {
    return v + n;  // TODO portable with overflow?
}

#define CASE    break; case
#define DEFAULT break; default
static void register_ins(M *m, u32 a, u32 b, u32 op, u32 n) {
    assert(a < 16 && b < 16);
    // XXX set m->flags too
    // TODO handle u=1, from "section 4 Special features"
    switch (op) {
    CASE MOV: m->r[a] = n;
    CASE LSL: m->r[a] = m->r[b] << n;
    CASE ASR: m->r[a] = signed_shift_right(m->r[b], n);
    CASE ROR: m->r[a] = rotate_right(m->r[b], n);
    CASE AND: m->r[a] = m->r[b] & n;
    CASE ANN: m->r[a] = m->r[b] & ~n;
    CASE IOR: m->r[a] = m->r[b] | n;
    CASE XOR: m->r[a] = m->r[b] ^ n;
    CASE ADD: m->r[a] = add(m->r[b], n);
    CASE SUB: m->r[a] = add(m->r[b], -n);  // TODO portable?
    CASE MUL: m->r[a] = m->r[b] * n; // TODO portable? should it be signed?
    CASE DIV: m->r[a] = m->r[b] / n; // TODO portable rounding; what on zero divide? should it be signed?
    DEFAULT:  panic("Unknown register instruction");
    }
}

enum {
    LDW = 8,
    LDB,
    STW,
    STB,
};

// TODO optional tracing
static void step(M *m) {
    u32 ir = fetch32(m, m->pc);
    switch (ir >> 28) {
    CASE 0: case 2:  // TODO well this CASEcase thing is ugly
        if (field(ir, 4, 12) != 0) panic("Crap in instruction");
        register_ins(m, field(ir, 24, 4), field(ir, 20, 4), field(ir, 16, 4),
                     m->r[field(ir, 0, 4)]);
    CASE 4: case 5: case 6: case 7:
        register_ins(m, field(ir, 24, 4), field(ir, 20, 4), field(ir, 16, 4),
                     field(ir, 0, 16));
    CASE LDW:
        m->r[field(ir,24,4)] = fetch32(m, add(field(ir,20,4), field(ir,0,20)));
    CASE LDB:
        m->r[field(ir,24,4)] = fetch8(m, add(field(ir,20,4), field(ir,0,20)));
    CASE STW:
        store32(m, add(field(ir,20,4), field(ir,0,20)),
                m->r[field(ir,24,4)]);
    CASE STB:
        store8(m, add(field(ir,20,4), field(ir,0,20)),
               0xFF & m->r[field(ir,24,4)]);
    // TODO "section 3 Branch instructions"
    DEFAULT:
        panic("Unknown instruction");
    }
}

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
