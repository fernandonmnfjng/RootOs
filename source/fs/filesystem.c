#include "filesystem.h"

#include "terminal.h"

#include "types.h"
#include "memory.h"
#include "string.h"
#include "path.h"

#include "system_config.h"


/*
 * ============================================================
 * LIMITS
 * ============================================================
 */

#define FS_MAX_NODES 256


/*
 * ============================================================
 * NODE TYPES
 * ============================================================
 */

typedef enum
{
    FS_DIRECTORY,
    FS_FILE

} FsType;


typedef struct
{
    bool used;

    FsType type;

    char path[
        ROOT_PATH_MAX
    ];

} FsNode;


/*
 * ============================================================
 * INITIAL FILESYSTEM
 * ============================================================
 *
 * Esto representa el RootOS base.
 *
 * filesystem_init() lo copia a la tabla mutable.
 */

typedef struct
{
    const char* path;

    FsType type;

} InitialFsNode;


static const InitialFsNode initial_nodes[] =
{
    { "/",                                  FS_DIRECTORY },

    { "/boot",                              FS_DIRECTORY },
    { "/boot/kernel.elf",                   FS_FILE },

    { "/home",                              FS_DIRECTORY },
    { "/home/user",                         FS_DIRECTORY },

    { "/home/user/projects",                FS_DIRECTORY },
    { "/home/user/projects/os",             FS_DIRECTORY },

    { "/home/user/readme.txt",              FS_FILE },

    { "/home/user/bin",                     FS_DIRECTORY },
    { "/home/user/bin/demo",                FS_FILE },

    { "/system",                            FS_DIRECTORY },

    { "/system/bin",                        FS_DIRECTORY },
    { "/system/bin/shell",                  FS_FILE },

    { "/system/drivers",                    FS_DIRECTORY },

    { "/system/fonts",                      FS_DIRECTORY },
    { "/system/fonts/unifont-17.0.05.otf",  FS_FILE },

    { "/system/libraries",                  FS_DIRECTORY },

    { "/system/version.txt",                FS_FILE },

    { "/config",                            FS_DIRECTORY },
    { "/config/users",                      FS_DIRECTORY },
    { "/config/shell",                      FS_DIRECTORY },
    { "/config/system",                     FS_DIRECTORY },

    { "/packages",                          FS_DIRECTORY },

    { "/temp",                              FS_DIRECTORY },

    { "/var",                               FS_DIRECTORY },
    { "/var/log",                           FS_DIRECTORY },
    { "/var/log/kernel.log",                FS_FILE }
};


#define INITIAL_NODE_COUNT \
    (sizeof(initial_nodes) / sizeof(initial_nodes[0]))


/*
 * ============================================================
 * MUTABLE FILESYSTEM
 * ============================================================
 */

static FsNode nodes[
    FS_MAX_NODES
];


static char current_directory[
    ROOT_PATH_MAX
];


/*
 * ============================================================
 * FIND NODE
 * ============================================================
 */

static int find_node(
    const char* path
)
{
    for (
        usize i = 0;
        i < FS_MAX_NODES;
        i++
    )
    {
        if (
            !nodes[i].used
        )
        {
            continue;
        }


        if (
            root_streq(
                nodes[i].path,
                path
            )
        )
        {
            return (int)i;
        }
    }


    return -1;
}


/*
 * ============================================================
 * FREE SLOT
 * ============================================================
 */

static int find_free_node(void)
{
    for (
        usize i = 0;
        i < FS_MAX_NODES;
        i++
    )
    {
        if (
            !nodes[i].used
        )
        {
            return (int)i;
        }
    }


    return -1;
}


/*
 * ============================================================
 * FREE NODE COUNT
 * ============================================================
 */

static usize count_free_nodes(void)
{
    usize count = 0;


    for (
        usize i = 0;
        i < FS_MAX_NODES;
        i++
    )
    {
        if (
            !nodes[i].used
        )
        {
            count++;
        }
    }


    return count;
}


/*
 * ============================================================
 * ADD NODE
 * ============================================================
 */

