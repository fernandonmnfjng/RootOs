#include "app_manager.h"

#include "filesystem.h"
#include "string.h"
#include "memory.h"
#include "path.h"

static char manifest_buffer[ROOT_APP_MANIFEST_MAX + 1u];

static bool safe_app_name(const char* name)
{
    if (name == NULL || name[0] == '\0')
    {
        return false;
    }

    usize length = 0;

    while (name[length] != '\0')
    {
        char c = name[length];

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

        if (!allowed || length >= ROOT_APP_NAME_MAX - 1u)
        {
            return false;
        }

        length++;
    }

    return true;
}

static bool safe_relative_path(const char* path)
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

    const char* cursor = path;

    while (*cursor != '\0')
    {
        if (
            cursor[0] == '.'
            &&
            cursor[1] == '.'
            &&
            (
                cursor[2] == '\0'
                ||
                cursor[2] == '/'
            )
        )
        {
            return false;
        }

        while (*cursor != '\0' && *cursor != '/')
        {
            cursor++;
        }

        if (*cursor == '/')
        {
            cursor++;
        }
    }

    return true;
}

static bool manifest_get_value(
    const char* text,
    const char* wanted_section,
    const char* wanted_key,
    char* output,
    usize output_size
)
{
    if (
        text == NULL
        ||
        wanted_section == NULL
        ||
        wanted_key == NULL
        ||
        output == NULL
        ||
        output_size == 0u
    )
    {
        return false;
    }

    output[0] = '\0';
    bool section_active = false;
    const char* cursor = text;

    while (*cursor != '\0')
    {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r')
        {
            cursor++;
        }

        const char* line_start = cursor;
        const char* line_end = cursor;

        while (*line_end != '\0' && *line_end != '\n')
        {
            line_end++;
        }

        if (
            line_start < line_end
            &&
            *line_start != '#'
        )
        {
            if (*line_start == '[')
            {
                const char* close = line_start + 1;

                while (close < line_end && *close != ']')
                {
                    close++;
                }

                if (close < line_end)
                {
                    usize section_length =
                        (usize)(close - (line_start + 1));
                    usize wanted_length = root_strlen(wanted_section);

                    section_active =
                        section_length == wanted_length
                        &&
                        root_strncmp(
                            line_start + 1,
                            wanted_section,
                            wanted_length
                        ) == 0;
                }
            }
            else if (section_active)
            {
                const char* key_end = line_start;

                while (
                    key_end < line_end
                    &&
                    *key_end != '='
                    &&
                    *key_end != ' '
                    &&
                    *key_end != '\t'
                )
                {
                    key_end++;
                }

                usize key_length = (usize)(key_end - line_start);
                usize wanted_length = root_strlen(wanted_key);

                if (
                    key_length == wanted_length
                    &&
                    root_strncmp(
                        line_start,
                        wanted_key,
                        wanted_length
                    ) == 0
                )
                {
                    const char* value = key_end;

                    while (
                        value < line_end
                        &&
                        (*value == ' ' || *value == '\t')
                    )
                    {
                        value++;
                    }

                    if (value >= line_end || *value != '=')
                    {
                        return false;
                    }

                    value++;

                    while (
                        value < line_end
                        &&
                        (*value == ' ' || *value == '\t')
                    )
                    {
                        value++;
                    }

                    if (value >= line_end || *value != '"')
                    {
                        return false;
                    }

                    value++;
                    usize length = 0u;

                    while (
                        value < line_end
                        &&
                        *value != '"'
                    )
                    {
                        if (length + 1u >= output_size)
                        {
                            return false;
                        }

                        output[length++] = *value++;
                    }

                    if (value >= line_end || *value != '"')
                    {
                        return false;
                    }

                    output[length] = '\0';
                    return true;
                }
            }
        }

        cursor = line_end;

        if (*cursor == '\n')
        {
            cursor++;
        }
    }

    return false;
}

static bool looks_like_path(const char* target)
{
    if (target == NULL)
    {
        return false;
    }

    for (usize i = 0; target[i] != '\0'; i++)
    {
        if (target[i] == '/')
        {
            return true;
        }
    }

    usize length = root_strlen(target);

    return
        length >= 4u
        &&
        root_streq(target + length - 4u, ".elf");
}

