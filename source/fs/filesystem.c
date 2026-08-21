#include "filesystem.h"

#include "terminal.h"

#include "types.h"
#include "memory.h"
#include "string.h"
#include "path.h"
#include "heap.h"

#include "rootfs_disk.h"
#include "system_config.h"


/*
 * ============================================================
 * LIMITS
 * ============================================================
 */

#define FS_MAX_NODES ROOTFS_DISK_MAX_NODES


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

    usize size;

    /*
     * Only used by the emergency RAM backend.
     * Persistent RootFS files live on disk and keep this NULL.
     */
    char* ram_content;

} FsNode;


/*
 * ============================================================
 * INITIAL FILESYSTEM
 * ============================================================
 *
 * These are the directories that belong to the RootOS base.
 * Desktop/Music/Pictures/Videos/etc. are intentionally omitted:
 * desktop/user-directory packages may create them later.
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
    { "/home/user/Documents",               FS_DIRECTORY },
    { "/home/user/Downloads",               FS_DIRECTORY },
    { "/home/user/Projects",                FS_DIRECTORY },
    { "/home/user/bin",                     FS_DIRECTORY },
    { "/home/user/readme.txt",              FS_FILE },

    { "/system",                            FS_DIRECTORY },
    { "/system/bin",                        FS_DIRECTORY },
    { "/system/bin/shell",                  FS_FILE },
    { "/system/drivers",                    FS_DIRECTORY },
    { "/system/fonts",                      FS_DIRECTORY },
    { "/system/fonts/unifont-17.0.05.otf",  FS_FILE },
    { "/system/libraries",                  FS_DIRECTORY },
    { "/system/services",                   FS_DIRECTORY },
    { "/system/apps",                       FS_DIRECTORY },
    { "/system/version.txt",                FS_FILE },

    { "/config",                            FS_DIRECTORY },
    { "/config/users",                      FS_DIRECTORY },
    { "/config/shell",                      FS_DIRECTORY },
    { "/config/system",                     FS_DIRECTORY },
    { "/config/services",                   FS_DIRECTORY },

    { "/packages",                          FS_DIRECTORY },

    { "/temp",                              FS_DIRECTORY },

    { "/var",                               FS_DIRECTORY },
    { "/var/log",                           FS_DIRECTORY },
    { "/var/log/kernel.log",                FS_FILE },
    { "/var/cache",                         FS_DIRECTORY },
    { "/var/cache/packages",                FS_DIRECTORY },
    { "/var/run",                           FS_DIRECTORY }
};


#define INITIAL_NODE_COUNT \
    (sizeof(initial_nodes) / sizeof(initial_nodes[0]))


/*
 * ============================================================
 * MUTABLE METADATA CACHE
 * ============================================================
 */

static FsNode nodes[
    FS_MAX_NODES
];


static char current_directory[
    ROOT_PATH_MAX
];


/*
 * true when the ROOTFS42 disk volume is mounted and all writes are persistent.
 */
static bool filesystem_persistent_backend = false;


/*
 * Set when a valid-looking backend could not be loaded/written correctly.
 * In that case RootOS falls back to RAM rather than risking unknown storage.
 */
static bool filesystem_storage_error = false;


/*
 * Shared scratch space. The kernel is currently single-threaded, so these
 * buffers avoid large stack allocations without creating permanent 1 MiB
 * per-file storage as the old RAM filesystem did.
 */
static char fs_io_buffer[
    FS_MAX_FILE_SIZE + 1
];


static char fs_copy_buffer[
    FS_MAX_FILE_SIZE + 1
];


/*
 * ============================================================
 * NODE MEMORY HELPERS
 * ============================================================
 */

static void release_ram_content(
    u32 index
)
{
    if (
        index >= FS_MAX_NODES
    )
    {
        return;
    }


    if (
        nodes[index].ram_content != NULL
    )
    {
        root_free(
            nodes[index].ram_content
        );

        nodes[index].ram_content = NULL;
    }
}


static void reset_node(
    u32 index
)
{
    if (
        index >= FS_MAX_NODES
    )
    {
        return;
    }


    release_ram_content(
        index
    );


    root_memzero(
        &nodes[index],
        sizeof(nodes[index])
    );
}


