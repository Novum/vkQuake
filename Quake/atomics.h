/*
 * atomics.h -- Atomics
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __ATOMICS_H
#define __ATOMICS_H

#ifdef _MSC_VER
#include <stdint.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <stdatomic.h>
#endif

#include "q_stdinc.h"

#ifdef _MSC_VER
// Microsoft doesn't implement C11 atomics yet
typedef struct
{
	volatile uint8_t value;
} atomic_uint8_t;

static inline uint8_t Atomic_OrUInt8 (volatile atomic_uint8_t *atomic, uint8_t val)
{
	return InterlockedOr8 ((volatile char *)&atomic->value, val);
}

typedef struct
{
	volatile uint32_t value;
} atomic_uint32_t;

static inline uint32_t Atomic_LoadUInt32 (volatile atomic_uint32_t *atomic)
{
	_WriteBarrier ();
	return atomic->value;
}

static inline void Atomic_StoreUInt32 (volatile atomic_uint32_t *atomic, uint32_t desired)
{
	atomic->value = desired;
	_ReadBarrier ();
}

static inline qboolean Atomic_CompareExchangeUInt32 (atomic_uint32_t *atomic, uint32_t *expected, uint32_t desired)
{
	const uint32_t actual = InterlockedCompareExchange ((volatile LONG *)&atomic->value, desired, *expected);
	if (actual == *expected)
	{
		return true;
	}
	*expected = actual;
	return false;
}

static inline uint32_t Atomic_AddUInt32 (volatile atomic_uint32_t *atomic, uint32_t value)
{
	return InterlockedAdd ((volatile LONG *)&atomic->value, value) - value;
}

static inline uint32_t Atomic_SubUInt32 (volatile atomic_uint32_t *atomic, uint32_t value)
{
	return InterlockedAdd ((volatile LONG *)&atomic->value, ~value + 1) + value;
}

static inline uint32_t Atomic_OrUInt32 (volatile atomic_uint32_t *atomic, uint32_t val)
{
	return InterlockedOr ((volatile LONG *)&atomic->value, val);
}

static inline uint32_t Atomic_IncrementUInt32 (volatile atomic_uint32_t *atomic)
{
	return InterlockedIncrement ((volatile LONG *)&atomic->value) - 1;
}

static inline uint32_t Atomic_DecrementUInt32 (volatile atomic_uint32_t *atomic)
{
	return InterlockedDecrement ((volatile LONG *)&atomic->value) + 1;
}

typedef struct
{
	volatile uint64_t value;
} atomic_uint64_t;

static inline uint64_t Atomic_LoadUInt64 (atomic_uint64_t *atomic)
{
	_WriteBarrier ();
	return atomic->value;
}

static inline void Atomic_StoreUInt64 (atomic_uint64_t *atomic, uint64_t desired)
{
	atomic->value = desired;
	_ReadBarrier ();
}

static inline qboolean Atomic_CompareExchangeUInt64 (atomic_uint64_t *atomic, uint64_t *expected, uint64_t desired)
{
	const uint64_t actual = InterlockedCompareExchange64 ((volatile LONG64 *)&atomic->value, desired, *expected);
	if (actual == *expected)
	{
		return true;
	}
	*expected = actual;
	return false;
}

static inline uint64_t Atomic_IncrementUInt64 (volatile atomic_uint64_t *atomic)
{
	return InterlockedIncrement64 ((volatile LONG64 *)&atomic->value) - 1;
}

static inline uint64_t Atomic_AddUInt64 (volatile atomic_uint64_t *atomic, uint64_t value)
{
	return InterlockedAdd64 ((volatile LONG64 *)&atomic->value, value) - value;
}

static inline uint64_t Atomic_SubUInt64 (volatile atomic_uint64_t *atomic, uint64_t value)
{
	return InterlockedAdd64 ((volatile LONG64 *)&atomic->value, (~value) + 1) + value;
}
#else
typedef _Atomic uint8_t atomic_uint8_t;

static inline uint8_t Atomic_OrUInt8 (atomic_uint8_t *atomic, uint8_t val)
{
	return atomic_fetch_or (atomic, val);
}

typedef _Atomic uint32_t atomic_uint32_t;

static inline uint32_t Atomic_LoadUInt32 (atomic_uint32_t *atomic)
{
	return atomic_load (atomic);
}

static inline void Atomic_StoreUInt32 (atomic_uint32_t *atomic, uint32_t desired)
{
	atomic_store (atomic, desired);
}

static inline uint32_t Atomic_AddUInt32 (atomic_uint32_t *atomic, uint32_t value)
{
	return atomic_fetch_add (atomic, value);
}

static inline uint32_t Atomic_SubUInt32 (atomic_uint32_t *atomic, uint32_t value)
{
	return atomic_fetch_sub (atomic, value);
}

static inline uint32_t Atomic_OrUInt32 (atomic_uint32_t *atomic, uint32_t value)
{
	return atomic_fetch_or (atomic, value);
}

static inline uint32_t Atomic_IncrementUInt32 (atomic_uint32_t *atomic)
{
	return atomic_fetch_add (atomic, 1);
}

static inline uint32_t Atomic_DecrementUInt32 (atomic_uint32_t *atomic)
{
	return atomic_fetch_sub (atomic, 1);
}

typedef _Atomic uint64_t atomic_uint64_t;

static inline uint64_t Atomic_LoadUInt64 (atomic_uint64_t *atomic)
{
	return atomic_load (atomic);
}

static inline void Atomic_StoreUInt64 (atomic_uint64_t *atomic, uint64_t desired)
{
	atomic_store (atomic, desired);
}

static inline qboolean Atomic_CompareExchangeUInt32 (atomic_uint32_t *atomic, uint32_t *expected, uint32_t desired)
{
	return atomic_compare_exchange_weak (atomic, expected, desired);
}

static inline qboolean Atomic_CompareExchangeUInt64 (atomic_uint64_t *atomic, uint64_t *expected, uint64_t desired)
{
	return atomic_compare_exchange_weak (atomic, expected, desired);
}

static inline uint64_t Atomic_IncrementUInt64 (atomic_uint64_t *atomic)
{
	return atomic_fetch_add (atomic, 1);
}

static inline uint64_t Atomic_AddUInt64 (atomic_uint64_t *atomic, const uint64_t value)
{
	return atomic_fetch_add (atomic, value);
}

static inline uint64_t Atomic_SubUInt64 (atomic_uint64_t *atomic, const uint64_t value)
{
	return atomic_fetch_sub (atomic, value);
}
#endif

#endif
