#include "elf_loader.h"

#include "filesystem.h"
#include "heap.h"
#include "memory.h"
#include "string.h"
#include "rootapi.h"
#include "process.h"

#define ELF_IDENT_SIZE 16u
#define ELFCLASS32 1u
#define ELFDATA2LSB 1u
#define EV_CURRENT 1u
#define ET_REL 1u
#define EM_386 3u

#define SHT_NULL 0u
#define SHT_PROGBITS 1u
#define SHT_SYMTAB 2u
#define SHT_STRTAB 3u
#define SHT_RELA 4u
#define SHT_NOBITS 8u
#define SHT_REL 9u

#define SHF_ALLOC 0x2u
#define SHN_UNDEF 0u
#define SHN_ABS 0xFFF1u
#define SHN_COMMON 0xFFF2u

#define R_386_NONE 0u
#define R_386_32 1u
#define R_386_PC32 2u

#define ROOT_ELF_MAX_SECTIONS 96u
#define ROOT_ELF_MAX_IMAGE (2u * 1024u * 1024u)
#define ROOT_ELF_MAX_ALIGNMENT 4096u

#define ELF32_R_SYM(info) ((info) >> 8)
#define ELF32_R_TYPE(info) ((u8)(info))

typedef struct __attribute__((packed))
{
    u8 e_ident[ELF_IDENT_SIZE];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u32 e_entry;
    u32 e_phoff;
    u32 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} Elf32_Ehdr;

typedef struct __attribute__((packed))
{
    u32 sh_name;
    u32 sh_type;
    u32 sh_flags;
    u32 sh_addr;
    u32 sh_offset;
    u32 sh_size;
    u32 sh_link;
    u32 sh_info;
    u32 sh_addralign;
    u32 sh_entsize;
} Elf32_Shdr;

typedef struct __attribute__((packed))
{
    u32 st_name;
    u32 st_value;
    u32 st_size;
    u8 st_info;
    u8 st_other;
    u16 st_shndx;
} Elf32_Sym;

typedef struct __attribute__((packed))
{
    u32 r_offset;
    u32 r_info;
} Elf32_Rel;

static bool range_valid(
    usize offset,
    usize amount,
    usize total
)
{
    return
        offset <= total
        &&
        amount <= total - offset;
}

static usize align_up(
    usize value,
    usize alignment
)
{
    if (alignment <= 1u)
    {
        return value;
    }

    usize mask = alignment - 1u;
    return (value + mask) & ~mask;
}

static bool power_of_two(usize value)
{
    return
        value != 0u
        &&
        (value & (value - 1u)) == 0u;
}

static const char* table_string(
    const u8* table,
    usize table_size,
    u32 offset
)
{
    if (
        table == NULL
        ||
        offset >= table_size
    )
    {
        return NULL;
    }

    for (usize i = offset; i < table_size; i++)
    {
        if (table[i] == 0u)
        {
            return (const char*)(table + offset);
        }
    }

    return NULL;
}

static bool elf_header_valid(
    const Elf32_Ehdr* header,
    usize file_size
)
{
    if (
        header == NULL
        ||
        file_size < sizeof(Elf32_Ehdr)
    )
    {
        return false;
    }

    if (
        header->e_ident[0] != 0x7Fu
        ||
        header->e_ident[1] != 'E'
        ||
        header->e_ident[2] != 'L'
        ||
        header->e_ident[3] != 'F'
        ||
        header->e_ident[4] != ELFCLASS32
        ||
        header->e_ident[5] != ELFDATA2LSB
        ||
        header->e_ident[6] != EV_CURRENT
    )
    {
        return false;
    }

    if (
        header->e_type != ET_REL
        ||
        header->e_machine != EM_386
        ||
        header->e_version != EV_CURRENT
        ||
        header->e_ehsize != sizeof(Elf32_Ehdr)
        ||
        header->e_shentsize != sizeof(Elf32_Shdr)
        ||
        header->e_shnum == 0u
        ||
        header->e_shnum > ROOT_ELF_MAX_SECTIONS
    )
    {
        return false;
    }

    return range_valid(
        header->e_shoff,
        (usize)header->e_shnum * sizeof(Elf32_Shdr),
        file_size
    );
}

