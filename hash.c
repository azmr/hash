#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define MAP_DEFAULT_HASH(key) map__fallback_hash(key)
#define MAP_TEST
#define MAP_TYPES (MyMap, map, void *, void *)
#include "hash.h"
#define MAP_INVALID_VAL -1
#define MAP_TYPES (MapStrInt, map_s_i, char *, int)
#include "hash.h"

static inline uint64_t
fnv1a_cstr(char const *string)
{
    uint64_t hash = 0xcbf29ce484222325;
    for(char byte = *string; byte; byte = *(++string))
    {
        hash ^= byte;
        hash *= 0x100000001b3;
    }
    return hash;
}

#define SET_TYPES (SetStrPtr, set_str_ptr, char *)
#include "hash.h"

#define MAP_HASH_KEY(key) fnv1a_cstr(key)
#define MAP_KEY_EQ(a,b) (!strcmp(a,b))
#define SET_TYPES (SetStr, set_str, char *)
#include "hash.h"

#include <stdio.h>

void put_str(char *s)
{
	for(;*s;s++) { putc(*s, stdout); }
}

int main()
{
	{ // use generic map
		char buf[256] = {0};
		MyMap map = {0};

		char *names[] = {"Andrew", "Patrick", "Jon", "Liz"};
		int ages[] = {20, 21, 22, 23, 24, 25, 26};
		map_insert(&map, names[0], ages+4);
		map_insert(&map, names[1], ages+6);
		map_insert(&map, names[2], ages+4);
		map_insert(&map, names[2], ages+0);
		map_insert(&map, names[3], ages+1);

		int *not_my_age = map_get(&map, "Someone else");
		if (not_my_age) {
			fprintf(stderr, "should not be valid");
		}
		int *my_age = map_get(&map, names[0]);
		if (my_age) {
			snprintf(buf, sizeof buf, "age: %d\n", *my_age);
			put_str(buf);
		}

		snprintf(buf, sizeof buf, "First name: %s, age: %d\n", names[0], *(int*)map_remove(&map, names[0]));
		put_str(buf);

		for(int i = 0; i < map.n; ++i) {
			char *name = map.keys[i];
			int  *age  = map_get(&map, name); // bug in code if this is null
			snprintf(buf, sizeof buf, "%d. %s is %d years old\n", i, name, *age);
			put_str(buf);
		}
	}

	{ // use type-safe map
		char buf[256] = {0};
		MapStrInt map = {0};

		char *names[] = {"Andrew", "Patrick", "Jon", "Liz"};
		int ages[] = {20, 21, 22, 23, 24, 25, 26};
		map_s_i_insert(&map, names[0], ages[4]);
		map_s_i_insert(&map, names[1], ages[6]);
		map_s_i_insert(&map, names[2], ages[4]);
		map_s_i_insert(&map, names[2], ages[0]);
		map_s_i_insert(&map, names[3], ages[1]);

		int not_my_age = map_s_i_get(&map, "Someone else");
		if (not_my_age != -1) {
			fprintf(stderr, "should not be valid");
		}
		int my_age = map_s_i_get(&map, names[0]);
		if (my_age != -1) {
			snprintf(buf, sizeof buf, "age: %d\n", my_age);
			put_str(buf);
		}

		snprintf(buf, sizeof buf, "First name: %s, age: %d\n", names[0], map_s_i_remove(&map, names[0]));
		put_str(buf);

		for(int i = 0; i < map.n; ++i) {
			char *name = map.keys[i];
			int  age  = map_s_i_get(&map, name); // bug in code if this is null
			snprintf(buf, sizeof buf, "%d. %s is %d years old\n", i, name, age);
			put_str(buf);
		}
	}


    { // use type-safe set
        char buf[256] = {0};
        SetStrPtr map = {0};

        char *names[] = {"Andrew", "Patrick", "Jon", "Liz"};
        set_str_ptr_insert(&map, names[0]);
        set_str_ptr_insert(&map, names[1]);
        set_str_ptr_insert(&map, names[2]);
        set_str_ptr_insert(&map, names[2]);
        set_str_ptr_insert(&map, names[3]);

        char *not_my_age = set_str_ptr_get(&map, "Someone else");
        if (not_my_age != NULL) {
            fprintf(stderr, "should not be valid");
        }
        char *my_age = set_str_ptr_get(&map, names[0]);
        if (my_age != NULL) {
            snprintf(buf, sizeof buf, "age: %s\n", my_age);
            put_str(buf);
        }

        char different_buf_same_name[] = "Andrew";
        snprintf(buf, sizeof buf, "First name: %s, different buf: %s\n", different_buf_same_name, set_str_ptr_get(&map, different_buf_same_name));
        put_str(buf);

        snprintf(buf, sizeof buf, "First name: %s, again: %s\n", names[0], set_str_ptr_remove(&map, names[0]));
        put_str(buf);

        for(int i = 0; i < map.n; ++i) {
            char *name  = map.keys[i];
            char *again = set_str_ptr_get(&map, name); // bug in code if this is null
            snprintf(buf, sizeof buf, "%d. %s is %s years old\n", i, name, again);
            put_str(buf);
        }
    }

    { // use type-safe set
        char buf[256] = {0};
        SetStr map = {0};

        char *names[] = {"Andrew", "Patrick", "Jon", "Liz"};
        set_str_insert(&map, names[0]);
        set_str_insert(&map, names[1]);
        set_str_insert(&map, names[2]);
        set_str_insert(&map, names[2]);
        set_str_insert(&map, names[3]);

        char *not_my_age = set_str_get(&map, "Someone else");
        if (not_my_age != NULL) {
            fprintf(stderr, "should not be valid");
        }
        char *my_age = set_str_get(&map, names[0]);
        if (my_age != NULL) {
            snprintf(buf, sizeof buf, "age: %s\n", my_age);
            put_str(buf);
        }

        char different_buf_same_name[] = "Andrew";
        snprintf(buf, sizeof buf, "First name: %s, different buf: %s\n", different_buf_same_name, set_str_get(&map, different_buf_same_name));
        put_str(buf);

        snprintf(buf, sizeof buf, "First name: %s, again: %s\n", names[0], set_str_remove(&map, names[0]));
        put_str(buf);

        for(int i = 0; i < map.n; ++i) {
            char *name  = map.keys[i];
            char *again = set_str_get(&map, name); // bug in code if this is null
            snprintf(buf, sizeof buf, "%d. %s is %s years old\n", i, name, again);
            put_str(buf);
        }
    }

	return 0;
}
