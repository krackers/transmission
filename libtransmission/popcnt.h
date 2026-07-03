/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "tr-macros.h"

#ifdef _MSC_VER
  #include <intrin.h>
#endif

#if !defined(__x86_64__) && !defined(_M_X64) && !defined(_M_AMD64)
  #error "Popcnt fallback requires 64-bit x86 (x86-64) architecture"
#endif

#define CPUID_POPCNT_MASK_ECX (1 << 23)
#define CPUID_QUERY_FEATURES_EAX 1

/* 0: unchecked, 1: fallback, 2: hardware supported */
static _Atomic int g_popcnt_state = 0; 

static inline void run_cpuid(int eax, int ecx, int* abcd)
{
#ifdef _MSC_VER
    __cpuidex(abcd, eax, ecx);
#else
    int ebx = 0;
    int edx = 0;

    // Volatile not needed since just doing feature detection
    __asm__ ("cpuid" : "+a" (eax), "=b" (ebx), "+c" (ecx), "=d" (edx));

    abcd[0] = eax; abcd[1] = ebx; abcd[2] = ecx; abcd[3] = edx;
#endif
}


/* Don't inline heavy initialization done only once */
static TR_NOINLINE int tr_system_init_popcnt(void)
{
    int abcd[4] = {0, 0, 0, 0};
    run_cpuid(CPUID_QUERY_FEATURES_EAX, 0, abcd);
    
    int s = ((abcd[2] & CPUID_POPCNT_MASK_ECX) != 0) ? 2 : 1;
    atomic_store_explicit(&g_popcnt_state, s, memory_order_relaxed);
    
    return s;
}

static TR_FORCE_INLINE bool tr_system_has_popcnt(void)
{
    int s = atomic_load_explicit(&g_popcnt_state, memory_order_relaxed);
    
    /* Initialization happens only once, hint as unlikely. Race/double initialization is fine */
    if (TR_UNLIKELY(s == 0))
    {
        s = tr_system_init_popcnt();
    }
    
    return s == 2;
}

static TR_FORCE_INLINE uint64_t popcnt64_hw(uint64_t x)
{
#ifdef _MSC_VER
    // msvc doesn't support inline assembly
    return __popcnt64(x);
#else
    /* Using 'x' as both input and output breaks false dependencies on older Intel chips */
    __asm__ ("popcnt %0, %0" : "+r" (x) : : "cc");
    return x;
#endif
}

/**
* popcount64c algorithm from https://en.wikipedia.org/wiki/Hamming_weight#Efficient_implementation 
* Optimized for CPUs with fast multiplication
*/
static TR_FORCE_INLINE uint64_t popcnt64_sw(uint64_t x)
{
    uint64_t m1 = 0x5555555555555555ull;
    uint64_t m2 = 0x3333333333333333ull;
    uint64_t m4 = 0x0F0F0F0F0F0F0F0Full;
    uint64_t h01 = 0x0101010101010101ull;

    x -= (x >> 1) & m1;
    x = (x & m2) + ((x >> 2) & m2);
    x = (x + (x >> 4)) & m4;

    return (x * h01) >> 56;
}

static size_t tr_popcount_hw(const void* data, size_t size)
{
    size_t cnt0 = 0, cnt1 = 0;
    const uint8_t* ptr = (const uint8_t*)data;
    size_t i = 0;

    /* Process 128 bits (16 bytes) at a time using dual accumulators for ILP */
    for (; i + 16 <= size; i += 16)
    {
        uint64_t w0, w1;
        memcpy(&w0, ptr + i, sizeof(w0));
        memcpy(&w1, ptr + i + 8, sizeof(w1));
        cnt0 += popcnt64_hw(w0);
        cnt1 += popcnt64_hw(w1);
    }
    
    /* Process 64 bits (8 bytes) at atime */
    for (; i + 8 <= size; i += 8)
    {
        uint64_t w0;
        memcpy(&w0, ptr + i, sizeof(w0));
        cnt0 += popcnt64_hw(w0);
    }
    
    /* Process tail bytes - aggregated into a single branchless popcnt */
    if (i < size)
    {
        uint64_t val = 0;
        size_t bytes = size - i;
        for (size_t j = 0; j < bytes; j++)
            val |= ((uint64_t)ptr[i + j]) << (j * 8);
        cnt0 += popcnt64_hw(val);
    }
    
    return cnt0 + cnt1;
}


static size_t tr_popcount_sw(const void* data, size_t size)
{
    size_t cnt0 = 0, cnt1 = 0;
    const uint8_t* ptr = (const uint8_t*)data;
    size_t i = 0;

    for (; i + 16 <= size; i += 16)
    {
        uint64_t w0, w1;
        memcpy(&w0, ptr + i, sizeof(w0));
        memcpy(&w1, ptr + i + 8, sizeof(w1));
        cnt0 += popcnt64_sw(w0);
        cnt1 += popcnt64_sw(w1);
    }
    
    for (; i + 8 <= size; i += 8)
    {
        uint64_t w0;
        memcpy(&w0, ptr + i, sizeof(w0));
        cnt0 += popcnt64_sw(w0);
    }
    
    if (i < size)
    {
        uint64_t val = 0;
        size_t bytes = size - i;
        for (size_t j = 0; j < bytes; j++)
            val |= ((uint64_t)ptr[i + j]) << (j * 8);
        cnt0 += popcnt64_sw(val);
    }
    
    return cnt0 + cnt1;
}

/*
 * Count the number of set bits in the array of given size in bytes
 */
static TR_FORCE_INLINE size_t tr_popcount(const void* data, size_t size)
{
    return tr_system_has_popcnt() ? tr_popcount_hw(data, size) : tr_popcount_sw(data, size);
}

static TR_FORCE_INLINE uint64_t tr_popcount_byte(uint8_t val)
{
    // Forcing a zero-extended promotion followed by mask to
    // ensure upper bits are properly cleared.
    // Avoids any possible compiler optimizations that would skip the zero-extension.
    uint64_t const zext = (uint64_t)val & 0xFFU;
    return tr_system_has_popcnt() ? popcnt64_hw(zext) : popcnt64_sw(zext);
}
