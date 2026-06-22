/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h> /* memset, memcpy */

#include "transmission.h"
#include "bitfield.h"
#include "tr-assert.h"
#include "utils.h" /* tr_new0() */

/* Portability macros for hardware population count */
#if defined(_MSC_VER)
  #include <intrin.h>
  #define POPCOUNT64(x) (__popcnt64(((uint64_t)(x))))
  #define POPCOUNT32(x) (__popcnt(((uint32_t)(x) & 0xFFFFFFFFU)))
  
  /* POPCNT was introduced in SSE4.2/ABM. Checking for __AVX__ 
     is a safe baseline to guarantee modern hardware instruction generation. */
  #ifndef __AVX__
    #pragma message("Warning: MSVC compiler flag /arch:AVX (or higher) is missing. Hardware POPCNT may not be guaranteed.")
  #endif

#else
  /* Strict compile-time guard for GCC and Clang */
  #if !defined(__POPCNT__) && (defined(__x86_64__) || defined(__i386__))
    #error "Hardware POPCNT is not enabled! Compile with -mpopcnt or -march=x86-64-v2 to prevent severe performance regressions."
  #endif
  
  #define POPCOUNT64(x) (__builtin_popcountll(((uint64_t)(x))))
  #define POPCOUNT32(x) (__builtin_popcount(((uint32_t)(x) & 0xFFFFFFFFU)))
#endif

/* 
 * Handles 8-bit popcounts by forcing a zero-extended 32-bit promotion.
 * Followed by masking to ensure that upper bits are properly cleared.
 * Avoids any possible compiler optimizations that would skip the zero-extension.
 */
#define POPCOUNT8(x) (POPCOUNT32(((uint32_t)(x) & 0xFFU)))

tr_bitfield const TR_BITFIELD_INIT =
{
    .bits = NULL,
    // Number of bytes allocated for the tracked bits
    // May be 0 if all-empty/full optimization.
    .alloc_count = 0,
    // Total number of tracked bits (i.e. size of bit vector)
    // This may be 0 in the case of "unknown length" bitfields
    // where bounds checking becomes disabled and the bitfield
    // grows to any `n` you set in add/rem. It is forbidden
    // to call GetRaw in such a case, since the actual length
    // is semantically unknown.
    .bit_count = 0,
    // Nnumber of bits set to 1
    .true_count = 0,
    // These hints are only used to provide semantic meaning to
    // zero-length (bit_count == 0) bitfields. They are ignored
    // when bit_count > 0.
    .have_all_hint = false,
    .have_none_hint = false
};

/****
*****
****/

static size_t countArray(tr_bitfield const* b)
{
    size_t ret0 = 0;
    size_t ret1 = 0; /* Second accumulator for ILP */
    size_t const bytes = b->alloc_count;
    
    size_t const num_words = bytes / 8;
    uint8_t const* data = b->bits;
    size_t i = 0;

    /* Process 128 bits (16 bytes) at a time using dual accumulators */
    for (; i + 1 < num_words; i += 2)
    {
        uint64_t w0, w1;
        memcpy(&w0, data + (i * 8), sizeof(w0));
        memcpy(&w1, data + ((i + 1) * 8), sizeof(w1));
        
        ret0 += POPCOUNT64(w0);
        ret1 += POPCOUNT64(w1);
    }

    /* Process the remaining 64-bit word if it exists */
    for (; i < num_words; ++i)
    {
        uint64_t word;
        memcpy(&word, data + (i * 8), sizeof(word));
        ret0 += POPCOUNT64(word);
    }

    /* Process the tail bytes */
    size_t const tail_bytes = bytes % 8;
    uint8_t const* tail = data + (num_words * 8);
    for (size_t j = 0; j < tail_bytes; ++j)
    {
        ret0 += POPCOUNT8(tail[j]);
    }

    return ret0 + ret1;
}

