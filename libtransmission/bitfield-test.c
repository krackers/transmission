/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h> /* strlen() */
#include "transmission.h"
#include "crypto-utils.h"
#include "bitfield.h"
#include "utils.h" /* tr_free */

#include "libtransmission-test.h"

static int check_true_count_integrity(tr_bitfield const* bf)
{
    size_t manual_count = 0;
    for (size_t i = 0; i < bf->bit_count; ++i)
    {
        if (tr_bitfieldHas(bf, i)) manual_count++;
    }
    check_int(manual_count, ==, tr_bitfieldCountTrueBits(bf));
    return 0;
}

static int test_bitfield_count_range(void)
{
    int begin;
    int end;
    int count1;
    int count2;
    int const bitCount = 100 + tr_rand_int_weak(1000);
    tr_bitfield bf;

    /* generate a random bitfield */
    tr_bitfieldConstruct(&bf, bitCount);

    for (int i = 0, n = tr_rand_int_weak(bitCount); i < n; ++i)
    {
        tr_bitfieldAdd(&bf, tr_rand_int_weak(bitCount));
    }

    begin = tr_rand_int_weak(bitCount);

    do
    {
        end = tr_rand_int_weak(bitCount);
    }
    while (end == begin);

    /* ensure end <= begin */
    if (end < begin)
    {
        int const tmp = begin;
        begin = end;
        end = tmp;
    }

    /* test the bitfield */
    count1 = 0;

    for (int i = begin; i < end; ++i)
    {
        if (tr_bitfieldHas(&bf, i))
        {
            ++count1;
        }
    }

    count2 = tr_bitfieldCountRange(&bf, begin, end);
    check_int(count1, ==, count2);

    /* cleanup */
    tr_bitfieldDestruct(&bf);
    return 0;
}

static int test_bitfields(void)
{
    unsigned int bitcount = 500;
    tr_bitfield field;

    tr_bitfieldConstruct(&field, bitcount);
    check_true_count_integrity(&field);

    /* test tr_bitfieldAdd */
    for (unsigned int i = 0; i < bitcount; i++)
    {
        if (i % 7 == 0)
        {
            tr_bitfieldAdd(&field, i);
        }
    }
    check_true_count_integrity(&field);

    for (unsigned int i = 0; i < bitcount; i++)
    {
        check_bool(tr_bitfieldHas(&field, i), ==, (i % 7 == 0));
    }

    /* test tr_bitfieldAddRange */
    tr_bitfieldAddRange(&field, 0, bitcount);
    check_true_count_integrity(&field);

    for (unsigned int i = 0; i < bitcount; i++)
    {
        check(tr_bitfieldHas(&field, i));
    }

    /* test tr_bitfieldRem */
    for (unsigned int i = 0; i < bitcount; i++)
    {
        if (i % 7 != 0)
        {
            tr_bitfieldRem(&field, i);
        }
    }
    check_true_count_integrity(&field);

    for (unsigned int i = 0; i < bitcount; i++)
    {
        check_bool(tr_bitfieldHas(&field, i), ==, (i % 7 == 0));
    }

    /* test tr_bitfieldRemRange in the middle of a boundary */
    tr_bitfieldAddRange(&field, 0, 64);
    check_true_count_integrity(&field);
    tr_bitfieldRemRange(&field, 4, 21);
    check_true_count_integrity(&field);

    for (unsigned int i = 0; i < 64; i++)
    {
        check_bool(tr_bitfieldHas(&field, i), ==, (i < 4 || i >= 21));
    }

    /* test tr_bitfieldRemRange on the boundaries */
    tr_bitfieldAddRange(&field, 0, 64);
    check_true_count_integrity(&field);
    tr_bitfieldRemRange(&field, 8, 24);
    check_true_count_integrity(&field);

    for (unsigned int i = 0; i < 64; i++)
    {
        check_bool(tr_bitfieldHas(&field, i), ==, (i < 8 || i >= 24));
    }

    /* test tr_bitfieldRemRange when begin & end is on the same word */
    tr_bitfieldAddRange(&field, 0, 64);
    check_true_count_integrity(&field);
    tr_bitfieldRemRange(&field, 4, 5);
    check_true_count_integrity(&field);

    for (unsigned int i = 0; i < 64; i++)
    {
        check_bool(tr_bitfieldHas(&field, i), ==, (i < 4 || i >= 5));
    }

    /* test tr_bitfieldAddRange */
    tr_bitfieldRemRange(&field, 0, 64);
    check_true_count_integrity(&field);
    tr_bitfieldAddRange(&field, 4, 21);
    check_true_count_integrity(&field);

    for (unsigned int i = 0; i < 64; i++)
    {
        check_bool(tr_bitfieldHas(&field, i), ==, (4 <= i && i < 21));
    }

    /* test tr_bitfieldAddRange on the boundaries */
    tr_bitfieldRemRange(&field, 0, 64);
    check_true_count_integrity(&field);
    tr_bitfieldAddRange(&field, 8, 24);
    check_true_count_integrity(&field);

    for (unsigned int i = 0; i < 64; i++)
    {
        check_bool(tr_bitfieldHas(&field, i), ==, (8 <= i && i < 24));
    }

    /* test tr_bitfieldAddRange when begin & end is on the same word */
    tr_bitfieldRemRange(&field, 0, 64);
    check_true_count_integrity(&field);
    tr_bitfieldAddRange(&field, 4, 5);
    check_true_count_integrity(&field);

    for (unsigned int i = 0; i < 64; i++)
    {
        check_bool(tr_bitfieldHas(&field, i), ==, (4 <= i && i < 5));
    }

    tr_bitfieldDestruct(&field);
    return 0;
}

