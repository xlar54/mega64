#include <stdio.h>
#include <stdint.h>
#include "emu.h"

//externally supplied functions
extern uint8_t read6502(uint16_t address);
extern void write6502(uint16_t address, uint8_t value);
extern uint8_t irq_triggered;  // Flag to avoid multiple IRQs

#define FLAG_CARRY     0x01
#define FLAG_ZERO      0x02
#define FLAG_INTERRUPT 0x04
#define FLAG_DECIMAL   0x08
#define FLAG_BREAK     0x10
#define FLAG_CONSTANT  0x20
#define FLAG_OVERFLOW  0x40
#define FLAG_SIGN      0x80

#define BASE_STACK     0x100

#define OP(code,label) [code] = &&label

//6502 CPU registers
uint16_t pc;
uint16_t oldpc;
uint8_t sp, a, x, y, status = FLAG_CONSTANT;

void dump_regs2(void) {
    fputs("PC:", stdout); print_hex16(pc);  fputc(' ', stdout);
    fputs("SP:", stdout); print_hex8(sp);    fputc(' ', stdout);
    fputs("A:",  stdout); print_hex8(a);     fputc(' ', stdout);
    fputs("X:",  stdout); print_hex8(x);     fputc(' ', stdout);
    fputs("Y:",  stdout); print_hex8(y);     fputc(' ', stdout);
    fputs("P:",  stdout);
    putchar((status & FLAG_SIGN)      ? 'N' : 'n');
    putchar((status & FLAG_OVERFLOW)  ? 'V' : 'v');
    putchar('-');  /* unused bit */
    putchar((status & FLAG_BREAK)     ? 'B' : 'b');
    putchar((status & FLAG_DECIMAL)   ? 'D' : 'd');
    putchar((status & FLAG_INTERRUPT) ? 'I' : 'i');
    putchar((status & FLAG_ZERO)      ? 'Z' : 'z');
    putchar((status & FLAG_CARRY)     ? 'C' : 'c');
    putchar('\r');
}

#define NEXT()  do { \
    dump_regs2(); getchar();                              \
    opcode = read6502(pc++);                  \
    goto *optab[opcode];                      \
} while (0)



//flag modifier macros
#define setcarry() status |= FLAG_CARRY
#define clearcarry() status &= (~FLAG_CARRY)
#define setzero() status |= FLAG_ZERO
#define clearzero() status &= (~FLAG_ZERO)
#define setinterrupt() status |= FLAG_INTERRUPT
#define clearinterrupt() status &= (~FLAG_INTERRUPT)
#define setdecimal() status |= FLAG_DECIMAL
#define cleardecimal() status &= (~FLAG_DECIMAL)
#define setoverflow() status |= FLAG_OVERFLOW
#define clearoverflow() status &= (~FLAG_OVERFLOW)
#define setsign() status |= FLAG_SIGN
#define clearsign() status &= (~FLAG_SIGN)


//flag calculation macros
#define zerocalc(n) {\
    if ((n) & 0x00FF) clearzero();\
        else setzero();\
}

#define signcalc(n) {\
    if ((n) & 0x0080) setsign();\
        else clearsign();\
}

#define carrycalc(n) {\
    if ((n) & 0xFF00) setcarry();\
        else clearcarry();\
}

#define overflowcalc(n, m, o) { /* n = result, m = accumulator, o = memory */ \
    if (((n) ^ (uint16_t)(m)) & ((n) ^ (o)) & 0x0080) setoverflow();\
        else clearoverflow();\
}

static const uint32_t ticktable[256] = {
    /*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |     */
    /* 0 */      7,    6,    2,    8,    3,    3,    5,    5,    3,    2,    2,    2,    4,    4,    6,    6,  /* 0 */
    /* 1 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 1 */
    /* 2 */      6,    6,    2,    8,    3,    3,    5,    5,    4,    2,    2,    2,    4,    4,    6,    6,  /* 2 */
    /* 3 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 3 */
    /* 4 */      6,    6,    2,    8,    3,    3,    5,    5,    3,    2,    2,    2,    3,    4,    6,    6,  /* 4 */
    /* 5 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 5 */
    /* 6 */      6,    6,    2,    8,    3,    3,    5,    5,    4,    2,    2,    2,    5,    4,    6,    6,  /* 6 */
    /* 7 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 7 */
    /* 8 */      2,    6,    2,    6,    3,    3,    3,    3,    2,    2,    2,    2,    4,    4,    4,    4,  /* 8 */
    /* 9 */      2,    6,    2,    6,    4,    4,    4,    4,    2,    5,    2,    5,    5,    5,    5,    5,  /* 9 */
    /* A */      2,    6,    2,    6,    3,    3,    3,    3,    2,    2,    2,    2,    4,    4,    4,    4,  /* A */
    /* B */      2,    5,    2,    5,    4,    4,    4,    4,    2,    4,    2,    4,    4,    4,    4,    4,  /* B */
    /* C */      2,    6,    2,    8,    3,    3,    5,    5,    2,    2,    2,    2,    4,    4,    6,    6,  /* C */
    /* D */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* D */
    /* E */      2,    6,    2,    8,    3,    3,    5,    5,    2,    2,    2,    2,    4,    4,    6,    6,  /* E */
    /* F */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7   /* F */
    };


//helper variables
uint64_t instructions = 0; //keep track of total instructions executed
uint32_t clockticks6502 = 0, clockgoal6502 = 0;
uint16_t ea, reladdr, value, result;
uint8_t opcode, oldstatus;

uint8_t callexternal = 0;
void (*loopexternal)();

//a few general functions used by various other functions
void push16(uint16_t pushval) {
    write6502(BASE_STACK + sp, (pushval >> 8) & 0xFF);
    write6502(BASE_STACK + ((sp - 1) & 0xFF), pushval & 0xFF);
    sp -= 2;
}

void push8(uint8_t pushval) {
    write6502(BASE_STACK + sp--, pushval);
}

uint16_t pull16() {
    uint16_t temp16;
    temp16 = read6502(BASE_STACK + ((sp + 1) & 0xFF)) | ((uint16_t)read6502(BASE_STACK + ((sp + 2) & 0xFF)) << 8);
    sp += 2;
    return(temp16);
}

uint8_t pull8() {
    return (read6502(BASE_STACK + ++sp));
}

void nmi6502() {
    push16(pc);
    push8(status);
    status |= FLAG_INTERRUPT;
    pc = (uint16_t)read6502(0xFFFA) | ((uint16_t)read6502(0xFFFB) << 8);
}

void irq6502() {
    push16(pc);
    push8(status & ~ FLAG_BREAK);
    //status |= FLAG_INTERRUPT;
    setinterrupt();
    pc = (uint16_t)read6502(0xFFFE) | ((uint16_t)read6502(0xFFFF) << 8);
}

void reset6502() {
    pc = (uint16_t)read6502(0xFFFC) | ((uint16_t)read6502(0xFFFD) << 8);
    a = 0;
    x = 0;
    y = 0;
    sp = 0xFF;
    status = FLAG_CONSTANT | FLAG_INTERRUPT;
    irq_triggered = 0;
}

void hookexternal(void *funcptr) {
    if (funcptr != (void *)NULL) {
        loopexternal = funcptr;
        callexternal = 1;
    } else callexternal = 0;
}