static void reset_all_nodes(void)
{
    for (
        u32 i = 0;
        i < FS_MAX_NODES;
        i++
    )
    {
        reset_node(i);
    }
}


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
 * ROOTFS DISK CONVERSION
 * ============================================================
 */

static RootFsDiskType fs_type_to_disk(
    FsType type
)
{
    return
        type == FS_DIRECTORY
        ?
        ROOTFS_DISK_DIRECTORY
        :
        ROOTFS_DISK_FILE;
}


static FsType disk_type_to_fs(
    RootFsDiskType type
)
{
    return
        type == ROOTFS_DISK_DIRECTORY
        ?
        FS_DIRECTORY
        :
        FS_FILE;
}


static bool persist_node(
    u32 index
)
{
    if (
        !filesystem_persistent_backend
    )
    {
        return true;
    }


    if (
        index >= FS_MAX_NODES
        ||
        !nodes[index].used
    )
    {
        return false;
    }


    RootFsDiskNode disk_node;


    root_memzero(
        &disk_node,
        sizeof(disk_node)
    );


    disk_node.used = true;

    disk_node.type =
        fs_type_to_disk(
            nodes[index].type
        );

    disk_node.size =
        nodes[index].size;


    if (
        root_strlcpy(
            disk_node.path,
            nodes[index].path,
            sizeof(disk_node.path)
        )
        >=
        sizeof(disk_node.path)
    )
    {
        return false;
    }


    if (
        !rootfs_disk_write_node(
            index,
            &disk_node
        )
    )
    {
        filesystem_storage_error = true;
        return false;
    }


    return true;
}


static bool clear_persisted_node(
    u32 index
)
{
    if (
        !filesystem_persistent_backend
    )
    {
        return true;
    }


    if (
        !rootfs_disk_clear_node(
            index
        )
    )
    {
        filesystem_storage_error = true;
        return false;
    }


    return true;
}


/*
 * ============================================================
 * RAM BACKEND
 * ============================================================
 */

static bool ram_write_file(
    u32 index,
    const void* data,
    usize size
)
{
    if (
        index >= FS_MAX_NODES
        ||
        !nodes[index].used
        ||
        nodes[index].type != FS_FILE
        ||
        size > FS_MAX_FILE_SIZE
        ||
        (
            size > 0
            &&
            data == NULL
        )
    )
    {
        return false;
    }


    char* replacement = NULL;


    if (
        size > 0
    )
    {
        replacement =
            (char*)root_malloc(
                size + 1
            );


        if (
            replacement == NULL
        )
        {
            return false;
        }


        root_memcpy(
            replacement,
            data,
            size
        );


        replacement[size] = '\0';
    }


    release_ram_content(
        index
    );


    nodes[index].ram_content =
        replacement;

    nodes[index].size =
        size;


    return true;
}


static bool ram_read_file(
    u32 index,
    void* output,
    usize capacity,
    usize* result_size
)
{
    if (
        result_size != NULL
    )
    {
        *result_size = 0;
    }


    if (
        index >= FS_MAX_NODES
        ||
        output == NULL
        ||
        !nodes[index].used
        ||
        nodes[index].type != FS_FILE
        ||
        nodes[index].size > capacity
    )
    {
        return false;
    }


    if (
        nodes[index].size > 0
    )
    {
        if (
            nodes[index].ram_content == NULL
        )
        {
            return false;
        }


        root_memcpy(
            output,
            nodes[index].ram_content,
            nodes[index].size
        );
    }


    if (
        result_size != NULL
    )
    {
        *result_size =
            nodes[index].size;
    }


    return true;
}


/*
 * ============================================================
 * FILE BACKEND
 * ============================================================
 */

static bool backend_read_file(
    u32 index,
    void* output,
    usize capacity,
    usize* result_size
)
{
    if (
        filesystem_persistent_backend
    )
    {
        if (
            !rootfs_disk_read_file(
                index,
                output,
                capacity,
                result_size
            )
        )
        {
            filesystem_storage_error = true;
            return false;
        }


        return true;
    }


    return
        ram_read_file(
            index,
            output,
            capacity,
            result_size
        );
}