static FsResult add_node_absolute(
    const char* path,
    FsType type
)
{
    if (
        find_node(path)
        >=
        0
    )
    {
        return
            FS_RESULT_ALREADY_EXISTS;
    }


    int index =
        find_free_node();


    if (
        index < 0
    )
    {
        return
            FS_RESULT_NO_SPACE;
    }


    if (
        root_strlcpy(
            nodes[index].path,
            path,
            ROOT_PATH_MAX
        )
        >=
        ROOT_PATH_MAX
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    nodes[index].type =
        type;


    nodes[index].used =
        true;


    return
        FS_RESULT_OK;
}


/*
 * ============================================================
 * PATH RELATION
 * ============================================================
 *
 * true:
 *
 * base:
 *     /home/user
 *
 * candidate:
 *     /home/user
 *     /home/user/a
 *     /home/user/a/b
 */

static bool path_is_inside(
    const char* base,
    const char* candidate
)
{
    /*
     * Root contiene todo.
     */
    if (
        root_streq(
            base,
            "/"
        )
    )
    {
        return
            candidate[0]
            ==
            '/';
    }


    usize length =
        root_strlen(
            base
        );


    if (
        root_strncmp(
            base,
            candidate,
            length
        )
        !=
        0
    )
    {
        return false;
    }


    return
        candidate[length] == '\0'
        ||
        candidate[length] == '/';
}


/*
 * ============================================================
 * PARENT PATH
 * ============================================================
 */

static bool get_parent_path(
    const char* path,
    char* output
)
{
    if (
        path == NULL
        ||
        output == NULL
    )
    {
        return false;
    }


    if (
        root_streq(
            path,
            "/"
        )
    )
    {
        return false;
    }


    if (
        root_strlcpy(
            output,
            path,
            ROOT_PATH_MAX
        )
        >=
        ROOT_PATH_MAX
    )
    {
        return false;
    }


    usize length =
        root_strlen(
            output
        );


    while (
        length > 0
        &&
        output[
            length - 1
        ]
        !=
        '/'
    )
    {
        length--;
    }


    /*
     * /archivo
     *
     * parent = /
     */
    if (
        length <= 1
    )
    {
        output[0] = '/';
        output[1] = '\0';

        return true;
    }


    /*
     * /home/user/file
     *
     * ->
     *
     * /home/user
     */

    output[
        length - 1
    ] =
        '\0';


    return true;
}


/*
 * ============================================================
 * APPEND PATH COMPONENT
 * ============================================================
 */

static bool append_component(
    char* path,
    const char* component
)
{
    usize path_length =
        root_strlen(
            path
        );


    usize component_length =
        root_strlen(
            component
        );


    bool root =
        root_streq(
            path,
            "/"
        );


    usize required =
        path_length
        +
        component_length
        +
        (
            root
            ?
            0
            :
            1
        )
        +
        1;


    if (
        required
        >
        ROOT_PATH_MAX
    )
    {
        return false;
    }


    if (!root)
    {
        path[
            path_length
        ] =
            '/';

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
    ] =
        '\0';


    return true;
}


/*
 * ============================================================
 * ENSURE DIRECTORY PATH
 * ============================================================
 *
 * Crea automáticamente carpetas intermedias.
 *
 * Ejemplo:
 *
 * /home/user/a/b/c
 *
 * si a/b/c no existen:
 *
 * crea:
 *
 * /home/user/a
 * /home/user/a/b
 * /home/user/a/b/c
 */

static FsResult ensure_directory_absolute(
    const char* absolute_path
)
{
    if (
        root_streq(
            absolute_path,
            "/"
        )
    )
    {
        return
            FS_RESULT_OK;
    }


    char current[
        ROOT_PATH_MAX
    ] = "/";


    const char* cursor =
        absolute_path;


    /*
     * Saltar / inicial.
     */
    while (
        *cursor == '/'
    )
    {
        cursor++;
    }


    while (*cursor)
    {
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
                return
                    FS_RESULT_INVALID_PATH;
            }


            component[
                length++
            ] =
                *cursor;


            cursor++;
        }


        component[
            length
        ] =
            '\0';


        while (
            *cursor == '/'
        )
        {
            cursor++;
        }


        if (
            !append_component(
                current,
                component
            )
        )
        {
            return
                FS_RESULT_INVALID_PATH;
        }


        int index =
            find_node(
                current
            );


        /*
         * Ya existe.
         */
        if (
            index >= 0
        )
        {
            /*
             * Pero tiene que ser carpeta.
             */
            if (
                nodes[index].type
                !=
                FS_DIRECTORY
            )
            {
                return
                    FS_RESULT_NOT_DIRECTORY;
            }


            continue;
        }


        FsResult result =
            add_node_absolute(
                current,
                FS_DIRECTORY
            );


        if (
            result
            !=
            FS_RESULT_OK
        )
        {
            return result;
        }
    }


    return
        FS_RESULT_OK;
}


