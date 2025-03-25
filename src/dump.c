#define _POSIX_C_SOURCE 1

#include <sys/mman.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct
{
	size_t offset;
	size_t size;
	size_t width;

	char *type;
	struct {
		char *offset;
		char *value;
		char *symbol;
	} format;

	char *palette;
	char *data;

} config;

static struct
{
	FILE *file;
	size_t size;
	char *base;
	char *value[UCHAR_MAX + 1];
} palette;

static struct
{
	FILE *file;
	size_t size;
	unsigned char *base;
} data;

static void usage(char *program);
static void cleanup(void);
static int parseArguments(int argc, char *argv[]);
static uintmax_t parseNumber(char **string, int *status, bool terminal);
static int parsePalette(void);
static int mapPalette(void);
static int mapData(void);
static void dumpFormat(void);
static void dumpRaw(void);

static void usage(char *program)
{
	fprintf(stderr, "usage: %s [OPTIONS] FILE\n"
	                "\n"
	                "OPTIONS\n"
	                "\n"
	                "  -o    OFFSET\n"
	                "  -s    SIZE\n"
	                "  -t    TYPE\n"
	                "\n"
	                "TYPES\n"
	                "\n"
 	                "   r    raw (default)\n"
	                "   f    format\n"
	                "\n"
	                "FORMAT OPTIONS\n"
	                "\n"
	                "  -fo   OFFSET-FORMAT (default: \"%%08zx  \")\n"
	                "  -fv   VALUE-FORMAT  (default: \"%%02x \")\n"
	                "  -fs   SYMBOL-FORMAT (default: \"%%c\")\n"
	                "  -w    WIDTH         (default: 16)\n"
	                "  -p    PALETTE-FILE\n"
	                "\n", program);
}

static void cleanup(void)
{
	if (palette.base)
		munmap(palette.base, palette.size);

	if (palette.file)
		fclose(palette.file);

	if (data.base)
		munmap(data.base, data.size);

	if (data.file)
		fclose(data.file);
}

static int parseArguments(int argc, char *argv[])
{
	static char   DefaultType[]         = "r";
	static char   DefaultOffsetFormat[] = "%08zx  ";
	static char   DefaultValueFormat[]  = "%02x ";
	static char   DefaultSymbolFormat[] = "%c";
	static size_t DefaultWidth          = 16;

	int status;

	if (argc < 2) {
		usage(argv[0]);
		return -1;
	}

	config.type          = DefaultType;
	config.format.offset = DefaultOffsetFormat;
	config.format.value  = DefaultValueFormat;
	config.format.symbol = DefaultSymbolFormat;
	config.width         = DefaultWidth;

	for (int index = 1; index < argc; index++) {
		char *option = argv[index];
		char *argument = argv[index + 1];

		if (strcmp(option, "-o") == 0) {
			config.offset = parseNumber(&argument, &status, true);
			if (status == -1) {
				fprintf(stderr, "parseArguments: bad offset\n");
				return -1;
			}

			index++;
		}

		else if (strcmp(option, "-s") == 0) {
			config.size = parseNumber(&argument, &status, true);
			if (status == -1) {
				fprintf(stderr, "parseArguments: bad size\n");
				return -1;
			}

			index++;
		}

		else if (strcmp(option, "-t") == 0) {
			if (strcmp(argument, "r") != 0 &&
			    strcmp(argument, "f") != 0) {
				usage(argv[0]);
				return -1;
			}

			config.type = argument;
			index++;
		}

		else if (strcmp(option, "-fo") == 0) {
			config.format.offset = argument;
			index++;
		}

		else if (strcmp(option, "-fv") == 0) {
			config.format.value = argument;
			index++;
		}

		else if (strcmp(option, "-fs") == 0) {
			config.format.symbol = argument;
			index++;
		}

		else if (strcmp(option, "-p") == 0) {
			config.palette = argument;

			if (mapPalette() == -1)
				return -1;

			if (parsePalette() == -1)
				return -1;

			index++;
		}

		else if (strcmp(option, "-w") == 0) {
			config.width = parseNumber(&argument, &status, true);
			if (status == -1) {
				fprintf(stderr, "parseArguments: bad width\n");
				return -1;
			}

			index++;
		}

		else {
			if (index != argc - 1) {
				usage(argv[0]);
				return -1;
			}

			config.data = argv[index];
		}
	}

	if (config.data == NULL) {
		usage(argv[0]);
		return -1;
	}

	return 0;
}

