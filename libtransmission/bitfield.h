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
       May be 0 even for non-zero bit_count if compressed
       with all-empty/full optimization. Note that this
       is lazy allocated. If 64 bits are tracked but only
       bits 0-7 are set, only 1 byte needs to be allocated
       with rest assumed to be 0.*/
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
 
 WARNING: This can be called with n >= b->bit_count only if the bitfield
 is of unknown length (bit_count == 0), which will automatically expand
 the underyling bitfield.
 Bounds checking is the caller's responsibility, otherwise crash/UB.
*/
void tr_bitfieldAdd(tr_bitfield*, size_t n);

/**
 Clear the nth bit, if not already empty. 
 
 WARNING: Like tr_bitfieldAdd, calling this with n >= b->bit_count is supported
 only for unknown length bitfields (bit_count == 0). Additionally, it is NOT
 allowed to call this with an unknown-length bitfield hinted as hasAll
 (as it would be impossible to materialize the resulting array).
 Bounds checking is the caller's responsibility, otherwise crash/UB.
*/
void tr_bitfieldRem(tr_bitfield*, size_t n);

/**
 Sets bit range [begin, end) to 1.
 Calling this with begin >= end is a noop.
 
 NOTE: For known bitfields, begin/end indices are implicitly
 capped to last tracked bit, and fully out of range queries will be a noop. 
 Calling this with an "unknown length" bitfield will automatically
 expand the underlying bitfield, similar to tr_bitfieldAdd.
*/
void tr_bitfieldAddRange(tr_bitfield*, size_t begin, size_t end);

/**
 Clears bit range [begin, end) to 0.
 Calling this with begin >= end is a noop.
 
 NOTE: As with AddRange, begin/end indices are implicitly capped to last
 tracked bit (for known length bitfields) or last allocated index (for unknown length bitfields),
 with fully out of range queries being a noop.

 Additionally, it is NOT allowed to call this with an unknown-length bitfield hinted as hasAll
 (as it would be impossible to materialize the resulting array).
*/
void tr_bitfieldRemRange(tr_bitfield*, size_t begin, size_t end);

/***
****  life cycle
***/

extern tr_bitfield const TR_BITFIELD_INIT;

/**
 Construct a bitfield tracking n bits.
 NOTE: bit_count can be 0 to indicate that the length is undefined.
 Special behavior applies for such bitfields, see rest of comments.
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
 Set bits [0, n) of the bitfield based on a boolean array of size n.
 Other bits are reset to 0.
 WARNING: n > current bit_count is allowed only if the bitfield is unknown length
*/
void tr_bitfieldSetFromFlags(tr_bitfield*, bool const* bytes, size_t n);

void tr_bitfieldSetFromBitfield(tr_bitfield*, tr_bitfield const*);

/**
 Set bits [0, byte_count * 8) of the bitfield based on the input byte array.
 Other bits are reset to 0.

 If bounded is true, the upper bound is capped to at most the current bit count.
 Passing bounded is invalid (and silently ignored) for unknown length arrays.
 
 WARNING: If bounded is false, the caller is responsible for ensuring that the
          bitfield is either of unknown length (bit_count == 0) or that the number
          of copied bits does not exceed the current b->bit_count. In the last copied byte,
          any extra trailing bits must be set 0.
*/
void tr_bitfieldSetRaw(tr_bitfield*, void const* bytes, size_t byte_count, bool bounded);

/**
 Create a COPY of the underlying materialized bitfield, returning the buffer and
 size in bytes. Size of returned buffer is equal to needed_bytes(bit_count).
 
 REQUIREMENT: This can only be called when the number of tracked bits 
 is known (bit_count > 0).
*/
void* tr_bitfieldGetRaw(tr_bitfield const* b, size_t* byte_count);

/***
****
***/

/**
 Return number of set bits in [begin, end). 
 
 NOTE: Begin/end indices are implicitly capped to last tracked bit,
 (or last allocated index for unknown length arrays)
 and fully out of range queries will result in 0.
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

/**
 Returns whether the nth bit is set. 
 NOTE: Out of range queries are supported only for unknown length arrays. Such OOB reads
       return false, unless the unknown length array is hinted as have_all.
*/
bool tr_bitfieldHas(tr_bitfield const* b, size_t n);
