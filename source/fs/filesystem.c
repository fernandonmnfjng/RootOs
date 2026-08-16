#include "filesystem.h"
#include "terminal.h"

#include "types.h"
#include "string.h"
#include "path.h"
#include "system_config.h"

/*
 * ============================================================
 * TIPOS DEL FILESYSTEM
 * ============================================================
 */

typedef enum
{
    FS_DIRECTORY,
    FS_FILE

} FsType;


typedef struct
{
    const char* path;
    FsType type;

} FsNode;


/*
 * ============================================================
 * FILESYSTEM VIRTUAL TEMPORAL
 * ============================================================
 *
 * IMPORTANTE:
 *
 * Esto TODAVIA NO lee realmente el contenido de rootfs/
 * ni escribe al disco.
 *
 * Es solamente una representacion temporal en memoria para
 * poder desarrollar:
 *
 *   godir
 *   seedir
 *   see
 *
 * Mas adelante sera reemplazado por:
 *
 *   Block Device
 *       ↓
 *   GPT
 *       ↓
 *   filesystem real
 *       ↓
 *   VFS
 *
 * Pero mantenemos desde ahora una estructura parecida a la
 * futura estructura real de RootOS.
 */

static const FsNode nodes[] =
{
    /*
     * Root
     */
    { "/",                                  FS_DIRECTORY },


    /*
     * Boot
     */
    { "/boot",                              FS_DIRECTORY },
    { "/boot/kernel.elf",                   FS_FILE },


    /*
     * Home
     */
    { "/home",                              FS_DIRECTORY },
    { "/home/user",                         FS_DIRECTORY },

    { "/home/user/projects",                FS_DIRECTORY },
    { "/home/user/projects/os",             FS_DIRECTORY },

    { "/home/user/readme.txt",              FS_FILE },

    { "/home/user/bin",                     FS_DIRECTORY },
    { "/home/user/bin/demo",                FS_FILE },


    /*
     * Sistema
     */
    { "/system",                            FS_DIRECTORY },

    { "/system/bin",                        FS_DIRECTORY },
    { "/system/bin/shell",                  FS_FILE },

    { "/system/drivers",                    FS_DIRECTORY },

    { "/system/fonts",                      FS_DIRECTORY },
    { "/system/fonts/unifont-17.0.05.otf",  FS_FILE },

    { "/system/libraries",                  FS_DIRECTORY },

    { "/system/version.txt",                FS_FILE },


    /*
     * Configuracion
     */
    { "/config",                            FS_DIRECTORY },

    { "/config/users",                      FS_DIRECTORY },

    { "/config/shell",                      FS_DIRECTORY },

    { "/config/system",                     FS_DIRECTORY },


    /*
     * Paquetes
     */
    { "/packages",                          FS_DIRECTORY },


    /*
     * Temporales
     */
    { "/temp",                              FS_DIRECTORY },


    /*
     * Datos variables / logs
     */
    { "/var",                               FS_DIRECTORY },

    { "/var/log",                           FS_DIRECTORY },
    { "/var/log/kernel.log",                FS_FILE }
};


/*
 * Cantidad de nodos.
 *
 * sizeof(nodes)
 *
 * da el tamaño total del array.
 *
 * sizeof(nodes[0])
 *
 * da el tamaño de un elemento.
 *
 * Por lo tanto:
 *
 * tamaño total / tamaño elemento
 *
 * =
 *
 * cantidad de elementos.
 */
#define NODE_COUNT \
    (sizeof(nodes) / sizeof(nodes[0]))


/*
 * ============================================================
 * DIRECTORIO ACTUAL
 * ============================================================
 *
 * ROOTOS_DEFAULT_HOME viene de path.h y actualmente es:
 *
 *     /home/user
 */

static char current_directory[
    ROOT_PATH_MAX
] = ROOTOS_DEFAULT_HOME;


/*
 * ============================================================
 * BUSCAR UN NODO
 * ============================================================
 *
 * Busca una ruta exacta.
 *
 * Ejemplo:
 *
 *     find_node("/home/user")
 *
 * Si existe:
 *
 *     devuelve su indice.
 *
 * Si no existe:
 *
 *     devuelve -1.
 */

