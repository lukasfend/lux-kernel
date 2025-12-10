/*
 * Date: 2025-12-10 00:00 UTC
 * Author: Lukas Fend <lukas.fend@outlook.com>
 * Description: Core size, pointer difference, and alignment typedefs.
 */
#pragma once

typedef unsigned int size_t;
typedef int ptrdiff_t;

typedef unsigned int wchar_t;

typedef long double max_align_t;

#ifndef NULL
#define NULL ((void *)0)
#endif