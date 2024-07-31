/*
 * This file Copyright (C) 2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <stdbool.h>

#include "tr-macros.h"

bool TR_NORETURN tr_assert_report(char const* file, int line, char const* message_fmt, ...) TR_GNUC_PRINTF(3, 4);

// Variant that always evalutes predicate & crashes if false, regardless of compilation mode
#define CHECK(x) ((void)(TR_LIKELY(x) || tr_assert_report(__FILE__, __LINE__, "%s", #x)))
#define CHECK_MSG(x, ...) ((void)(TR_LIKELY(x) || tr_assert_report(__FILE__, __LINE__, __VA_ARGS__)))

#if !defined(NDEBUG) || defined(TR_FORCE_ASSERTIONS)

#define TR_ASSERT(x) ((void)(TR_LIKELY(x) || tr_assert_report(__FILE__, __LINE__, "%s", #x)))
#define TR_ASSERT_MSG(x, ...) ((void)(TR_LIKELY(x) || tr_assert_report(__FILE__, __LINE__, __VA_ARGS__)))

#define TR_ENABLE_ASSERTS

#else

#define TR_ASSERT(x) ((void)0)
#define TR_ASSERT_MSG(x, ...) ((void)0)

#undef TR_ENABLE_ASSERTS

#endif