void step6502()
{
    static void *optab[256];
    static uint8_t opt_init = 0;
    uint16_t zp_ptr = 0;
    uint16_t ea  = 0;
    uint8_t val = 0;
    uint8_t v = 0;
    uint16_t adr=0;
    int8_t off = 0;
    uint8_t zp =0;
    uint16_t base = 0;
    uint16_t b = 0;
    uint16_t a16=0;
    uint8_t oldC = 0;
    uint16_t res = 0;
    uint8_t lo = 0;
    uint8_t hi = 0;
    uint16_t old = 0;
    uint16_t ret = 0;
    uint16_t ptr = 0;

    if (!opt_init) {                   /* build table once */

        optab[0x00] = &&OP_BRK;
        optab[0x01] = &&OP_ORA_INDX;
        optab[0x02] = &&OP_NIL;
        optab[0x03] = &&OP_NIL;
        optab[0x04] = &&OP_NIL;
        optab[0x05] = &&OP_ORA_ZP;
        optab[0x06] = &&OP_ASL_ZP;
        optab[0x07] = &&OP_NIL;
        optab[0x08] = &&OP_PHP;
        optab[0x09] = &&OP_ORA_IMM;
        optab[0x0A] = &&OP_ASL_ACC;
        optab[0x0B] = &&OP_NIL;
        optab[0x0C] = &&OP_NIL;
        optab[0x0D] = &&OP_ORA_ABS;
        optab[0x0E] = &&OP_ASL_ABS;
        optab[0x0F] = &&OP_NIL;

        optab[0x10] = &&OP_BPL_REL;
        optab[0x11] = &&OP_ORA_INDY;
        optab[0x12] = &&OP_NIL;
        optab[0x13] = &&OP_NIL;
        optab[0x14] = &&OP_NIL;
        optab[0x15] = &&OP_ORA_ZPX;
        optab[0x16] = &&OP_ASL_ZPX;
        optab[0x17] = &&OP_NIL;
        optab[0x18] = &&OP_CLC;
        optab[0x19] = &&OP_ORA_ABSY;
        optab[0x1A] = &&OP_NIL;
        optab[0x1B] = &&OP_NIL;
        optab[0x1C] = &&OP_NIL;
        optab[0x1D] = &&OP_ORA_ABSX;
        optab[0x1E] = &&OP_ASL_ABSX;
        optab[0x1F] = &&OP_NIL;

        optab[0x20] = &&OP_JSR_ABS;
        optab[0x21] = &&OP_AND_INDX;
        optab[0x22] = &&OP_NIL;
        optab[0x23] = &&OP_NIL;
        optab[0x24] = &&OP_BIT_ZP;
        optab[0x25] = &&OP_AND_ZP;
        optab[0x26] = &&OP_ROL_ZP;
        optab[0x27] = &&OP_NIL;
        optab[0x28] = &&OP_PLP;
        optab[0x29] = &&OP_AND_IMM;
        optab[0x2A] = &&OP_ROL_ACC;
        optab[0x2B] = &&OP_NIL;
        optab[0x2C] = &&OP_BIT_ABS;
        optab[0x2D] = &&OP_AND_ABS;
        optab[0x2E] = &&OP_ROL_ABS;
        optab[0x2F] = &&OP_NIL;

        optab[0x30] = &&OP_BMI_REL;
        optab[0x31] = &&OP_AND_INDY;
        optab[0x32] = &&OP_NIL;
        optab[0x33] = &&OP_NIL;
        optab[0x34] = &&OP_NIL;
        optab[0x35] = &&OP_AND_ZPX;
        optab[0x36] = &&OP_ROL_ZPX;
        optab[0x37] = &&OP_NIL;
        optab[0x38] = &&OP_SEC;
        optab[0x39] = &&OP_AND_ABSY;
        optab[0x3A] = &&OP_NIL;
        optab[0x3B] = &&OP_NIL;
        optab[0x3C] = &&OP_NIL;
        optab[0x3D] = &&OP_AND_ABSX;
        optab[0x3E] = &&OP_ROL_ABSX;
        optab[0x3F] = &&OP_NIL;

        optab[0x40] = &&OP_RTI;
        optab[0x41] = &&OP_EOR_INDX;
        optab[0x42] = &&OP_NIL;
        optab[0x43] = &&OP_NIL;
        optab[0x44] = &&OP_NIL;
        optab[0x45] = &&OP_EOR_ZP;
        optab[0x46] = &&OP_LSR_ZP;
        optab[0x47] = &&OP_NIL;
        optab[0x48] = &&OP_PHA;
        optab[0x49] = &&OP_EOR_IMM;
        optab[0x4A] = &&OP_LSR_ACC;
        optab[0x4B] = &&OP_NIL;
        optab[0x4C] = &&OP_JMP_ABS;
        optab[0x4D] = &&OP_EOR_ABS;
        optab[0x4E] = &&OP_LSR_ABS;
        optab[0x4F] = &&OP_NIL;

        optab[0x50] = &&OP_BVC_REL;
        optab[0x51] = &&OP_EOR_INDY;
        optab[0x52] = &&OP_NIL;
        optab[0x53] = &&OP_NIL;
        optab[0x54] = &&OP_NIL;
        optab[0x55] = &&OP_EOR_ZPX;
        optab[0x56] = &&OP_LSR_ZPX;
        optab[0x57] = &&OP_NIL;
        optab[0x58] = &&OP_CLI;
        optab[0x59] = &&OP_EOR_ABSY;
        optab[0x5A] = &&OP_NIL;
        optab[0x5B] = &&OP_NIL;
        optab[0x5C] = &&OP_NIL;
        optab[0x5D] = &&OP_EOR_ABSX;
        optab[0x5E] = &&OP_LSR_ABSX;
        optab[0x5F] = &&OP_NIL;

        optab[0x60] = &&OP_RTS;
        optab[0x61] = &&OP_ADC_INDX;
        optab[0x62] = &&OP_NIL;
        optab[0x63] = &&OP_NIL;
        optab[0x64] = &&OP_NIL;
        optab[0x65] = &&OP_ADC_ZP;
        optab[0x66] = &&OP_ROR_ZP;
        optab[0x67] = &&OP_NIL;
        optab[0x68] = &&OP_PLA;
        optab[0x69] = &&OP_ADC_IMM;
        optab[0x6A] = &&OP_ROR_ACC;
        optab[0x6B] = &&OP_NIL;
        optab[0x6C] = &&OP_JMP_IND;
        optab[0x6D] = &&OP_ADC_ABS;
        optab[0x6E] = &&OP_ROR_ABS;
        optab[0x6F] = &&OP_NIL;

        optab[0x70] = &&OP_BVS_REL;
        optab[0x71] = &&OP_ADC_INDY;
        optab[0x72] = &&OP_NIL;
        optab[0x73] = &&OP_NIL;
        optab[0x74] = &&OP_NIL;
        optab[0x75] = &&OP_ADC_ZPX;
        optab[0x76] = &&OP_ROR_ZPX;
        optab[0x77] = &&OP_NIL;
        optab[0x78] = &&OP_SEI;
        optab[0x79] = &&OP_ADC_ABSY;
        optab[0x7A] = &&OP_NIL;
        optab[0x7B] = &&OP_NIL;
        optab[0x7C] = &&OP_NIL;
        optab[0x7D] = &&OP_ADC_ABSX;
        optab[0x7E] = &&OP_ROR_ABSX;
        optab[0x7F] = &&OP_NIL;

        optab[0x80] = &&OP_NIL;
        optab[0x81] = &&OP_STA_INDX;
        optab[0x82] = &&OP_NIL;
        optab[0x83] = &&OP_NIL;
        optab[0x84] = &&OP_STY_ZP;
        optab[0x85] = &&OP_STA_ZP;
        optab[0x86] = &&OP_STX_ZP;
        optab[0x87] = &&OP_NIL;
        optab[0x88] = &&OP_DEY;
        optab[0x89] = &&OP_NIL;
        optab[0x8A] = &&OP_TXA;
        optab[0x8B] = &&OP_NIL;
        optab[0x8C] = &&OP_STY_ABS;
        optab[0x8D] = &&OP_STA_ABS;
        optab[0x8E] = &&OP_STX_ABS;
        optab[0x8F] = &&OP_NIL;

        optab[0x90] = &&OP_BCC_REL;
        optab[0x91] = &&OP_STA_INDY;
        optab[0x92] = &&OP_NIL;
        optab[0x93] = &&OP_NIL;
        optab[0x94] = &&OP_STY_ZPX;
        optab[0x95] = &&OP_STA_ZPX;
        optab[0x96] = &&OP_STX_ZPY;
        optab[0x97] = &&OP_NIL;
        optab[0x98] = &&OP_TYA;
        optab[0x99] = &&OP_STA_ABSY;
        optab[0x9A] = &&OP_TXS;
        optab[0x9B] = &&OP_NIL;
        optab[0x9C] = &&OP_NIL;
        optab[0x9D] = &&OP_STA_ABSX;
        optab[0x9E] = &&OP_NIL;
        optab[0x9F] = &&OP_NIL;

        optab[0xA0] = &&OP_LDY_IMM;
        optab[0xA1] = &&OP_LDA_INDX;
        optab[0xA2] = &&OP_LDX_IMM;
        optab[0xA3] = &&OP_NIL;
        optab[0xA4] = &&OP_LDY_ZP;
        optab[0xA5] = &&OP_LDA_ZP;
        optab[0xA6] = &&OP_LDX_ZP;
        optab[0xA7] = &&OP_NIL;
        optab[0xA8] = &&OP_TAY;
        optab[0xA9] = &&OP_LDA_IMM;
        optab[0xAA] = &&OP_TAX;
        optab[0xAB] = &&OP_NIL;
        optab[0xAC] = &&OP_LDY_ABS;
        optab[0xAD] = &&OP_LDA_ABS;
        optab[0xAE] = &&OP_LDX_ABS;
        optab[0xAF] = &&OP_NIL;

        optab[0xB0] = &&OP_BCS_REL;
        optab[0xB1] = &&OP_LDA_INDY;
        optab[0xB2] = &&OP_NIL;
        optab[0xB3] = &&OP_NIL;
        optab[0xB4] = &&OP_LDY_ZPX;
        optab[0xB5] = &&OP_LDA_ZPX;
        optab[0xB6] = &&OP_LDX_ZPY;
        optab[0xB7] = &&OP_NIL;
        optab[0xB8] = &&OP_CLV;
        optab[0xB9] = &&OP_LDA_ABSY;
        optab[0xBA] = &&OP_TSX;
        optab[0xBB] = &&OP_NIL;
        optab[0xBC] = &&OP_LDY_ABSX;
        optab[0xBD] = &&OP_LDA_ABSX;
        optab[0xBE] = &&OP_LDX_ABSY;
        optab[0xBF] = &&OP_NIL;

        optab[0xC0] = &&OP_CPY_IMM;
        optab[0xC1] = &&OP_CMP_INDX;
        optab[0xC2] = &&OP_NIL;
        optab[0xC3] = &&OP_NIL;
        optab[0xC4] = &&OP_CPY_ZP;
        optab[0xC5] = &&OP_CMP_ZP;
        optab[0xC6] = &&OP_DEC_ZP;
        optab[0xC7] = &&OP_NIL;
        optab[0xC8] = &&OP_INY;
        optab[0xC9] = &&OP_CMP_IMM;
        optab[0xCA] = &&OP_DEX;
        optab[0xCB] = &&OP_NIL;
        optab[0xCC] = &&OP_CPY_ABS;
        optab[0xCD] = &&OP_CMP_ABS;
        optab[0xCE] = &&OP_DEC_ABS;
        optab[0xCF] = &&OP_NIL;

        optab[0xD0] = &&OP_BNE_REL;
        optab[0xD1] = &&OP_CMP_INDY;
        optab[0xD2] = &&OP_NIL;
        optab[0xD3] = &&OP_NIL;
        optab[0xD4] = &&OP_NIL;
        optab[0xD5] = &&OP_CMP_ZPX;
        optab[0xD6] = &&OP_DEC_ZPX;
        optab[0xD7] = &&OP_NIL;
        optab[0xD8] = &&OP_CLD;
        optab[0xD9] = &&OP_CMP_ABSY;
        optab[0xDA] = &&OP_NIL;
        optab[0xDB] = &&OP_NIL;
        optab[0xDC] = &&OP_NIL;
        optab[0xDD] = &&OP_CMP_ABSX;
        optab[0xDE] = &&OP_DEC_ABSX;
        optab[0xDF] = &&OP_NIL;

        optab[0xE0] = &&OP_CPX_IMM;
        optab[0xE1] = &&OP_SBC_INDX;
        optab[0xE2] = &&OP_NIL;
        optab[0xE3] = &&OP_NIL;
        optab[0xE4] = &&OP_CPX_ZP;
        optab[0xE5] = &&OP_SBC_ZP;
        optab[0xE6] = &&OP_INC_ZP;
        optab[0xE7] = &&OP_NIL;
        optab[0xE8] = &&OP_INX;
        optab[0xE9] = &&OP_SBC_IMM;
        optab[0xEA] = &&OP_NOP_IMM;
        optab[0xEB] = &&OP_NIL;
        optab[0xEC] = &&OP_CPX_ABS;
        optab[0xED] = &&OP_SBC_ABS;
        optab[0xEE] = &&OP_INC_ABS;
        optab[0xEF] = &&OP_NIL;

        optab[0xF0] = &&OP_BEQ_REL;
        optab[0xF1] = &&OP_SBC_INDY;
        optab[0xF2] = &&OP_NIL;
        optab[0xF3] = &&OP_NIL;
        optab[0xF4] = &&OP_NIL;
        optab[0xF5] = &&OP_SBC_ZPX;
        optab[0xF6] = &&OP_INC_ZPX;
        optab[0xF7] = &&OP_NIL;
        optab[0xF8] = &&OP_SED;
        optab[0xF9] = &&OP_SBC_ABSY;
        optab[0xFA] = &&OP_NIL;
        optab[0xFB] = &&OP_NIL;
        optab[0xFC] = &&OP_NIL;
        optab[0xFD] = &&OP_SBC_ABSX;
        optab[0xFE] = &&OP_INC_ABSX;
        optab[0xFF] = &&OP_NIL;

        opt_init = 1;
    }

        NEXT();
 
    
OP_BRK:
    pc++;
    push16(pc);  
    push8(status | FLAG_BREAK);
    setinterrupt();
    pc = read6502(0xFFFE) | (read6502(0xFFFF) << 8);
    //clockticks6502 += 7;
    NEXT();

OP_ORA_INDX:
    zp_ptr = (read6502(pc++) + x) & 0xFF;
    ea  =  read6502(zp_ptr) |
                   (read6502((zp_ptr + 1) & 0xFF) << 8);

    val = read6502(ea);
    a |= val;
    zerocalc(a);
    signcalc(a);
    clockticks6502 += 6;
    NEXT(); 
OP_ORA_ZP:
    v=read6502(read6502(pc++));
    a|=v;
    zerocalc(a);signcalc(a);
    clockticks6502+=3;
    NEXT();
OP_ASL_ZP:
    adr=read6502(pc++),v=read6502(adr);
    carrycalc(v<<1); v<<=1;
    write6502(adr,v);
    zerocalc(v); signcalc(v);
    clockticks6502+=5;
    NEXT();
OP_PHP:
    push8(status|FLAG_BREAK);
    clockticks6502+=3;
    NEXT();
OP_ORA_IMM:
    a|=read6502(pc++);
    zerocalc(a);signcalc(a);
    clockticks6502+=2;
    NEXT();
OP_ASL_ACC:
    carrycalc(a<<1);a<<=1;
    zerocalc(a);signcalc(a);
    clockticks6502+=2;
    NEXT();
OP_ORA_ABS:
    adr=read6502(pc)|((uint16_t)read6502(pc+1)<<8);pc+=2;
    a|=read6502(adr);
    zerocalc(a);signcalc(a);
    clockticks6502+=4;
    NEXT();
OP_ASL_ABS:
    adr=read6502(pc)|((uint16_t)read6502(pc+1)<<8);pc+=2;
    v=read6502(adr); carrycalc(v<<1); v<<=1;
    write6502(adr,v);
    zerocalc(v); signcalc(v);
    clockticks6502+=6;
    NEXT();
OP_BPL_REL:
    off = (int8_t)read6502(pc++);
    clockticks6502 += 2;
    if(!(status & FLAG_SIGN)){
        old = pc;
        pc += off;
        clockticks6502++;
        if((old & 0xFF00)!=(pc & 0xFF00)) clockticks6502++;
    }
    NEXT();
OP_ORA_INDY:
    zp = read6502(pc++);
    base = read6502(zp) | (read6502((zp+1)&0xFF)<<8);
    adr = base + y;
    a |= read6502(adr);
    zerocalc(a); signcalc(a);
    clockticks6502 += 5 + ((base & 0xFF00)!=(adr & 0xFF00));
    NEXT();
OP_ORA_ZPX:
    adr = (read6502(pc++) + x) & 0xFF;
    a |= read6502(adr);
    zerocalc(a); signcalc(a);
    clockticks6502 += 4;
    NEXT();
OP_ASL_ZPX:
    adr = (read6502(pc++) + x) & 0xFF;
    v = read6502(adr); carrycalc(v<<1); v <<= 1;
    write6502(adr, v);
    zerocalc(v); signcalc(v);
    clockticks6502 += 6;
    NEXT();
OP_CLC:
    clearcarry();
    clockticks6502 += 2;
    NEXT();
OP_ORA_ABSY:
    b=read6502(pc)|((uint16_t)read6502(pc+1)<<8);
    pc+=2;
    a16=b+y;
    a|=read6502(a16);
    zerocalc(a);
    signcalc(a);
    clockticks6502+=4+((b&0xFF00)!=(a16&0xFF00));
    NEXT();
OP_ORA_ABSX:
    base = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    adr  = base + x;
    a |= read6502(adr);
    zerocalc(a);
    signcalc(a);
    clockticks6502 += 4;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_ASL_ABSX:
    base = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    adr  = base + x;
    v = read6502(adr);
    carrycalc(v << 1);
    v <<= 1;
    write6502(adr, v);
    zerocalc(v);
    signcalc(v);
    clockticks6502 += 7;
    NEXT();
OP_JSR_ABS:
    adr = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    push16(pc - 1);
    pc = adr;
    clockticks6502 += 6;
    NEXT();
OP_AND_INDX:
    zp = (read6502(pc++) + x) & 0xFF;
    adr = read6502(zp) | ((uint16_t)read6502((zp + 1) & 0xFF) << 8);
    a &= read6502(adr);
    zerocalc(a);
    signcalc(a);
    clockticks6502 += 6;
    NEXT();
OP_BIT_ZP:
    adr = read6502(pc++);
    v = read6502(adr);
    zerocalc(a & v);
    status = (status & 0x3F) | (v & 0xC0);
    clockticks6502 += 3;
    NEXT();
OP_AND_ZP:
    adr = read6502(pc++);
    a &= read6502(adr);
    zerocalc(a);
    signcalc(a);
    clockticks6502 += 3;
    NEXT();
OP_ROL_ZP:
    adr = read6502(pc++);
    val = read6502(adr);
    oldC = status & FLAG_CARRY;
    carrycalc(val << 1);                 /* sets C from old bit-7          */
    val = (val << 1) | oldC;             /* rotate through carry           */
    write6502(adr, val);
    zerocalc(val);  signcalc(val);
    clockticks6502 += 5;
    NEXT();
OP_PLP:
    status = pull8() | FLAG_CONSTANT;    /* bit-5 always 1 on pull         */
    clockticks6502 += 4;
    NEXT();
OP_AND_IMM:
    a &= read6502(pc++);
    zerocalc(a);  signcalc(a);
    clockticks6502 += 2;
    NEXT();
OP_ROL_ACC:
    oldC = status & FLAG_CARRY;
    carrycalc(a << 1);
    a = (a << 1) | oldC;
    zerocalc(a);  signcalc(a);
    clockticks6502 += 2;
    NEXT();
OP_BIT_ABS:
    adr = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    val = read6502(adr);
    zerocalc(a & val);
    status = (status & 0x3F) | (val & 0xC0);   /* copy bits 7-6 into N,V     */
    clockticks6502 += 4;
    NEXT();
OP_AND_ABS:
    adr = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    a &= read6502(adr);
    zerocalc(a);  signcalc(a);
    clockticks6502 += 4;
    NEXT();
OP_ROL_ABS:
    adr = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    val  = read6502(adr);
    oldC = status & FLAG_CARRY;
    carrycalc(val << 1);
    val = (val << 1) | oldC;
    write6502(adr, val);
    zerocalc(val);  signcalc(val);
    clockticks6502 += 6;
    NEXT();
OP_BMI_REL:
    off = (int8_t)read6502(pc++);
    clockticks6502 += 2;
    if (status & FLAG_SIGN){
        old = pc;
        pc += off;
        clockticks6502++;
        if ((old & 0xFF00) != (pc & 0xFF00)) clockticks6502++;
    }
    NEXT();
OP_AND_INDY:
    zp = read6502(pc++);
    base = read6502(zp) | ((uint16_t)read6502((zp + 1) & 0xFF) << 8);
    adr  = base + y;
    a &= read6502(adr);
    zerocalc(a);  signcalc(a);
    clockticks6502 += 5;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_AND_ZPX:
    adr = (read6502(pc++) + x) & 0xFF;
    a &= read6502(adr);
    zerocalc(a);  signcalc(a);
    clockticks6502 += 4;
    NEXT();
OP_ROL_ZPX:
    adr = (read6502(pc++) + x) & 0xFF;
    val  = read6502(adr);
    oldC = status & FLAG_CARRY;
    carrycalc(val << 1);
    val = (val << 1) | oldC;
    write6502(adr, val);
    zerocalc(val);  signcalc(val);
    clockticks6502 += 6;
    NEXT();
OP_SEC:
    setcarry();
    clockticks6502 += 2;
    NEXT();
OP_AND_ABSY:
    base = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    adr  = base + y;
    a &= read6502(adr);
    zerocalc(a);  signcalc(a);
    clockticks6502 += 4;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_AND_ABSX:
    base = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    adr  = base + x;
    a &= read6502(adr);
    zerocalc(a);  signcalc(a);
    clockticks6502 += 4;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_ROL_ABSX:
    base = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    adr  = base + x;
    val  = read6502(adr);
    oldC = status & FLAG_CARRY;
    carrycalc(val << 1);
    val = (val << 1) | oldC;
    write6502(adr, val);
    zerocalc(val);  signcalc(val);
    clockticks6502 += 7;
    NEXT();
OP_RTI:
    status = pull8() | FLAG_CONSTANT;
    pc = pull16();
    irq_triggered = 0;
    clockticks6502 += 6;
    NEXT();
OP_EOR_INDX:
    zp = (read6502(pc++) + x) & 0xFF;
    adr = read6502(zp) | ((uint16_t)read6502((zp + 1) & 0xFF) << 8);
    a ^= read6502(adr);
    zerocalc(a);  signcalc(a);
    clockticks6502 += 6;
    NEXT();
OP_EOR_ZP:
    adr = read6502(pc++);
    a ^= read6502(adr);
    zerocalc(a);  signcalc(a);
    clockticks6502 += 3;
    NEXT();
OP_LSR_ZP:
    adr = read6502(pc++);
    v   = read6502(adr);
    if (v & 1) setcarry(); else clearcarry();
    v >>= 1;
    write6502(adr, v);
    zerocalc(v);  signcalc(v);
    clockticks6502 += 5;
    NEXT();
OP_PHA:
    push8(a);
    clockticks6502 += 3;
    NEXT();
OP_EOR_IMM:
    a ^= read6502(pc++);
    zerocalc(a);  signcalc(a);
    clockticks6502 += 2;
    NEXT();
OP_LSR_ACC:
    if (a & 1) setcarry(); else clearcarry();
    a >>= 1;
    zerocalc(a);  signcalc(a);
    clockticks6502 += 2;
    NEXT();
OP_JMP_ABS:
    pc = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    clockticks6502 += 3;
    NEXT();
OP_EOR_ABS:
    adr = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    a ^= read6502(adr);
    zerocalc(a);  signcalc(a);
    clockticks6502 += 4;
    NEXT();
OP_LSR_ABS:
    adr = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    v = read6502(adr);
    if (v & 1) setcarry(); else clearcarry();
    v >>= 1;
    write6502(adr, v);
    zerocalc(v);  signcalc(v);
    clockticks6502 += 6;
    NEXT();
OP_BVC_REL:
    off = (int8_t)read6502(pc++);
    clockticks6502 += 2;
    if (!(status & FLAG_OVERFLOW)) {
        old = pc;
        pc += off;
        clockticks6502++;
        if ((old & 0xFF00) != (pc & 0xFF00)) clockticks6502++;
    }
    NEXT();
OP_EOR_INDY:
    zp = read6502(pc++);
    base = read6502(zp) | ((uint16_t)read6502((zp + 1) & 0xFF) << 8);
    adr  = base + y;
    a ^= read6502(adr);
    zerocalc(a);  signcalc(a);
    clockticks6502 += 5;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_EOR_ZPX:
    adr = (read6502(pc++) + x) & 0xFF;
    a ^= read6502(adr);
    zerocalc(a); signcalc(a);
    clockticks6502 += 4;
    NEXT();
OP_LSR_ZPX:
    adr = (read6502(pc++) + x) & 0xFF;
    v = read6502(adr);
    if (v & 1) setcarry(); else clearcarry();
    v >>= 1;
    write6502(adr, v);
    zerocalc(v); signcalc(v);
    clockticks6502 += 6;
    NEXT();
OP_CLI:
    clearinterrupt();
    clockticks6502 += 2;
    NEXT();
OP_EOR_ABSY:
    base = read6502(pc) | ((uint16_t)read6502(pc+1) << 8);
    pc += 2;
    adr  = base + y;
    a ^= read6502(adr);
    zerocalc(a); signcalc(a);
    clockticks6502 += 4;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_EOR_ABSX:
    base = read6502(pc) | ((uint16_t)read6502(pc+1) << 8);
    pc += 2;
    adr  = base + x;
    a ^= read6502(adr);
    zerocalc(a); signcalc(a);
    clockticks6502 += 4;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_LSR_ABSX:
    base = read6502(pc) | ((uint16_t)read6502(pc+1) << 8);
    pc += 2;
    adr  = base + x;
    v     = read6502(adr);
    if (v & 1) setcarry(); else clearcarry();
    v >>= 1;
    write6502(adr, v);
    zerocalc(v); signcalc(v);
    clockticks6502 += 7;
    NEXT();
OP_RTS:
    ret = pull16();
    pc = ret + 1;
    clockticks6502 += 6;
    NEXT();
OP_ADC_INDX:
    zp = (read6502(pc++) + x) & 0xFF;
    adr = read6502(zp) | ((uint16_t)read6502((zp+1)&0xFF)<<8);
    val = read6502(adr);
    res = (uint16_t)a + val + (status & FLAG_CARRY);
    carrycalc(res); zerocalc(res); overflowcalc(res, a, val); signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 6;
    NEXT();
OP_ADC_ZP:
    zp = read6502(pc++);
    val = read6502(zp);
    res = (uint16_t)a + val + (status & FLAG_CARRY);
    carrycalc(res); zerocalc(res); overflowcalc(res, a, val); signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 3;
    NEXT();
OP_ROR_ZP:
    zp = read6502(pc++);
    v = read6502(zp);
    oldC = status & FLAG_CARRY;
    if (v & 1) setcarry(); else clearcarry();
    v = (v >> 1) | (oldC << 7);
    write6502(zp, v);
    zerocalc(v); signcalc(v);
    clockticks6502 += 5;
    NEXT();
OP_PLA:
    a = pull8();
    zerocalc(a); signcalc(a);
    clockticks6502 += 4;
    NEXT();
OP_ADC_IMM:
    value = read6502(pc++);
    result = a + value + (status & FLAG_CARRY);
    carrycalc(result);  zerocalc(result);
    overflowcalc(result, a, value);  signcalc(result);
    a = (uint8_t)result;
    clockticks6502 += 2;
    NEXT();
OP_ROR_ACC:
    oldC = status & FLAG_CARRY;
    if (a & 1) setcarry(); else clearcarry();
    a = (a >> 1) | (oldC << 7);
    zerocalc(a);
    signcalc(a);
    clockticks6502 += 2;
    NEXT();
OP_JMP_IND:
    ptr = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    lo = read6502(ptr);
    hi = read6502((ptr & 0xFF00) | ((ptr + 1) & 0x00FF));
    pc = lo | ((uint16_t)hi << 8);
    clockticks6502 += 5;
    NEXT();
OP_ADC_ABS:
    adr = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    val = read6502(adr);
    res = (uint16_t)a + val + (status & FLAG_CARRY);
    carrycalc(res);
    zerocalc(res);
    overflowcalc(res, a, val);
    signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 4;
    NEXT();
OP_ROR_ABS:
    adr = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    v = read6502(adr);
    oldC = status & FLAG_CARRY;
    if (v & 1) setcarry(); else clearcarry();
    v = (v >> 1) | (oldC << 7);
    write6502(adr, v);
    zerocalc(v);
    signcalc(v);
    clockticks6502 += 6;
    NEXT();
OP_BVS_REL:
    off = (int8_t)read6502(pc++);
    clockticks6502 += 2;
    if (status & FLAG_OVERFLOW) {
        old = pc;
        pc += off;
        clockticks6502++;
        if ((old & 0xFF00) != (pc & 0xFF00)) clockticks6502++;
    }
    NEXT();
OP_ADC_INDY:
    zp = read6502(pc++);
    base = read6502(zp) | ((uint16_t)read6502((zp + 1) & 0xFF) << 8);
    adr  = base + y;
    val   = read6502(adr);
    res  = (uint16_t)a + val + (status & FLAG_CARRY);
    carrycalc(res);
    zerocalc(res);
    overflowcalc(res, a, val);
    signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 5;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_ADC_ZPX:
    adr = (read6502(pc++) + x) & 0xFF;
    val = read6502(adr);
    res = (uint16_t)a + val + (status & FLAG_CARRY);
    carrycalc(res);
    zerocalc(res);
    overflowcalc(res, a, val);
    signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 4;
    NEXT();
OP_ROR_ZPX:
    adr = (read6502(pc++) + x) & 0xFF;
    v   = read6502(adr);
    oldC = status & FLAG_CARRY;
    if (v & 1) setcarry(); else clearcarry();
    v = (v >> 1) | (oldC << 7);
    write6502(adr, v);
    zerocalc(v);
    signcalc(v);
    clockticks6502 += 6;
    NEXT();
OP_SEI:
    setinterrupt();
    clockticks6502 += 2;
    NEXT();
OP_ADC_ABSY:
    base = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    adr  = base + y;
    val   = read6502(adr);
    res  = (uint16_t)a + val + (status & FLAG_CARRY);
    carrycalc(res);
    zerocalc(res);
    overflowcalc(res, a, val);
    signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 4;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_ADC_ABSX:
    base = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    adr  = base + x;
    val   = read6502(adr);
    res  = (uint16_t)a + val + (status & FLAG_CARRY);
    carrycalc(res);
    zerocalc(res);
    overflowcalc(res, a, val);
    signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 4;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_ROR_ABSX:
    base = read6502(pc) | ((uint16_t)read6502(pc + 1) << 8);
    pc += 2;
    adr  = base + x;
    v     = read6502(adr);
    oldC = status & FLAG_CARRY;
    if (v & 1) setcarry(); else clearcarry();
    v = (v >> 1) | (oldC << 7);
    write6502(adr, v);
    zerocalc(v);
    signcalc(v);
    clockticks6502 += 7;
    NEXT();
OP_STA_INDX:
    zp=(read6502(pc++)+x)&0xFF;
    adr=read6502(zp)|((uint16_t)read6502((zp+1)&0xFF)<<8);
    write6502(adr,a);
    clockticks6502+=6;
    NEXT();
OP_STY_ZP:
    adr=read6502(pc++);
    write6502(adr,y);
    clockticks6502+=3;
    NEXT();
OP_STA_ZP:
    adr=read6502(pc++);
    write6502(adr,a);
    clockticks6502+=3;
    NEXT();
OP_STX_ZP:
    adr=read6502(pc++);
    write6502(adr,x);
    clockticks6502+=3;
    NEXT();
OP_DEY:
    y--;
    zerocalc(y); signcalc(y);
    clockticks6502+=2;
    NEXT();
OP_TXA:
    a=x;
    zerocalc(a); signcalc(a);
    clockticks6502+=2;
    NEXT();
OP_STY_ABS:
    adr=read6502(pc)|((uint16_t)read6502(pc+1)<<8);
    pc+=2;
    write6502(adr,y);
    clockticks6502+=4;
    NEXT();
OP_STA_ABS:
    adr=read6502(pc)|((uint16_t)read6502(pc+1)<<8);
    pc+=2;
    write6502(adr,a);
    clockticks6502+=4;
    NEXT();
OP_STX_ABS:
    adr=read6502(pc)|((uint16_t)read6502(pc+1)<<8);
    pc+=2;
    write6502(adr,x);
    clockticks6502+=4;
    NEXT();
OP_BCC_REL:
    off=(int8_t)read6502(pc++);
    clockticks6502+=2;
    if(!(status&FLAG_CARRY)){
        old=pc;
        pc+=off;
        clockticks6502++;
        if((old&0xFF00)!=(pc&0xFF00)) clockticks6502++;
    }
    NEXT();
OP_STA_INDY:
    zp=read6502(pc++);
    base=read6502(zp)|((uint16_t)read6502((zp+1)&0xFF)<<8);
    adr=base+y;
    write6502(adr,a);
    clockticks6502+=6;
    NEXT();
OP_STY_ZPX:
    adr=(read6502(pc++)+x)&0xFF;
    write6502(adr,y);
    clockticks6502+=4;
    NEXT();
OP_STA_ZPX:
    adr=(read6502(pc++)+x)&0xFF;
    write6502(adr,a);
    clockticks6502+=4;
    NEXT();
OP_STX_ZPY:
    adr=(read6502(pc++)+y)&0xFF;
    write6502(adr,x);
    clockticks6502+=4;
    NEXT();
OP_TYA:
    a=y;
    zerocalc(a);
    signcalc(a);
    clockticks6502+=2;
    NEXT();
OP_STA_ABSY:
    base=read6502(pc)|((uint16_t)read6502(pc+1)<<8);
    pc+=2;
    write6502(base+y,a);
    clockticks6502+=5;
    NEXT();
OP_TXS:
    sp=x;
    clockticks6502+=2;
    NEXT();
OP_STA_ABSX:
    base=read6502(pc)|((uint16_t)read6502(pc+1)<<8);
    pc+=2;
    write6502(base+x,a);
    clockticks6502+=5;
    NEXT();
OP_LDY_IMM:
    y=read6502(pc++);
    zerocalc(y);
    signcalc(y);
    clockticks6502+=2;
    NEXT();
OP_LDA_INDX:
    zp=(read6502(pc++)+x)&0xFF;
    adr=read6502(zp)|((uint16_t)read6502((zp+1)&0xFF)<<8);
    a=read6502(adr);
    zerocalc(a);
    signcalc(a);
    clockticks6502+=6;
    NEXT();
OP_LDX_IMM:
puts("\rhere");
    x=read6502(pc++);
    zerocalc(x);
    signcalc(x);
    clockticks6502+=2;
    NEXT();
OP_LDY_ZP:
    y=read6502(read6502(pc++));
    zerocalc(y);
    signcalc(y);
    clockticks6502+=3;
    NEXT();
OP_LDA_ZP:
    a=read6502(read6502(pc++));
    zerocalc(a);
    signcalc(a);
    clockticks6502+=3;
    NEXT();
OP_LDX_ZP:
    x = read6502(read6502(pc++));
    zerocalc(x);
    signcalc(x);
    clockticks6502 += 3;
    NEXT();
OP_TAY:
    y = a;
    zerocalc(y);
    signcalc(y);
    clockticks6502 += 2;
    NEXT();
OP_LDA_IMM:
    a  = read6502(pc++);
    zerocalc(a);  signcalc(a);
    clockticks6502 += 2;
    NEXT();
OP_TAX:
    x=a;
    zerocalc(x);signcalc(x);
    clockticks6502+=2;
    NEXT();
OP_LDY_ABS:
    adr=read6502(pc)|((uint16_t)read6502(pc+1)<<8);pc+=2;
    y=read6502(adr);
    zerocalc(y);signcalc(y);
    clockticks6502+=4;
    NEXT();
OP_LDA_ABS:
    adr=read6502(pc)|((uint16_t)read6502(pc+1)<<8);pc+=2;
    a=read6502(adr);
    zerocalc(a);signcalc(a);
    clockticks6502+=4;
    NEXT();
OP_LDX_ABS:
    adr=read6502(pc)|((uint16_t)read6502(pc+1)<<8);pc+=2;
    x=read6502(adr);
    zerocalc(x);signcalc(x);
    clockticks6502+=4;
    NEXT();
OP_BCS_REL:
    off=(int8_t)read6502(pc++);
    clockticks6502+=2;
    if(status&FLAG_CARRY){
        old=pc;
        pc+=off;
        clockticks6502++;
        if((old&0xFF00)!=(pc&0xFF00))clockticks6502++;
    }
    NEXT();
OP_LDA_INDY:
    zp=read6502(pc++);
    base=read6502(zp)|((uint16_t)read6502((zp+1)&0xFF)<<8);
    adr=base+y;
    a=read6502(adr);
    zerocalc(a);signcalc(a);
    clockticks6502+=5;
    if((base&0xFF00)!=(adr&0xFF00))clockticks6502++;
    NEXT();
OP_LDY_ZPX:
    adr=(read6502(pc++)+x)&0xFF;
    y=read6502(adr);
    zerocalc(y);signcalc(y);
    clockticks6502+=4;
    NEXT();
OP_LDA_ZPX:
    adr=(read6502(pc++)+x)&0xFF;
    a=read6502(adr);
    zerocalc(a);signcalc(a);
    clockticks6502+=4;
    NEXT();
OP_LDX_ZPY:
    adr=(read6502(pc++)+y)&0xFF;
    x=read6502(adr);
    zerocalc(x);signcalc(x);
    clockticks6502+=4;
    NEXT();
OP_CLV:
    clearoverflow();
    clockticks6502+=2;
    NEXT();
OP_LDA_ABSY:
    base=read6502(pc)|((uint16_t)read6502(pc+1)<<8);pc+=2;
    adr=base+y;
    a=read6502(adr);
    zerocalc(a);signcalc(a);
    clockticks6502+=4;
    if((base&0xFF00)!=(adr&0xFF00))clockticks6502++;
    NEXT();
OP_TSX:
    x=sp;
    zerocalc(x);signcalc(x);
    clockticks6502+=2;
    NEXT();
OP_LDY_ABSX:
    base=read6502(pc)|((uint16_t)read6502(pc+1)<<8);pc+=2;
    adr=base+x;
    y=read6502(adr);
    zerocalc(y);signcalc(y);
    clockticks6502+=4;
    if((base&0xFF00)!=(adr&0xFF00))clockticks6502++;
    NEXT();
OP_LDA_ABSX:
    base=read6502(pc)|((uint16_t)read6502(pc+1)<<8);pc+=2;
    adr=base+x;
    a=read6502(adr);
    zerocalc(a);signcalc(a);
    clockticks6502+=4;
    if((base&0xFF00)!=(adr&0xFF00))clockticks6502++;
    NEXT();
OP_LDX_ABSY:
    base=read6502(pc)|((uint16_t)read6502(pc+1)<<8);pc+=2;
    adr=base+y;
    x=read6502(adr);
    zerocalc(x);signcalc(x);
    clockticks6502+=4;
    if((base&0xFF00)!=(adr&0xFF00))clockticks6502++;
    NEXT();
OP_CPY_IMM:
    v=read6502(pc++);
    res=(uint16_t)y - v;
    if(y>=v) setcarry(); else clearcarry();
    zerocalc(res);signcalc(res);
    clockticks6502+=2;
    NEXT();
OP_CMP_INDX:
    zp=(read6502(pc++)+x)&0xFF;
    adr=read6502(zp)|((uint16_t)read6502((zp+1)&0xFF)<<8);
    v=read6502(adr);
    res=(uint16_t)a - v;
    if(a>=v) setcarry(); else clearcarry();
    zerocalc(res);signcalc(res);
    clockticks6502+=6;
    NEXT();
OP_CPY_ZP:
    v=read6502(read6502(pc++));
    res=(uint16_t)y - v;
    if(y>=v) setcarry(); else clearcarry();
    zerocalc(res);signcalc(res);
    clockticks6502+=3;
    NEXT();
OP_CMP_ZP:
    zp=read6502(pc++);
    v=read6502(zp);
    res=(uint16_t)a - v;
    if(a>=v) setcarry(); else clearcarry();
    zerocalc(res);signcalc(res);
    clockticks6502+=3;
    NEXT();
OP_DEC_ZP:
    zp=read6502(pc++);
    v=read6502(zp)-1;
    write6502(zp,v);
    zerocalc(v);signcalc(v);
    clockticks6502+=5;
    NEXT();
OP_INY:
    y++;
    zerocalc(y);signcalc(y);
    clockticks6502+=2;
    NEXT();
OP_CMP_IMM:
    v=read6502(pc++);
    res=(uint16_t)a - v;
    if(a>=v) setcarry(); else clearcarry();
    zerocalc(res);signcalc(res);
    clockticks6502+=2;
    NEXT();
OP_DEX:
    x--;
    zerocalc(x);signcalc(x);
    clockticks6502+=2;
    NEXT();
OP_CPY_ABS:
    adr=read6502(pc)|((uint16_t)read6502(pc+1)<<8);pc+=2;
    v=read6502(adr);
    res=(uint16_t)y - v;
    if(y>=v) setcarry(); else clearcarry();
    zerocalc(res);signcalc(res);
    clockticks6502+=4;
    NEXT();
OP_CMP_ABS:
    adr = read6502(pc) | ((uint16_t)read6502(pc+1) << 8);
    pc += 2;
    v = read6502(adr);
    res = (uint16_t)a - v;
    if (a >= v) setcarry(); else clearcarry();
    zerocalc(res); signcalc(res);
    clockticks6502 += 4;
    NEXT();
OP_DEC_ABS:
    adr = read6502(pc) | ((uint16_t)read6502(pc+1) << 8);
    pc += 2;
    v = read6502(adr) - 1;
    write6502(adr, v);
    zerocalc(v); signcalc(v);
    clockticks6502 += 6;
    NEXT();
OP_BNE_REL:
    off = (int8_t)read6502(pc++);
    clockticks6502 += 2;
    if (!(status & FLAG_ZERO)) {
        old = pc;
        pc += off;
        clockticks6502++;
        if ((old & 0xFF00) != (pc & 0xFF00)) clockticks6502++;
    }
    NEXT();
OP_CMP_INDY:
    zp = read6502(pc++);
    base = read6502(zp) | ((uint16_t)read6502((zp+1)&0xFF) << 8);
    adr  = base + y;
    v     = read6502(adr);
    res  = (uint16_t)a - v;
    if (a >= v) setcarry(); else clearcarry();
    zerocalc(res); signcalc(res);
    clockticks6502 += 5;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_CMP_ZPX:
    adr = (read6502(pc++) + x) & 0xFF;
    v   = read6502(adr);
    res = (uint16_t)a - v;
    if (a >= v) setcarry(); else clearcarry();
    zerocalc(res); signcalc(res);
    clockticks6502 += 4;
    NEXT();
OP_DEC_ZPX:
    adr = (read6502(pc++) + x) & 0xFF;
    v   = read6502(adr) - 1;
    write6502(adr, v);
    zerocalc(v); signcalc(v);
    clockticks6502 += 6;
    NEXT();
OP_CLD:
    cleardecimal();
    clockticks6502 += 2;
    NEXT();
OP_CMP_ABSY:
    base = read6502(pc) | ((uint16_t)read6502(pc+1) << 8);
    pc += 2;
    adr  = base + y;
    v     = read6502(adr);
    res  = (uint16_t)a - v;
    if (a >= v) setcarry(); else clearcarry();
    zerocalc(res); signcalc(res);
    clockticks6502 += 4;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_CMP_ABSX:
    base = read6502(pc) | ((uint16_t)read6502(pc+1) << 8);
    pc += 2;
    adr  = base + x;
    v     = read6502(adr);
    res  = (uint16_t)a - v;
    if (a >= v) setcarry(); else clearcarry();
    zerocalc(res); signcalc(res);
    clockticks6502 += 4;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_DEC_ABSX:
    base = read6502(pc) | ((uint16_t)read6502(pc+1) << 8);
    pc += 2;
    adr  = base + x;
    v     = read6502(adr) - 1;
    write6502(adr, v);
    zerocalc(v); signcalc(v);
    clockticks6502 += 7;
    NEXT();
OP_CPX_IMM:
    v = read6502(pc++);
    res = (uint16_t)x - v;
    if (x >= v) setcarry(); else clearcarry();
    zerocalc(res); signcalc(res);
    clockticks6502 += 2;
    NEXT();
OP_SBC_INDX:
    zp = (read6502(pc++) + x) & 0xFF;
    adr = read6502(zp) | ((uint16_t)read6502((zp+1)&0xFF) << 8);
    v = read6502(adr) ^ 0xFF;
    res = (uint16_t)a + v + (status & FLAG_CARRY);
    carrycalc(res); zerocalc(res); overflowcalc(res, a, v); signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 6;
    NEXT();
OP_CPX_ZP:
    v = read6502(read6502(pc++));
    res = (uint16_t)x - v;
    if (x >= v) setcarry(); else clearcarry();
    zerocalc(res); signcalc(res);
    clockticks6502 += 3;
    NEXT();
OP_SBC_ZP:
    zp = read6502(pc++);
    v = read6502(zp) ^ 0xFF;
    res = (uint16_t)a + v + (status & FLAG_CARRY);
    carrycalc(res); zerocalc(res); overflowcalc(res, a, v); signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 3;
    NEXT();
OP_INC_ZP:
    zp = read6502(pc++);
    v = read6502(zp) + 1;
    write6502(zp, v);
    zerocalc(v); signcalc(v);
    clockticks6502 += 5;
    NEXT();
OP_INX:
    x++;
    zerocalc(x); signcalc(x);
    clockticks6502 += 2;
    NEXT();
OP_SBC_IMM:
    v = (read6502(pc++) ^ 0xFF);
    res = (uint16_t)a + v + (status & FLAG_CARRY);
    carrycalc(res); zerocalc(res); overflowcalc(res, a, v); signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 2;
    NEXT();
OP_NOP_IMM:       /* official NOP ($EA) */
    clockticks6502 += 2;
    NEXT();
OP_CPX_ABS:
    adr = read6502(pc) | ((uint16_t)read6502(pc+1)<<8);
    pc += 2;
    v = read6502(adr);
    res = (uint16_t)x - v;
    if (x >= v) setcarry(); else clearcarry();
    zerocalc(res); signcalc(res);
    clockticks6502 += 4;
    NEXT();
OP_SBC_ABS:
    adr = read6502(pc) | ((uint16_t)read6502(pc+1)<<8);
    pc += 2;
    v = read6502(adr) ^ 0xFF;
    res = (uint16_t)a + v + (status & FLAG_CARRY);
    carrycalc(res); zerocalc(res); overflowcalc(res, a, v); signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 4;
    NEXT();
OP_INC_ABS:
    adr = read6502(pc) | ((uint16_t)read6502(pc+1)<<8);
    pc += 2;
    v = read6502(adr) + 1;
    write6502(adr, v);
    zerocalc(v); signcalc(v);
    clockticks6502 += 6;
    NEXT();
OP_BEQ_REL:
    off = (int8_t)read6502(pc++);
    clockticks6502 += 2;
    if (status & FLAG_ZERO) {
        old = pc;
        pc += off;
        clockticks6502++;
        if ((old & 0xFF00) != (pc & 0xFF00)) clockticks6502++;
    }
    NEXT();
OP_SBC_INDY:
    zp = read6502(pc++);
    base = read6502(zp) | ((uint16_t)read6502((zp+1)&0xFF)<<8);
    adr = base + y;
    v = read6502(adr) ^ 0xFF;
    res = (uint16_t)a + v + (status & FLAG_CARRY);
    carrycalc(res); zerocalc(res); overflowcalc(res, a, v); signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 5;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_SBC_ZPX:
    adr = (read6502(pc++) + x) & 0xFF;
    v = read6502(adr) ^ 0xFF;
    res = (uint16_t)a + v + (status & FLAG_CARRY);
    carrycalc(res); zerocalc(res); overflowcalc(res, a, v); signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 4;
    NEXT();
OP_INC_ZPX:
    adr = (read6502(pc++) + x) & 0xFF;
    v   = read6502(adr) + 1;
    write6502(adr, v);
    zerocalc(v);  signcalc(v);
    clockticks6502 += 6;
    NEXT();
OP_SED:
    setdecimal();
    clockticks6502 += 2;
    NEXT();
OP_SBC_ABSY:
    base = read6502(pc) | ((uint16_t)read6502(pc+1) << 8);
    pc += 2;
    adr  = base + y;
    v     = read6502(adr) ^ 0xFF;
    res  = (uint16_t)a + v + (status & FLAG_CARRY);
    carrycalc(res);  zerocalc(res);  overflowcalc(res, a, v);  signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 4;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_SBC_ABSX:
    base = read6502(pc) | ((uint16_t)read6502(pc+1) << 8);
    pc += 2;
    adr  = base + x;
    v     = read6502(adr) ^ 0xFF;
    res  = (uint16_t)a + v + (status & FLAG_CARRY);
    carrycalc(res);  zerocalc(res);  overflowcalc(res, a, v);  signcalc(res);
    a = (uint8_t)res;
    clockticks6502 += 4;
    if ((base & 0xFF00) != (adr & 0xFF00)) clockticks6502++;
    NEXT();
OP_INC_ABSX:
    base = read6502(pc) | ((uint16_t)read6502(pc+1) << 8);
    pc += 2;
    adr  = base + x;
    v     = read6502(adr) + 1;
    write6502(adr, v);
    zerocalc(v);  signcalc(v);
    clockticks6502 += 7;
    NEXT();
OP_NIL:
    clockticks6502 += 2;   /* or correct cycles if you want */
    NEXT();  




}