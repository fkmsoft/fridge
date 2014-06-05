#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include <jansson.h>

#define DEFAULT_FILE "../game.json"

int main(int argc, char **argv)
{
	json_t *obj;

	char const *filename;
	if (argc == 2) {
		filename = argv[1];
	} else {
		filename = DEFAULT_FILE;
	}

	obj = json_load_file(filename, 0, 0);
	printf("%s\n", json_dumps(obj, 0));
	json_decref(obj);

	return 0;
}
