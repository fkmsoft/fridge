#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include <json-c/json.h>

#define DEFAULT_FILE "game.json"

int main(int argc, char **argv)
{
	json_object *obj;

	char const *filename;
	if (argc == 2) {
		filename = argv[1];
	} else {
		filename = DEFAULT_FILE;
	}

	obj = json_object_from_file(filename);
	printf("%s\n", json_object_to_json_string(obj));
	json_object_put(obj);

	return 0;
}
