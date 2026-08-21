#include "rtpgk.h"

#include "string.h"

static const u8 rtpgk_magic[RTPGK_MAGIC_SIZE] =
{
    'R', 'T', 'P', 'G', 'K', '0', '0', '1'
};

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

static bool fixed_string_terminated(
    const char* text,
    usize capacity
)
{
    for (usize i = 0; i < capacity; i++)
    {
        if (text[i] == '\0')
        {
            return true;
        }
    }

    return false;
}

bool rtpgk_safe_name(const char* name)
{
    if (name == NULL || name[0] == '\0')
    {
        return false;
    }

    for (usize i = 0; name[i] != '\0'; i++)
    {
        char c = name[i];

        bool allowed =
            (c >= 'a' && c <= 'z')
            ||
            (c >= 'A' && c <= 'Z')
            ||
            (c >= '0' && c <= '9')
            ||
            c == '-'
            ||
            c == '_'
            ||
            c == '.';

        if (
            !allowed
            ||
            i >= RTPGK_NAME_MAX - 1u
        )
        {
            return false;
        }
    }

    return true;
}

bool rtpgk_safe_relative_path(const char* path)
{
    if (
        path == NULL
        ||
        path[0] == '\0'
        ||
        path[0] == '/'
    )
    {
        return false;
    }

    usize length = 0u;
    const char* component = path;

    while (*component != '\0')
    {
        const char* end = component;

        while (*end != '\0' && *end != '/')
        {
            end++;
        }

        usize component_length = (usize)(end - component);

        if (
            component_length == 0u
            ||
            (
                component_length == 1u
                &&
                component[0] == '.'
            )
            ||
            (
                component_length == 2u
                &&
                component[0] == '.'
                &&
                component[1] == '.'
            )
        )
        {
            return false;
        }

        length += component_length;

        if (*end == '/')
        {
            length++;
            component = end + 1;
        }
        else
        {
            component = end;
        }

        if (length >= RTPGK_PATH_MAX)
        {
            return false;
        }
    }

    return true;
}

bool rtpgk_validate(
    const void* data,
    usize size,
    const RtpgkHeader** header_output
)
{
    if (header_output != NULL)
    {
        *header_output = NULL;
    }

    if (
        data == NULL
        ||
        size < sizeof(RtpgkHeader)
    )
    {
        return false;
    }

    const RtpgkHeader* header =
        (const RtpgkHeader*)data;

    for (u32 i = 0; i < RTPGK_MAGIC_SIZE; i++)
    {
        if (header->magic[i] != rtpgk_magic[i])
        {
            return false;
        }
    }

    if (
        header->version != RTPGK_VERSION
        ||
        header->header_size != RTPGK_HEADER_SIZE
        ||
        header->package_type != RTPGK_TYPE_APPLICATION
        ||
        header->entry_count == 0u
        ||
        header->entry_count > 128u
        ||
        header->table_offset < RTPGK_HEADER_SIZE
        ||
        header->total_size != size
    )
    {
        return false;
    }

    usize table_bytes =
        (usize)header->entry_count
        *
        sizeof(RtpgkEntry);

    if (
        !range_valid(
            header->table_offset,
            table_bytes,
            size
        )
        ||
        header->data_offset
        <
        header->table_offset + table_bytes
        ||
        header->data_offset > size
    )
    {
        return false;
    }

    if (
        !fixed_string_terminated(
            header->name,
            sizeof(header->name)
        )
        ||
        !fixed_string_terminated(
            header->version_text,
            sizeof(header->version_text)
        )
        ||
        !fixed_string_terminated(
            header->application_entry,
            sizeof(header->application_entry)
        )
        ||
        !rtpgk_safe_name(header->name)
        ||
        !rtpgk_safe_relative_path(header->application_entry)
    )
    {
        return false;
    }

    for (u32 i = 0; i < header->entry_count; i++)
    {
        const RtpgkEntry* entry =
            (const RtpgkEntry*)(
                (const u8*)data
                +
                header->table_offset
                +
                (usize)i * sizeof(RtpgkEntry)
            );

        if (
            !fixed_string_terminated(
                entry->path,
                sizeof(entry->path)
            )
            ||
            !rtpgk_safe_relative_path(entry->path)
            ||
            entry->flags != 0u
            ||
            entry->data_offset < header->data_offset
            ||
            !range_valid(
                entry->data_offset,
                entry->size,
                size
            )
        )
        {
            return false;
        }
    }

    if (header_output != NULL)
    {
        *header_output = header;
    }

    return true;
}

bool rtpgk_entry(
    const void* data,
    usize size,
    const RtpgkHeader* header,
    u32 index,
    const RtpgkEntry** entry_output,
    const u8** payload_output
)
{
    if (entry_output != NULL)
    {
        *entry_output = NULL;
    }

    if (payload_output != NULL)
    {
        *payload_output = NULL;
    }

    if (
        data == NULL
        ||
        header == NULL
        ||
        index >= header->entry_count
    )
    {
        return false;
    }

    usize entry_offset =
        header->table_offset
        +
        (usize)index * sizeof(RtpgkEntry);

    if (
        !range_valid(
            entry_offset,
            sizeof(RtpgkEntry),
            size
        )
    )
    {
        return false;
    }

    const RtpgkEntry* entry =
        (const RtpgkEntry*)((const u8*)data + entry_offset);

    if (
        !range_valid(
            entry->data_offset,
            entry->size,
            size
        )
    )
    {
        return false;
    }

    if (entry_output != NULL)
    {
        *entry_output = entry;
    }

    if (payload_output != NULL)
    {
        *payload_output =
            (const u8*)data
            +
            entry->data_offset;
    }

    return true;
}