/*
 * ============================================================
 * INITIALIZE
 * ============================================================
 */

void filesystem_init(void)
{
    root_memzero(
        nodes,
        sizeof(nodes)
    );


    for (
        usize i = 0;
        i < INITIAL_NODE_COUNT;
        i++
    )
    {
        add_node_absolute(
            initial_nodes[i].path,
            initial_nodes[i].type
        );
    }


    root_strlcpy(
        current_directory,
        ROOTOS_DEFAULT_HOME,
        ROOT_PATH_MAX
    );
}


/*
 * ============================================================
 * EXISTS
 * ============================================================
 */

bool filesystem_exists(
    const char* path
)
{
    char resolved[
        ROOT_PATH_MAX
    ];


    if (
        !root_path_resolve(
            current_directory,
            path,
            resolved,
            ROOT_PATH_MAX
        )
    )
    {
        return false;
    }


    return
        find_node(
            resolved
        )
        >=
        0;
}


/*
 * ============================================================
 * IS DIRECTORY
 * ============================================================
 */

bool filesystem_is_directory(
    const char* path
)
{
    char resolved[
        ROOT_PATH_MAX
    ];


    if (
        !root_path_resolve(
            current_directory,
            path,
            resolved,
            ROOT_PATH_MAX
        )
    )
    {
        return false;
    }


    int index =
        find_node(
            resolved
        );


    if (
        index < 0
    )
    {
        return false;
    }


    return
        nodes[index].type
        ==
        FS_DIRECTORY;
}


/*
 * ============================================================
 * CHANGE DIRECTORY
 * ============================================================
 */

int filesystem_change_directory(
    const char* path
)
{
    char resolved[
        ROOT_PATH_MAX
    ];


    if (
        !root_path_resolve(
            current_directory,
            path,
            resolved,
            ROOT_PATH_MAX
        )
    )
    {
        return 0;
    }


    int index =
        find_node(
            resolved
        );


    if (
        index < 0
    )
    {
        return 0;
    }


    if (
        nodes[index].type
        !=
        FS_DIRECTORY
    )
    {
        return 0;
    }


    root_strlcpy(
        current_directory,
        resolved,
        ROOT_PATH_MAX
    );


    return 1;
}


/*
 * ============================================================
 * CURRENT DIRECTORY
 * ============================================================
 */

void filesystem_print_current_directory(void)
{
    terminal_print(
        current_directory
    );

    terminal_putchar(
        '\n'
    );
}


const char* filesystem_current_directory(void)
{
    return
        current_directory;
}


/*
 * ============================================================
 * LIST DIRECTORY
 * ============================================================
 */

int filesystem_list(
    const char* path
)
{
    char resolved[
        ROOT_PATH_MAX
    ];


    if (
        !root_path_resolve(
            current_directory,
            path,
            resolved,
            ROOT_PATH_MAX
        )
    )
    {
        return 0;
    }


    int directory_index =
        find_node(
            resolved
        );


    if (
        directory_index < 0
    )
    {
        return 0;
    }


    if (
        nodes[directory_index].type
        !=
        FS_DIRECTORY
    )
    {
        return 0;
    }


    bool found =
        false;


    for (
        usize i = 0;
        i < FS_MAX_NODES;
        i++
    )
    {
        if (
            !nodes[i].used
        )
        {
            continue;
        }


        if (
            root_path_is_direct_child(
                resolved,
                nodes[i].path
            )
        )
        {
            terminal_print(
                root_path_basename(
                    nodes[i].path
                )
            );


            if (
                nodes[i].type
                ==
                FS_DIRECTORY
            )
            {
                terminal_putchar(
                    '/'
                );
            }


            terminal_putchar(
                '\n'
            );


            found =
                true;
        }
    }


    if (!found)
    {
        terminal_print(
            "(vacio)\n"
        );
    }


    return 1;
}