static int test_out_of_bounds(void)
{
    tr_bitfield bf;
    tr_bitfieldConstruct(&bf, 10);

    /* Asking for a bit way past the boundary should return false, not segfault */
    check(!tr_bitfieldHas(&bf, 5000));
    check(!tr_bitfieldHas(&bf, SIZE_MAX));

    // Unknown length bitfield
    tr_bitfieldDestruct(&bf);
    tr_bitfieldConstruct(&bf, 0);

    /* Adding SIZE_MAX should safely abort inside tr_bitfieldEnsureNthBitAlloced */
    tr_bitfieldAdd(&bf, SIZE_MAX);
    check_int(bf.alloc_count, ==, 0); /* Ensure it didn't try to allocate max memory */

    // Should not trigger oob if undefined length
    tr_bitfieldAdd(&bf, 500);
    check(tr_bitfieldHas(&bf, 500));

    tr_bitfieldDestruct(&bf);
    return 0;
}

static int test_bitfield_has_all_none(void)
{
    tr_bitfield field;

    tr_bitfieldConstruct(&field, 3);

    check(!tr_bitfieldHasAll(&field));
    check(tr_bitfieldHasNone(&field));

    tr_bitfieldAdd(&field, 0);
    check(!tr_bitfieldHasAll(&field));
    check(!tr_bitfieldHasNone(&field));

    tr_bitfieldRem(&field, 0);
    tr_bitfieldAdd(&field, 1);
    check(!tr_bitfieldHasAll(&field));
    check(!tr_bitfieldHasNone(&field));

    tr_bitfieldRem(&field, 1);
    tr_bitfieldAdd(&field, 2);
    check(!tr_bitfieldHasAll(&field));
    check(!tr_bitfieldHasNone(&field));

    tr_bitfieldAdd(&field, 0);
    tr_bitfieldAdd(&field, 1);
    check(tr_bitfieldHasAll(&field));
    check(!tr_bitfieldHasNone(&field));

    tr_bitfieldSetHasNone(&field);
    check(!tr_bitfieldHasAll(&field));
    check(tr_bitfieldHasNone(&field));

    tr_bitfieldSetHasAll(&field);
    check(tr_bitfieldHasAll(&field));
    check(!tr_bitfieldHasNone(&field));

    tr_bitfieldDestruct(&field);
    tr_bitfieldConstruct(&field, 0);

    check(!tr_bitfieldHasAll(&field));
    check(!tr_bitfieldHasNone(&field));

    tr_bitfieldSetHasNone(&field);
    check(!tr_bitfieldHasAll(&field));
    check(tr_bitfieldHasNone(&field));

    tr_bitfieldSetHasAll(&field);
    check(tr_bitfieldHasAll(&field));
    check(!tr_bitfieldHasNone(&field));

    tr_bitfieldDestruct(&field);
    return 0;
}

