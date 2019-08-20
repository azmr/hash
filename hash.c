#define MAP_TYPES (MyMap, map, void *, void *)
#include "hash.h"
#define MAP_INVALID_VAL -1
#define MAP_TYPES (MapStrInt, map_s_i, char *, int)
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

		for(int i = 0; i < map.keys_n; ++i) {
			char *name = map.keys[i];
			int  *age  = map_get(&map, name); // bug in code if this is null
			snprintf(buf, sizeof buf, "%d. %s is %d years old", i, name, *age);
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

		for(int i = 0; i < map.keys_n; ++i) {
			char *name = map.keys[i];
			int  age  = map_s_i_get(&map, name); // bug in code if this is null
			snprintf(buf, sizeof buf, "%d. %s is %d years old", i, name, age);
			put_str(buf);
		}
	}

	return 0;
}
