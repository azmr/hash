/* TODO
 * - varying semantics based on whether key is already in table
 *   - `insert` succeeds if not already there
 *   - `update` succeeds only if already there
 *   - `set` inserts or updates
 *   - `put`?
 *   - `has`
 *   - partial read/update via ptr/index
 *
 * - user definable key equality
 *
 * - Keep separate array segment(s) via predicate(s)
 * - Only keep indexes into key/val arrays rather than duplicating key
 *
 * - multithreading without locks (CAS etc)
 *
 * - user-facing fn to add multiple keys/vals at once
 */

#if 1 // MACROS
#if !defined(MAP_TYPES)
# error map, key and value types must be defined: #define MAP_TYPES (map_type_name, function_prefix, key_type, value_type)
#endif

#ifndef  MAP_API
# define MAP_API static
#endif /*MAP_API*/

#ifndef  MAP_GENERIC
# define MAP_GENERIC
# define map__mod_pow2(a, x) ((a) & (x - 1))
# define map__assert(e) assert(e)
# define Map_Load_Factor 2
#endif /*MAP_GENERIC*/

#define MAP_CAT1(a,b) a ## b
#define MAP_CAT2(a,b) MAP_CAT1(a,b)
#define MAP_CAT(a,b)  MAP_CAT2(a,b)

#define MAP_DECORATE_TYPE(x) MAP_CAT(Map, x)
#define MAP_DECORATE_FUNC(x) MAP_CAT(map_fn, _ ## x)

#if 1 // USER TYPES
#define MapEntry MAP_DECORATE_TYPE(Entry)
#define MapSlots MAP_DECORATE_TYPE(Slots)

#if 1 // BASIC TYPES
#define MAP_TYPE(map_t, func_prefix, key_t, val_t) map_t
#define MAP_FUNC(map_t, func_prefix, key_t, val_t) func_prefix
#define MAP_KEY( map_t, func_prefix, key_t, val_t) key_t
#define MAP_VAL( map_t, func_prefix, key_t, val_t) val_t

#define Map    MAP_CAT(MAP_TYPE, MAP_TYPES)
#define map_fn MAP_CAT(MAP_FUNC, MAP_TYPES)
#define MapKey MAP_CAT(MAP_KEY,  MAP_TYPES)
#define MapVal MAP_CAT(MAP_VAL,  MAP_TYPES)

#ifndef MapIdx
#define MapIdx uint64_t
#endif/*MapIdx*/
#endif // BASIC TYPES

#if 1 // MUTEX TYPE
#define MAP_MTX_TYPE(  mtx_t, lock_fn, unlock_fn) mtx_t
#define MAP_MTX_LOCK(  mtx_t, lock_fn, unlock_fn) lock_fn
#define MAP_MTX_UNLOCK(mtx_t, lock_fn, unlock_fn) unlock_fn

#ifdef MAP_MUTEX
#define MAP_MTX(name) MAP_CAT(MAP_MTX_TYPE,   MAP_MUTEX) name;
#define MAP_LOCK      MAP_CAT(MAP_MTX_LOCK,   MAP_MUTEX)
#define MAP_UNLOCK    MAP_CAT(MAP_MTX_UNLOCK, MAP_MUTEX)
#else // MAP_MUTEX
// mutex no-ops:
#define MAP_MTX(x)
#define MAP_LOCK(x)
#define MAP_UNLOCK(x)
#endif//MAP_MUTEX
#endif // MUTEX TYPE

#endif // USER TYPES

#if 1 // FUNCTIONS
// INTERNAL FUNCTIONS:
#define map__hash            MAP_DECORATE_FUNC(_hash)
#define map__slots           MAP_DECORATE_FUNC(_hashed_slots)
#define map__hashed_keys     MAP_DECORATE_FUNC(_hashed_keys)
#define map__idx_i           MAP_DECORATE_FUNC(_idx_i)
#define map__key_i           MAP_DECORATE_FUNC(_key_i)
#define map__make_room_for   MAP_DECORATE_FUNC(_make_room_for)
#define map__hashed_entries  MAP_DECORATE_FUNC(_hashed_entries)

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
#endif // FUNCTIONS

#if 1 // USER CONSTANTS

#ifndef  MAP_MIN_ELEMENTS
# define MAP_MIN_ELEMENTS 4
#endif /*MAP_MIN_ELEMENTS*/

