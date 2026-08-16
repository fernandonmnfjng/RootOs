#include "path.h"
#include "string.h"


static bool path_set_root(
    char* path,
    usize size
)
{
    if (size < 2)
    {
        return false;
    }


    path[0] = '/';
    path[1] = '\0';


    return true;
}


static void path_pop(
    char* path
)
{
    usize length =
        root_strlen(path);


    /*
     * Ya estamos en /.
     */
    if (length <= 1)
    {
        path[0] = '/';
        path[1] = '\0';

        return;
    }


    while (
        length > 1
        &&
        path[length - 1]
        !=
        '/'
    )
    {
        length--;
    }


    /*
     * /home -> /
     */
    if (length <= 1)
    {
        path[0] = '/';
        path[1] = '\0';

        return;
    }


    /*
     * /home/user -> /home
     */
    path[
        length - 1
    ] = '\0';
}


static bool path_append(
    char* path,
    usize path_size,
    const char* component
)
{
    usize path_length =
        root_strlen(path);

    usize component_length =
        root_strlen(component);


    bool root =
        path_length == 1
        &&
        path[0] == '/';


    usize required =
        path_length
        +
        component_length
        +
        (root ? 0 : 1)
        +
        1;


    if (
        required
        >
        path_size
    )
    {
        return false;
    }


    if (!root)
    {
        path[
            path_length
        ] = '/';

        path_length++;
    }


    for (
        usize i = 0;
        i < component_length;
        i++
    )
    {
        path[
            path_length + i
        ] =
            component[i];
    }


    path[
        path_length
        +
        component_length
    ] = '\0';


    return true;
}


bool root_path_resolve(
    const char* current_directory,
    const char* input,
    char* output,
    usize output_size
)
{
    if (
        output == NULL
        ||
        output_size < 2
    )
    {
        return false;
    }


    /*
     * Sin argumento:
     *
     * conservar carpeta actual.
     */
    if (
        input == NULL
        ||
        input[0] == '\0'
    )
    {
        return
            root_strlcpy(
                output,
                current_directory,
                output_size
            )
            <
            output_size;
    }


    const char* cursor =
        input;


    /*
     * Ruta absoluta.
     */
    if (
        cursor[0] == '/'
    )
    {
        if (
            !path_set_root(
                output,
                output_size
            )
        )
        {
            return false;
        }


        while (
            *cursor == '/'
        )
        {
            cursor++;
        }
    }


    /*
     * Home del usuario.
     *
     * ~
     * ~/Documents
     */
    else if (
        cursor[0] == '~'
        &&
        (
            cursor[1] == '\0'
            ||
            cursor[1] == '/'
        )
    )
    {
        if (
            root_strlcpy(
                output,
                ROOTOS_DEFAULT_HOME,
                output_size
            )
            >=
            output_size
        )
        {
            return false;
        }


        cursor++;


        while (
            *cursor == '/'
        )
        {
            cursor++;
        }
    }


    /*
     * Ruta relativa.
     */
    else
    {
        if (
            root_strlcpy(
                output,
                current_directory,
                output_size
            )
            >=
            output_size
        )
        {
            return false;
        }
    }


    while (*cursor)
    {
        /*
         * Ignorar ///.
         */
        while (
            *cursor == '/'
        )
        {
            cursor++;
        }


        if (!*cursor)
        {
            break;
        }


        char component[
            ROOT_NAME_MAX
        ];


        usize length = 0;


        while (
            *cursor
            &&
            *cursor != '/'
        )
        {
            if (
                length
                >=
                ROOT_NAME_MAX - 1
            )
            {
                return false;
            }


            component[length] =
                *cursor;


            length++;

            cursor++;
        }


        component[length] =
            '\0';


        /*
         * .
         */
        if (
            root_streq(
                component,
                "."
            )
        )
        {
            continue;
        }


        /*
         * ..
         */
        if (
            root_streq(
                component,
                ".."
            )
        )
        {
            path_pop(output);

            continue;
        }


        if (
            !path_append(
                output,
                output_size,
                component
            )
        )
        {
            return false;
        }
    }


    return true;
}


const char* root_path_basename(
    const char* path
)
{
    if (
        path == NULL
        ||
        path[0] == '\0'
    )
    {
        return "";
    }


    const char* result =
        path;


    while (*path)
    {
        if (
            *path == '/'
            &&
            path[1] != '\0'
        )
        {
            result =
                path + 1;
        }


        path++;
    }


    return result;
}


bool root_path_is_direct_child(
    const char* parent,
    const char* child
)
{
    const char* remainder;


    /*
     * /
     */
    if (
        parent[0] == '/'
        &&
        parent[1] == '\0'
    )
    {
        if (
            child[0] != '/'
        )
        {
            return false;
        }


        remainder =
            child + 1;
    }


    else
    {
        usize parent_length =
            root_strlen(parent);


        if (
            root_strncmp(
                child,
                parent,
                parent_length
            )
            !=
            0
        )
        {
            return false;
        }


        if (
            child[
                parent_length
            ]
            !=
            '/'
        )
        {
            return false;
        }


        remainder =
            child
            +
            parent_length
            +
            1;
    }


    if (
        *remainder == '\0'
    )
    {
        return false;
    }


    while (*remainder)
    {
        if (
            *remainder == '/'
        )
        {
            return false;
        }


        remainder++;
    }


    return true;
}