static uintmax_t parseNumber(char **string, int *status, bool terminal)
{
	char *terminator;
	uintmax_t number;

	errno = 0;
	number = strtoumax(*string, &terminator, 0);

	if (errno) {
		*status = -1;
		return 0;
	}

	if (terminal) {
		if (*terminator != 0) {
			*status = -1;
			return 0;
		}
	}
	else {
		if (!isspace(*terminator)) {
			*status = -1;
			return 0;
		}
	}

	*string = terminator;
	*status = 0;

	return number;
}

static int mapPalette(void)
{
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE;

	palette.file = fopen(config.palette, "r");
	if (palette.file == NULL) {
		perror("mapPalette: fopen");
		return -1;
	}

	if (fseek(palette.file, 0, SEEK_END) == -1) {
		perror("mapPalette: fseek");
		return -1;
	}

	palette.size = ftell(palette.file);
	if (palette.size == 0)
		return -1;

	palette.base = mmap(NULL, palette.size, prot, flags,
	                    fileno(palette.file), 0);

	if (palette.base == MAP_FAILED) {
		perror("mapPalette: mmap");
		return -1;
	}

	return 0;
}

static int parsePalette(void)
{
	size_t lines = 0;
	char *cursor, *delimiter, *terminator;

	cursor = palette.base;
	terminator = palette.base + palette.size - 1;
	*terminator = '\n';

	while (cursor < terminator) {
		int status;
		unsigned char start, end;

		delimiter = strchr(cursor, '\n');
		if (delimiter == NULL)
			return -1;

		lines++;

		start = parseNumber(&cursor, &status, false);
		if (status == -1) {
			fprintf(stderr,
			        "parsePalette: %zu: bad start\n", lines);
			return -1;
		}

		end = parseNumber(&cursor, &status, false);
		if (status == -1) {
			fprintf(stderr,
			        "parsePalette: %zu: bad end\n", lines);
			return -1;
		}

		if (end < start) {
			fprintf(stderr,
			        "parsePalette: %zu: invalid!\n", lines);
			return -1;
		}

		while (isspace(*cursor)) {
			if (*cursor == '\n') {
				fprintf(stderr,
				        "parsePalette: %zu: invalid?\n", lines);
				return -1;
			}

			cursor++;
		}

		for (int index = start; index <= end; index++)
			palette.value[index] = cursor;

		*delimiter = 0;
		cursor = delimiter + 1;
	}

	return 0;
}

static int mapData(void)
{
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE;

	data.file = fopen(config.data, "r");
	if (data.file == NULL) {
		perror("mapData: fopen");
		return -1;
	}

	if (fseek(data.file, 0, SEEK_END) == -1) {
		perror("mapData: fseek");
		return -1;
	}

	data.size = ftell(data.file);
	data.base = mmap(NULL, data.size, prot, flags, fileno(data.file), 0);

	if (data.base == MAP_FAILED) {
		perror("mapData: mmap");
		return -1;
	}

	if (config.offset >= data.size)
		config.offset = data.size - 1;

	if (config.size == 0)
		config.size = data.size;

	if (config.size > data.size - config.offset)
		config.size = data.size - config.offset;

	return 0;
}

static void dumpFormat(void)
{
	while (config.size > 0) {
		size_t index, padding = 0;
		unsigned char byte;

		if (config.width > config.size) {
			padding = config.width - config.size;
			config.width = config.size;
		}

		printf(config.format.offset, config.offset);

		for (index = 0; index < config.width; index++) {
			byte = data.base[config.offset + index];

			if (palette.value[byte])
				printf("%s", palette.value[byte]);

			printf(config.format.value, byte);
		}

		for (index = 0; index < padding; index++) {
			size_t size = snprintf(0, 0, config.format.value, 0);
			while (size-- > 0)
				putchar(' ');
		}

		putchar(' ');

		for (index = 0; index < config.width; index++) {
			byte = data.base[config.offset + index];

			if (palette.value[byte])
				printf("%s", palette.value[byte]);

			printf(config.format.symbol,
			       isgraph(byte) ? byte : '.');
		}

		if (config.palette)
			printf("\033[0m");

		putchar('\n');

		config.offset += config.width;
		config.size   -= config.width;
	}

	putchar('\n');
}

static void dumpRaw(void)
{
	unsigned char *cursor = data.base + config.offset;

	while (config.size > 0) {
		putchar(*cursor++);
		config.size--;
	}
}

int main(int argc, char *argv[])
{
	if (parseArguments(argc, argv) == -1)
		return EXIT_FAILURE;

	atexit(cleanup);

	if (mapData() == -1)
		return EXIT_FAILURE;

	if (strcmp(config.type, "r") == 0)
		dumpRaw();

	else if (strcmp(config.type, "f") == 0)
		dumpFormat();

	return EXIT_SUCCESS;
}
