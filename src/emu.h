#ifndef __EMU_H
#define __EMU_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static uint8_t irq_triggered = 0;  // Flag to avoid multiple IRQs
static const char hex_chars[] = "0123456789ABCDEF";

static void print_hex8(uint8_t v) {
    putchar(hex_chars[(v >> 4) & 0xF]);
    putchar(hex_chars[v & 0xF]);
}

static void print_hex16(uint16_t v) {
    putchar(hex_chars[(v >> 12) & 0xF]);
    putchar(hex_chars[(v >>  8) & 0xF]);
    putchar(hex_chars[(v >>  4) & 0xF]);
    putchar(hex_chars[ v        & 0xF]);
}



#endif