static int test_large_ranges(void)
{
    unsigned int const bitcount = 1024;
    tr_bitfield field;
    tr_bitfieldConstruct(&field, bitcount);

    /* Spanning multiple 64-bit words (e.g., bits 70 through 400) */
    tr_bitfieldAddRange(&field, 70, 400);

    for (unsigned int i = 0; i < bitcount; i++)
    {
        check_bool(tr_bitfieldHas(&field, i), ==, (70 <= i && i < 400));
    }

    /* Punch a hole spanning multiple words */
    tr_bitfieldRemRange(&field, 100, 350);

    for (unsigned int i = 0; i < bitcount; i++)
    {
        bool const expected = (70 <= i && i < 100) || (350 <= i && i < 400);
        check_bool(tr_bitfieldHas(&field, i), ==, expected);
    }

    tr_bitfieldDestruct(&field);
    return 0;
}

static int test_raw_and_flags(void)
{
    tr_bitfield f1, f2;
    bool flags[16] = { true, false, true, true, false, false, false, false,
                       true, true, true, false, false, true, false, true };

    tr_bitfieldConstruct(&f1, 16);
    tr_bitfieldConstruct(&f2, 16);

    /* Test SetFromFlags */
    tr_bitfieldSetFromFlags(&f1, flags, 16);
    for (int i = 0; i < 16; ++i)
    {
        check_bool(tr_bitfieldHas(&f1, i), ==, flags[i]);
    }

    /* Test GetRaw */
    size_t byte_count;
    uint8_t* raw_bytes = tr_bitfieldGetRaw(&f1, &byte_count);
    check_int(byte_count, ==, 2);
    /* 10110000 = 0xB0, 11100101 = 0xE5 */
    check_int(raw_bytes[0], ==, 0xB0); 
    check_int(raw_bytes[1], ==, 0xE5); 

    /* Test SetRaw */
    tr_bitfieldSetRaw(&f2, raw_bytes, byte_count, true);
    for (int i = 0; i < 16; ++i)
    {
        check_bool(tr_bitfieldHas(&f2, i), ==, flags[i]);
    }

    /* Test SetFromBitfield */
    tr_bitfield f3;
    tr_bitfieldConstruct(&f3, 16);
    tr_bitfieldSetFromBitfield(&f3, &f1);
    check_int(tr_bitfieldCountTrueBits(&f3), ==, tr_bitfieldCountTrueBits(&f1));

    tr_free(raw_bytes);
    tr_bitfieldDestruct(&f1);
    tr_bitfieldDestruct(&f2);
    tr_bitfieldDestruct(&f3);
    return 0;
}

static int test_unaligned_flags_and_raw(void)
{
    tr_bitfield f1;
    /* 13 flags: tests the 8-bit chunk loop AND the tail loop */
    size_t const n = 13;
    bool flags[13] = { true, false, true, true, false, false, false, false, /* 8 bits */
                       true, true, true, false, false };                    /* 5 tail bits */

    tr_bitfieldConstruct(&f1, n);
    tr_bitfieldSetFromFlags(&f1, flags, n);

    for (size_t i = 0; i < n; ++i)
    {
        check_bool(tr_bitfieldHas(&f1, i), ==, flags[i]);
    }
    
    check_int(tr_bitfieldCountTrueBits(&f1), ==, 6);

    /* Test GetRaw / SetRaw with excess bits */
    size_t byte_count;
    uint8_t* raw_bytes = tr_bitfieldGetRaw(&f1, &byte_count);
    
    /* 13 bits requires 2 bytes */
    check_int(byte_count, ==, 2); 
    
    /* First byte: 10110000 (0xB0) */
    check_int(raw_bytes[0], ==, 0xB0);
    
    /* Second byte: 11100000 (0xE0) - The last 3 bits MUST be 0 */
    check_int(raw_bytes[1], ==, 0xE0);

    /* Mutate the raw bytes to add "dirty" excess bits at the end */
    raw_bytes[1] |= 0x07; /* Make it 0xE7 */
    
    tr_bitfield f2;
    tr_bitfieldConstruct(&f2, n);
    tr_bitfieldSetRaw(&f2, raw_bytes, byte_count, true); /* bounded = true */

    /* Ensure SetRaw correctly masked out the dirty excess bits */
    for (size_t i = 0; i < n; ++i)
    {
        check_bool(tr_bitfieldHas(&f2, i), ==, flags[i]);
    }
    check(!tr_bitfieldHas(&f2, 14)); /* Should not read the dirty bits */

    tr_free(raw_bytes);
    tr_bitfieldDestruct(&f1);
    tr_bitfieldDestruct(&f2);
    return 0;
}