static size_t countRange(tr_bitfield const* b, size_t begin, size_t end)
{
    size_t ret = 0;
    size_t const first_byte = begin >> 3U;
    size_t const last_byte = (end - 1) >> 3U;

    if (b->bit_count == 0) return 0;
    if (first_byte >= b->alloc_count) return 0;

    TR_ASSERT(begin < end);
    TR_ASSERT(b->bits != NULL);

    if (first_byte == last_byte)
    {
        uint8_t val = b->bits[first_byte];
        
        size_t const left_shift = begin - (first_byte * 8);
        size_t const right_shift = (last_byte + 1) * 8 - end;
        
        val &= (0xFF >> left_shift);
        val &= (0xFF << right_shift);

        ret += POPCOUNT8(val);
    }
    else
    {
        uint8_t val;
        size_t const walk_end = MIN(b->alloc_count, last_byte);

        /* first byte */
        size_t const first_shift = begin - (first_byte * 8);
        val = b->bits[first_byte];
        val &= (0xFF >> first_shift);
        ret += POPCOUNT8(val);

        /* middle bytes - Optimized chunking, memcpy guarantees alignment */
        size_t i = first_byte + 1;
        while (i + 8 <= walk_end)
        {
            uint64_t word;
            memcpy(&word, b->bits + i, sizeof(word));
            ret += POPCOUNT64(word);
            i += 8;
        }

        /* remaining middle tail bytes */
        while (i < walk_end)
        {
            ret += POPCOUNT8(b->bits[i++]);
        }

        /* last byte */
        if (last_byte < b->alloc_count)
        {
            size_t const last_shift = (last_byte + 1) * 8 - end;
            val = b->bits[last_byte];
            val &= (0xFF << last_shift);
            ret += POPCOUNT8(val);
        }
    }

    TR_ASSERT(ret <= (end - begin));
    return ret;
}

size_t tr_bitfieldCountRange(tr_bitfield const* b, size_t begin, size_t end)
{
    if (begin >= end) return 0;
    if (tr_bitfieldHasAll(b))
    {
        return end - begin;
    }

    if (tr_bitfieldHasNone(b))
    {
        return 0;
    }

    return countRange(b, begin, end);
}

bool tr_bitfieldHas(tr_bitfield const* b, size_t n)
{
    if (tr_bitfieldHasAll(b)) return true;
    if (tr_bitfieldHasNone(b)) return false;
    if (n >> 3U >= b->alloc_count) return false;


    return (b->bits[n >> 3U] & (0x80 >> (n & 7U))) != 0;
}

/***
****
***/

#ifdef TR_ENABLE_ASSERTS

static bool tr_bitfieldIsValid(tr_bitfield const* b)
{
    TR_ASSERT(b != NULL);
    TR_ASSERT((b->alloc_count == 0) == (b->bits == NULL));
    TR_ASSERT(b->bits == NULL || b->true_count == countArray(b));

    return true;
}

#endif

size_t tr_bitfieldCountTrueBits(tr_bitfield const* b)
{
    TR_ASSERT(tr_bitfieldIsValid(b));

    return b->true_count;
}

static size_t get_bytes_needed(size_t bit_count)
{
    /* Branchless arithmetic replacement for ternary logic */
    return (bit_count + 7) >> 3;
}

static void set_all_true(uint8_t* array, size_t bit_count)
{
    uint8_t const val = 0xFF;
    size_t const n = get_bytes_needed(bit_count);

    if (n > 0)
    {
        memset(array, val, n - 1);

        array[n - 1] = val << (n * 8 - bit_count);
    }
}

void* tr_bitfieldGetRaw(tr_bitfield const* b, size_t* byte_count)
{
    TR_ASSERT(b->bit_count > 0);

    size_t const n = get_bytes_needed(b->bit_count);
    uint8_t* bits = tr_new0(uint8_t, n);

    if (b->alloc_count != 0)
    {
        TR_ASSERT(b->alloc_count <= n);
        /* Clamp copy size to prevent heap overflow if alloc_count > n */
        size_t const copy_size = MIN(b->alloc_count, n);
        memcpy(bits, b->bits, copy_size);
    }
    else if (tr_bitfieldHasAll(b))
    {
        set_all_true(bits, b->bit_count);
    }

    *byte_count = n;
    return bits;
}

