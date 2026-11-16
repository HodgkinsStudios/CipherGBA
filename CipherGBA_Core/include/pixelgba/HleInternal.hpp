/* Internal CipherGBA HLE BIOS services (built directly into emulator core).
 * Copyright (c) HodgkinsStudios. C ABI version 1. Reuse is subject to
 * the project copyright holder's licensing decisions.
 * The internal bridge never includes proprietary BIOS or game ROM bytes.
 */
#ifndef CIPHER_GBA_BIOS_HLE_H
#define CIPHER_GBA_BIOS_HLE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CIPHER_GBA_BIOS_HLE_ABI_VERSION 1u

typedef struct CipherGbaHleHost {
    uint32_t abi_version;
    void* user;
    uint32_t* regs;                 /* sixteen ARM registers */
    uint32_t* cpsr;
    uint32_t* spsr;
    uint16_t* interrupt_enable;
    uint16_t* interrupt_flags;
    uint16_t* wait_mask;
    uint32_t* irq_return;
    uint32_t* irq_status;
    uint32_t* saved_registers;      /* five values (r0-r3, r12) */
    uint32_t* protected_bios_latch;
    uint64_t* unsupported_swi_count;
    uint64_t* irq_dispatch_count;
    uint64_t* halt_write_count;
    uint8_t (*halted)(void*);
    void (*set_halted)(void*, uint8_t);
    uint8_t (*irq_active)(void*);
    void (*set_irq_active)(void*, uint8_t);
    uint8_t (*halt_swi_pending)(void*);
    void (*set_halt_swi_pending)(void*, uint8_t);
    uint32_t (*read)(void*, uint32_t addr, uint32_t bytes);
    void (*write)(void*, uint32_t addr, uint32_t value, uint32_t bytes);
    uint32_t (*access_cycles)(void*, uint32_t addr, uint32_t bytes, uint8_t sequential, uint8_t instruction);
    void (*advance_clocks)(void*, uint32_t clocks);
    void (*cpu_irq)(void*);
    void (*set_cpsr)(void*, uint32_t);
    void (*clear_memory)(void*, uint32_t flags);
    uint8_t (*prefetch_enabled)(void*);
    void (*prime_rom_return)(void*, uint32_t pc, uint32_t width, uint8_t prefetch);
} CipherGbaHleHost;

/* All functions return failure for invalid bridge/ABI, and never assume a
 * particular emulator's object layout. 
 * The library does not own host state; it may be used by multiple cores. */
uint32_t cipher_gba_bios_hle_abi_version(void);
uint32_t cipher_gba_bios_hle_valid(const CipherGbaHleHost* host);
uint32_t cipher_gba_bios_hle_swi(const CipherGbaHleHost* host, uint32_t swi);
void cipher_gba_bios_hle_begin_irq(const CipherGbaHleHost* host);
void cipher_gba_bios_hle_finish_irq(const CipherGbaHleHost* host);
void cipher_gba_bios_hle_complete_wait(const CipherGbaHleHost* host, uint16_t flags);
void cipher_gba_bios_hle_complete_halt(const CipherGbaHleHost* host);
#ifdef __cplusplus
}
#endif
#endif