static int test_exact_64bit_boundaries(void)
{
    tr_bitfield field;
    tr_bitfieldConstruct(&field, 512);

    /* Exactly one 64-bit word (8 bytes = 64 bits) */
    tr_bitfieldAddRange(&field, 64, 128);
    check_int(tr_bitfieldCountTrueBits(&field), ==, 64);

    for (unsigned int i = 0; i < 512; i++)
    {
        check_bool(tr_bitfieldHas(&field, i), ==, (64 <= i && i < 128));
    }

    /* Clear exactly on a 64-bit boundary */
    tr_bitfieldRemRange(&field, 64, 128);
    check_int(tr_bitfieldCountTrueBits(&field), ==, 0);

    tr_bitfieldDestruct(&field);
    return 0;
}

static int test_setraw_short_buffer(void)
{
    tr_bitfield bf;

    tr_bitfieldConstruct(&bf, 16);

    uint8_t const short_buffer[1] = { 0xFF }; /* 8 bits of 1s */
    
    tr_bitfieldSetRaw(&bf, short_buffer, 1, true); /* bounded = true */

    /* 3. Verify the first 8 bits were parsed correctly */
    for (int i = 0; i < 8; ++i)
    {
        check(tr_bitfieldHas(&bf, i));
    }

    /* 4. Verify the missing 8 bits safely default to false 
          (since they are beyond the 1 byte we allocated) */
    for (int i = 8; i < 16; ++i)
    {
        check(!tr_bitfieldHas(&bf, i));
    }

    /* 5. The true count should be exactly 8 */
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 8);

    tr_bitfieldDestruct(&bf);
    return 0;
}

static int fuzz_add_rem_ranges(void)
{
    int const bitCount = 500 + tr_rand_int_weak(1000);
    tr_bitfield bf;
    bool* naive_array = tr_new0(bool, bitCount);

    tr_bitfieldConstruct(&bf, bitCount);

    /* Fuzz 100 random Add/Rem operations */
    for (int step = 0; step < 100; ++step)
    {
        int begin = tr_rand_int_weak(bitCount);
        int end = tr_rand_int_weak(bitCount);
        
        if (begin > end) 
        {
            int tmp = begin; begin = end; end = tmp;
        }
        else if (begin == end)
        {
            end = MIN(begin + 1, bitCount);
        }

        bool is_add = tr_rand_int_weak(2) == 0;

        if (is_add)
        {
            tr_bitfieldAddRange(&bf, begin, end);
            for (int i = begin; i < end; ++i) naive_array[i] = true;
        }
        else
        {
            tr_bitfieldRemRange(&bf, begin, end);
            for (int i = begin; i < end; ++i) naive_array[i] = false;
        }

        /* Verify integrity after every mutation */
        int true_count = 0;
        for (int i = 0; i < bitCount; ++i)
        {
            check_bool(tr_bitfieldHas(&bf, i), ==, naive_array[i]);
            if (naive_array[i]) true_count++;
        }
        check_int(tr_bitfieldCountTrueBits(&bf), ==, true_count);
    }

    tr_free(naive_array);
    tr_bitfieldDestruct(&bf);
    return 0;
}

static int test_unbounded_0_length_bitfield(void)
{
    tr_bitfield bf;
    tr_bitfieldConstruct(&bf, 0);

    /* 1. Initial state should be perfectly clean */
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 0);
    check(!tr_bitfieldHas(&bf, 0));
    check(!tr_bitfieldHas(&bf, 1000));

    /* 2. Single additions should dynamically grow the array */
    tr_bitfieldAdd(&bf, 10);
    tr_bitfieldAdd(&bf, 500);
    check(tr_bitfieldHas(&bf, 10));
    check(tr_bitfieldHas(&bf, 500));
    check(!tr_bitfieldHas(&bf, 9));
    check(!tr_bitfieldHas(&bf, 11));
    check(!tr_bitfieldHas(&bf, 499));
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 2);

    /* 3. Single removals should shrink true_count properly */
    tr_bitfieldRem(&bf, 10);
    check(!tr_bitfieldHas(&bf, 10));
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 1);

    /* 4. Range operations are mathematically guaranteed silent no-ops.
          They should safely return without altering the data. */
    
    /* Adding a range shouldn't change anything */
    tr_bitfieldAddRange(&bf, 20, 50);
    check(!tr_bitfieldHas(&bf, 25)); 
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 1);

    /* Removing a range shouldn't clear the bits we dynamically added */
    tr_bitfieldRemRange(&bf, 0, 1000);
    check(tr_bitfieldHas(&bf, 500)); 
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 1);

    /* 5. CountRange explicitly returns 0 for bit_count == 0 */
    check_int(tr_bitfieldCountRange(&bf, 0, 1000), ==, 0);

    bool flags[5] = { true, false, true, false, true };
    tr_bitfieldSetFromFlags(&bf, flags, 5);
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 3);
    check(tr_bitfieldHas(&bf, 0));
    check(!tr_bitfieldHas(&bf, 1));
    check(tr_bitfieldHas(&bf, 4));
    
    /* SetFromFlags frees the old array, so the old 500th bit is wiped */
    check(!tr_bitfieldHas(&bf, 500));

    /* NOTE: tr_bitfieldGetRaw(&bf, &byte_count) is intentionally omitted. 
     * Calling it with bit_count == 0 hits a TR_ASSERT and will crash the test runner. */

    tr_bitfieldDestruct(&bf);
    return 0;
}