/*
 * ============================================================
 * FIND DIRECTORIES
 * ============================================================
 */

int filesystem_find_directories(
    const char* name
)
{
    int count = 0;


    if (
        name == NULL
        ||
        name[0] == '\0'
    )
    {
        return 0;
    }


    for (
        usize i = 0;
        i < FS_MAX_NODES;
        i++
    )
    {
        if (
            !nodes[i].used
        )
        {
            continue;
        }


        if (
            nodes[i].type
            !=
            FS_DIRECTORY
        )
        {
            continue;
        }


        if (
            root_streq(
                root_path_basename(
                    nodes[i].path
                ),
                name
            )
        )
        {
            terminal_print(
                nodes[i].path
            );

            terminal_putchar(
                '\n'
            );


            count++;
        }
    }


    return count;
}


/*
 * ============================================================
 * CREATE DIRECTORY
 * ============================================================
 */

FsResult filesystem_create_directory(
    const char* path
)
{
    char resolved[
        ROOT_PATH_MAX
    ];


    if (
        path == NULL
        ||
        path[0] == '\0'
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    if (
        !root_path_resolve(
            current_directory,
            path,
            resolved,
            ROOT_PATH_MAX
        )
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    if (
        find_node(
            resolved
        )
        >=
        0
    )
    {
        return
            FS_RESULT_ALREADY_EXISTS;
    }


    /*
     * Crea también todos los padres.
     */
    return
        ensure_directory_absolute(
            resolved
        );
}


/*
 * ============================================================
 * CREATE FILE
 * ============================================================
 */

FsResult filesystem_create_file(
    const char* path
)
{
    char resolved[
        ROOT_PATH_MAX
    ];


    if (
        path == NULL
        ||
        path[0] == '\0'
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    if (
        !root_path_resolve(
            current_directory,
            path,
            resolved,
            ROOT_PATH_MAX
        )
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    if (
        root_streq(
            resolved,
            "/"
        )
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    if (
        find_node(
            resolved
        )
        >=
        0
    )
    {
        return
            FS_RESULT_ALREADY_EXISTS;
    }


    char parent[
        ROOT_PATH_MAX
    ];


    if (
        !get_parent_path(
            resolved,
            parent
        )
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    /*
     * Crear carpetas intermedias.
     */
    FsResult parent_result =
        ensure_directory_absolute(
            parent
        );


    if (
        parent_result
        !=
        FS_RESULT_OK
    )
    {
        return
            parent_result;
    }


    return
        add_node_absolute(
            resolved,
            FS_FILE
        );
}


/*
 * ============================================================
 * REMOVE
 * ============================================================
 */

FsResult filesystem_remove(
    const char* path,
    bool recursive
)
{
    char resolved[
        ROOT_PATH_MAX
    ];


    if (
        !root_path_resolve(
            current_directory,
            path,
            resolved,
            ROOT_PATH_MAX
        )
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    /*
     * Nunca eliminar /.
     */
    if (
        root_streq(
            resolved,
            "/"
        )
    )
    {
        return
            FS_RESULT_BUSY;
    }


    int index =
        find_node(
            resolved
        );


    if (
        index < 0
    )
    {
        return
            FS_RESULT_NOT_FOUND;
    }


    /*
     * No podemos borrar la carpeta
     * actual ni uno de sus padres.
     */
    if (
        path_is_inside(
            resolved,
            current_directory
        )
    )
    {
        return
            FS_RESULT_BUSY;
    }


    /*
     * Archivo.
     */
    if (
        nodes[index].type
        ==
        FS_FILE
    )
    {
        nodes[index].used =
            false;


        return
            FS_RESULT_OK;
    }


    /*
     * Directorio.
     *
     * Comprobar si tiene contenido.
     */
    bool has_children =
        false;


    for (
        usize i = 0;
        i < FS_MAX_NODES;
        i++
    )
    {
        if (
            !nodes[i].used
            ||
            (int)i == index
        )
        {
            continue;
        }


        if (
            path_is_inside(
                resolved,
                nodes[i].path
            )
        )
        {
            has_children =
                true;

            break;
        }
    }


    if (
        has_children
        &&
        !recursive
    )
    {
        return
            FS_RESULT_DIRECTORY_NOT_EMPTY;
    }


    /*
     * Recursive:
     * borrar carpeta y descendientes.
     */
    for (
        usize i = 0;
        i < FS_MAX_NODES;
        i++
    )
    {
        if (
            !nodes[i].used
        )
        {
            continue;
        }


        if (
            path_is_inside(
                resolved,
                nodes[i].path
            )
        )
        {
            nodes[i].used =
                false;
        }
    }


    return
        FS_RESULT_OK;
}


/*
 * ============================================================
 * COPY
 * ============================================================
 */

FsResult filesystem_copy(
    const char* source,
    const char* destination
)
{
    char source_path[
        ROOT_PATH_MAX
    ];


    char destination_path[
        ROOT_PATH_MAX
    ];


    if (
        !root_path_resolve(
            current_directory,
            source,
            source_path,
            ROOT_PATH_MAX
        )
        ||
        !root_path_resolve(
            current_directory,
            destination,
            destination_path,
            ROOT_PATH_MAX
        )
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    int source_index =
        find_node(
            source_path
        );


    if (
        source_index < 0
    )
    {
        return
            FS_RESULT_NOT_FOUND;
    }


    if (
        find_node(
            destination_path
        )
        >=
        0
    )
    {
        return
            FS_RESULT_ALREADY_EXISTS;
    }


    /*
     * No copiar un directorio dentro
     * de sí mismo.
     */
    if (
        nodes[source_index].type
            ==
            FS_DIRECTORY
        &&
        path_is_inside(
            source_path,
            destination_path
        )
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    /*
     * Crear padres del destino.
     */
    char parent[
        ROOT_PATH_MAX
    ];


    if (
        !get_parent_path(
            destination_path,
            parent
        )
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    FsResult parent_result =
        ensure_directory_absolute(
            parent
        );


    if (
        parent_result
        !=
        FS_RESULT_OK
    )
    {
        return
            parent_result;
    }


    /*
     * Contar cuántos nodos vamos a copiar.
     */
    usize required = 0;


    for (
        usize i = 0;
        i < FS_MAX_NODES;
        i++
    )
    {
        if (
            nodes[i].used
            &&
            path_is_inside(
                source_path,
                nodes[i].path
            )
        )
        {
            required++;
        }
    }


    if (
        count_free_nodes()
        <
        required
    )
    {
        return
            FS_RESULT_NO_SPACE;
    }


    usize source_length =
        root_strlen(
            source_path
        );


    /*
     * Validar primero longitudes.
     */
    for (
        usize i = 0;
        i < FS_MAX_NODES;
        i++
    )
    {
        if (
            !nodes[i].used
            ||
            !path_is_inside(
                source_path,
                nodes[i].path
            )
        )
        {
            continue;
        }


        const char* suffix =
            nodes[i].path
            +
            source_length;


        if (
            root_strlen(
                destination_path
            )
            +
            root_strlen(
                suffix
            )
            >=
            ROOT_PATH_MAX
        )
        {
            return
                FS_RESULT_INVALID_PATH;
        }
    }


    /*
     * Copiar.
     */
    usize original_count =
        FS_MAX_NODES;


    for (
        usize i = 0;
        i < original_count;
        i++
    )
    {
        if (
            !nodes[i].used
            ||
            !path_is_inside(
                source_path,
                nodes[i].path
            )
        )
        {
            continue;
        }


        /*
         * Las copias están fuera de source,
         * por lo tanto no serán procesadas de nuevo.
         */

        const char* suffix =
            nodes[i].path
            +
            source_length;


        char target[
            ROOT_PATH_MAX
        ];


        root_strlcpy(
            target,
            destination_path,
            ROOT_PATH_MAX
        );


        usize target_length =
            root_strlen(
                target
            );


        root_strlcpy(
            target + target_length,
            suffix,
            ROOT_PATH_MAX - target_length
        );


        FsResult result =
            add_node_absolute(
                target,
                nodes[i].type
            );


        if (
            result
            !=
            FS_RESULT_OK
        )
        {
            return result;
        }
    }


    return
        FS_RESULT_OK;
}


/*
 * ============================================================
 * MOVE
 * ============================================================
 */

FsResult filesystem_move(
    const char* source,
    const char* destination
)
{
    char source_path[
        ROOT_PATH_MAX
    ];


    char destination_path[
        ROOT_PATH_MAX
    ];


    if (
        !root_path_resolve(
            current_directory,
            source,
            source_path,
            ROOT_PATH_MAX
        )
        ||
        !root_path_resolve(
            current_directory,
            destination,
            destination_path,
            ROOT_PATH_MAX
        )
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    int source_index =
        find_node(
            source_path
        );


    if (
        source_index < 0
    )
    {
        return
            FS_RESULT_NOT_FOUND;
    }


    if (
        root_streq(
            source_path,
            "/"
        )
    )
    {
        return
            FS_RESULT_BUSY;
    }


    if (
        find_node(
            destination_path
        )
        >=
        0
    )
    {
        return
            FS_RESULT_ALREADY_EXISTS;
    }


    if (
        nodes[source_index].type
            ==
            FS_DIRECTORY
        &&
        path_is_inside(
            source_path,
            destination_path
        )
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    /*
     * Crear padres de destino.
     */
    char parent[
        ROOT_PATH_MAX
    ];


    if (
        !get_parent_path(
            destination_path,
            parent
        )
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    FsResult parent_result =
        ensure_directory_absolute(
            parent
        );


    if (
        parent_result
        !=
        FS_RESULT_OK
    )
    {
        return
            parent_result;
    }


    usize source_length =
        root_strlen(
            source_path
        );


    /*
     * Validar todas las nuevas rutas.
     */
    for (
        usize i = 0;
        i < FS_MAX_NODES;
        i++
    )
    {
        if (
            !nodes[i].used
            ||
            !path_is_inside(
                source_path,
                nodes[i].path
            )
        )
        {
            continue;
        }


        const char* suffix =
            nodes[i].path
            +
            source_length;


        if (
            root_strlen(
                destination_path
            )
            +
            root_strlen(
                suffix
            )
            >=
            ROOT_PATH_MAX
        )
        {
            return
                FS_RESULT_INVALID_PATH;
        }
    }


    /*
     * Si estamos dentro del directorio movido,
     * actualizar también cwd.
     */
    bool update_current =
        path_is_inside(
            source_path,
            current_directory
        );


    char new_current[
        ROOT_PATH_MAX
    ];


    if (update_current)
    {
        const char* current_suffix =
            current_directory
            +
            source_length;


        root_strlcpy(
            new_current,
            destination_path,
            ROOT_PATH_MAX
        );


        usize length =
            root_strlen(
                new_current
            );


        root_strlcpy(
            new_current + length,
            current_suffix,
            ROOT_PATH_MAX - length
        );
    }


    /*
     * Renombrar fuente y descendientes.
     */
    for (
        usize i = 0;
        i < FS_MAX_NODES;
        i++
    )
    {
        if (
            !nodes[i].used
            ||
            !path_is_inside(
                source_path,
                nodes[i].path
            )
        )
        {
            continue;
        }


        const char* suffix =
            nodes[i].path
            +
            source_length;


        char target[
            ROOT_PATH_MAX
        ];


        root_strlcpy(
            target,
            destination_path,
            ROOT_PATH_MAX
        );


        usize length =
            root_strlen(
                target
            );


        root_strlcpy(
            target + length,
            suffix,
            ROOT_PATH_MAX - length
        );


        root_strlcpy(
            nodes[i].path,
            target,
            ROOT_PATH_MAX
        );
    }


    if (update_current)
    {
        root_strlcpy(
            current_directory,
            new_current,
            ROOT_PATH_MAX
        );
    }


    return
        FS_RESULT_OK;
}