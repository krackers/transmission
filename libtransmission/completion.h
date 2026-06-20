/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
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
#include "bitfield.h"
#include "utils.h" /* tr_getRatio() */

typedef struct tr_completion
{
    tr_torrent* tor;

    tr_bitfield blockBitfield;

    /* Cached target byte count. Sum of all 'Wanted' pieces plus any 
       already-downloaded blocks residing in 'Do Not Download' pieces. 
       [0..info.totalSize] 
       DON'T access this directly; it's a lazy field.
       Use tr_cpSizeWhenDone() instead! */
    uint64_t sizeWhenDoneLazy;

    /* Set when sizeWhenDone needs to be recalculated.
       Dirtied when file DND priorities change, or when blocks belonging 
       to purely DND pieces are added/removed. */
    bool sizeWhenDoneIsDirty;

    /* Cached byte count of all fully complete, checksum-verified pieces. 
       [0..info.totalSize]
       DON'T access this directly; it's a lazy field.
       Use tr_cpHaveValid() instead! */
    uint64_t haveValidLazy;

    /* Set when haveValidLazy needs to be recalculated.
       Dirtied when a piece finishes downloading and passes 
       its hash check, or when a piece is removed/discarded */
    bool haveValidIsDirty;

    /* Total byte count for all downloading/downloaded blocks.
       This tracks the raw block bitfield and includes verified blocks, 
       unverified or partial blocks, and orphaned blocks belonging to DND files. 
       (haveValid + haveUnchecked)
       [0..sizeWhenDone] */
    uint64_t sizeNow;
}
tr_completion;

/**
*** Life Cycle
**/

void tr_cpConstruct(tr_completion*, tr_torrent*);

void tr_cpBlockInit(tr_completion* cp, tr_bitfield const* blocks);

static inline void tr_cpDestruct(tr_completion* cp)
{
    tr_bitfieldDestruct(&cp->blockBitfield);
}

/**
*** General
**/

double tr_cpPercentComplete(tr_completion const* cp);

double tr_cpPercentDone(tr_completion const* cp);

tr_completeness tr_cpGetStatus(tr_completion const*);

uint64_t tr_cpHaveValid(tr_completion const*);

uint64_t tr_cpSizeWhenDone(tr_completion const*);

uint64_t tr_cpLeftUntilDone(tr_completion const*);

void tr_cpGetAmountDone(tr_completion const* completion, float* tab, int tabCount);

static inline uint64_t tr_cpHaveTotal(tr_completion const* cp)
{
    return cp->sizeNow;
}

static inline bool tr_cpHasAll(tr_completion const* cp)
{
    return tr_torrentHasMetadata(cp->tor) && tr_bitfieldHasAll(&cp->blockBitfield);
}

static inline bool tr_cpHasNone(tr_completion const* cp)
{
    return !tr_torrentHasMetadata(cp->tor) || tr_bitfieldHasNone(&cp->blockBitfield);
}

/**
***  Pieces
**/

void tr_cpPieceAdd(tr_completion* cp, tr_piece_index_t i);

void tr_cpPieceRem(tr_completion* cp, tr_piece_index_t i);

size_t tr_cpMissingBlocksInPiece(tr_completion const*, tr_piece_index_t);

size_t tr_cpMissingBytesInPiece(tr_completion const*, tr_piece_index_t);

static inline bool tr_cpPieceIsComplete(tr_completion const* cp, tr_piece_index_t i)
{
    return tr_cpMissingBlocksInPiece(cp, i) == 0;
}

/**
***  Blocks
**/

void tr_cpBlockAdd(tr_completion* cp, tr_block_index_t i);

static inline bool tr_cpBlockIsComplete(tr_completion const* cp, tr_block_index_t i)
{
    return tr_bitfieldHas(&cp->blockBitfield, i);
}

/***
****  Misc
***/

bool tr_cpFileIsComplete(tr_completion const* cp, tr_file_index_t);

void* tr_cpCreatePieceBitfield(tr_completion const* cp, size_t* byte_count);

static inline void tr_cpInvalidateDND(tr_completion* cp)
{
    cp->sizeWhenDoneIsDirty = true;
}
