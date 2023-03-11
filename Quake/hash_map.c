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

#include "quakedef.h"

#define MIN_KEY_VALUE_STORAGE_SIZE 16
#define MIN_HASH_SIZE			   32

typedef struct hash_map_s
{
	uint32_t num_entries;
	uint32_t hash_size;
	uint32_t key_value_storage_size;
	uint32_t key_size;
	uint32_t value_size;
	uint32_t (*hasher) (const void *const);
	qboolean (*comp) (const void *const, const void *const);
	uint32_t *hash_to_index;
	uint32_t *index_chain;
	void	 *keys;
	void	 *values;
} hash_map_t;

/*
=================
HashMap_GetKeyImpl
=================
*/
void *HashMap_GetKeyImpl (hash_map_t *map, uint32_t index)
{
	return (byte *)map->keys + (map->key_size * index);
}

/*
=================
HashMap_GetValueImpl
=================
*/
void *HashMap_GetValueImpl (hash_map_t *map, uint32_t index)
{
	return (byte *)map->values + (map->value_size * index);
}

/*
=================
HashMap_Rehash
=================
*/
static void HashMap_Rehash (hash_map_t *map, const uint32_t new_size)
{
	if (map->hash_size >= new_size)
		return;
	map->hash_size = new_size;
	map->hash_to_index = Mem_Realloc (map->hash_to_index, map->hash_size * sizeof (uint32_t));
	memset (map->hash_to_index, 0xFF, map->hash_size * sizeof (uint32_t));
	for (uint32_t i = 0; i < map->num_entries; ++i)
	{
		void		  *key = HashMap_GetKeyImpl (map, i);
		const uint32_t hash = map->hasher (key);
		const uint32_t hash_index = hash & (map->hash_size - 1);
		map->index_chain[i] = map->hash_to_index[hash_index];
		map->hash_to_index[hash_index] = i;
	}
}

/*
=================
HashMap_ExpandKeyValueStorage
=================
*/
static void HashMap_ExpandKeyValueStorage (hash_map_t *map, const uint32_t new_size)
{
	map->keys = Mem_Realloc (map->keys, new_size * map->key_size);
	map->values = Mem_Realloc (map->values, new_size * map->value_size);
	map->index_chain = Mem_Realloc (map->index_chain, new_size * sizeof (uint32_t));
	map->key_value_storage_size = new_size;
}

/*
=================
HashMap_CreateImpl
=================
*/
hash_map_t *HashMap_CreateImpl (
	const uint32_t key_size, const uint32_t value_size, uint32_t (*hasher) (const void *const), qboolean (*comp) (const void *const, const void *const))
{
	hash_map_t *map = Mem_Alloc (sizeof (hash_map_t));
	map->key_size = key_size;
	map->value_size = value_size;
	map->hasher = hasher;
	map->comp = comp;
	return map;
}

/*
=================
HashMap_Destroy
=================
*/
void HashMap_Destroy (hash_map_t *map)
{
	Mem_Free (map->hash_to_index);
	Mem_Free (map->index_chain);
	Mem_Free (map->keys);
	Mem_Free (map->values);
	Mem_Free (map);
}

/*
=================
HashMap_Reserve
=================
*/
void HashMap_Reserve (hash_map_t *map, int capacity)
{
	const uint32_t new_key_value_storage_size = Q_nextPow2 (capacity);
	if (map->key_value_storage_size < new_key_value_storage_size)
		HashMap_ExpandKeyValueStorage (map, new_key_value_storage_size);
	const uint32_t new_hash_size = Q_nextPow2 (capacity + (capacity / 4));
	if (map->hash_size < new_hash_size)
		HashMap_Rehash (map, new_hash_size);
}

