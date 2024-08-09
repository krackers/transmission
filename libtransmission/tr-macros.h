/*
 * This file Copyright (C) 2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

/***
****
***/

#ifndef __has_feature
#define __has_feature(x) 0
#endif

#ifndef __has_extension
#define __has_extension __has_feature
#endif

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#ifdef _WIN32
#define TR_IF_WIN32(ThenValue, ElseValue) ThenValue
#else
#define TR_IF_WIN32(ThenValue, ElseValue) ElseValue
#endif

#ifdef __GNUC__
#define TR_GNUC_CHECK_VERSION(major, minor) \
    (__GNUC__ > (major) || \
    (__GNUC__ == (major) && __GNUC_MINOR__ >= (minor)))
#else
#define TR_GNUC_CHECK_VERSION(major, minor) 0
#endif

#ifdef __UCLIBC__
#define TR_UCLIBC_CHECK_VERSION(major, minor, micro) \
    (__UCLIBC_MAJOR__ > (major) || \
    (__UCLIBC_MAJOR__ == (major) && __UCLIBC_MINOR__ > (minor)) || \
    (__UCLIBC_MAJOR__ == (major) && __UCLIBC_MINOR__ == (minor) && __UCLIBC_SUBLEVEL__ >= (micro)))
#else
#define TR_UCLIBC_CHECK_VERSION(major, minor, micro) 0
#endif

/***
****
***/

#ifndef UNUSED
#if __has_attribute(__unused__) || TR_GNUC_CHECK_VERSION(2, 7)
#define UNUSED __attribute__((__unused__))
#else
#define UNUSED
#endif
#endif

/***
****
***/

#if __has_builtin(__builtin_expect) || TR_GNUC_CHECK_VERSION(3, 0)
#define TR_LIKELY(x) __builtin_expect(!!(x), 1)
#define TR_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define TR_LIKELY(x) (x)
#define TR_UNLIKELY(x) (x)
#endif

/***
****
***/

#if __has_attribute(__noreturn__) || TR_GNUC_CHECK_VERSION(2, 5)
#define TR_NORETURN __attribute__((__noreturn__))
#elif defined(_MSC_VER)
#define TR_NORETURN __declspec(noreturn)
#else
#define TR_NORETURN
#endif

#if __has_attribute(__deprecated__) || TR_GNUC_CHECK_VERSION(3, 1)
#define TR_DEPRECATED __attribute__((__deprecated__))
#elif defined(_MSC_VER)
#define TR_DEPRECATED __declspec(deprecated)
#else
#define TR_DEPRECATED
#endif

#if __has_attribute(__format__) || TR_GNUC_CHECK_VERSION(2, 3)
#define TR_GNUC_PRINTF(fmt, args) __attribute__((__format__(printf, fmt, args)))
#else
#define TR_GNUC_PRINTF(fmt, args)
#endif

#if __has_attribute(__nonnull__) || TR_GNUC_CHECK_VERSION(3, 3)
#define TR_GNUC_NONNULL(...) __attribute__((__nonnull__(__VA_ARGS__)))
#else
#define TR_GNUC_NONNULL(...)
#endif

#if __has_attribute(__sentinel__) || TR_GNUC_CHECK_VERSION(4, 3)
#define TR_GNUC_NULL_TERMINATED __attribute__((__sentinel__))
#else
#define TR_GNUC_NULL_TERMINATED
#endif

#if __has_attribute(__hot__) || TR_GNUC_CHECK_VERSION(4, 3)
#define TR_GNUC_HOT __attribute__((__hot__))
#else
#define TR_GNUC_HOT
#endif

#if __has_attribute(__malloc__) || TR_GNUC_CHECK_VERSION(2, 96)
#define TR_GNUC_MALLOC __attribute__((__malloc__))
#else
#define TR_GNUC_MALLOC
#endif

#if __has_attribute(__fallthrough__) || TR_GNUC_CHECK_VERSION(7, 0)
#define TR_GNUC_FALLTHROUGH __attribute__((__fallthrough__))
#else
#define TR_GNUC_FALLTHROUGH
#endif

/***
****
***/

/**
 * @def TR_STATIC_ASSERT
 * @brief This helper allows to perform static checks at compile time
 */
#if defined(static_assert)
#define TR_STATIC_ASSERT static_assert
#elif __has_feature(c_static_assert) || __has_extension(c_static_assert)
#define TR_STATIC_ASSERT _Static_assert
#else
#define TR_STATIC_ASSERT(x, msg) \
    { \
        typedef char __tr_static_check__ [(x) ? 1 : -1] UNUSED; \
    }
#endif

#define TR_CONCAT_(a, b) a ## b
#define TR_CONCAT(a, b) TR_CONCAT_(a, b)
#define TR_UNIQUE(prefix) TR_CONCAT(TR_CONCAT(_unq_, prefix), __COUNTER__)

/* Sometimes the system defines MAX/MIN, sometimes not.
   In the latter case, define those here since we will use them */

#ifndef MAX
#define _MAX(a, b, aa, bb) \
    ({ __typeof__(a) aa = (a); \
       __typeof__(b) bb = (b); \
       aa > bb ? aa : bb; })

#define MAX(a, b) \
        _MAX(a, b, TR_UNIQUE(tmpa), TR_UNIQUE(tmpb))
#endif

#ifndef MIN
#define _MIN(a, b, aa, bb) \
    ({ __typeof__(a) aa = (a); \
       __typeof__(b) bb = (b); \
       aa > bb ? bb : aa; })

#define MIN(a, b) \
        _MIN(a, b, TR_UNIQUE(tmpa), TR_UNIQUE(tmpb))
#endif

/***
****
***/

/* Only use this macro to suppress false-positive alignment warnings */
#define TR_DISCARD_ALIGN(ptr, type) ((type)(void*)(ptr))

#define SHA_DIGEST_LENGTH 20

#define TR_INET6_ADDRSTRLEN 46

#define TR_BAD_SIZE ((size_t)-1)
