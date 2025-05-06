#ifndef __M65_H
#define __M65_H

#include <stdint.h>
#include <stddef.h>

#define DMA_COPY_CMD 0x00; //!< DMA copy command
#define DMA_MIX_CMD 0x01;  //!< DMA mix command (unimplemented)
#define DMA_SWAP_CMD 0x02; //!< DMA swap command (unimplemented)
#define DMA_FILL_CMD 0x03; //!< DMA fill command

#define DMA_LINEAR_ADDR 0x00; //!< DMA linear (normal) addressing mode
#define DMA_MODULO_ADDR 0x01; //!< DMA modulo (rectangular) addressing mode
#define DMA_HOLD_ADDR   0x02;   //!< DMA hold (constant address) addressing mode
#define DMA_XYMOD_ADDR  0x03; //!< DMA XY MOD (bitmap rectangular) addressing mode (unimplemented)

#define POKE(addr, val) (*(volatile unsigned char *)(addr) = (val))
#define PEEK(addr) (*(unsigned char *)(addr))

#define POKE32(addr, val) (*(volatile uint8_t __huge *)(addr) = (val))
#define PEEK32(addr) (*(uint8_t __huge *)(addr))


struct dmagic_dmalist {
    // Enhanced DMA options
    uint8_t option_0b;
    uint8_t option_80;
    uint8_t source_mb;
    uint8_t option_81;
    uint8_t dest_mb;
    uint8_t option_85;
    uint8_t dest_skip;
    uint8_t end_of_options;

    // F018B format DMA request
    uint8_t command; //!< Command (LSB), e.g. DMA_COPY_CMD, DMA_FILL_CMD, etc.
    uint16_t count;  //!< Number of bytes to copy
    uint16_t source_addr; //!< Source address
    uint8_t source_bank;  //!< Source bank and flags
    uint16_t dest_addr;   //!< Destination address
    uint8_t dest_bank;    //!< Destination bank and flags
    uint8_t sub_cmd;      //!< Command (MSB) or F018B subcmd
    uint16_t modulo;      //!< Modulo mode
};

struct dmagic_dmalist dmalist;
uint8_t dma_byte;

void mega65_io_enable(void);
void do_dma(void);
uint8_t dma_peek(uint32_t address);
void dma_poke(uint32_t address, uint8_t value);
void lcopy(uint32_t source_address, uint32_t destination_address, size_t count);
void lfill(uint32_t destination_address, uint8_t value, size_t count);
void lfill_skip(uint32_t destination_address, uint8_t value, size_t count, uint8_t skip);
void mega65_io_enable(void);

#endif