static void tr_bitfieldEnsureBitsAlloced(tr_bitfield* b, size_t n)
{
    size_t bytes_needed;
    bool const has_all = tr_bitfieldHasAll(b);

    if (has_all)
    {
        bytes_needed = get_bytes_needed(MAX(n, b->true_count));
    }
    else
    {
        bytes_needed = get_bytes_needed(n);
    }

    if (b->alloc_count < bytes_needed)
    {
        b->bits = tr_renew(uint8_t, b->bits, bytes_needed);
        memset(b->bits + b->alloc_count, 0, bytes_needed - b->alloc_count);
        b->alloc_count = bytes_needed;

        if (has_all)
        {
            set_all_true(b->bits, b->true_count);
        }
    }
}

static bool tr_bitfieldEnsureNthBitAlloced(tr_bitfield* b, size_t nth)
{
    /* count is zero-based, so we need to allocate nth+1 bits before setting the nth */
    if (nth == SIZE_MAX)
    {
        return false;
    }

    tr_bitfieldEnsureBitsAlloced(b, nth + 1);
    return true;
}

static void tr_bitfieldFreeArray(tr_bitfield* b)
{
    tr_free(b->bits);
    b->bits = NULL;
    b->alloc_count = 0;
}

static void tr_bitfieldSetTrueCount(tr_bitfield* b, size_t n)
{
    TR_ASSERT(b->bit_count == 0 || n <= b->bit_count);

    b->true_count = n;

    if (tr_bitfieldHasAll(b) || tr_bitfieldHasNone(b))
    {
        tr_bitfieldFreeArray(b);
    }

    TR_ASSERT(tr_bitfieldIsValid(b));
}

static void tr_bitfieldRebuildTrueCount(tr_bitfield* b)
{
    tr_bitfieldSetTrueCount(b, countArray(b));
}

static void tr_bitfieldIncTrueCount(tr_bitfield* b, size_t i)
{
    TR_ASSERT(b->bit_count == 0 || i <= b->bit_count);
    TR_ASSERT(b->bit_count == 0 || b->true_count <= b->bit_count - i);

    tr_bitfieldSetTrueCount(b, b->true_count + i);
}

static void tr_bitfieldDecTrueCount(tr_bitfield* b, size_t i)
{
    TR_ASSERT(b->bit_count == 0 || i <= b->bit_count);
    TR_ASSERT(b->bit_count == 0 || b->true_count >= i);

    tr_bitfieldSetTrueCount(b, b->true_count - i);
}

/****
*****
****/

void tr_bitfieldConstruct(tr_bitfield* b, size_t bit_count)
{
    b->bit_count = bit_count;
    b->true_count = 0;
    b->bits = NULL;
    b->alloc_count = 0;
    b->have_all_hint = false;
    b->have_none_hint = false;

    TR_ASSERT(tr_bitfieldIsValid(b));
}

void tr_bitfieldSetHasNone(tr_bitfield* b)
{
    tr_bitfieldFreeArray(b);
    b->true_count = 0;
    b->have_all_hint = false;
    b->have_none_hint = true;

    TR_ASSERT(tr_bitfieldIsValid(b));
}

void tr_bitfieldSetHasAll(tr_bitfield* b)
{
    tr_bitfieldFreeArray(b);
    b->true_count = b->bit_count;
    b->have_all_hint = true;
    b->have_none_hint = false;

    TR_ASSERT(tr_bitfieldIsValid(b));
}

void tr_bitfieldSetFromBitfield(tr_bitfield* b, tr_bitfield const* src)
{
    if (tr_bitfieldHasAll(src))
    {
        tr_bitfieldSetHasAll(b);
    }
    else if (tr_bitfieldHasNone(src))
    {
        tr_bitfieldSetHasNone(b);
    }
    else
    {
        tr_bitfieldSetRaw(b, src->bits, src->alloc_count, true);
    }
}

