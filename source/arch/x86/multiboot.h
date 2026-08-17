#ifndef ROOTOS_MULTIBOOT_H
#define ROOTOS_MULTIBOOT_H

#include "types.h"


/*
 * ============================================================
 * MULTIBOOT CONSTANTS
 * ============================================================
 */

#define MULTIBOOT_BOOTLOADER_MAGIC \
    0x2BADB002u


#define MULTIBOOT_INFO_FRAMEBUFFER \
    (1u << 12)


/*
 * ============================================================
 * MULTIBOOT INFORMATION STRUCTURE
 * ============================================================
 *
 * Layout compatible con Multiboot v1.
 */

typedef struct __attribute__((packed))
{
    u32 flags;

    u32 mem_lower;
    u32 mem_upper;

    u32 boot_device;

    u32 cmdline;

    u32 mods_count;
    u32 mods_addr;


    /*
     * a.out / ELF symbols.
     *
     * Ambas variantes ocupan 16 bytes.
     */

    u32 symbols[4];


    u32 mmap_length;
    u32 mmap_addr;


    u32 drives_length;
    u32 drives_addr;


    u32 config_table;

    u32 boot_loader_name;

    u32 apm_table;


    /*
     * VBE.
     */

    u32 vbe_control_info;

    u32 vbe_mode_info;

    u16 vbe_mode;

    u16 vbe_interface_seg;

    u16 vbe_interface_off;

    u16 vbe_interface_len;


    /*
     * ========================================================
     * FRAMEBUFFER
     * ========================================================
     */

    u64 framebuffer_addr;

    u32 framebuffer_pitch;

    u32 framebuffer_width;

    u32 framebuffer_height;

    u8 framebuffer_bpp;

    u8 framebuffer_type;


    /*
     * Direct RGB framebuffer.
     *
     * Solo usamos estos campos cuando:
     *
     * framebuffer_type == 1
     */

    u8 framebuffer_red_field_position;

    u8 framebuffer_red_mask_size;

    u8 framebuffer_green_field_position;

    u8 framebuffer_green_mask_size;

    u8 framebuffer_blue_field_position;

    u8 framebuffer_blue_mask_size;

} MultibootInfo;


#endif