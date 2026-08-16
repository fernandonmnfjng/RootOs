#ifndef ROOTOS_UNICODE_H
#define ROOTOS_UNICODE_H

#include "types.h"


typedef u32 RootCodepoint;


#define ROOT_UNICODE_REPLACEMENT 0xFFFD


bool root_unicode_valid(
    RootCodepoint codepoint
);


/*
 * Convierte un codepoint a UTF-8.
 *
 * output necesita mínimo 4 bytes.
 *
 * Devuelve cantidad de bytes:
 * 1, 2, 3 o 4.
 */
usize root_utf8_encode(
    RootCodepoint codepoint,
    char output[4]
);


/*
 * Lee un carácter UTF-8.
 *
 * Devuelve cantidad de bytes consumidos.
 *
 * 0 = secuencia inválida.
 */
usize root_utf8_decode(
    const char* input,
    usize available,
    RootCodepoint* output
);


#endif