static bool symbol_runtime_address(
    const Elf32_Sym* symbol,
    const Elf32_Shdr* sections,
    u16 section_count,
    u8* const* runtime,
    u32* address
)
{
    if (
        symbol == NULL
        ||
        address == NULL
    )
    {
        return false;
    }

    if (symbol->st_shndx == SHN_ABS)
    {
        *address = symbol->st_value;
        return true;
    }

    if (
        symbol->st_shndx == SHN_UNDEF
        ||
        symbol->st_shndx == SHN_COMMON
        ||
        symbol->st_shndx >= section_count
        ||
        runtime[symbol->st_shndx] == NULL
    )
    {
        return false;
    }

    if (
        symbol->st_value
        >
        sections[symbol->st_shndx].sh_size
    )
    {
        return false;
    }

    *address =
        (u32)(usize)runtime[symbol->st_shndx]
        +
        symbol->st_value;

    return true;
}

RootElfResult elf_loader_run(
    const char* path,
    const char* process_name,
    int argc,
    const char** argv,
    int* exit_code,
    u32* pid_output
)
{
    if (exit_code != NULL)
    {
        *exit_code = -1;
    }

    if (pid_output != NULL)
    {
        *pid_output = 0u;
    }

    if (path == NULL || path[0] == '\0')
    {
        return ROOT_ELF_BAD_FORMAT;
    }

    usize file_size = 0;

    if (!filesystem_file_size(path, &file_size))
    {
        return ROOT_ELF_NOT_FOUND;
    }

    if (
        file_size < sizeof(Elf32_Ehdr)
        ||
        file_size > FS_MAX_FILE_SIZE
    )
    {
        return ROOT_ELF_BAD_FORMAT;
    }

    u8* file = (u8*)root_malloc(file_size + 1u);

    if (file == NULL)
    {
        return ROOT_ELF_NO_MEMORY;
    }

    usize loaded_size = 0;
    FsResult read_result = filesystem_read_file(
        path,
        (char*)file,
        file_size + 1u,
        &loaded_size
    );

    if (
        read_result != FS_RESULT_OK
        ||
        loaded_size != file_size
    )
    {
        root_free(file);
        return ROOT_ELF_IO_ERROR;
    }

    const Elf32_Ehdr* header = (const Elf32_Ehdr*)file;

    if (!elf_header_valid(header, file_size))
    {
        root_free(file);
        return ROOT_ELF_BAD_FORMAT;
    }

    const Elf32_Shdr* sections =
        (const Elf32_Shdr*)(file + header->e_shoff);

    usize section_offsets[ROOT_ELF_MAX_SECTIONS];
    u8* runtime[ROOT_ELF_MAX_SECTIONS];

    root_memzero(section_offsets, sizeof(section_offsets));
    root_memzero(runtime, sizeof(runtime));

    usize image_size = 0u;
    usize max_alignment = 16u;

    for (u16 i = 0; i < header->e_shnum; i++)
    {
        const Elf32_Shdr* section = &sections[i];

        if ((section->sh_flags & SHF_ALLOC) == 0u)
        {
            continue;
        }

        usize alignment =
            section->sh_addralign == 0u
            ?
            1u
            :
            section->sh_addralign;

        if (
            !power_of_two(alignment)
            ||
            alignment > ROOT_ELF_MAX_ALIGNMENT
        )
        {
            root_free(file);
            return ROOT_ELF_UNSUPPORTED;
        }

        if (alignment > max_alignment)
        {
            max_alignment = alignment;
        }

        image_size = align_up(image_size, alignment);
        section_offsets[i] = image_size;

        if (
            section->sh_size > ROOT_ELF_MAX_IMAGE
            ||
            image_size > ROOT_ELF_MAX_IMAGE - section->sh_size
        )
        {
            root_free(file);
            return ROOT_ELF_UNSUPPORTED;
        }

        image_size += section->sh_size;

        if (
            section->sh_type != SHT_NOBITS
            &&
            !range_valid(
                section->sh_offset,
                section->sh_size,
                file_size
            )
        )
        {
            root_free(file);
            return ROOT_ELF_BAD_FORMAT;
        }
    }

    if (image_size == 0u)
    {
        root_free(file);
        return ROOT_ELF_BAD_FORMAT;
    }

    u8* image_raw =
        (u8*)root_malloc(
            image_size + max_alignment
        );

    if (image_raw == NULL)
    {
        root_free(file);
        return ROOT_ELF_NO_MEMORY;
    }

    usize aligned_address = align_up(
        (usize)image_raw,
        max_alignment
    );

    u8* image = (u8*)aligned_address;
    root_memzero(image, image_size);

    for (u16 i = 0; i < header->e_shnum; i++)
    {
        const Elf32_Shdr* section = &sections[i];

        if ((section->sh_flags & SHF_ALLOC) == 0u)
        {
            continue;
        }

        runtime[i] = image + section_offsets[i];

        if (
            section->sh_type != SHT_NOBITS
            &&
            section->sh_size > 0u
        )
        {
            root_memcpy(
                runtime[i],
                file + section->sh_offset,
                section->sh_size
            );
        }
    }

    const Elf32_Shdr* symbol_section = NULL;
    const Elf32_Sym* symbols = NULL;
    usize symbol_count = 0u;
    const u8* symbol_strings = NULL;
    usize symbol_strings_size = 0u;

    for (u16 i = 0; i < header->e_shnum; i++)
    {
        if (sections[i].sh_type != SHT_SYMTAB)
        {
            continue;
        }

        if (
            sections[i].sh_entsize != sizeof(Elf32_Sym)
            ||
            sections[i].sh_link >= header->e_shnum
            ||
            sections[sections[i].sh_link].sh_type != SHT_STRTAB
            ||
            !range_valid(
                sections[i].sh_offset,
                sections[i].sh_size,
                file_size
            )
            ||
            !range_valid(
                sections[sections[i].sh_link].sh_offset,
                sections[sections[i].sh_link].sh_size,
                file_size
            )
        )
        {
            root_free(image_raw);
            root_free(file);
            return ROOT_ELF_BAD_FORMAT;
        }

        symbol_section = &sections[i];
        symbols = (const Elf32_Sym*)(file + sections[i].sh_offset);
        symbol_count = sections[i].sh_size / sizeof(Elf32_Sym);
        symbol_strings = file + sections[sections[i].sh_link].sh_offset;
        symbol_strings_size = sections[sections[i].sh_link].sh_size;
        break;
    }

    if (
        symbol_section == NULL
        ||
        symbols == NULL
        ||
        symbol_count == 0u
    )
    {
        root_free(image_raw);
        root_free(file);
        return ROOT_ELF_NO_ENTRY;
    }

    for (u16 i = 0; i < header->e_shnum; i++)
    {
        const Elf32_Shdr* rel_section = &sections[i];

        if (rel_section->sh_type == SHT_RELA)
        {
            root_free(image_raw);
            root_free(file);
            return ROOT_ELF_UNSUPPORTED;
        }

        if (rel_section->sh_type != SHT_REL)
        {
            continue;
        }

        if (
            rel_section->sh_entsize != sizeof(Elf32_Rel)
            ||
            rel_section->sh_link >= header->e_shnum
            ||
            &sections[rel_section->sh_link] != symbol_section
            ||
            rel_section->sh_info >= header->e_shnum
            ||
            runtime[rel_section->sh_info] == NULL
            ||
            !range_valid(
                rel_section->sh_offset,
                rel_section->sh_size,
                file_size
            )
        )
        {
            root_free(image_raw);
            root_free(file);
            return ROOT_ELF_RELOCATION_ERROR;
        }

        const Elf32_Rel* relocations =
            (const Elf32_Rel*)(file + rel_section->sh_offset);

        usize relocation_count =
            rel_section->sh_size / sizeof(Elf32_Rel);

        const Elf32_Shdr* target = &sections[rel_section->sh_info];

        for (usize r = 0; r < relocation_count; r++)
        {
            u32 symbol_index = ELF32_R_SYM(relocations[r].r_info);
            u32 type = ELF32_R_TYPE(relocations[r].r_info);

            if (
                symbol_index >= symbol_count
                ||
                relocations[r].r_offset > target->sh_size
                ||
                target->sh_size - relocations[r].r_offset < sizeof(u32)
            )
            {
                root_free(image_raw);
                root_free(file);
                return ROOT_ELF_RELOCATION_ERROR;
            }

            u32 symbol_address = 0u;

            if (
                type != R_386_NONE
                &&
                !symbol_runtime_address(
                    &symbols[symbol_index],
                    sections,
                    header->e_shnum,
                    runtime,
                    &symbol_address
                )
            )
            {
                root_free(image_raw);
                root_free(file);
                return ROOT_ELF_RELOCATION_ERROR;
            }

            u32* place =
                (u32*)(runtime[rel_section->sh_info]
                + relocations[r].r_offset);

            u32 addend = *place;
            u32 place_address = (u32)(usize)place;

            switch (type)
            {
                case R_386_NONE:
                    break;

                case R_386_32:
                    *place = symbol_address + addend;
                    break;

                case R_386_PC32:
                    *place =
                        symbol_address
                        +
                        addend
                        -
                        place_address;
                    break;

                default:
                    root_free(image_raw);
                    root_free(file);
                    return ROOT_ELF_UNSUPPORTED;
            }
        }
    }

    u32 entry_address = 0u;

    for (usize i = 0; i < symbol_count; i++)
    {
        const char* name = table_string(
            symbol_strings,
            symbol_strings_size,
            symbols[i].st_name
        );

        if (
            name != NULL
            &&
            root_streq(name, "root_main")
        )
        {
            if (
                !symbol_runtime_address(
                    &symbols[i],
                    sections,
                    header->e_shnum,
                    runtime,
                    &entry_address
                )
            )
            {
                entry_address = 0u;
            }

            break;
        }
    }

    if (entry_address == 0u)
    {
        root_free(image_raw);
        root_free(file);
        return ROOT_ELF_NO_ENTRY;
    }

    u32 pid = process_begin(
        process_name != NULL ? process_name : path,
        image_size
    );

    if (pid == 0u)
    {
        root_free(image_raw);
        root_free(file);
        return ROOT_ELF_PROCESS_TABLE_FULL;
    }

    if (pid_output != NULL)
    {
        *pid_output = pid;
    }

    process_mark_running(pid);

    RootAppEntry entry = (RootAppEntry)(usize)entry_address;
    int result = entry(
        rootapi_get(),
        argc,
        argv
    );

    process_mark_exit(pid, result);

    if (exit_code != NULL)
    {
        *exit_code = result;
    }

    root_free(image_raw);
    root_free(file);

    return ROOT_ELF_OK;
}

const char* elf_loader_result_string(
    RootElfResult result
)
{
    switch (result)
    {
        case ROOT_ELF_OK:
            return "ok";

        case ROOT_ELF_NOT_FOUND:
            return "file not found";

        case ROOT_ELF_IO_ERROR:
            return "filesystem I/O error";

        case ROOT_ELF_BAD_FORMAT:
            return "invalid ELF32 relocatable file";

        case ROOT_ELF_UNSUPPORTED:
            return "unsupported ELF feature or relocation";

        case ROOT_ELF_NO_MEMORY:
            return "not enough memory";

        case ROOT_ELF_NO_ENTRY:
            return "root_main entry not found";

        case ROOT_ELF_RELOCATION_ERROR:
            return "ELF relocation failed";

        case ROOT_ELF_PROCESS_TABLE_FULL:
            return "process table is full";

        default:
            return "unknown ELF loader error";
    }
}