static bool backend_write_file(
    u32 index,
    const void* data,
    usize size
)
{
    if (
        filesystem_persistent_backend
    )
    {
        if (
            !rootfs_disk_write_file(
                index,
                data,
                size
            )
        )
        {
            filesystem_storage_error = true;
            return false;
        }


        nodes[index].size =
            size;


        return true;
    }


    return
        ram_write_file(
            index,
            data,
            size
        );
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
        find_node(
            path
        )
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


    reset_node(
        (u32)index
    );


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

    nodes[index].size =
        0;

    nodes[index].ram_content =
        NULL;

    nodes[index].used =
        true;


    if (
        !persist_node(
            (u32)index
        )
    )
    {
        reset_node(
            (u32)index
        );

        return
            FS_RESULT_IO_ERROR;
    }


    return
        FS_RESULT_OK;
}


/*
 * ============================================================
 * PATH RELATION
 * ============================================================
 */

static bool path_is_inside(
    const char* base,
    const char* candidate
)
{
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


    if (
        length <= 1
    )
    {
        output[0] = '/';
        output[1] = '\0';

        return true;
    }


    output[
        length - 1
    ] = '\0';


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


    if (
        !root
    )
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


/*
 * ============================================================
 * ENSURE DIRECTORY PATH
 * ============================================================
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


    while (
        *cursor == '/'
    )
    {
        cursor++;
    }


    while (
        *cursor
    )
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
        ] = '\0';


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


        if (
            index >= 0
        )
        {
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
 * INITIAL ROOTOS TREE
 * ============================================================
 */

static FsResult create_initial_tree(void)
{
    for (
        usize i = 0;
        i < INITIAL_NODE_COUNT;
        i++
    )
    {
        FsResult result =
            add_node_absolute(
                initial_nodes[i].path,
                initial_nodes[i].type
            );


        if (
            result != FS_RESULT_OK
            &&
            result != FS_RESULT_ALREADY_EXISTS
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
 * LOAD ROOTFS42 METADATA CACHE
 * ============================================================
 */

static bool load_persistent_nodes(void)
{
    bool any_node = false;


    for (
        u32 i = 0;
        i < FS_MAX_NODES;
        i++
    )
    {
        RootFsDiskNode disk_node;


        if (
            !rootfs_disk_read_node(
                i,
                &disk_node
            )
        )
        {
            filesystem_storage_error = true;
            return false;
        }


        if (
            !disk_node.used
        )
        {
            continue;
        }


        if (
            disk_node.path[0] != '/'
            ||
            disk_node.size > FS_MAX_FILE_SIZE
        )
        {
            filesystem_storage_error = true;
            return false;
        }


        nodes[i].used = true;

        nodes[i].type =
            disk_type_to_fs(
                disk_node.type
            );

        nodes[i].size =
            disk_node.size;

        nodes[i].ram_content =
            NULL;


        if (
            root_strlcpy(
                nodes[i].path,
                disk_node.path,
                ROOT_PATH_MAX
            )
            >=
            ROOT_PATH_MAX
        )
        {
            filesystem_storage_error = true;
            return false;
        }


        any_node = true;
    }


    if (
        !any_node
    )
    {
        return
            create_initial_tree()
            ==
            FS_RESULT_OK;
    }


    int root_index =
        find_node(
            "/"
        );


    if (
        root_index < 0
        ||
        nodes[root_index].type != FS_DIRECTORY
    )
    {
        filesystem_storage_error = true;
        return false;
    }


    /*
     * Keep the RootOS base hierarchy forward-compatible. New base directories
     * added by later releases are created if missing without touching user data.
     */
    FsResult tree_result = create_initial_tree();

    if (
        tree_result != FS_RESULT_OK
        &&
        tree_result != FS_RESULT_ALREADY_EXISTS
    )
    {
        filesystem_storage_error = true;
        return false;
    }


    return true;
}


/*
 * ============================================================
 * INITIALIZE
 * ============================================================
 */

void filesystem_init(void)
{
    reset_all_nodes();


    filesystem_storage_error =
        false;


    filesystem_persistent_backend =
        rootfs_disk_ready();


    if (
        filesystem_persistent_backend
    )
    {
        if (
            !load_persistent_nodes()
        )
        {
            /*
             * Never try to repair/overwrite a volume automatically after an
             * unexpected on-disk error. Fall back to a temporary RAM tree.
             */
            filesystem_persistent_backend =
                false;


            reset_all_nodes();

            (void)create_initial_tree();
        }
    }
    else
    {
        (void)create_initial_tree();
    }


    if (
        find_node(
            ROOTOS_DEFAULT_HOME
        )
        >=
        0
    )
    {
        root_strlcpy(
            current_directory,
            ROOTOS_DEFAULT_HOME,
            ROOT_PATH_MAX
        );
    }
    else
    {
        root_strlcpy(
            current_directory,
            "/",
            ROOT_PATH_MAX
        );
    }
}


/*
 * ============================================================
 * BACKEND INFORMATION
 * ============================================================
 */

bool filesystem_is_persistent(void)
{
    return
        filesystem_persistent_backend;
}


bool filesystem_storage_faulted(void)
{
    return
        filesystem_storage_error;
}


usize filesystem_node_count(void)
{
    usize count = 0;


    for (
        usize i = 0;
        i < FS_MAX_NODES;
        i++
    )
    {
        if (
            nodes[i].used
        )
        {
            count++;
        }
    }


    return count;
}


usize filesystem_free_node_count(void)
{
    return
        count_free_nodes();
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
        ||
        nodes[index].type != FS_DIRECTORY
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
        ||
        nodes[directory_index].type != FS_DIRECTORY
    )
    {
        return 0;
    }


    bool found = false;


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

            found = true;
        }
    }


    if (
        !found
    )
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
            ||
            nodes[i].type != FS_DIRECTORY
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


    if (
        nodes[index].type == FS_FILE
    )
    {
        if (
            !clear_persisted_node(
                (u32)index
            )
        )
        {
            return
                FS_RESULT_IO_ERROR;
        }


        reset_node(
            (u32)index
        );


        return
            FS_RESULT_OK;
    }


    bool has_children = false;


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
            has_children = true;
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
            if (
                !clear_persisted_node(
                    (u32)i
                )
            )
            {
                return
                    FS_RESULT_IO_ERROR;
            }


            reset_node(
                (u32)i
            );
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


    if (
        nodes[source_index].type == FS_DIRECTORY
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


        usize target_length =
            root_strlen(
                target
            );


        root_strlcpy(
            target + target_length,
            suffix,
            ROOT_PATH_MAX - target_length
        );


        FsType source_type =
            nodes[i].type;

        usize source_size =
            nodes[i].size;


        if (
            source_type == FS_FILE
            &&
            source_size > 0
        )
        {
            usize read_size = 0;


            if (
                !backend_read_file(
                    (u32)i,
                    fs_copy_buffer,
                    FS_MAX_FILE_SIZE,
                    &read_size
                )
                ||
                read_size != source_size
            )
            {
                return
                    FS_RESULT_IO_ERROR;
            }
        }


        FsResult result =
            add_node_absolute(
                target,
                source_type
            );


        if (
            result
            !=
            FS_RESULT_OK
        )
        {
            return result;
        }


        if (
            source_type == FS_FILE
        )
        {
            int target_index =
                find_node(
                    target
                );


            if (
                target_index < 0
            )
            {
                return
                    FS_RESULT_IO_ERROR;
            }


            if (
                !backend_write_file(
                    (u32)target_index,
                    fs_copy_buffer,
                    source_size
                )
            )
            {
                return
                    FS_RESULT_IO_ERROR;
            }
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
        nodes[source_index].type == FS_DIRECTORY
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


    bool update_current =
        path_is_inside(
            source_path,
            current_directory
        );


    char new_current[
        ROOT_PATH_MAX
    ];


    if (
        update_current
    )
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


        char old_path[
            ROOT_PATH_MAX
        ];

        char target[
            ROOT_PATH_MAX
        ];


        root_strlcpy(
            old_path,
            nodes[i].path,
            ROOT_PATH_MAX
        );


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


        if (
            !persist_node(
                (u32)i
            )
        )
        {
            root_strlcpy(
                nodes[i].path,
                old_path,
                ROOT_PATH_MAX
            );

            return
                FS_RESULT_IO_ERROR;
        }
    }


    if (
        update_current
    )
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


/*
 * ============================================================
 * READ FILE
 * ============================================================
 */

FsResult filesystem_read_file(
    const char* path,
    char* output,
    usize output_size,
    usize* result_size
)
{
    if (
        result_size != NULL
    )
    {
        *result_size = 0;
    }


    if (
        path == NULL
        ||
        output == NULL
        ||
        output_size == 0
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


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


    if (
        nodes[index].type
        !=
        FS_FILE
    )
    {
        return
            FS_RESULT_NOT_FILE;
    }


    if (
        nodes[index].size + 1
        >
        output_size
    )
    {
        return
            FS_RESULT_NO_SPACE;
    }


    usize size = 0;


    if (
        !backend_read_file(
            (u32)index,
            output,
            output_size - 1,
            &size
        )
    )
    {
        return
            FS_RESULT_IO_ERROR;
    }


    output[size] = '\0';


    if (
        result_size != NULL
    )
    {
        *result_size = size;
    }


    return
        FS_RESULT_OK;
}


/*
 * ============================================================
 * WRITE FILE
 * ============================================================
 */

FsResult filesystem_write_file(
    const char* path,
    const char* data,
    usize size
)
{
    if (
        path == NULL
        ||
        (
            data == NULL
            &&
            size != 0
        )
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    if (
        size
        >
        FS_MAX_FILE_SIZE
    )
    {
        return
            FS_RESULT_FILE_TOO_LARGE;
    }


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


    if (
        nodes[index].type
        !=
        FS_FILE
    )
    {
        return
            FS_RESULT_NOT_FILE;
    }


    if (
        !backend_write_file(
            (u32)index,
            data,
            size
        )
    )
    {
        return
            filesystem_persistent_backend
            ?
            FS_RESULT_IO_ERROR
            :
            FS_RESULT_NO_SPACE;
    }


    return
        FS_RESULT_OK;
}


/*
 * ============================================================
 * APPEND FILE
 * ============================================================
 */

FsResult filesystem_append_file(
    const char* path,
    const char* data,
    usize size
)
{
    if (
        path == NULL
        ||
        (
            data == NULL
            &&
            size != 0
        )
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


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


    if (
        nodes[index].type
        !=
        FS_FILE
    )
    {
        return
            FS_RESULT_NOT_FILE;
    }


    if (
        nodes[index].size
        +
        size
        >
        FS_MAX_FILE_SIZE
    )
    {
        return
            FS_RESULT_FILE_TOO_LARGE;
    }


    usize old_size = 0;


    if (
        !backend_read_file(
            (u32)index,
            fs_io_buffer,
            FS_MAX_FILE_SIZE,
            &old_size
        )
    )
    {
        return
            FS_RESULT_IO_ERROR;
    }


    if (
        size > 0
    )
    {
        root_memcpy(
            fs_io_buffer + old_size,
            data,
            size
        );
    }


    usize new_size =
        old_size
        +
        size;


    if (
        !backend_write_file(
            (u32)index,
            fs_io_buffer,
            new_size
        )
    )
    {
        return
            filesystem_persistent_backend
            ?
            FS_RESULT_IO_ERROR
            :
            FS_RESULT_NO_SPACE;
    }


    return
        FS_RESULT_OK;
}


/*
 * ============================================================
 * FILE SIZE
 * ============================================================
 */

bool filesystem_file_size(
    const char* path,
    usize* size
)
{
    if (
        path == NULL
        ||
        size == NULL
    )
    {
        return false;
    }

    *size = 0;

    char resolved[ROOT_PATH_MAX];

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

    int index = find_node(resolved);

    if (
        index < 0
        ||
        nodes[index].type != FS_FILE
    )
    {
        return false;
    }

    *size = nodes[index].size;
    return true;
}