/*
=================
HashMap_InsertImpl
=================
*/
qboolean HashMap_InsertImpl (hash_map_t *map, const uint32_t key_size, const uint32_t value_size, const void *const key, const void *const value)
{
	assert (map->key_size == key_size);
	assert (map->value_size == value_size);

	if (map->num_entries >= map->key_value_storage_size)
		HashMap_ExpandKeyValueStorage (map, q_max (map->key_value_storage_size * 2, MIN_KEY_VALUE_STORAGE_SIZE));
	if ((map->num_entries + (map->num_entries / 4)) >= map->hash_size)
		HashMap_Rehash (map, q_max (map->hash_size * 2, MIN_HASH_SIZE));

	const uint32_t hash = map->hasher (key);
	const uint32_t hash_index = hash & (map->hash_size - 1);
	{
		uint32_t storage_index = map->hash_to_index[hash_index];
		while (storage_index != UINT32_MAX)
		{
			const void *const storage_key = HashMap_GetKeyImpl (map, storage_index);
			if (map->comp ? map->comp (key, storage_key) : (memcmp (key, storage_key, key_size) == 0))
			{
				memcpy (HashMap_GetValueImpl (map, storage_index), value, value_size);
				return true;
			}
			storage_index = map->index_chain[storage_index];
		}
	}

	map->index_chain[map->num_entries] = map->hash_to_index[hash_index];
	map->hash_to_index[hash_index] = map->num_entries;
	memcpy (HashMap_GetKeyImpl (map, map->num_entries), key, key_size);
	memcpy (HashMap_GetValueImpl (map, map->num_entries), value, value_size);
	++map->num_entries;

	return false;
}

/*
=================
HashMap_EraseImpl
=================
*/
qboolean HashMap_EraseImpl (hash_map_t *map, const uint32_t key_size, const void *const key)
{
	assert (key_size == map->key_size);
	if (map->num_entries == 0)
		return false;

	const uint32_t hash = map->hasher (key);
	const uint32_t hash_index = hash & (map->hash_size - 1);
	uint32_t	   storage_index = map->hash_to_index[hash_index];
	uint32_t	  *prev_storage_index_ptr = NULL;
	while (storage_index != UINT32_MAX)
	{
		const void *storage_key = HashMap_GetKeyImpl (map, storage_index);
		if (map->comp ? map->comp (key, storage_key) : (memcmp (key, storage_key, key_size) == 0))
		{
			{
				// Remove found key from index
				if (prev_storage_index_ptr == NULL)
					map->hash_to_index[hash_index] = map->index_chain[storage_index];
				else
					*prev_storage_index_ptr = map->index_chain[storage_index];
			}

			const uint32_t last_index = map->num_entries - 1;
			const uint32_t last_hash = map->hasher (HashMap_GetKeyImpl (map, last_index));
			const uint32_t last_hash_index = last_hash & (map->hash_size - 1);

			if (storage_index == last_index)
			{
				--map->num_entries;
				return true;
			}

			{
				// Remove last key from index
				if (map->hash_to_index[last_hash_index] == last_index)
					map->hash_to_index[last_hash_index] = map->index_chain[last_index];
				else
				{
					qboolean found = false;
					for (uint32_t last_storage_index = map->hash_to_index[last_hash_index]; last_storage_index != UINT32_MAX;
						 last_storage_index = map->index_chain[last_storage_index])
					{
						if (map->index_chain[last_storage_index] == last_index)
						{
							map->index_chain[last_storage_index] = map->index_chain[last_index];
							found = true;
							break;
						}
					}
					(void)found;
					assert (found);
				}
			}

			{
				// Copy last key to current key position and add back to index
				memcpy (HashMap_GetKeyImpl (map, storage_index), HashMap_GetKeyImpl (map, last_index), map->key_size);
				memcpy (HashMap_GetValueImpl (map, storage_index), HashMap_GetValueImpl (map, last_index), map->value_size);
				map->index_chain[storage_index] = map->hash_to_index[last_hash_index];
				map->hash_to_index[last_hash_index] = storage_index;
			}

			--map->num_entries;
			return true;
		}
		prev_storage_index_ptr = &map->index_chain[storage_index];
		storage_index = map->index_chain[storage_index];
	}
	return false;
}

