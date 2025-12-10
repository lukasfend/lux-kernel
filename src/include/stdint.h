/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Fixed-width integer typedefs for the kernel toolchain.
 */
#pragma once

typedef signed char int8_t;
typedef unsigned char uint8_t;

typedef signed short int16_t;
typedef unsigned short uint16_t;

typedef signed int int32_t;
typedef unsigned int uint32_t;

typedef signed long long int64_t;
typedef unsigned long long uint64_t;

typedef int32_t intptr_t;
typedef uint32_t uintptr_t;