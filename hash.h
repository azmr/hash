/* TODO
 * - varying semantics based on whether key is already in table
 *   - `insert` succeeds if not already there
 *   - `update` succeeds only if already there
 *   - `set` inserts or updates
 *   - `put`?
 *   - `has`
 *   - partial read/update via ptr/index
 * - version with/without iterable keys?
 *
 */

#if !defined(MAP_TYPES)
# error map, key and value types must be defined
#endif

#ifndef  MAP_API
# define MAP_API static
#endif /*MAP_API*/

#ifndef  MAP_INVALID_VAL
# define MAP_INVALID_VAL 0
#endif /*MAP_INVALID_VAL*/

#ifndef  MAP_INVALID_KEY
# define MAP_INVALID_KEY 0
#endif /*MAP_INVALID_KEY*/

#ifndef  MAP_MIN_ELEMENTS
# define MAP_MIN_ELEMENTS 4
#endif /*MAP_MIN_ELEMENTS*/

#ifndef  MAP_GENERIC
# define MAP_GENERIC
# define map__mod_pow2(a, x) ((a) & (x - 1))
# define map__assert(e) assert(e)
# define Map_Load_Factor 2
#endif /*MAP_GENERIC*/

#define MAP_TYPE(type, func, key, val) type
#define MAP_FUNC(type, func, key, val) func
#define MAP_KEY(type, func, key, val) key
#define MAP_VAL(type, func, key, val) val

#define MAP_CAT1(a,b) a ## b
#define MAP_CAT2(a,b) MAP_CAT1(a,b)
#define MAP_CAT(a,b)  MAP_CAT2(a,b)

#define Map    MAP_TYPES(MAP_TYPE)
#define map_fn MAP_TYPES(MAP_FUNC)
#define MapKey MAP_TYPES(MAP_KEY)
#define MapVal MAP_TYPES(MAP_VAL)
#define MAP_DECORATE_TYPE(x) MAP_CAT(Map, x)
#define MAP_DECORATE_FUNC(x) MAP_CAT(map_fn, _ ## x)

#define MapEntry MAP_DECORATE_TYPE(Entry)
#define MapSlots MAP_DECORATE_TYPE(Slots)
#define Map_Invalid_Key MAP_DECORATE_TYPE(_Invalid_Key)
#define Map_Invalid_Val MAP_DECORATE_TYPE(_Invalid_Val)

#define map__hash           MAP_DECORATE_FUNC(_hash)
#define map__slots          MAP_DECORATE_FUNC(_hashed_slots)
#define map__hashed_keys    MAP_DECORATE_FUNC(_hashed_keys)
#define map__probe_linear   MAP_DECORATE_FUNC(_probe_linear)
#define map__hashed_entries MAP_DECORATE_FUNC(_hashed_entries)

// USER FUNCTIONS:
#define map_has             MAP_DECORATE_FUNC(has)
#define map_ptr             MAP_DECORATE_FUNC(ptr)
#define map_get             MAP_DECORATE_FUNC(get)
#define map_set             MAP_DECORATE_FUNC(set)
#define map_clear           MAP_DECORATE_FUNC(clear)
#define map_update          MAP_DECORATE_FUNC(update)
#define map_insert          MAP_DECORATE_FUNC(insert)
#define map_remove          MAP_DECORATE_FUNC(remove)
#define map_resize          MAP_DECORATE_FUNC(resize)


#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

static MapKey Map_Invalid_Key = MAP_INVALID_KEY;
static MapVal Map_Invalid_Val = MAP_INVALID_VAL;

typedef struct MapEntry {
	MapVal   val;
	uint64_t key_i;
} MapEntry;

typedef struct MapSlots {
	MapKey   *keys;
	MapEntry *entries;
} MapSlots;

typedef struct Map {
	void   *slots;
	MapKey *keys;
	size_t keys_max; // always a power of 2; the size of the array is 2x this (allows super-quick mod_pow2)
	size_t keys_n;
} Map;

static inline MapKey   *map__hashed_keys(Map *map)
{ return (MapKey *) map->slots; }
static inline MapEntry *map__hashed_entries(Map *map)
{ return (MapEntry *) ((char *)map->slots + Map_Load_Factor * map->keys_max * sizeof(MapKey)); }
static inline MapSlots map__slots(Map *map) {
	MapSlots result;
	result.keys    = map__hashed_keys(map);
	result.entries = map__hashed_entries(map);
	return result;
}

// (as obliged by Casey) TODO: better hash function
static uint64_t map__hash(MapKey key) {
	uintptr_t hash = (uintptr_t)key;
	map__assert(key != Map_Invalid_Key);
	hash *= 0xff51afd7ed558ccd;
	hash ^= hash >> 32;
	return hash;
}

static uint64_t map__probe_linear(Map *map, MapKey key) {
	uint64_t slots_n = Map_Load_Factor * map->keys_max,
	         hash_i  = map__mod_pow2(map__hash(key), slots_n);
	MapKey *keys = map__hashed_keys(map);
	for(uint64_t i = 0; i < slots_n; ++i) {
		// TODO: don't really need to check for n as should never be full!
		// unless aren't able to alloc any more? Just suck up the performance hit but keep working...?
		uint64_t slot_i = map__mod_pow2(hash_i + i, slots_n);
		MapKey slot_key = keys[slot_i];
		if ( slot_key == 0 || slot_key == key)
		{ return slot_i; }
	}

	return hash_i;
}