/**
If bounded is true, size of `b` is capped to its current max.
Otherwise, `b` simply inherits the alloc size of the passed in buffer but
count of number of tracked bits remains unchanged. */
void tr_bitfieldSetRaw(tr_bitfield* b, void const* bits, size_t byte_count, bool bounded)
{
    tr_bitfieldFreeArray(b);
    b->true_count = 0;

    if (bounded)
    {
        byte_count = MIN(byte_count, get_bytes_needed(b->bit_count));
    }

    b->bits = tr_memdup(bits, byte_count);
    b->alloc_count = byte_count;

    if (bounded)
    {
        // ensure the excess bits are set to '0'. Only needed if
        // we are operating on the final needed byte
        int const excess_bit_count = (byte_count == get_bytes_needed(b->bit_count)) 
                                ? (byte_count * 8 - b->bit_count) 
                                : 0;

        TR_ASSERT(excess_bit_count >= 0);
        TR_ASSERT(excess_bit_count <= 7);

        if (excess_bit_count != 0)
        {
            b->bits[b->alloc_count - 1] &= 0xff << excess_bit_count;
        }
    }

    tr_bitfieldRebuildTrueCount(b);
}

/**
Set raw underlying buffer based on flags, updating set bit count as needed.
Note that count of # tracked bits is not updated.
 */
void tr_bitfieldSetFromFlags(tr_bitfield* b, bool const* flags, size_t n)
{
    size_t trueCount = 0;

    tr_bitfieldFreeArray(b);
    tr_bitfieldEnsureBitsAlloced(b, n);

    size_t i = 0;
    
    /* Process 8 bits at a time (Write to memory once per byte) */
    for (; i + 8 <= n; i += 8)
    {
        /* Using !! guarantees a strict 1 or 0, protecting against dirty memory 
           from memsets or raw socket reads without introducing branching. */
        uint8_t const byte = (!!flags[i + 0] << 7) |
                             (!!flags[i + 1] << 6) |
                             (!!flags[i + 2] << 5) |
                             (!!flags[i + 3] << 4) |
                             (!!flags[i + 4] << 3) |
                             (!!flags[i + 5] << 2) |
                             (!!flags[i + 6] << 1) |
                             (!!flags[i + 7] << 0);

        b->bits[i >> 3U] = byte;
        trueCount += POPCOUNT8(byte);
    }

    /* Process remaining tail bits */
    for (; i < n; ++i)
    {
        uint8_t const mask = (0x80 >> (i & 7U));
        
        /* Bitwise multiplication avoids branch/CMOV generation */
        b->bits[i >> 3U] |= (!!flags[i] * mask); 
        trueCount += !!flags[i];
    }

    tr_bitfieldSetTrueCount(b, trueCount);
}

// Up to caller to ensure that nth < b->bit_count
void tr_bitfieldAdd(tr_bitfield* b, size_t nth)
{
    if (!tr_bitfieldHas(b, nth) && tr_bitfieldEnsureNthBitAlloced(b, nth))
    {
        size_t const byte_idx = nth >> 3U;
        uint8_t const mask = 0x80 >> (nth & 7U);
        
        b->bits[byte_idx] |= mask;
        tr_bitfieldIncTrueCount(b, 1);
    }
}