static int test_setraw(void)
{
    tr_bitfield bf;
    tr_bitfieldConstruct(&bf, 0);

    uint8_t const raw_bytes[2] = { 0xFF, 0x80 };

    
    tr_bitfieldSetRaw(&bf, raw_bytes, 2, true);
    
    /* Because bit_count is 0, MIN(2, get_bytes_needed(0)) becomes 0.
     * The bitfield should safely discard the input and remain completely empty. */
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 0);
    check(!tr_bitfieldHas(&bf, 0));

    // -- test bounded = false --
    tr_bitfieldSetRaw(&bf, raw_bytes, 2, false);

    /* It should absorb the 2 bytes exactly as-is and rebuild the true_count. */
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 9);
    
    /* Check the first byte (all 8 bits should be true) */
    for (int i = 0; i < 8; ++i)
    {
        check(tr_bitfieldHas(&bf, i));
    }

    /* Check the second byte. 
     * 0x80 puts the 1 at the 8th overall index, and 0s for indices 9 through 15. */
    check(tr_bitfieldHas(&bf, 8));
    for (int i = 9; i < 16; ++i)
    {
        check(!tr_bitfieldHas(&bf, i));
    }

    /* Accessing way out of bounds should still return false safely */
    check(!tr_bitfieldHas(&bf, 16));
    check(!tr_bitfieldHas(&bf, 500));

    tr_bitfieldDestruct(&bf);
    return 0;
}

static int test_all_none_optimizations(void)
{
    tr_bitfield bf;
    size_t const bit_count = 100;
    
    tr_bitfieldConstruct(&bf, bit_count);
    tr_bitfieldSetHasAll(&bf);
    
    /* Verify the optimization guarantees no internal array is allocated yet */
    check(tr_bitfieldHasAll(&bf));
    check_int(bf.alloc_count, ==, 0);
    check(bf.bits == NULL);

    /* Removing a single bit should trigger EnsureBitsAlloced.
     * The bitfield must allocate the array, fill it entirely with 1s, 
     * and then successfully remove the requested bit. */
    tr_bitfieldRem(&bf, 50);
    
    check(!tr_bitfieldHasAll(&bf));
    check(bf.bits != NULL);
    check(bf.alloc_count > 0);
    check_int(tr_bitfieldCountTrueBits(&bf), ==, bit_count - 1);
    
    for (size_t i = 0; i < bit_count; ++i)
    {
        check_bool(tr_bitfieldHas(&bf, i), ==, (i != 50));
    }

    tr_bitfieldDestruct(&bf);


    tr_bitfieldConstruct(&bf, 16);
    tr_bitfieldSetHasAll(&bf);
    
    size_t byte_count;
    uint8_t* raw = tr_bitfieldGetRaw(&bf, &byte_count);    
    check_int(byte_count, ==, 2);
    check_int(raw[0], ==, 0xFF);
    check_int(raw[1], ==, 0xFF);
    
    /* Extracting raw bytes should use the fast-path and NOT force 
     * the bitfield to allocate its internal array. */
    check(bf.bits == NULL);
    check_int(bf.alloc_count, ==, 0);

    tr_free(raw);
    tr_bitfieldDestruct(&bf);


    tr_bitfieldConstruct(&bf, 100);
    
    /* Test "All" fast paths */
    tr_bitfieldSetHasAll(&bf);
    check_int(tr_bitfieldCountRange(&bf, 10, 30), ==, 20);
    check(tr_bitfieldHas(&bf, 99));

    /* Test "None" fast paths */
    tr_bitfieldSetHasNone(&bf);
    check_int(tr_bitfieldCountRange(&bf, 10, 30), ==, 0);
    check(!tr_bitfieldHas(&bf, 99));
    
    tr_bitfieldDestruct(&bf);

    tr_bitfield src, dest;
    tr_bitfieldConstruct(&src, 50);
    tr_bitfieldConstruct(&dest, 50);

    /* Source has the "All" optimization applied */
    tr_bitfieldSetHasAll(&src);
    
    /* Dest should recognize the source is "All" and apply the same 
     * optimization, rather than allocating memory and copying 1s. */
    tr_bitfieldSetFromBitfield(&dest, &src);
    
    check(tr_bitfieldHasAll(&dest));
    check(dest.bits == NULL); 
    check_int(dest.alloc_count, ==, 0);

    tr_bitfieldDestruct(&src);
    tr_bitfieldDestruct(&dest);

    return 0;
}