MAP_API MapVal * map_ptr(Map *map, MapKey key) {
	uint64_t slot_i = map__probe_linear(map, key);
	if (map->keys_n && key == map__hashed_keys(map)[slot_i])
	{ return &map__hashed_entries(map)[slot_i].val; }
	else
	{ return 0; }
}

MAP_API MapVal map_get(Map *map, MapKey key) {
	uint64_t slot_i = map__probe_linear(map, key);
	if (map->keys_n && key == map__hashed_keys(map)[slot_i])
	{ return map__hashed_entries(map)[slot_i].val; }
	else
	{ return Map_Invalid_Val; }
}

// returns non-zero if map contains key
MAP_API int map_has(Map *map, MapKey key) {
	uint64_t slot_i = map__probe_linear(map, key);
	return map->keys_n && key == map__hashed_keys(map)[slot_i];
}

// returns non-zero on success
MAP_API int map_resize(Map *map, uint64_t values_n)
{
	int result = 0;
	Map old = *map, new = old;

	uint64_t n = values_n ? values_n : MAP_MIN_ELEMENTS; // account for unalloc'd
	--n, n|=n>>1, n|=n>>2, n|=n>>4, n|=n>>8, n|=n>>16, n|=n>>32, ++n; // ceiling pow 2
	new.keys_max = n;

	uint64_t new_slots_n     = new.keys_max * Map_Load_Factor;
	size_t linear_array_size = new.keys_max *  sizeof(MapKey),
		   slots_size        = new_slots_n  * (sizeof(MapKey) + sizeof(MapVal)),
		   size_new          = linear_array_size + slots_size;

	if (new.keys_max > old.keys_max) {
		if (!(new.keys = malloc(size_new))) { goto end; }
		free(old.keys);
	}
	else if (new.keys_max < old.keys_max) {
		MapKey *NewKeys = realloc(old.keys, size_new); // should shrink in place...
		if (! NewKeys) { goto end; }                   // old.keys and new.keys are now aliases
		new.keys = NewKeys;
	}
	else  // (new.keys_max == old.keys_max) -> no need to resize
	{   result = 1; goto end;    }
	result = 1;

	new.slots = (char *)new.keys + new.keys_max*sizeof(MapKey);
	MapSlots new_slots	  = map__slots(&new);
	MapEntry *old_entries = map__hashed_entries(&old);

	for(uint64_t i = 0; i < new_slots_n; ++i)
	{   new_slots.keys[i] = Map_Invalid_Key;   }

	for(uint64_t i = 0; i < new.keys_n; ++i)
	{
		uint64_t new_slot_i, old_slot_i;
		MapKey key;
		key = new.keys[i] = old.keys[i];
		new_slot_i = map__probe_linear(&new, key);
		old_slot_i = map__probe_linear(&old, key);

		new_slots.keys   [new_slot_i]       = key;
		new_slots.entries[new_slot_i].val   = old_entries[old_slot_i].val;
		new_slots.entries[new_slot_i].key_i = i;
	}

	*map = new;

end: return result;
}


// -1 - isn't in map, couldn't allocate sufficient space
//  0 - wasn't previously in map, successfully inserted
//  1 - was already in map, successfully updated
MAP_API int map_set(Map *map, MapKey key, MapVal val) {
	MapSlots slots	       = map__slots(map);
	uint64_t slot_i        = map__probe_linear(map, key);
	int key_already_in_map = map->keys_n && slots.keys[slot_i] == key,
		sufficient_space   = 1;
	map__assert(key_already_in_map || !map->keys_n || slots.keys[slot_i] == 0);

	if (! key_already_in_map  &&  (map->keys_n + 1) > map->keys_max)
	{
		sufficient_space = map_resize(map, Map_Load_Factor*map->keys_max);
		slot_i           = map__probe_linear(map, key);
		slots            = map__slots(map);
	}

	slots.keys[slot_i]          = key;
	map->keys[map->keys_n]      = key;
	slots.entries[slot_i].key_i = map->keys_n++;
	slots.entries[slot_i].val   = val;

	return key_already_in_map ? key_already_in_map : -!sufficient_space;
}

// -1 - isn't in map, couldn't allocate sufficient space
//  0 - wasn't previously in map, successfully inserted
//  1 - was already in map
MAP_API int map_insert(Map *map, MapKey key, MapVal val) {
	MapSlots slots         = map__slots(map);
	uint64_t slot_i        = map__probe_linear(map, key);
	int key_already_in_map = map->keys_n && slots.keys[slot_i] == key,
		sufficient_space   = 1;
	map__assert(key_already_in_map || !map->keys_n || slots.keys[slot_i] == 0);

	if (! key_already_in_map)
	{
		if (map->keys_n + 1 > map->keys_max)
		{
			sufficient_space = map_resize(map, Map_Load_Factor*map->keys_max);
			slot_i           = map__probe_linear(map, key);
			slots            = map__slots(map);
		}

		slots.keys[slot_i]          = key;
		map->keys[map->keys_n]      = key;
		slots.entries[slot_i].key_i = map->keys_n++;
		slots.entries[slot_i].val   = val;
	}

	return key_already_in_map ? key_already_in_map : -!sufficient_space;
}

