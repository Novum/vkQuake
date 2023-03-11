/*
Copyright (C) 2023 Axel Gneiting

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

typedef struct hash_map_s hash_map_t;

hash_map_t *HashMap_CreateImpl (
	const uint32_t key_size, const uint32_t value_size, uint32_t (*hasher) (const void *const), qboolean (*comp) (const void *const, const void *const));
void	 HashMap_Destroy (hash_map_t *map);
void	 HashMap_Reserve (hash_map_t *map, int capacity);
qboolean HashMap_InsertImpl (hash_map_t *map, const uint32_t key_size, const uint32_t value_size, const void *const key, const void *const value);
qboolean HashMap_EraseImpl (hash_map_t *map, const uint32_t key_size, const void *const key);
void	*HashMap_LookupImpl (hash_map_t *map, const uint32_t key_size, const void *const key);
uint32_t HashMap_Size (hash_map_t *map);
void	*HashMap_GetKeyImpl (hash_map_t *map, uint32_t index);
void	*HashMap_GetValueImpl (hash_map_t *map, uint32_t index);

#define HashMap_Create(key_type, value_type, hasher, comp) HashMap_CreateImpl (sizeof (key_type), sizeof (value_type), hasher, comp)
#define HashMap_Insert(map, key, value)					   HashMap_InsertImpl (map, sizeof (*key), sizeof (*value), key, value)
#define HashMap_Erase(map, key)							   HashMap_EraseImpl (map, sizeof (*key), key)
#define HashMap_Lookup(type, map, key)					   ((type *)HashMap_LookupImpl (map, sizeof (*key), key))
#define HashMap_GetKey(type, map, index)				   ((type *)HashMap_GetKeyImpl (map, index))
#define HashMap_GetValue(type, map, index)				   ((type *)HashMap_GetValueImpl (map, index))

// Murmur3 fmix32
static inline uint32_t HashInt32 (const void *const val)
{
	uint32_t h = *(uint32_t *)val;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h;
}

// Murmur3 fmix64
static inline uint32_t HashInt64 (const void *const val)
{
	uint64_t k = *(uint64_t *)val;
	k ^= k >> 33;
	k *= 0xff51afd7ed558ccdull;
	k ^= k >> 33;
	k *= 0xc4ceb9fe1a85ec53ull;
	k ^= k >> 33;
	// Truncates, but all bits should be equally good
	return k;
}

static inline uint32_t HashFloat (const void *const val)
{
	uint32_t float_bits;
	memcpy (&float_bits, val, sizeof (uint32_t));
	if (float_bits == 0x80000000)
		float_bits = 0;
	return HashInt32 (&float_bits);
}

static inline uint32_t HashPtr (const void *const val)
{
	if (sizeof (void *) == sizeof (uint64_t))
		return HashInt64 (val);
	return HashInt32 (val);
}

// Murmur3 hash combine
static inline uint32_t HashCombine (uint32_t a, uint32_t b)
{
	a *= 0xcc9e2d51;
	a = (a >> 17) | (a << 15);
	a *= 0x1b873593;
	b ^= a;
	b = (b >> 19) | (b << 13);
	return (b * 5) + 0xe6546b64;
}

static inline uint32_t HashVec2 (const void *const val)
{
	vec2_t *vec = (vec2_t *)val;
	return HashCombine (HashFloat (&(*vec)[0]), HashFloat (&(*vec)[1]));
}

static inline uint32_t HashVec3 (const void *const val)
{
	vec3_t *vec = (vec3_t *)val;
	return HashCombine (HashFloat (&(*vec)[0]), HashCombine (HashFloat (&(*vec)[1]), HashFloat (&(*vec)[2])));
}

// FNV-1a hash
static inline uint32_t HashStr (const void *const val)
{
	const unsigned char	 *str = *(const unsigned char **)val;
	static const uint32_t FNV_32_PRIME = 0x01000193;

	uint32_t hval = 0;
	while (*str)
	{
		hval ^= (uint32_t)*str;
		hval *= FNV_32_PRIME;
		++str;
	}

	return hval;
}

static inline qboolean HashStrCmp (const void *const a, const void *const b)
{
	const char *str_a = *(const char **)a;
	const char *str_b = *(const char **)b;
	return strcmp (str_a, str_b) == 0;
}

#ifdef _DEBUG
void TestHashMap_f (void);
#endif