static int test_range_all_none_optimizations(void)
{
    tr_bitfield bf;
    size_t const bit_count = 100;

    tr_bitfieldConstruct(&bf, bit_count);

    // Short circuit No-Ops

    /* AddRange on "All" should exit before allocating memory */
    tr_bitfieldSetHasAll(&bf);
    check(bf.bits == NULL);
    
    tr_bitfieldAddRange(&bf, 10, 50);
    check(tr_bitfieldHasAll(&bf));
    check(bf.bits == NULL); /* Array remains completely unallocated */

    /* RemRange on "None" should exit before allocating memory */
    tr_bitfieldSetHasNone(&bf);
    check(bf.bits == NULL);

    tr_bitfieldRemRange(&bf, 10, 50);
    check(tr_bitfieldHasNone(&bf));
    check(bf.bits == NULL);

    // Lazy materialization

    /* RemRange on "All" must force the array to materialize and fill with 1s */
    tr_bitfieldSetHasAll(&bf);
    check(bf.bits == NULL);

    tr_bitfieldRemRange(&bf, 20, 40); /* Remove 20 bits */

    check(!tr_bitfieldHasAll(&bf));
    check(bf.bits != NULL); /* Array must now exist in memory */
    check_int(bf.alloc_count, >, 0);
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 80);

    for (size_t i = 0; i < bit_count; ++i)
    {
        check_bool(tr_bitfieldHas(&bf, i), ==, (i < 20 || i >= 40));
    }

    /* AddRange on "None" must force the array to materialize and fill with 0s */
    tr_bitfieldSetHasNone(&bf);
    check(bf.bits == NULL);

    tr_bitfieldAddRange(&bf, 20, 40); /* Add 20 bits */

    check(!tr_bitfieldHasNone(&bf));
    check(bf.bits != NULL);
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 20);

    for (size_t i = 0; i < bit_count; ++i)
    {
        check_bool(tr_bitfieldHas(&bf, i), ==, (i >= 20 && i < 40));
    }

    // Auto-pruning

    /* If consecutive AddRanges complete the bitfield, it should auto-free */
    tr_bitfieldSetHasNone(&bf);
    check(bf.bits == NULL);

    tr_bitfieldAddRange(&bf, 0, 50); /* Add first half */
    check(bf.bits != NULL);          /* Materialized */

    tr_bitfieldAddRange(&bf, 50, 100); /* Add second half */
    
    /* true_count hits 100, tr_bitfieldHasAll becomes true, array gets freed! */
    check(tr_bitfieldHasAll(&bf));
    check(bf.bits == NULL);
    check_int(bf.alloc_count, ==, 0);

    /* If consecutive RemRanges empty the bitfield, it should auto-free */
    tr_bitfieldSetHasAll(&bf);
    check(bf.bits == NULL);

    tr_bitfieldRemRange(&bf, 0, 50); /* Remove first half */
    check(bf.bits != NULL);          /* Materialized */

    tr_bitfieldRemRange(&bf, 50, 100); /* Remove second half */
    
    /* true_count hits 0, tr_bitfieldHasNone becomes true, array gets freed! */
    check(tr_bitfieldHasNone(&bf));
    check(bf.bits == NULL);
    check_int(bf.alloc_count, ==, 0);

    tr_bitfieldDestruct(&bf);
    return 0;
}

