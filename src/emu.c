#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "emu.h"
#include "m65.h"
#include "cpu.c"

#define BANK_1_RAM              0x10000
#define ATTIC_RAM               0x08000000
#define CPU_HZ                  985248u   
#define IRQ_RATE                50u
#define VIC_RASTER_LINES        312u     // PAL C-64 has 312 visible lines per frame
#define CYCLES_PER_LINE         (CPU_HZ / (VIC_RASTER_LINES * IRQ_RATE))

// how many 6502 cycles per IRQ
static const uint32_t cycles_per_irq = CPU_HZ / IRQ_RATE;
static uint32_t cycle_acc       = 0;
static uint32_t frame_ticks     = 0;
static uint8_t raster_line      = 0;


// CIA 1 Timer A state
static uint16_t cia1_timer      = 0;
static uint8_t cia1_talo        = 0;    // last-written low byte
static uint8_t cia1_tahi        = 0;    // last-written high byte
static uint8_t cia1_ctrl        = 0;    // $DC0E: control register
static uint8_t cia1_icr_mask    = 0;
static uint8_t cia1_ifr         = 0;     // $DC0D: interrupt flag register
static uint8_t cia1_crb         = 0;    // $DC0F control register B

static uint16_t cia2_timer;
static uint8_t  cia2_talo, cia2_tahi, cia2_ctrl, cia2_ifr;

uint8_t running = 1;
uint8_t __huge *m65io   = (uint8_t __huge *)0x0ffd3000;
uint8_t __huge *ram     = (uint8_t __huge *)BANK_1_RAM;
uint8_t __huge *rom     = (uint8_t __huge *)ATTIC_RAM;

uint8_t __huge *basic   = (uint8_t __huge *)ATTIC_RAM + 0xa000;  // BASIC at $a000-$bfff
uint8_t __huge *freeram = (uint8_t __huge *)ATTIC_RAM + 0xc000;  // ram mapping for monitor
uint8_t __huge *chars   = (uint8_t __huge *)ATTIC_RAM + 0xd000;  // KERNAL at $d000–$dFFF
uint8_t __huge *kernal  = (uint8_t __huge *)ATTIC_RAM + 0xe000;  // KERNAL at $e000–$FFFF

static uint8_t raster = 0;


