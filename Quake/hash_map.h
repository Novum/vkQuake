/*
Copyright (C) 2023 Axel Gneiting
*/

typedef struct hash_map_s hash_map_t;

hash_map_t *HashMap_CreateImpl (const uint32_t key_size, const uint32_t value_size, uint32_t (*hasher) (const void *const));
void		HashMap_Destroy (hash_map_t *map);
void		HashMap_Reserve (hash_map_t *map, int capacity);
qboolean	HashMap_InsertImpl (hash_map_t *map, const uint32_t key_size, const uint32_t value_size, const void *const key, const void *const value);
qboolean	HashMap_EraseImpl (hash_map_t *map, const uint32_t key_size, const void *const key);
void	   *HashMap_LookupImpl (hash_map_t *map, const uint32_t key_size, const void *const key);
uint32_t	HashMap_Size (hash_map_t *map);
void	   *HashMap_GetKeyImpl (hash_map_t *map, uint32_t index);
void	   *HashMap_GetValueImpl (hash_map_t *map, uint32_t index);

#define HashMap_Create(key_type, value_type, hasher) HashMap_CreateImpl (sizeof (key_type), sizeof (value_type), hasher)
#define HashMap_Insert(map, key, value)				 HashMap_InsertImpl (map, sizeof (*key), sizeof (*value), key, value)
#define HashMap_Erase(map, key)						 HashMap_EraseImpl (map, sizeof (*key), key)
#define HashMap_Lookup(type, map, key)				 ((type *)HashMap_LookupImpl (map, sizeof (*key), key))
#define HashMap_GetKey(type, map, index)			 ((type *)HashMap_GetKeyImpl (map, index))
#define HashMap_GetValue(type, map, index)			 ((type *)HashMap_GetValueImpl (map, index))

// Murmur3 fmix32
static inline uint32_t HashInt32 (const uint32_t *const val)
{
	uint32_t h = *val;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h;
}

// Murmur3 fmix64
static inline uint32_t HashInt64 (const uint64_t *const val)
{
	uint64_t k = *val;
	k ^= k >> 33;
	k *= 0xff51afd7ed558ccdull;
	k ^= k >> 33;
	k *= 0xc4ceb9fe1a85ec53ull;
	k ^= k >> 33;
	// Truncates, but all bits should be equally good
	return k;
}

static inline uint32_t HashFloat (const float *const val)
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

#ifdef _DEBUG
void TestHashMap_f (void);
#endif