static int test_state_mutate_hinted_bitfield_unknown_length(void)
{
    tr_bitfield bf;
    
    /* Construct as UNKNOWN length */
    tr_bitfieldConstruct(&bf, 0);

    /* 1. Initial limbo phase: no hints, no array */
    check(!tr_bitfieldHasAll(&bf));
    check(!tr_bitfieldHasNone(&bf));

    /* 2. Apply protocol hint: state compresses to HaveNone */
    tr_bitfieldSetHasNone(&bf);
    check(tr_bitfieldHasNone(&bf));
    check(!tr_bitfieldHasAll(&bf));

    /* 3. Add bit (Transitions to dynamic array tracking). 
     * The HaveNone hint MUST be cleared to avoid "Sticky Flag" */
    tr_bitfieldAdd(&bf, 0);
    check(!tr_bitfieldHasNone(&bf)); 
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 1);

    /* 4. Remove bit (Transitions back to 0). 
     * SetTrueCount MUST re-compress the state to HaveNone */
    tr_bitfieldRem(&bf, 0);
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 0);
    check(tr_bitfieldHasNone(&bf)); 
    check(bf.bits == NULL); /* Verify the array was successfully freed (memory optimization) */

    tr_bitfieldDestruct(&bf);
    return 0;
}

static int test_state_mutate_hinted_bitfield_fixed_length(void)
{
    tr_bitfield bf;
    
    /* Construct as fixed length */
    tr_bitfieldConstruct(&bf, 5);

    check(!tr_bitfieldHasAll(&bf));
    check(tr_bitfieldHasNone(&bf));

    /* 3. Add bit (Transitions to dynamic array tracking). 
     * The HaveNone hint MUST be cleared to avoid "Sticky Flag" */
    tr_bitfieldAdd(&bf, 0);
    check(!tr_bitfieldHasNone(&bf)); 
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 1);

    /* 4. Remove bit (Transitions back to 0). 
     * SetTrueCount MUST re-compress the state to HaveNone */
    tr_bitfieldRem(&bf, 0);
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 0);
    check(tr_bitfieldHasNone(&bf)); 
    check(bf.bits == NULL); /* Verify the array was successfully freed (memory optimization) */

    tr_bitfieldAddRange(&bf, 0, 5);
    check_int(tr_bitfieldCountTrueBits(&bf), ==, 5);
    check(tr_bitfieldHasAll(&bf)); 

    tr_bitfieldDestruct(&bf);
    return 0;
}

static int test_bulk_operations_clear_hints(void)
{
    tr_bitfield bf;
    
    /* Construct as KNOWN length */
    tr_bitfieldConstruct(&bf, 100);

    /* Test 1: RemRange clears HasAll */
    tr_bitfieldSetHasAll(&bf);
    check(tr_bitfieldHasAll(&bf));
    tr_bitfieldRemRange(&bf, 0, 5);
    /* Strict mutator must have cleared the hint before evaluating math */
    check(!tr_bitfieldHasAll(&bf)); 

    /* Test 2: AddRange clears HasNone */
    tr_bitfieldSetHasNone(&bf);
    check(tr_bitfieldHasNone(&bf));
    tr_bitfieldAddRange(&bf, 0, 5);
    /* Strict mutator must have cleared the hint */
    check(!tr_bitfieldHasNone(&bf)); 

    /* Test 3: SetFromFlags clears both */
    tr_bitfieldSetHasAll(&bf);
    bool flags[5] = { true, false, true, false, true };
    tr_bitfieldSetFromFlags(&bf, flags, 5);
    check(!tr_bitfieldHasAll(&bf));
    check(!tr_bitfieldHasNone(&bf));

    /* Test 4: SetRaw clears both */
    tr_bitfieldSetHasAll(&bf);
    uint8_t raw[1] = { 0xAA }; /* 10101010 */
    tr_bitfieldSetRaw(&bf, raw, 1, true);
    check(!tr_bitfieldHasAll(&bf));
    check(!tr_bitfieldHasNone(&bf));

    tr_bitfieldDestruct(&bf);
    return 0;
}