void dump_regs(void) {
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

uint8_t read6502(uint16_t address) {

    uint8_t port = ram[0x0001];

    // RAM
    if (address < 0xA000) {
        return ram[address];
    }

    // BASIC ROM or RAM
    if (address >= 0xA000 && address <= 0xBFFF) {
        if(port & 0x01)
            return basic[address - 0xA000];
        else
            return ram[address];
    }

    // IO or CHAR ROM
    if (address >= 0xD000 && address <= 0xDFFF) {
       
        if (!(port & 0x04))
            return chars[address - 0xD000];

        // VIC-II raster counter
        if (address == 0xD012)
        {
            return raster_line;
        }
        
        // VIC IRQ control/status register
        if (address == 0xD019) {
            
            // Reading clears IRQ flags
            uint8_t value = ram[address];
            if (irq_triggered && (value & 0x01)) {
                irq_triggered = 0;
                ram[address] &= ~0x01;  // Clear bit 0 (raster interrupt)
            }
            return value;
        }

        // VIC screen border and back colors
        if (address == 0xD020 || address == 0xD021)
            return PEEK(address);

        // Keyboard column input - return value as if no keys are pressed
        if (address == 0xDC01) {
            return 0xFF;
        }

        // CIA #1 timer for keyboard
        if (address == 0xDC04) {
            return cia1_timer & 0xFF; 
        }

        if (address == 0xDC05) {
            return cia1_timer >> 8; 
        }

        if (address == 0xDC0D) {
            // reading the IFR clears it and returns current value
            uint8_t v = cia1_ifr | 0x80;  // Add bit 7 set to indicate IRQ source
            cia1_ifr = 0;
            irq_triggered = 0;
            return v;
        }

        if (address == 0xDC0E) {
            return cia1_ctrl;
        }

        if (address == 0xDD0D) {
            // pretend the serial bus is live immediately
            return 0x80;   // bit 7 set
        }

        // CARTRIDGE port
        if (address >= 0xDE00 && address <= 0xDE03) {
            // on real hardware these bits come from the user port lines
            // but if you return 0x00, the cart-init will immediately exit.
            return 0x00;
        }

        return ram[address];
    }

    // KERNAL ROM
    if (address >= 0xE000) {
        if(port & 0x02)
            return kernal[address - 0xe000];
        else
            return ram[address];
    }

    return ram[address];

}

void write6502(uint16_t address, uint8_t value) {

    uint8_t port = ram[0x0001];

    // ── 1) Screen text RAM (host console) ───────────────────────
    if(address >= 0x0400 && address <= 0x07e8)
    {
        // screen text ram
        POKE(2048+(address-0x400), value);
    }

    // ── 2) IO Region ───────────────────────
    if(address >= 0xD000 && address <= 0xDFFF)
    {
         //VIC-II I/O at $D000–$D02E ────────────────────────────
        if(address <= 0xD02E) {
            switch (address) {
                case 0xD012:
                    // raster register is read-only
                    return;
                case 0xD019:
                    return;
                case 0xD01A:
                    // IRQ mask register
                    ram[0xD01A] = value;
                    return;
                case 0xD020:  // border color
                case 0xD021:  // background color
                    POKE(address, value & 0x0F);
                    return;
                default:
                    // any other VIC register we just remember the last write
                    ram[address] = value;
                    return;
            }
        }
        
        // color ram
        if(address >= 0xD800 && address <= 0xDBFF) {
            
            POKE(address, value & 0x0f);
            ram[address] = value;
            return;
        }

        // CIA1
        if(address >= 0xDC00 && address <= 0xDC0F)
        {
            if (address == 0xDC04) {
                cia1_talo = value;
                ram[address] = value;
                return;
            }
        
            if (address == 0xDC05) {
                cia1_tahi = value;
                ram[address] = value;
                return;
            }

            if (address == 0xDC0D) {
                // CIA1 Interrupt control register - acknowledge interrupts
                if (value & 0x80) {
                    // Set interrupts
                    cia1_icr_mask |= (value & 0x7F);
                } else {
                    // Clear interrupts
                    cia1_icr_mask   &= ~(value & 0x7F);
                    cia1_ifr        &= ~(value & 0x7F); // clear flags acknowleged
                    irq_triggered   = 0;
                }
                ram[address] = value;
                return;
            }

            if (address == 0xDC0E) {
                if ((value & 0x01) && !(cia1_ctrl & 0x01)) {
                    // Starting timer - load from latch
                    cia1_timer = ((uint16_t)cia1_tahi << 8) | cia1_talo;
                    cia1_ifr   &= (uint8_t)~0x01;
                }
                cia1_ctrl = value;
                ram[address] = value;
                return;
            }

            if (address == 0xDC0F) {
                // on a 0→1 transition of bit0, clear any old Timer B IFR:
                if ((value & 0x01) && !(cia1_crb & 0x01)) {
                    cia1_ifr   &= (uint8_t)~0x02;  // clear Timer B flag
                    frame_ticks = 0;               // reset your jiffy accumulator
                }
                cia1_crb = value;
                ram[address] = value;
                return;
            }
        }
    }

    ram[address] = value;

}


void tick_50hz(void) {
    uint8_t port = ram[0x0001];

    // ── 1) VIC raster ────────────────────────────────────────────
    cycle_acc += ticktable[opcode];
    while (cycle_acc >= CYCLES_PER_LINE) {
        cycle_acc -= CYCLES_PER_LINE;
        raster_line = (raster_line + 1) % VIC_RASTER_LINES;
        
        // Check if we're hitting the programmed raster line
        uint8_t trigger_line = ram[0xD012];
        uint8_t d011 = ram[0xD011];
        uint16_t compare_line = trigger_line + ((d011 & 0x80) ? 256 : 0);
        
        if (raster_line == compare_line) {
            ram[0xD019] |= 0x01;  // Set VIC raster interrupt flag
            irq_triggered = 1;
        }
    }

    // ── 2) CIA-1 Timer A (cursor blink and keyboard scan) ────────
    if (cia1_ctrl & 0x01) {  // Only decrement if timer is running
        uint32_t s = ticktable[opcode];
        if (cia1_timer > s) {
            cia1_timer -= s;
        } else {
            // Timer underflow - reload from latch & raise interrupt
            cia1_timer = ((uint16_t)cia1_tahi << 8) | cia1_talo;
            cia1_ifr |= 0x01;  // Set Timer A interrupt flag
        }
    }

    // ── 3) Jiffy-clock 60 Hz counter ─────────────────────────────
    if(cia1_crb & 0x01) {
        frame_ticks += ticktable[opcode];
        if (frame_ticks >= cycles_per_irq) {
            frame_ticks -= cycles_per_irq;
            // set CIA-1 IFR bit 1 for the jiffy clock (Timer B on real hardware)
            cia1_ifr |= 0x02;  // Set Timer B interrupt flag
        }
    }

    // ── 4) Fire IRQ (one-shot) ───────────────────────────────────
    // Only if I-flag clear, no IRQ already in progress, and a source+mask match:
    if (!(status & FLAG_INTERRUPT) && !irq_triggered) {

        //printf("I-flag clear, firing IRQ…\r");
        //getchar();

        uint8_t do_irq = 0;

        // CIA-1 Timer B (jiffy clock)
        if ((cia1_ifr & 0x02) && (cia1_icr_mask & 0x02)) {
            do_irq = 1;
        }       
        // CIA-1 Timer A - check if both flag and control are set
        else if ((cia1_ifr & 0x01) && (cia1_icr_mask & 0x01)) {
            do_irq = 1;
        }
         // VIC raster - check if both flag and mask are set
        else if ((ram[0xD019] & ram[0xD01A] & 0x01) != 0) {
            do_irq = 1;
        }

        if (do_irq == 1) {
            irq_triggered = 1;
            irq6502();
        }
    }
}

// Initialize C64 RAM with proper startup values
void init(void) {

    // Clear RAM
    lfill(0x10000, 0x00, 65535);

    // Setup RAM with proper startup values
    ram[0x00] = 0xFF; 
    ram[0x01] = 0x17;
 
    POKE(0xD020, 14);  // Light blue border
    POKE(0xD021, 6);   // Blue background

    cia1_ifr = 0;
    cia1_icr_mask = 0;
    cia1_ctrl = 0;
    cia1_ifr = 0;
    cia1_timer = 0;

    // copy monitor if it exists
    for(int t=0xc000; t<=0xcfff; t++)
        write6502(t, freeram[t-0xc000]);
}

void keyboard_handler() {

    if(PEEK32(0xffd3619) != 255)
    {
        uint8_t key = PEEK32(0xffd3619);
        POKE32(0xffd3619, 0);

        write6502(631,key);
        write6502(198,1);
    }
}

int main() {
    
    uint8_t show_regs = 0;
    uint8_t do_step = 0;

    POKE(0xD020, 0);  // Set border color to black
    POKE(0xD021, 0);  // Set background color to black

    putchar(0x93);     // Clear screen
    putchar(0x98);     // white text
    putchar(0X1B);     // esc-x - 40 col screen
    putchar(0x58);

    init();
    reset6502();

    hookexternal(tick_50hz);

    while(running) {
        
        if(show_regs == 1) dump_regs();
        
        step6502();
        keyboard_handler();
       
        if(do_step == 1) getchar();
    }

    return 0;
}