#include "package_manager.h"

#include "filesystem.h"
#include "heap.h"
#include "memory.h"
#include "string.h"

#define PACKAGE_DB_PATH "/packages/installed.db"
#define PACKAGE_DB_MAX 16384u

static RootPackageInfo installed[ROOT_PACKAGE_MAX_INSTALLED];
static usize installed_count = 0u;
static char db_buffer[PACKAGE_DB_MAX + 1u];

static bool text_append(
    char* output,
    usize capacity,
    usize* used,
    const char* text
)
{
    if (
        output == NULL
        ||
        used == NULL
        ||
        text == NULL
    )
    {
        return false;
    }

    usize length = root_strlen(text);

    if (
        *used > capacity
        ||
        length >= capacity - *used
    )
    {
        return false;
    }

    root_memcpy(
        output + *used,
        text,
        length
    );

    *used += length;
    output[*used] = '\0';
    return true;
}

static bool package_database_save(void)
{
    usize used = 0u;
    db_buffer[0] = '\0';

    for (usize i = 0; i < installed_count; i++)
    {
        if (
            !text_append(
                db_buffer,
                sizeof(db_buffer),
                &used,
                installed[i].name
            )
            ||
            !text_append(db_buffer, sizeof(db_buffer), &used, "|")
            ||
            !text_append(
                db_buffer,
                sizeof(db_buffer),
                &used,
                installed[i].version
            )
            ||
            !text_append(db_buffer, sizeof(db_buffer), &used, "|")
            ||
            !text_append(
                db_buffer,
                sizeof(db_buffer),
                &used,
                installed[i].entry
            )
            ||
            !text_append(db_buffer, sizeof(db_buffer), &used, "\n")
        )
        {
            return false;
        }
    }

    if (!filesystem_exists(PACKAGE_DB_PATH))
    {
        if (
            filesystem_create_file(PACKAGE_DB_PATH)
            !=
            FS_RESULT_OK
        )
        {
            return false;
        }
    }

    return
        filesystem_write_file(
            PACKAGE_DB_PATH,
            db_buffer,
            used
        )
        ==
        FS_RESULT_OK;
}

static bool parse_field(
    const char** cursor,
    char delimiter,
    char* output,
    usize capacity
)
{
    if (
        cursor == NULL
        ||
        *cursor == NULL
        ||
        output == NULL
        ||
        capacity == 0u
    )
    {
        return false;
    }

    usize length = 0u;
    const char* text = *cursor;

    while (
        *text != '\0'
        &&
        *text != delimiter
        &&
        *text != '\n'
        &&
        *text != '\r'
    )
    {
        if (length + 1u >= capacity)
        {
            return false;
        }

        output[length++] = *text++;
    }

    output[length] = '\0';

    if (*text != delimiter)
    {
        return false;
    }

    *cursor = text + 1;
    return length > 0u;
}

static bool package_database_load(void)
{
    installed_count = 0u;
    root_memzero(installed, sizeof(installed));

    if (!filesystem_exists(PACKAGE_DB_PATH))
    {
        FsResult create = filesystem_create_file(PACKAGE_DB_PATH);

        return
            create == FS_RESULT_OK
            ||
            create == FS_RESULT_ALREADY_EXISTS;
    }

    usize size = 0u;

    if (
        filesystem_read_file(
            PACKAGE_DB_PATH,
            db_buffer,
            sizeof(db_buffer),
            &size
        )
        !=
        FS_RESULT_OK
    )
    {
        return false;
    }

    db_buffer[size] = '\0';
    const char* cursor = db_buffer;

    while (*cursor != '\0')
    {
        while (*cursor == '\n' || *cursor == '\r')
        {
            cursor++;
        }

        if (*cursor == '\0')
        {
            break;
        }

        if (installed_count >= ROOT_PACKAGE_MAX_INSTALLED)
        {
            return false;
        }

        RootPackageInfo* info = &installed[installed_count];

        if (
            !parse_field(
                &cursor,
                '|',
                info->name,
                sizeof(info->name)
            )
            ||
            !parse_field(
                &cursor,
                '|',
                info->version,
                sizeof(info->version)
            )
        )
        {
            return false;
        }

        usize entry_length = 0u;

        while (
            *cursor != '\0'
            &&
            *cursor != '\n'
            &&
            *cursor != '\r'
        )
        {
            if (entry_length + 1u >= sizeof(info->entry))
            {
                return false;
            }

            info->entry[entry_length++] = *cursor++;
        }

        info->entry[entry_length] = '\0';

        if (
            !rtpgk_safe_name(info->name)
            ||
            !rtpgk_safe_relative_path(info->entry)
            ||
            info->version[0] == '\0'
        )
        {
            return false;
        }

        installed_count++;

        while (*cursor == '\n' || *cursor == '\r')
        {
            cursor++;
        }
    }

    return true;
}