static int test_hint_operations_with_unknown_length(void) {
    tr_bitfield bf;
    tr_bitfieldConstruct(&bf, 0);
    tr_bitfieldSetHasAll(&bf);

    // Rem or RemRange not allowed to be called with HasAll hinted

    // SetFromFlags should reset the hints
    bool flags[5] = { true, false, true, false, true };
    tr_bitfieldSetFromFlags(&bf, flags, 5);
    check(!tr_bitfieldHasAll(&bf));
    check(!tr_bitfieldHasNone(&bf));

    // SetRaw should work too
    tr_bitfieldSetHasAll(&bf);
    uint8_t raw[1] = { 0xAA }; /* 10101010 */
    tr_bitfieldSetRaw(&bf, raw, 1, false);
    check(!tr_bitfieldHasAll(&bf));
    check(!tr_bitfieldHasNone(&bf));

    // None hints should be cleared for 
    tr_bitfieldSetHasNone(&bf);
    bool flags2[5] = { true, false, false};
    tr_bitfieldSetFromFlags(&bf, flags2, 3);
    check(!tr_bitfieldHasAll(&bf));
    check(!tr_bitfieldHasNone(&bf));

    // Should expand as needed even when hinted
    tr_bitfieldSetHasNone(&bf);
    tr_bitfieldAdd(&bf, 30);
    check(tr_bitfieldHas(&bf, 30));
    check(!tr_bitfieldHas(&bf, 29));
    check(!tr_bitfieldHas(&bf, 31));
    tr_bitfieldRem(&bf, 30);
    check(!tr_bitfieldHas(&bf, 30));
    check(!tr_bitfieldHas(&bf, 29));
    check(!tr_bitfieldHas(&bf, 31));
    
}

static int test_constructor_strict_hints(void)
{
    tr_bitfield bf;

    /* KNOWN length starts mathematically empty -> SetTrueCount(0) 
     * should immediately elevate the hint to HasNone. */
    tr_bitfieldConstruct(&bf, 100);
    check(tr_bitfieldHasNone(&bf));
    check(!tr_bitfieldHasAll(&bf));
    tr_bitfieldDestruct(&bf);

    /* UNKNOWN length starts in limbo -> SetTrueCount(0) 
     * should leave both hints as false because there is no known bound. */
    tr_bitfieldConstruct(&bf, 0);
    check(!tr_bitfieldHasNone(&bf));
    check(!tr_bitfieldHasAll(&bf));
    tr_bitfieldDestruct(&bf);

    return 0;
}

static int test_set_true_count_compression(void)
{
    tr_bitfield bf;
    
    /* KNOWN length */
    tr_bitfieldConstruct(&bf, 10);
    check(tr_bitfieldHasNone(&bf));

    /* Add exactly all bits via a range (simulates receiving all pieces).
     * SetTrueCount should recognize it hit bit_count and compress state. */
    tr_bitfieldAddRange(&bf, 0, 10);
    check(tr_bitfieldHasAll(&bf));
    check(!tr_bitfieldHasNone(&bf));
    check(bf.bits == NULL); /* Array should be freed instantly */

    /* Remove all bits via a range.
     * SetTrueCount should recognize it hit 0 and compress state. */
    tr_bitfieldRemRange(&bf, 0, 10);
    check(tr_bitfieldHasNone(&bf));
    check(!tr_bitfieldHasAll(&bf));
    check(bf.bits == NULL); /* Array should be freed instantly */

    tr_bitfieldDestruct(&bf);
    return 0;
}

int main(void)
{
    testFunc const tests[] =
    {
        test_bitfields,
        test_bitfield_has_all_none,
        test_large_ranges,
        test_raw_and_flags,
        test_out_of_bounds,
        test_unaligned_flags_and_raw,
        test_exact_64bit_boundaries,
        test_setraw_short_buffer,
        test_unbounded_0_length_bitfield,
        test_setraw,
        test_all_none_optimizations,
        test_range_all_none_optimizations,
        test_state_mutate_hinted_bitfield_unknown_length,
        test_state_mutate_hinted_bitfield_fixed_length,
        test_bulk_operations_clear_hints,
        test_constructor_strict_hints,
        test_set_true_count_compression,
        test_hint_operations_with_unknown_length
    };

    int ret = runTests(tests, NUM_TESTS(tests));

    /* bitfield count range */
    for (int l = 0; l < 10000; ++l)
    {
        if (test_bitfield_count_range() != 0)
        {
            ++ret;
        }
    }

    for (int l = 0; l < 1000; l++) {
        fuzz_add_rem_ranges();
    }

    return ret;
}