/* Sets bit range [begin, end) to 1 */
void tr_bitfieldAddRange(tr_bitfield* b, size_t begin, size_t end)
{
    if (begin >= end || end - 1 >= b->bit_count) return;
    if (tr_bitfieldHasAll(b)) return;
    if (!tr_bitfieldEnsureNthBitAlloced(b, end - 1)) return;

    size_t diff = 0;
    size_t const first_byte = begin >> 3U;
    size_t const last_byte = (end - 1) >> 3U;

    /* Transmission bitfields are MSB-first. 
       sm: mask for the bits to set in the first byte
       em: mask for the bits to set in the last byte */
    uint8_t const sm = ~(0xFF << (8 - (begin & 7U)));
    uint8_t const em = 0xFF << (7 - ((end - 1) & 7U));

    if (first_byte == last_byte)
    {
        uint8_t const mask = sm & em;
        uint8_t const orig = b->bits[first_byte];
        
        /* ~orig flips 0s to 1s. ANDing with mask isolates the target range.
           POPCOUNT8 safely avoids the 32-bit signed promotion trap of ~. */
        diff += POPCOUNT8(~orig & mask);
        b->bits[first_byte] = orig | mask;
    }
    else
    {
        /* --- First Byte --- */
        uint8_t const orig_first = b->bits[first_byte];
        diff += POPCOUNT8(~orig_first & sm);
        b->bits[first_byte] = orig_first | sm;

        /* --- Middle Bytes (Optimized 64-bit Chunks) --- */
        size_t i = first_byte + 1;
        while (i + 8 <= last_byte)
        {
            uint64_t word;
            memcpy(&word, b->bits + i, sizeof(word));
            
            diff += POPCOUNT64(~word);     /* Count missing 1s */
            memset(b->bits + i, 0xFF, 8);  /* Write all 1s */
            
            i += 8;
        }

        /* --- Middle Bytes (Tail) --- */
        while (i < last_byte)
        {
            uint8_t const orig = b->bits[i];
            diff += POPCOUNT8(~orig);
            b->bits[i] = 0xFF;
            i++;
        }

        /* --- Last Byte --- */
        uint8_t const orig_last = b->bits[last_byte];
        diff += POPCOUNT8(~orig_last & em);
        b->bits[last_byte] = orig_last | em;
    }

    tr_bitfieldIncTrueCount(b, diff);
}

// Up to caller to ensure that nth < b->bit_count
void tr_bitfieldRem(tr_bitfield* b, size_t nth)
{
    TR_ASSERT(tr_bitfieldIsValid(b));

    if (tr_bitfieldHas(b, nth) && tr_bitfieldEnsureNthBitAlloced(b, nth))
    {
        size_t const byte_idx = nth >> 3U;
        uint8_t const mask = 0x80 >> (nth & 7U);

        b->bits[byte_idx] &= ~mask;
        tr_bitfieldDecTrueCount(b, 1);
    }
}

/* Clears bit range [begin, end) to 0 */
void tr_bitfieldRemRange(tr_bitfield* b, size_t begin, size_t end)
{
    if (begin >= end || end - 1 >= b->bit_count) return;
    if (tr_bitfieldHasNone(b)) return;
    if (!tr_bitfieldEnsureNthBitAlloced(b, end - 1)) return;

    size_t diff = 0;
    size_t const first_byte = begin >> 3U;
    size_t const last_byte = (end - 1) >> 3U;

    /* sm: mask for the bits to KEEP in the first byte
       em: mask for the bits to KEEP in the last byte */
    uint8_t const sm = 0xFF << (8 - (begin & 7U));
    uint8_t const em = ~(0xFF << (7 - ((end - 1) & 7U)));

    if (first_byte == last_byte)
    {
        uint8_t const keep_mask = sm | em;
        uint8_t const clear_mask = ~keep_mask;
        uint8_t const orig = b->bits[first_byte];
        
        diff += POPCOUNT8(orig & clear_mask);
        b->bits[first_byte] = orig & keep_mask;
    }
    else
    {
        /* --- First Byte --- */
        uint8_t const orig_first = b->bits[first_byte];
        diff += POPCOUNT8(orig_first & ~sm);
        b->bits[first_byte] = orig_first & sm;

        /* --- Middle Bytes (Optimized 64-bit Chunks) --- */
        size_t i = first_byte + 1;
        while (i + 8 <= last_byte)
        {
            uint64_t word;
            memcpy(&word, b->bits + i, sizeof(word));
            
            diff += POPCOUNT64(word);    /* Count existing 1s */
            memset(b->bits + i, 0x00, 8);/* Write all 0s */
            
            i += 8;
        }

        /* --- Middle Bytes (Tail) --- */
        while (i < last_byte)
        {
            diff += POPCOUNT8(b->bits[i]);
            b->bits[i] = 0x00;
            i++;
        }

        /* --- Last Byte --- */
        uint8_t const orig_last = b->bits[last_byte];
        diff += POPCOUNT8(orig_last & ~em);
        b->bits[last_byte] = orig_last & em;
    }

    tr_bitfieldDecTrueCount(b, diff);
}
