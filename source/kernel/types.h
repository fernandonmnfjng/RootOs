#ifndef ROOTOS_TYPES_H
#define ROOTOS_TYPES_H


typedef unsigned char u8;
typedef signed char i8;

typedef unsigned short u16;
typedef signed short i16;

typedef unsigned int u32;
typedef signed int i32;

typedef unsigned long long u64;
typedef signed long long i64;


/*
 * Tamaño natural de la arquitectura.
 *
 * En nuestro RootOS actual de 32 bits:
 *     usize = 32 bits
 *
 * Cuando pasemos a x86_64:
 *     usize = 64 bits
 */
typedef unsigned long usize;
typedef signed long isize;


/*
 * Boolean propio.
 */
typedef enum
{
    false = 0,
    true = 1

} bool;


#define NULL ((void*)0)


#endif
