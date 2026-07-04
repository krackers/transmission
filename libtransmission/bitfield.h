/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include "transmission.h"

/** @brief Implementation of the BitTorrent spec's Bitfield array of bits */
typedef struct tr_bitfield
{
    /* Raw storage for bitvector, packed into bytes */
    uint8_t* bits;

    /* Number of bytes allocated for the tracked bits
       May be 0 if all-empty/full optimization.*/
    size_t alloc_count;

    /* Total number of tracked bits (length of bit vector)
       This may be 0 in the case of "unknown length" bitfields
       where bounds checking becomes disabled and the bitfield
       grows to any `n` you set in add/rem. It is forbidden
       to call GetRaw in such a case, since the actual length
       is semantically unknown. */
    size_t bit_count;

    /* Nnumber of bits set to 1 */
    size_t true_count;


    /* State flags for "all" or "none" conditions.
       These serve two distinct purposes depending on the bitfield's lifecycle:

      1. Source of truth for Unknown Length (bit_count == 0) bitsets:
         When magnet links receive BEP 6 HaveAll/HaveNone messages, we have no length 
         limit to do the math, so tr_bitfieldHasAll/None rely entirely on these flags.
         Note that unknown length bitsets may also be backed by an actual allocation,
         in which case only the second point below might apply.
      
      2. Memory Pruning: When a bitfield mathematically reaches 0 bits or bit_count (if
         a length is defined), the underlying array is freed to save memory,
         and these flags are set to true to reflect that compressed state. */
    bool have_all_hint;
    bool have_none_hint;
}
tr_bitfield;

/***
****
***/

void tr_bitfieldSetHasAll(tr_bitfield*);

void tr_bitfieldSetHasNone(tr_bitfield*);

/**
 Set the nth bit, if not already set. 
 Note that calling this with n >= b->bit_count will cause
 the underlying array to be expanded, without updating the tracked bit_count.
 It is up to caller to prevent this undefined behavior.
*/
void tr_bitfieldAdd(tr_bitfield*, size_t n);

/**
 Clear the nth bit, if not already empty. 
 Note that calling this with n >= b->bit_count will cause
 the underlying array to be expanded, without updating the tracked bit_count.
 It is up to caller to prevent this undefined behavior.
*/
void tr_bitfieldRem(tr_bitfield*, size_t n);

/**
 Sets bit range [begin, end) to 1.
 Calling this with end out of range or begin >= end is a noop.
*/
void tr_bitfieldAddRange(tr_bitfield*, size_t begin, size_t end);

/**
 Clears bit range [begin, end) to 0
 Calling this with end out of range or begin >= end is a noop.
*/
void tr_bitfieldRemRange(tr_bitfield*, size_t begin, size_t end);

/***
****  life cycle
***/

extern tr_bitfield const TR_BITFIELD_INIT;

/**
 Construct a bitfield tracking n bits.
*/
void tr_bitfieldConstruct(tr_bitfield*, size_t bit_count);

static inline void tr_bitfieldDestruct(tr_bitfield* b)
{
    tr_bitfieldSetHasNone(b);
}

/***
****
***/

/**
 Populate bitfield based on a boolean array, updating set bit count as needed.
 Note that bitfield must already by constructed to track n bits as bit_count.
*/
void tr_bitfieldSetFromFlags(tr_bitfield*, bool const* bytes, size_t n);

void tr_bitfieldSetFromBitfield(tr_bitfield*, tr_bitfield const*);

/**
 If bounded is true, number of bytes copied is capped by current bit count.
 Otherwise, all bytes are copied over into the bitfield, with no attempt to
  keep alloc_bytes in sync with number of tracked bits.
*/
void tr_bitfieldSetRaw(tr_bitfield*, void const* bits, size_t byte_count, bool bounded);

/**
 Create a COPY of the underlying byte array, returning the buffer and
 size in bytes. This can only be called when the number of tracked bits
 of the bitfield is known (> 0).
*/
void* tr_bitfieldGetRaw(tr_bitfield const* b, size_t* byte_count);

/***
****
***/

/**
 Return number of set bits in [begin, end). Calling this with
 a begin out of range returns 0.
 Calling this with an end out of range implicitly caps the end
 to the bit of the last allocated byte.
*/
size_t tr_bitfieldCountRange(tr_bitfield const*, size_t begin, size_t end);

size_t tr_bitfieldCountTrueBits(tr_bitfield const* b);

// Nit: checking b->bit_count != 0 is likely redundant
static inline bool tr_bitfieldHasAll(tr_bitfield const* b)
{
    return b->bit_count != 0 ? (b->true_count == b->bit_count) : b->have_all_hint;
}

static inline bool tr_bitfieldHasNone(tr_bitfield const* b)
{
    return b->bit_count != 0 ? (b->true_count == 0) : b->have_none_hint;
}

bool tr_bitfieldHas(tr_bitfield const* b, size_t n);