static bool destination_path(
    const char* package_name,
    const char* relative,
    char* output,
    usize capacity
)
{
    if (
        package_name == NULL
        ||
        relative == NULL
        ||
        output == NULL
        ||
        capacity == 0u
    )
    {
        return false;
    }

    output[0] = '\0';
    usize used = 0u;

    return
        text_append(output, capacity, &used, "/system/apps/")
        &&
        text_append(output, capacity, &used, package_name)
        &&
        text_append(output, capacity, &used, "/")
        &&
        text_append(output, capacity, &used, relative);
}

void package_manager_init(void)
{
    (void)package_database_load();
}

bool package_find(
    const char* name,
    RootPackageInfo* output
)
{
    if (name == NULL)
    {
        return false;
    }

    for (usize i = 0; i < installed_count; i++)
    {
        if (root_streq(installed[i].name, name))
        {
            if (output != NULL)
            {
                *output = installed[i];
            }

            return true;
        }
    }

    return false;
}

RootPackageResult package_install(
    const char* package_path
)
{
    if (package_path == NULL || package_path[0] == '\0')
    {
        return ROOT_PACKAGE_NOT_FOUND;
    }

    usize size = 0u;

    if (!filesystem_file_size(package_path, &size))
    {
        return ROOT_PACKAGE_NOT_FOUND;
    }

    if (
        size < sizeof(RtpgkHeader)
        ||
        size > FS_MAX_FILE_SIZE
    )
    {
        return ROOT_PACKAGE_INVALID;
    }

    u8* package_data = (u8*)root_malloc(size + 1u);

    if (package_data == NULL)
    {
        return ROOT_PACKAGE_NO_MEMORY;
    }

    usize loaded = 0u;

    if (
        filesystem_read_file(
            package_path,
            (char*)package_data,
            size + 1u,
            &loaded
        )
        !=
        FS_RESULT_OK
        ||
        loaded != size
    )
    {
        root_free(package_data);
        return ROOT_PACKAGE_IO_ERROR;
    }

    const RtpgkHeader* header = NULL;

    if (!rtpgk_validate(package_data, size, &header))
    {
        root_free(package_data);
        return ROOT_PACKAGE_INVALID;
    }

    if (package_find(header->name, NULL))
    {
        root_free(package_data);
        return ROOT_PACKAGE_ALREADY_INSTALLED;
    }

    if (installed_count >= ROOT_PACKAGE_MAX_INSTALLED)
    {
        root_free(package_data);
        return ROOT_PACKAGE_NO_SPACE;
    }

    char install_root[512];
    root_strlcpy(
        install_root,
        "/system/apps/",
        sizeof(install_root)
    );

    usize root_length = root_strlen(install_root);

    if (
        root_strlcpy(
            install_root + root_length,
            header->name,
            sizeof(install_root) - root_length
        )
        >=
        sizeof(install_root) - root_length
    )
    {
        root_free(package_data);
        return ROOT_PACKAGE_INVALID;
    }

    if (filesystem_exists(install_root))
    {
        root_free(package_data);
        return ROOT_PACKAGE_ALREADY_INSTALLED;
    }

    FsResult create_root = filesystem_create_directory(install_root);

    if (
        create_root != FS_RESULT_OK
        &&
        create_root != FS_RESULT_ALREADY_EXISTS
    )
    {
        root_free(package_data);
        return ROOT_PACKAGE_IO_ERROR;
    }

    bool has_manifest = false;
    bool has_entry = false;

    for (u32 i = 0; i < header->entry_count; i++)
    {
        const RtpgkEntry* entry = NULL;
        const u8* payload = NULL;

        if (
            !rtpgk_entry(
                package_data,
                size,
                header,
                i,
                &entry,
                &payload
            )
        )
        {
            (void)filesystem_remove(install_root, true);
            root_free(package_data);
            return ROOT_PACKAGE_INVALID;
        }

        if (root_streq(entry->path, "app.toml"))
        {
            has_manifest = true;
        }

        if (root_streq(entry->path, header->application_entry))
        {
            has_entry = true;
        }

        char destination[512];

        if (
            !destination_path(
                header->name,
                entry->path,
                destination,
                sizeof(destination)
            )
        )
        {
            (void)filesystem_remove(install_root, true);
            root_free(package_data);
            return ROOT_PACKAGE_INVALID;
        }

        FsResult create = filesystem_create_file(destination);

        if (
            create != FS_RESULT_OK
            &&
            create != FS_RESULT_ALREADY_EXISTS
        )
        {
            (void)filesystem_remove(install_root, true);
            root_free(package_data);
            return ROOT_PACKAGE_NO_SPACE;
        }

        if (
            filesystem_write_file(
                destination,
                (const char*)payload,
                entry->size
            )
            !=
            FS_RESULT_OK
        )
        {
            (void)filesystem_remove(install_root, true);
            root_free(package_data);
            return ROOT_PACKAGE_IO_ERROR;
        }
    }

    if (!has_manifest || !has_entry)
    {
        (void)filesystem_remove(install_root, true);
        root_free(package_data);
        return ROOT_PACKAGE_INVALID;
    }

    RootPackageInfo* record = &installed[installed_count];
    root_memzero(record, sizeof(*record));

    root_strlcpy(
        record->name,
        header->name,
        sizeof(record->name)
    );

    root_strlcpy(
        record->version,
        header->version_text,
        sizeof(record->version)
    );

    root_strlcpy(
        record->entry,
        header->application_entry,
        sizeof(record->entry)
    );

    installed_count++;

    if (!package_database_save())
    {
        installed_count--;
        (void)filesystem_remove(install_root, true);
        root_free(package_data);
        return ROOT_PACKAGE_IO_ERROR;
    }

    root_free(package_data);
    return ROOT_PACKAGE_OK;
}