#ifndef  MAP_INVALID_VAL
# define MAP_INVALID_VAL {0}
#endif /*MAP_INVALID_VAL*/

#ifndef  MAP_INVALID_KEY
# define MAP_INVALID_KEY {0}
#endif /*MAP_INVALID_KEY*/

#define Map_Invalid_Key MAP_DECORATE_TYPE(_Invalid_Key)
#define Map_Invalid_Val MAP_DECORATE_TYPE(_Invalid_Val)
#endif // USER CONSTANTS
#endif // MACROS

static MapKey const Map_Invalid_Key = MAP_INVALID_KEY;
static MapVal const Map_Invalid_Val = MAP_INVALID_VAL;

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

typedef struct Map {
	MapIdx *idxs; // TODO: add at end of keys allocation?
	MapVal *vals;
	MapKey *keys;
	size_t  max; // always a power of 2; the size of the array is 2x this (allows super-quick mod_pow2)
	size_t  n;

    MAP_MTX (lock)
} Map;

#if 1 // CONSTANTS
#ifndef MAP_CONSTANTS
# define MAP_CONSTANTS

typedef enum MapResult {
    MAP_error   = -1,
    MAP_absent  = 0,
    MAP_present = 1,
} MapResult;

#endif // MAP_CONSTANTS
#endif // CONSTANTS

// (as obliged by Casey) TODO: better hash function
static uint64_t map__hash(MapKey key)
{
	uintptr_t hash = (uintptr_t)key;
	map__assert(key != Map_Invalid_Key);
	hash *= 0xff51afd7ed558ccd;
	hash ^= hash >> 32;
	return hash;
}

// returns:
// 1) the index of a key index that may or may not be valid (but will always be within array bounds)
// 2) ~0 -> no allocation has been made so far, allocate
static MapIdx map__idx_i(Map *map, MapKey key)
{
    map__assert(key != Map_Invalid_Key);
	MapIdx  idxs_n = Map_Load_Factor * map->max,
            hash_i = map__hash(key); // this is the index on an infinite-length array if there are no collisions
	MapKey *keys   = map->keys;
	MapIdx *idxs   = map->idxs;

    // TODO: don't really need to check for n as should never be full!
    // ...unless aren't able to alloc any more? Just suck up the performance hit but keep working...?
	for(MapIdx i = 0; i < idxs_n; ++i)
    { // find either the key or the fact that it's not present
		MapIdx idx_i      = map__mod_pow2(hash_i + i, idxs_n);
		MapIdx key_i      = idxs[idx_i];
        int key_is_in_map = ~key_i;
        if (! key_is_in_map
            || keys[key_i] == key) // key is found
        {   return idx_i;   }
        // else there is a different key in this idx, possibly a collision, check the next one
	}

    // No indexes allocated
	return ~(MapIdx)0;
}

// returns key index if found, or ~0 (0xFF...FF) otherwise
MAP_API MapIdx map__key_i(Map *map, MapKey key)
{
    map__assert(map);
    MapIdx result = ~(MapIdx)0;
    if (map->n)
    {
        MapIdx idx_i = map__idx_i(map, key);
        map__assert(idx_i < map->max * Map_Load_Factor);
        result = map->idxs[idx_i];
    }
    return result;
}

MAP_API MapVal * map_ptr(Map *map, MapKey key)
{
    map__assert(map);
    MAP_LOCK(&map->lock);
	MapIdx  key_i  = map__key_i(map, key);
	MapVal *result = (~key_i) ? &map->vals[key_i]
	                          : 0;
    MAP_UNLOCK(&map->lock);
    return result;
}

MAP_API MapVal map_get(Map *map, MapKey key)
{
    map__assert(map);
    MAP_LOCK(&map->lock);
	MapIdx key_i  = map__key_i(map, key);
	MapVal result = (~key_i) ? map->vals[key_i]
	                         : Map_Invalid_Val;
    MAP_UNLOCK(&map->lock);
    return result;
}

// returns non-zero if map contains key
MAP_API MapResult map_has(Map *map, MapKey key)
{
    map__assert(map);
    MAP_LOCK(&map->lock);
	MapIdx    key_i = map__key_i(map, key);
    MapResult result = !!(~key_i);
    MAP_UNLOCK(&map->lock);
	return result;
}

