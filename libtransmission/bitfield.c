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
#include "popcnt.h"

// See comments in bitfield.h
tr_bitfield const TR_BITFIELD_INIT =
{
    .bits = NULL,
    .alloc_count = 0,
    .bit_count = 0,
    .true_count = 0,
    .have_all_hint = false,
    .have_none_hint = false
};

/****
*****
****/

static size_t countArray(tr_bitfield const* b)
{
    return tr_popcount(b->bits, b->alloc_count);
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
        
        val &= (0xFFU >> left_shift);
        val &= (0xFFU << right_shift);

        ret += tr_popcount_byte(val);
    }
    else
    {
        uint8_t val;
        size_t const walk_end = MIN(b->alloc_count, last_byte);

        /* first byte */
        size_t const first_shift = begin - (first_byte * 8);
        val = b->bits[first_byte];
        val &= (0xFFU >> first_shift);
        ret += tr_popcount_byte(val);

        /* middle bytes count in bulk */
        size_t i = first_byte + 1;
        if (i < walk_end)
        {
            size_t const mid_len = walk_end - i;
            ret += tr_popcount(b->bits + i, mid_len);
        }

        /* last byte */
        if (last_byte < b->alloc_count)
        {
            size_t const last_shift = (last_byte + 1) * 8 - end;
            val = b->bits[last_byte];
            val &= (0xFFU << last_shift);
            ret += tr_popcount_byte(val);
        }
    }

    TR_ASSERT(ret <= (end - begin));
    return ret;
}

size_t tr_bitfieldCountRange(tr_bitfield const* b, size_t begin, size_t end)
{
    if (b->bit_count > 0)
    {
        begin = MIN(begin, b->bit_count);
        end = MIN(end, b->bit_count);
    }

    if (begin >= end) return 0;
    if (tr_bitfieldHasAll(b)) return end - begin;
    if (tr_bitfieldHasNone(b)) return 0;

    return countRange(b, begin, end);
}

bool tr_bitfieldHas(tr_bitfield const* b, size_t n)
{
    if (tr_bitfieldHasAll(b)) return true;
    if (tr_bitfieldHasNone(b)) return false;
    if (n >> 3U >= b->alloc_count) return false;

    return (b->bits[n >> 3U] & (0x80 >> (n & 7U))) != 0;
}


static size_t get_bytes_needed(size_t bit_count)
{
    /* Branchless arithmetic replacement for ternary logic */
    return (bit_count + 7) >> 3;
}

/***
****
***/

#ifdef TR_ENABLE_ASSERTS

static bool tr_bitfieldIsValid(tr_bitfield const* b)
{
    TR_ASSERT(b != NULL);
    TR_ASSERT((b->alloc_count == 0) == (b->bits == NULL));

    // Hints are mutually excusive
    TR_ASSERT(!(b->have_all_hint && b->have_none_hint));

    if (b->alloc_count > 0)
    {
        TR_ASSERT(b->bits != NULL);
        // Array should only be allocated if not all full/empty
        TR_ASSERT(b->have_none_hint == false);
        TR_ASSERT(b->have_all_hint == false);
        TR_ASSERT(b->true_count > 0); // should not be empty
        if (b->bit_count > 0) {
            TR_ASSERT(b->true_count < b->bit_count); // should not be full
            // Ensure that Add/Rem was never called out of bounds.
            TR_ASSERT(b->alloc_count <= get_bytes_needed(b->bit_count));
        }

        // The raw math must match
        // Note: This check can be commented out as can be quite slow.
        // TR_ASSERT(b->true_count == countArray(b));
    }
    else 
    {
        TR_ASSERT(b->bits == NULL);
        // Compressed / Hint Invariant
        if (b->bit_count > 0) {
            // At least one hint must be set.
            TR_ASSERT(b->have_none_hint || b->have_all_hint);
            TR_ASSERT(b->have_none_hint == (b->true_count == 0));
            TR_ASSERT(b->have_all_hint == (b->true_count == b->bit_count));
        } else {
            // Unknown length, with no backing array, relying purely
            // on BEP 6 all/none protocol hint. Since no backing array
            // the true_count must be set to 0.
            // Note that both hints can be flse if it is just constructed
            // but not yet initialized by peer.
            TR_ASSERT(b->true_count == 0); 
        }
    }

    return true;
}

#endif

size_t tr_bitfieldCountTrueBits(tr_bitfield const* b)
{
    TR_ASSERT(tr_bitfieldIsValid(b));
    return b->true_count;
}

static void set_all_true(uint8_t* array, size_t bit_count)
{
    uint8_t const val = 0xFFU;
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
        memcpy(bits, b->bits, b->alloc_count);
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
    TR_ASSERT(b->bit_count == 0 || n <= b->bit_count);
    size_t bytes_needed;
    bool const has_all = tr_bitfieldHasAll(b);

    if (has_all)
    {
        // Allocating an unknown-length have-all bitfield 
        // is semantically invalid
        TR_ASSERT(b->bit_count > 0);
        bytes_needed = get_bytes_needed(b->bit_count);
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
            set_all_true(b->bits, b->bit_count);
        }
    }
}