/*
=================
HashMap_LookupImpl
=================
*/
void *HashMap_LookupImpl (hash_map_t *map, const uint32_t key_size, const void *const key)
{
	assert (map->key_size == key_size);

	if (map->num_entries == 0)
		return NULL;

	const uint32_t hash = map->hasher (key);
	const uint32_t hash_index = hash & (map->hash_size - 1);
	uint32_t	   storage_index = map->hash_to_index[hash_index];
	while (storage_index != UINT32_MAX)
	{
		const void *const storage_key = HashMap_GetKeyImpl (map, storage_index);
		if (map->comp ? map->comp (key, storage_key) : (memcmp (key, storage_key, key_size) == 0))
			return (byte *)map->values + (storage_index * map->value_size);
		storage_index = map->index_chain[storage_index];
	}

	return NULL;
}

/*
=================
HashMap_Size
=================
*/
uint32_t HashMap_Size (hash_map_t *map)
{
	return map->num_entries;
}

#ifdef _DEBUG
/*
=================
HashMap_TestAssert
=================
*/
#define HashMap_TestAssert(cond, what) \
	if (!(cond))                       \
	{                                  \
		Con_Printf ("%s", what);       \
		abort ();                      \
	}

/*
=================
HashMap_BasicTest
=================
*/
static void HashMap_BasicTest (const qboolean reserve)
{
	const int	TEST_SIZE = 1000;
	hash_map_t *map = HashMap_Create (int32_t, int64_t, &HashInt32, NULL);
	if (reserve)
		HashMap_Reserve (map, TEST_SIZE);
	for (int i = 0; i < TEST_SIZE; ++i)
	{
		int64_t value = i;
		HashMap_TestAssert (!HashMap_Insert (map, &i, &value), va ("%d should not be overwritten\n", i));
	}
	for (int i = 0; i < TEST_SIZE; ++i)
		HashMap_TestAssert (*HashMap_Lookup (int64_t, map, &i) == i, va ("Wrong lookup for %d\n", i));
	for (int i = 0; i < TEST_SIZE; i += 2)
		HashMap_Erase (map, &i);
	for (int i = 1; i < TEST_SIZE; i += 2)
		HashMap_TestAssert (*HashMap_Lookup (int64_t, map, &i) == i, va ("Wrong lookup for %d\n", i));
	for (int i = 0; i < TEST_SIZE; i += 2)
		HashMap_TestAssert (HashMap_Lookup (int64_t, map, &i) == NULL, va ("Wrong lookup for %d\n", i));
	for (int i = 0; i < TEST_SIZE; ++i)
		HashMap_Erase (map, &i);
	HashMap_TestAssert (HashMap_Size (map) == 0, "Map is not empty");
	for (int i = 0; i < TEST_SIZE; ++i)
		HashMap_TestAssert (HashMap_Lookup (int64_t, map, &i) == NULL, va ("Wrong lookup for %d\n", i));
	HashMap_Destroy (map);
}

/*
=================
HashMap_BasicTest
=================
*/
static void HashMap_StressTest (void)
{
	srand (0);
	const int TEST_SIZE = 10000;
	TEMP_ALLOC (int64_t, keys, TEST_SIZE);
	hash_map_t *map = HashMap_Create (int64_t, int32_t, &HashInt64, NULL);
	for (int j = 0; j < 10; ++j)
	{
		for (int i = 0; i < TEST_SIZE; ++i)
		{
			keys[i] = i;
		}
		for (int i = TEST_SIZE - 1; i > 0; --i)
		{
			const int swap_index = rand () % (i + 1);
			const int temp = keys[swap_index];
			keys[swap_index] = keys[i];
			keys[i] = temp;
		}
		for (int i = 0; i < TEST_SIZE; ++i)
			HashMap_Insert (map, &keys[i], &i);
		for (int i = 0; i < TEST_SIZE; ++i)
			HashMap_TestAssert (*HashMap_Lookup (int32_t, map, &keys[i]) == i, va ("Wrong lookup for %d\n", i));
		for (int i = TEST_SIZE - 1; i >= 0; --i)
			HashMap_Erase (map, &keys[i]);
		HashMap_TestAssert (HashMap_Size (map) == 0, "Map is not empty");
	}
	HashMap_Destroy (map);
	TEMP_FREE (keys);
}

/*
=================
TestHashMap_f
=================
*/
void TestHashMap_f (void)
{
	HashMap_BasicTest (false);
	HashMap_BasicTest (true);
	HashMap_StressTest ();
}
#endif