// returns non-zero on success
MAP_API int map_resize(Map *map, uint64_t values_n)
{
    map__assert(map);
    MAP_LOCK(&map->lock);
	int result = 0;
	Map old    = *map,
	    new    = old;

    { // ensure appropriate max count
        uint64_t m = values_n ? values_n : MAP_MIN_ELEMENTS; // account for unalloc'd
        --m, m|=m>>1, m|=m>>2, m|=m>>4, m|=m>>8, m|=m>>16, m|=m>>32, ++m; // ceiling pow 2
        new.max = m;
    }
    size_t idxs_n = new.max * Map_Load_Factor;

    if (new.max == old.max) { result = 1; goto end; } // no need to resize
    else
    { // allocate space as necessary
        size_t keys_size = new.max * sizeof(MapKey),
               vals_size = new.max * sizeof(MapVal),
               idxs_size = idxs_n  * sizeof(MapIdx);

        // TODO: consolidate into fewer allocations?
        new.keys = realloc(old.keys, keys_size);
        new.vals = realloc(old.vals, vals_size);
        new.idxs = realloc(old.idxs, idxs_size);
        if (! (new.keys && new.vals && new.idxs)) { goto end; }
    }

    { // set up new indexes
        for(MapIdx i = 0; i < idxs_n; ++i)
        {   new.idxs[i] = ~(MapIdx)0;   } // invalidate indexes by default // TODO: could memset...

        for(MapIdx i = 0; i < new.n; ++i)
        { // hash key indexes into new slots given new size
            MapIdx idx_i = map__idx_i(&new, new.keys[i]);
            map__assert(~new.idxs[idx_i] == 0 && "should be invalid at this stage");
            new.idxs[idx_i] = i;
        }
    }

    result = 1;
	*map = new;

end:
    MAP_UNLOCK(&map->lock);
    return result;
}


static inline MapResult map__make_room_for(Map *map, MapKey key, MapIdx *idx_out)
{
    map__assert(map);
    map__assert(idx_out);
	size_t max = map->max;

	MapIdx idx_i = map__idx_i(map, key),
	       idx   = (~idx_i) ? map->idxs[idx_i] // possibly valid idx
	                        : ~(MapIdx)0;      // map currently unallocated

    MapResult result = (~idx) ? MAP_present
                              : MAP_absent;
	if (result == MAP_absent)
    { // set the idx to the end of the array, resizing if necessary
        idx = map->n;

        if (idx >= max)
        { // resize and set the index
            if (! map_resize(map, Map_Load_Factor * max)) { result = MAP_error; goto end; }
            idx_i = map__idx_i(map, key);
            map__assert(~idx_i);
            map__assert(! ~map->idxs[idx_i]);
        }

        ++map->n;
        map->idxs[idx_i] = idx;
        map->keys[idx]   = key;
    }

    *idx_out = idx;
end:
    return result;
}

// returns:
// -1 - isn't in map, couldn't allocate sufficient space
//  0 - wasn't previously in map, successfully inserted
//  1 - was already in map, successfully updated
MAP_API MapResult map_set(Map *map, MapKey key, MapVal val)
{
    MAP_LOCK(&map->lock);

    MapIdx idx = 0;
    MapResult result = map__make_room_for(map, key, &idx);
    if (result != MAP_error)
    {   map->vals[idx] = val;   }

    MAP_UNLOCK(&map->lock);
    return result;
}

// -1 - isn't in map, couldn't allocate sufficient space
//  0 - wasn't previously in map, successfully inserted
//  1 - key was already in map, no change made
MAP_API MapResult map_insert(Map *map, MapKey key, MapVal val)
{
    MAP_LOCK(&map->lock);

    MapIdx idx = 0;
    MapResult result = map__make_room_for(map, key, &idx);
    if (result == MAP_absent)
    {   map->vals[idx] = val;   }

    MAP_UNLOCK(&map->lock);
    return result;
}