static bool tr_bitfieldEnsureNthBitAlloced(tr_bitfield* b, size_t nth)
{
    /* count is zero-based, so we need to allocate nth+1 bits before setting the nth */
    if (nth == SIZE_MAX)
        return false;

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

    // If we mathematically hit 0 bits, compress back to the hint
    if (b->true_count == 0)
    {
        b->have_none_hint = true;
        b->have_all_hint = false;
    }
    // Also compress if we mathematically hit a full bitfield (only possible if known length)
    else if (b->bit_count > 0 && b->true_count == b->bit_count)
    {
        b->have_all_hint = true;
        b->have_none_hint = false;
    }
    if (tr_bitfieldHasAll(b) || tr_bitfieldHasNone(b)) {
        tr_bitfieldFreeArray(b);
    }

    TR_ASSERT(tr_bitfieldIsValid(b));
    TR_ASSERT(b->bit_count == 0 || b->alloc_count <= get_bytes_needed(b->bit_count));
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
    // If length is known, it is necessarily all empty.
    b->have_none_hint = (bit_count > 0);

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

void tr_bitfieldSetRaw(tr_bitfield* b, void const* bytes, size_t byte_count, bool bounded)
{
    tr_bitfieldFreeArray(b);
    b->true_count = 0;

    if (bounded)
    {
        byte_count = MIN(byte_count, get_bytes_needed(b->bit_count));
    }

    b->bits = tr_memdup(bytes, byte_count);
    b->alloc_count = byte_count;

    // Bounded implicitly maintains this, while caller must be careful if bounded=false
    TR_ASSERT(b->bit_count == 0 || b->alloc_count <= get_bytes_needed(b->bit_count));

    // Writing to a final byte that possibly contains trailing padding bits
    if (b->alloc_count == get_bytes_needed(b->bit_count)) {
        int const excess_bit_count = (b->alloc_count * 8 - b->bit_count);
        TR_ASSERT(excess_bit_count >= 0 && excess_bit_count <= 7);
        if (excess_bit_count != 0)
        {
            if (bounded) {
                // Ensure excess bits are set to 0
                b->bits[b->alloc_count - 1] &= 0xFFU << excess_bit_count;
            } else {
                // Ensure that trailing bits are already all 0,
                // as otherwise true_count could exceed tracked bit count.
                TR_ASSERT((b->bits[b->alloc_count - 1] &
                    (uint8_t) ~(0xFFU << excess_bit_count)) == 0);
            }
        }
    }



    b->have_all_hint = false;
    b->have_none_hint = false;
    tr_bitfieldRebuildTrueCount(b);
}

void tr_bitfieldSetFromFlags(tr_bitfield* b, bool const* flags, size_t n)
{
    TR_ASSERT(b->bit_count == 0 || n <= b->bit_count);
    // Clearing hints must be done before allocating, as otherwise
    // the array would get allocated with all bits set.
    tr_bitfieldFreeArray(b);
    b->true_count = 0;
    b->have_all_hint = false;
    b->have_none_hint = false;
    tr_bitfieldEnsureBitsAlloced(b, n);
    
    size_t i = 0;
    for (; i + 8 <= n; i += 8)
    {
        /* Using !! guarantees a strict 1 or 0 */
        uint8_t const byte = (!!flags[i + 0] << 7) |
                             (!!flags[i + 1] << 6) |
                             (!!flags[i + 2] << 5) |
                             (!!flags[i + 3] << 4) |
                             (!!flags[i + 4] << 3) |
                             (!!flags[i + 5] << 2) |
                             (!!flags[i + 6] << 1) |
                             (!!flags[i + 7] << 0);

        b->bits[i >> 3U] = byte;
    }

    for (; i < n; ++i)
    {
        /* Bitwise multiplication avoids branch/CMOV generation */
        uint8_t const mask = (0x80 >> (i & 7U));
        b->bits[i >> 3U] |= (!!flags[i] * mask); 
    }

    size_t trueCount = tr_popcount(b->bits, get_bytes_needed(n));
    tr_bitfieldSetTrueCount(b, trueCount);
}

// Up to caller to ensure that nth < b->bit_count
void tr_bitfieldAdd(tr_bitfield* b, size_t nth)
{
    TR_ASSERT(tr_bitfieldIsValid(b));
    TR_ASSERT(b->bit_count == 0 || nth < b->bit_count);
    if (!tr_bitfieldHas(b, nth) && tr_bitfieldEnsureNthBitAlloced(b, nth))
    {
        size_t const byte_idx = nth >> 3U;
        uint8_t const mask = 0x80 >> (nth & 7U);
        b->have_none_hint = false;
        b->bits[byte_idx] |= mask;
        tr_bitfieldIncTrueCount(b, 1);
    }
}

void tr_bitfieldAddRange(tr_bitfield* b, size_t begin, size_t end)
{
    if (begin >= end || end - 1 >= b->bit_count) return;
    if (tr_bitfieldHasAll(b)) return;
    if (!tr_bitfieldEnsureNthBitAlloced(b, end - 1)) return;

    b->have_none_hint = false;
    size_t diff = 0;
    size_t const first_byte = begin >> 3U;
    size_t const last_byte = (end - 1) >> 3U;

    /* Transmission bitfields are MSB-first. 
       sm: mask for the bits to set in the first byte
       em: mask for the bits to set in the last byte */
    uint8_t const sm = (uint8_t) ~(0xFFU << (8 - (begin & 7U)));
    uint8_t const em = 0xFFU << (7 - ((end - 1) & 7U));
    
    if (first_byte == last_byte)
    {
        uint8_t const mask = sm & em;
        uint8_t const orig = b->bits[first_byte];
        
        diff += tr_popcount_byte((uint8_t)~orig & mask); // Count missing 1s
        b->bits[first_byte] = orig | mask;
    }
    else
    {
        /* --- First Byte --- */
        uint8_t const orig_first = b->bits[first_byte];
        diff += tr_popcount_byte((uint8_t)~orig_first & sm);
        b->bits[first_byte] = orig_first | sm;

        /* --- Middle Bytes --- */
        size_t i = first_byte + 1;
        if (i < last_byte)
        {
            size_t const mid_len = last_byte - i;
            size_t const existing_1s = tr_popcount(b->bits + i, mid_len);

            diff += (mid_len * 8) - existing_1s;  // count missing 1s
            memset(b->bits + i, 0xFFU, mid_len); // write all 1s
        }

        /* --- Last Byte --- */
        uint8_t const orig_last = b->bits[last_byte];
        diff += tr_popcount_byte((uint8_t)~orig_last & em);
        b->bits[last_byte] = orig_last | em;
    }

    tr_bitfieldIncTrueCount(b, diff);
}

// Up to caller to ensure that nth < b->bit_count
void tr_bitfieldRem(tr_bitfield* b, size_t nth)
{
    TR_ASSERT(tr_bitfieldIsValid(b));
    TR_ASSERT(b->bit_count == 0 || nth < b->bit_count);
    TR_ASSERT(b->bit_count >= 0 || !b->have_all_hint);

    if (tr_bitfieldHas(b, nth) && tr_bitfieldEnsureNthBitAlloced(b, nth))
    {
        size_t const byte_idx = nth >> 3U;
        uint8_t const mask = 0x80 >> (nth & 7U);
        b->have_all_hint = false;
        b->bits[byte_idx] &= ~mask;
        tr_bitfieldDecTrueCount(b, 1);
    }
}

void tr_bitfieldRemRange(tr_bitfield* b, size_t begin, size_t end)
{
    if (begin >= end || end - 1 >= b->bit_count) return;
    if (tr_bitfieldHasNone(b)) return;
    if (!tr_bitfieldEnsureNthBitAlloced(b, end - 1)) return;

    b->have_all_hint = false;
    size_t diff = 0;
    size_t const first_byte = begin >> 3U;
    size_t const last_byte = (end - 1) >> 3U;


    /* sm: mask for the bits to KEEP in the first byte
       em: mask for the bits to KEEP in the last byte */
    uint8_t const sm = 0xFFU << (8 - (begin & 7U));
    uint8_t const em = (uint8_t) ~(0xFFU << (7 - ((end - 1) & 7U)));

    if (first_byte == last_byte)
    {
        uint8_t const keep_mask = sm | em;
        uint8_t const clear_mask = (uint8_t)~keep_mask;
        uint8_t const orig = b->bits[first_byte];
        
        diff += tr_popcount_byte(orig & clear_mask);
        b->bits[first_byte] = orig & keep_mask;
    }
    else
    {
        /* --- First Byte --- */
        uint8_t const orig_first = b->bits[first_byte];
        diff += tr_popcount_byte(orig_first & (uint8_t)~sm);
        b->bits[first_byte] = orig_first & sm;

        /* --- Middle Bytes --- */
        size_t i = first_byte + 1;
        if (i < last_byte)
        {
            size_t const mid_len = last_byte - i;
            
            diff += tr_popcount(b->bits + i, mid_len); // Count existing 1s
            memset(b->bits + i, 0x00, mid_len); // Write all 0s
        }

        /* --- Last Byte --- */
        uint8_t const orig_last = b->bits[last_byte];
        diff += tr_popcount_byte(orig_last & (uint8_t)~em);
        b->bits[last_byte] = orig_last & em;
    }

    tr_bitfieldDecTrueCount(b, diff);
}