RootPackageResult package_remove(
    const char* name
)
{
    if (name == NULL || name[0] == '\0')
    {
        return ROOT_PACKAGE_NOT_INSTALLED;
    }

    usize index = installed_count;

    for (usize i = 0; i < installed_count; i++)
    {
        if (root_streq(installed[i].name, name))
        {
            index = i;
            break;
        }
    }

    if (index >= installed_count)
    {
        return ROOT_PACKAGE_NOT_INSTALLED;
    }

    char install_root[512];
    root_strlcpy(
        install_root,
        "/system/apps/",
        sizeof(install_root)
    );

    usize used = root_strlen(install_root);
    root_strlcpy(
        install_root + used,
        installed[index].name,
        sizeof(install_root) - used
    );

    if (filesystem_exists(install_root))
    {
        FsResult removed = filesystem_remove(install_root, true);

        if (removed != FS_RESULT_OK)
        {
            return ROOT_PACKAGE_IO_ERROR;
        }
    }

    for (usize i = index + 1u; i < installed_count; i++)
    {
        installed[i - 1u] = installed[i];
    }

    installed_count--;
    root_memzero(
        &installed[installed_count],
        sizeof(installed[installed_count])
    );

    if (!package_database_save())
    {
        return ROOT_PACKAGE_IO_ERROR;
    }

    return ROOT_PACKAGE_OK;
}

usize package_count(void)
{
    return installed_count;
}

bool package_get(
    usize index,
    RootPackageInfo* output
)
{
    if (
        output == NULL
        ||
        index >= installed_count
    )
    {
        return false;
    }

    *output = installed[index];
    return true;
}

const char* package_result_string(
    RootPackageResult result
)
{
    switch (result)
    {
        case ROOT_PACKAGE_OK:
            return "ok";

        case ROOT_PACKAGE_NOT_FOUND:
            return "package file not found";

        case ROOT_PACKAGE_ALREADY_INSTALLED:
            return "package is already installed";

        case ROOT_PACKAGE_INVALID:
            return "invalid .rtpgk package";

        case ROOT_PACKAGE_IO_ERROR:
            return "filesystem I/O error";

        case ROOT_PACKAGE_NO_MEMORY:
            return "not enough memory";

        case ROOT_PACKAGE_NO_SPACE:
            return "not enough package/filesystem space";

        case ROOT_PACKAGE_NOT_INSTALLED:
            return "package is not installed";

        default:
            return "unknown package manager error";
    }
}