//  MAP_absent  (0) - isn't in map, no change
//  MAP_present (1) - was already in map, successfully updated
MAP_API MapResult map_update(Map *map, MapKey key, MapVal val)
{
    map__assert(map);
    MAP_LOCK(&map->lock);
	MapIdx idx = map__key_i(map, key);

    MapResult result = (~idx) ? MAP_present
                              : MAP_absent;
	if (result == MAP_present)
	{   map->vals[idx] = val;   }

    MAP_UNLOCK(&map->lock);
    return result;
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
    MAP_LOCK(&map->lock);
    map__assert(map);
	size_t  max    = map->max;
	MapIdx  idxs_n = Map_Load_Factor * max;
	MapIdx *idxs   = map->idxs;
	MapKey *keys   = map->keys;
	MapVal *vals   = map->vals;
	MapVal  result = Map_Invalid_Val;

	MapIdx empty_idx_i = map__idx_i(map, key),
	       rm_idx      = (~empty_idx_i) ? idxs[empty_idx_i] // possibly valid idx
	                                    : ~(MapIdx)0;       // map currently unallocated

	if (! (~rm_idx)) { goto end; } // key is not in map, nothing to remove
	result = vals[rm_idx];

    { // end-swap key/value and update indices
        MapIdx end_i    = --map->n;
        MapKey swap_key = keys[end_i];

        { // update indices, ensuring no holes?
            /* idxs[map__idx_i(map, swap_key)] = ~(MapIdx)0; // make sure no stale values are left */
            idxs[map__idx_i(map, swap_key)] = rm_idx;     // update index for swappee. If a hole is left it will be caught later
            idxs[empty_idx_i]               = ~(MapIdx)0; // invalidate deleted index, possibly leaving a hole to be caught next
            /* idxs[map__idx_i(map, swap_key)] = rm_idx; // update index for swappee*/
        }

        keys[rm_idx]    = swap_key;    // overwrite deleted key with key from end
        vals[rm_idx]    = vals[end_i]; // overwrite deleted val with val from end
    }

    // move back elements to make sure they're valid for linear-probing
    for(MapIdx check_idx_i = map__mod_pow2(empty_idx_i + 1, idxs_n), check_idx = idxs[check_idx_i];
		 ~check_idx;
			   check_idx_i = map__mod_pow2(check_idx_i + 1, idxs_n), check_idx = idxs[check_idx_i])
    { // go through all contiguous filled keys following deleted one
        MapKey check_key             = keys[check_idx];
        MapIdx ideal_idx_i           = map__mod_pow2(map__hash(check_key), idxs_n),
        // NOTE: adding idxs_n to keep positive, primarily to avoid oddities with mod
               d_from_ideal_to_empty = map__mod_pow2((idxs_n + empty_idx_i - ideal_idx_i), idxs_n),
               d_from_ideal_to_check = map__mod_pow2((idxs_n + check_idx_i - ideal_idx_i), idxs_n);
        int was_bumped = d_from_ideal_to_empty < d_from_ideal_to_check; // half open and may be more than 1 different as explained above
        if (was_bumped)
        { // move element from check idx to empty idx
            MapIdx test_idx = map__idx_i(map, key);
            map__assert(empty_idx_i == test_idx &&
                        "The check idx's content should be where it would have been had "
                        "the empty idx never been filled");
            idxs[empty_idx_i] = idxs[check_idx_i];
            idxs[check_idx_i] = ~(MapIdx)0;
            // check idx is now empty, so subsequent checks will be against that
            empty_idx_i = check_idx_i;
        }
	
	}

end:
    MAP_UNLOCK(&map->lock);
    return result;
}

MAP_API uint64_t map_clear(Map *map)
{
    map__assert(map);
    MAP_LOCK(&map->lock);
	MapIdx idxs_n = Map_Load_Factor * map->max,
		   n = map->n;
    map->n = 0;
	for (MapIdx i = 0; i < idxs_n; ++i)
	{   map->idxs[i] = ~(MapIdx)0;   }
    MAP_UNLOCK(&map->lock);
	return n;
}

#if 1 // UNDEFS
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
 
#undef MAP_MTX_TYPE
#undef MAP_MTX_LOCK
#undef MAP_MTX_UNLOCK

#undef MAP_MTX
#undef MAP_LOCK
#undef MAP_UNLOCK

#undef map__hash
#undef map__slots
#undef map__hashed_keys
#undef map__key_i
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
#undef MAP_MUTEX

#undef MAP_CAT1
#undef MAP_CAT2
#undef MAP_CAT
#endif /*undefs*/