static int find_node(
    const char* path
)
{
    for (
        usize i = 0;
        i < NODE_COUNT;
        i++
    )
    {
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
 * API PUBLICA DEL FILESYSTEM
 * ============================================================
 */


/*
 * Inicializar filesystem.
 *
 * Por ahora simplemente comenzamos en:
 *
 *     /home/user
 */

void filesystem_init(void)
{
    root_strlcpy(
        current_directory,
        ROOTOS_DEFAULT_HOME,
        ROOT_PATH_MAX
    );
}


/*
 * ============================================================
 * CAMBIAR DIRECTORIO
 * ============================================================
 *
 * Ejemplos:
 *
 *     godir /
 *
 *     godir /system
 *
 *     godir projects
 *
 *     godir ..
 *
 *     godir ~
 */

int filesystem_change_directory(
    const char* path
)
{
    char resolved[
        ROOT_PATH_MAX
    ];


    /*
     * Convertir cualquier tipo de ruta:
     *
     * projects
     * ../
     * /
     * ~
     * /system/bin
     *
     * a una ruta absoluta normal.
     */
    if (
        !root_path_resolve(
            current_directory,
            path,
            resolved,
            ROOT_PATH_MAX
        )
    )
    {
        /*
         * Ruta demasiado larga
         * o invalida.
         */
        return 0;
    }


    int index =
        find_node(
            resolved
        );


    /*
     * La ruta no existe.
     */
    if (
        index < 0
    )
    {
        return 0;
    }


    /*
     * Existe pero es un archivo,
     * no un directorio.
     */
    if (
        nodes[index].type
        !=
        FS_DIRECTORY
    )
    {
        return 0;
    }


    /*
     * Cambiar directorio actual.
     */
    root_strlcpy(
        current_directory,
        resolved,
        ROOT_PATH_MAX
    );


    return 1;
}


/*
 * ============================================================
 * MOSTRAR DIRECTORIO ACTUAL
 * ============================================================
 *
 * Usado por:
 *
 *     seedir
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


/*
 * ============================================================
 * LISTAR DIRECTORIO
 * ============================================================
 *
 * Ejemplo:
 *
 *     see
 *
 * o:
 *
 *     see /system
 */

int filesystem_list(
    const char* path
)
{
    char resolved[
        ROOT_PATH_MAX
    ];


    /*
     * path puede ser:
     *
     * ""
     *
     * En ese caso root_path_resolve()
     * usa current_directory.
     */
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


    /*
     * Comprobar que existe.
     */
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


    /*
     * Comprobar que realmente
     * sea un directorio.
     */
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


    /*
     * Buscar únicamente hijos directos.
     *
     * Ejemplo:
     *
     * /home
     *
     * mostrara:
     *
     * user/
     *
     * pero NO:
     *
     * user/projects/
     * user/projects/os/
     */
    for (
        usize i = 0;
        i < NODE_COUNT;
        i++
    )
    {
        if (
            root_path_is_direct_child(
                resolved,
                nodes[i].path
            )
        )
        {
            const char* name =
                root_path_basename(
                    nodes[i].path
                );


            terminal_print(
                name
            );


            /*
             * Diferenciar visualmente
             * carpetas de archivos.
             */
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


    /*
     * Directorio existente pero vacío.
     */
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
 * BUSCAR DIRECTORIOS POR NOMBRE
 * ============================================================
 *
 * Usado por:
 *
 *     seedir("bin")
 *
 * Por ejemplo podría mostrar:
 *
 *     /home/user/bin
 *     /system/bin
 */

int filesystem_find_directories(
    const char* name
)
{
    int count = 0;


    /*
     * Nombre vacío.
     */
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
        i < NODE_COUNT;
        i++
    )
    {
        /*
         * Ignorar archivos.
         */
        if (
            nodes[i].type
            !=
            FS_DIRECTORY
        )
        {
            continue;
        }


        /*
         * Obtener solamente el último
         * componente de la ruta.
         *
         * /system/bin
         *
         * ->
         *
         * bin
         */
        const char* node_name =
            root_path_basename(
                nodes[i].path
            );


        /*
         * ¿Tiene el nombre buscado?
         */
        if (
            root_streq(
                node_name,
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
 * OBTENER DIRECTORIO ACTUAL
 * ============================================================
 *
 * Se usa, por ejemplo, para construir:
 *
 *     user@RootOS:/home/user$
 */

const char* filesystem_current_directory(void)
{
    return current_directory;
}