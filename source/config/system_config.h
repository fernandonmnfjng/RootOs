#ifndef ROOTOS_SYSTEM_CONFIG_H
#define ROOTOS_SYSTEM_CONFIG_H


/*
 * ============================================================
 * IDENTIDAD DEL SISTEMA
 * ============================================================
 *
 * Por ahora estos valores son compile-time.
 *
 * Más adelante compOs podrá generar este archivo
 * automáticamente basándose en build.toml.
 */

#define ROOTOS_NAME "RootOS"

#define ROOTOS_VERSION_MAJOR 0
#define ROOTOS_VERSION_MINOR 36
#define ROOTOS_VERSION_PATCH 0

#define ROOTOS_VERSION_STRING "0.36"

#define ROOTOS_BUILD_TYPE "Development Build"


/*
 * ============================================================
 * USUARIO PREDETERMINADO
 * ============================================================
 */

#define ROOTOS_DEFAULT_USER "user"

#define ROOTOS_DEFAULT_HOME "/home/user"

#define ROOTOS_ROOT_USER "root"


/*
 * ============================================================
 * HOSTNAME
 * ============================================================
 */

#define ROOTOS_HOSTNAME "RootOS"


/*
 * ============================================================
 * BANNER
 * ============================================================
 */

#define ROOTOS_BANNER_SEPARATOR \
    "--------------------------------------------------"

#define ROOTOS_BANNER_HELP \
    "Type 'help' to view available commands."


const char* rootos_name(void);

const char* rootos_version(void);

const char* rootos_build_type(void);

const char* rootos_hostname(void);

const char* rootos_default_user(void);

const char* rootos_default_home(void);


#endif