void app_manager_init(void)
{
    manifest_buffer[0] = '\0';
}

RootAppRunInfo app_manager_run(
    const char* target,
    int argc,
    const char** argv
)
{
    RootAppRunInfo info;
    root_memzero(&info, sizeof(info));
    info.result = ROOT_APP_RUN_NOT_FOUND;
    info.elf_result = ROOT_ELF_NOT_FOUND;
    info.exit_code = -1;

    if (target == NULL || target[0] == '\0')
    {
        info.result = ROOT_APP_RUN_INVALID_NAME;
        return info;
    }

    char process_name[ROOT_APP_NAME_MAX];
    process_name[0] = '\0';

    if (looks_like_path(target))
    {
        if (
            root_strlcpy(
                info.executable,
                target,
                sizeof(info.executable)
            )
            >=
            sizeof(info.executable)
        )
        {
            info.result = ROOT_APP_RUN_INVALID_NAME;
            return info;
        }

        root_strlcpy(
            process_name,
            root_path_basename(target),
            sizeof(process_name)
        );
    }
    else
    {
        if (!safe_app_name(target))
        {
            info.result = ROOT_APP_RUN_INVALID_NAME;
            return info;
        }

        char manifest_path[512];
        usize used = 0u;

        const char* prefix = "/system/apps/";
        root_strlcpy(manifest_path, prefix, sizeof(manifest_path));
        used = root_strlen(manifest_path);

        if (
            root_strlcpy(
                manifest_path + used,
                target,
                sizeof(manifest_path) - used
            )
            >=
            sizeof(manifest_path) - used
        )
        {
            info.result = ROOT_APP_RUN_INVALID_NAME;
            return info;
        }

        used = root_strlen(manifest_path);

        if (
            root_strlcpy(
                manifest_path + used,
                "/app.toml",
                sizeof(manifest_path) - used
            )
            >=
            sizeof(manifest_path) - used
        )
        {
            info.result = ROOT_APP_RUN_INVALID_NAME;
            return info;
        }

        usize manifest_size = 0u;

        if (
            filesystem_read_file(
                manifest_path,
                manifest_buffer,
                sizeof(manifest_buffer),
                &manifest_size
            )
            !=
            FS_RESULT_OK
        )
        {
            info.result = ROOT_APP_RUN_NOT_FOUND;
            return info;
        }

        manifest_buffer[manifest_size] = '\0';

        char executable[256];

        if (
            !manifest_get_value(
                manifest_buffer,
                "application",
                "executable",
                executable,
                sizeof(executable)
            )
            ||
            !safe_relative_path(executable)
        )
        {
            info.result = ROOT_APP_RUN_BAD_MANIFEST;
            return info;
        }

        root_strlcpy(
            info.executable,
            "/system/apps/",
            sizeof(info.executable)
        );

        used = root_strlen(info.executable);
        root_strlcpy(
            info.executable + used,
            target,
            sizeof(info.executable) - used
        );

        used = root_strlen(info.executable);

        if (used + 1u >= sizeof(info.executable))
        {
            info.result = ROOT_APP_RUN_BAD_MANIFEST;
            return info;
        }

        info.executable[used++] = '/';
        info.executable[used] = '\0';

        if (
            root_strlcpy(
                info.executable + used,
                executable,
                sizeof(info.executable) - used
            )
            >=
            sizeof(info.executable) - used
        )
        {
            info.result = ROOT_APP_RUN_BAD_MANIFEST;
            return info;
        }

        root_strlcpy(
            process_name,
            target,
            sizeof(process_name)
        );
    }

    info.elf_result = elf_loader_run(
        info.executable,
        process_name,
        argc,
        argv,
        &info.exit_code,
        &info.pid
    );

    info.result =
        info.elf_result == ROOT_ELF_OK
        ?
        ROOT_APP_RUN_OK
        :
        ROOT_APP_RUN_ELF_ERROR;

    return info;
}