//  0 - isn't in map, no change
//  1 - was already in map, successfully updated
MAP_API int map_update(Map *map, MapKey key, MapVal val) {
	MapSlots slots         = map__slots(map);
	uint64_t slot_i        = map__probe_linear(map, key);
	int key_already_in_map = map->keys_n && slots.keys[slot_i] == key;
	map__assert(key_already_in_map || !map->keys_n || slots.keys[slot_i] == 0);

	if (key_already_in_map) {
		slots.keys[slot_i]          = key;
		map->keys[map->keys_n]      = key;
		slots.entries[slot_i].key_i = map->keys_n++;
		slots.entries[slot_i].val   = val;
	}

	return key_already_in_map;
}

/* Valid layouts for rearranging:
 * | - - a - c d e f - x y z - - - - - |
 *	   h i	 j   X
 * |d e f - x y z - - - - -  - - a - c |
 *	j   X					  h i   
 * | - c d e f - x y z - - - - - - - a |
 *   i	 j   X					 h 
 *
 * i.e. i is inside the modular interval of [h,j)
 * (if h == j, it's already in the right place and
 * nothing should be done)
 *
 * Should never get to this:
 * | - a - - c d e f - x y z - - - - - |
 *	 h f i	 j   X   
 * ...as e should have linear probed into position f
 */
MAP_API MapVal map_remove(Map *map, MapKey key)
{
	MapSlots slots      = map__slots(map);
	uint64_t slot_empty = map__probe_linear(map, key);
	int key_is_in_map   = map->keys_n && slots.keys[slot_empty] == key;

	MapVal result = key_is_in_map ? slots.entries[slot_empty].val : Map_Invalid_Val;
	if (! key_is_in_map) { goto end; }

	slots.keys[slot_empty] = Map_Invalid_Key;
	// quick remove (end-swap) from packed key array
	uint64_t  removed_key_linear_i  = slots.entries[slot_empty].key_i; 
	map->keys[removed_key_linear_i] =  map->keys[--map->keys_n];

	// move back elements to make sure they're valid for linear-probing
	// go through all contiguous filled keys following deleted one
	uint64_t slots_n = Map_Load_Factor * map->keys_max;
	for(uint64_t slot_check = map__mod_pow2(slot_empty + 1, slots_n);
			slots.keys[slot_check] != Map_Invalid_Key;
			slot_check      = map__mod_pow2(slot_check + 1, slots_n))
	{
		// NOTE: adding slots_n to keep positive, primarily to avoid oddities with mod
		uint64_t slot_ideal	           = map__mod_pow2(map__hash(key), slots_n),
				 d_from_ideal_to_empty = map__mod_pow2((slots_n + slot_empty - slot_ideal), slots_n),
				 d_from_ideal_to_check = map__mod_pow2((slots_n + slot_check - slot_ideal), slots_n);

		if (d_from_ideal_to_empty < d_from_ideal_to_check) // half open as explained above
		{ // move element from check slot to empty slot
			map__assert(slot_empty == map__probe_linear(map, key) &&
					"The check slot's content should be where it would have been had "
					"the empty slot never been filled");
			slots.keys   [slot_empty] = slots.keys   [slot_check];
			slots.entries[slot_empty] = slots.entries[slot_check];
			// check slot is now empty, so subsequent checks will be against that
			slot_empty = slot_check;
		}
	}

end: return result;
}

MAP_API uint64_t map_clear(Map *map) {
	MapSlots slots = map__slots(map);
	uint64_t slots_n = Map_Load_Factor * map->keys_max,
			 keys_n = map->keys_n;
	for(uint64_t i = 0; i < map->keys_n; map->keys[i++] = Map_Invalid_Key);
	for(uint64_t i = 0; i < slots_n;    slots.keys[i++] = Map_Invalid_Key);
	map->keys_n = 0;
	return keys_n;
}

#undef MAP_INVALID_KEY
#undef MAP_INVALID_VAL

#undef MAP_TYPE
#undef MAP_FUNC
#undef MAP_KEY
#undef MAP_VAL

#undef Map
#undef map_fn
#undef MapKey
#undef MapVal
#undef MAP_DECORATE_TYPE
#undef MAP_DECORATE_FUNC

#undef MapEntry
#undef MapSlots
#undef Map_Invalid_Key
#undef Map_Invalid_Val

#undef map__hash
#undef map__slots
#undef map__hashed_keys
#undef map__probe_linear
#undef map__hashed_entries

// USER FUNCTIONS:
#undef map_has
#undef map_get
#undef map_set
#undef map_clear
#undef map_update
#undef map_insert
#undef map_remove
#undef map_resize
#undef MAP_TYPES

#undef MAP_CAT1
#undef MAP_CAT2
#undef MAP_CAT





