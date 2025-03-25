#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum endianness {
	little,
	big
};

static enum endianness endianness;

enum key
{
	none,
	int8,
	uint8,
	int16le,
	int16be,
	int16,
	uint16le,
	uint16be,
	uint16,
	int32le,
	int32be,
	int32,
	uint32le,
	uint32be,
	uint32,
	int64le,
	int64be,
	int64,
	uint64le,
	uint64be,
	uint64,
	string,
};

struct type
{
	enum key key;
	size_t size;
	char *label;
	char *format;
};

static struct type types[] = {
	{ none,      0, NULL,        NULL       },
	{ int8,      1, "int8",      "%" PRId8  },
	{ uint8,     1, "uint8",     "%" PRIu8  },
	{ int16le,   2, "int16le",   "%" PRId16 },
	{ int16be,   2, "int16be",   "%" PRId16 },
	{ int16,     2, "int16",     "%" PRId16 },
	{ uint16le,  2, "uint16le",  "%" PRIu16 },
	{ uint16be,  2, "uint16be",  "%" PRIu16 },
	{ uint16,    2, "uint16",    "%" PRIu16 },
	{ int32le,   4, "int32le",   "%" PRId32 },
	{ int32be,   4, "int32be",   "%" PRId32 },
	{ int32,     4, "int32",     "%" PRId32 },
	{ uint32le,  4, "uint32le",  "%" PRIu32 },
	{ uint32be,  4, "uint32be",  "%" PRIu32 },
	{ uint32,    4, "uint32",    "%" PRIu32 },
	{ int64le,   8, "int64le",   "%" PRId64 },
	{ int64be,   8, "int64be",   "%" PRId64 },
	{ int64,     8, "int64",     "%" PRId64 },
	{ uint64le,  8, "uint64le",  "%" PRIu64 },
	{ uint64be,  8, "uint64be",  "%" PRIu64 },
	{ uint64,    8, "uint64",    "%" PRIu64 },
	{ string,    0, "string",    "%s"       }
};

static size_t typeCount = sizeof(types) / sizeof(*types);

static struct {
	size_t size;
	size_t count;
	struct type *type;
	char *format;
} config;

static void usage(char *program)
{
	fprintf(stderr, "usage: %s [OPTIONS]\n"
	                "\n"
	                "OPTIONS\n"
	                "\n"
	                "  -c COUNT\n"
	                "  -s SIZE\n"
	                "  -f FORMAT\n"
	                "  -t TYPE\n"
	                "\n"
	                "TYPES\n"
	                "\n"
	                "   int8\n"
	                "  uint8\n"
	                "   int16   int16le   int16be\n"
	                "  uint16  uint16le  uint16be\n"
	                "   int32   int32le   int32be\n"
	                "  uint32  uint32le  uint32be\n"
	                "   int64   int64le   int64be\n"
	                "  uint64  uint64le  uint64be\n"
	                "  string\n"
	                "\n", program);
}

static enum endianness testEndianness(void)
{
	static union {
		unsigned char data[sizeof(unsigned int)];
		unsigned int  result;
	} test;

	test.data[0] = UCHAR_MAX;
	if (test.result == test.data[0])
		return little;

	return big;
}

static uintmax_t parseNumber(char *string, int *status)
{
	char *terminator;
	uintmax_t number;
	
	errno = 0;
	number = strtoumax(string, &terminator, 0);
	
	if (errno) {
		perror("strtoumax");
		*status = -1;
		return 0;
	}
	
	if (*terminator != 0) {
		*status = -1;
		return 0;
	}

	return number;
}

static int parseArguments(int argc, char *argv[])
{
	int status = 0;

	config.count = 1;
	config.type  = types + none;

	if (argc < 2) {
		usage(argv[0]);
		return -1;
	}

	for (int index = 1; index < argc; index += 2) {
		char *option   = argv[index];
		char *argument = argv[index + 1];

		if (strcmp(option, "-c") == 0) {
			config.count = parseNumber(argument, &status);
			if (status == -1)
				return -1;
		}

		if (strcmp(option, "-s") == 0) {
			config.size = parseNumber(argument, &status);
			if (status == -1)
				return -1;
		}

		else if (strcmp(option, "-f") == 0) {
			config.format = argument;
		}

		else if (strcmp(option, "-t") == 0) { 
			for (size_t key = int8; key < typeCount; key++) {
				struct type *type = types + key;

				if (strcmp(argument, type->label) == 0) {
					config.type = type;

					if (config.size == 0)
						config.size = type->size;

					if (config.format == NULL)
						config.format = type->format;

					break;
				}
			}
		}
	}

	if (config.type == types + none) {
		usage(argv[0]);
		return -1;
	}

	return 0;
}

static int printString(void)
{
	char buffer[BUFSIZ];

	size_t remaining;
	size_t index = 0;

	while (!feof(stdin)) {
		char *cursor, *terminator;

		remaining = fread(buffer, 1, sizeof(buffer) - 1, stdin);
		if (remaining < 1) {
			if (ferror(stdin)) {
				perror("fread");
				return -1;
			}
		}

		cursor = buffer;
		terminator = buffer + remaining;
		*terminator = 0;

		while (cursor < terminator) {
			printf(config.format, cursor);

			if (cursor + strlen(cursor) == terminator)
				break;

			putchar('\n');

			if (++index == config.count)
				return 0;

			cursor = strchr(cursor, 0);
			if (cursor == NULL)
				return -1;

			cursor++;
		}
	}

	return -1;
}

static void reverseNumber(uintmax_t *number, size_t size)
{
	unsigned char *data = (unsigned char *)number;

	for (size_t index = 0; index < size / 2; index++) {
		unsigned char aux = data[index] ^ data[size - index - 1];
		data[index] ^= aux;
		data[size - index - 1] ^= aux;
	}
}

static int printNumber(void)
{
	union {
		int8_t    int8;
		uint8_t   uint8;
		int16_t   int16le;
		int16_t   int16be;
		int16_t   int16;
		uint16_t  uint16le;
		uint16_t  uint16be;
		uint16_t  uint16;
		int32_t   int32le;
		int32_t   int32be;
		int32_t   int32;
		uint32_t  uint32le;
		uint32_t  uint32be;
		uint32_t  uint32;
		int64_t   int64le;
		int64_t   int64be;
		int64_t   int64;
		uint64_t  uint64le;
		uint64_t  uint64be;
		uint64_t  uint64;
		uintmax_t uintmax;
	} number;

	number.uintmax = 0;

	for (size_t index = 0; index < config.count; index++) {
		struct type *type = config.type;

		if (fread(&number, config.size, 1, stdin) < 1) {
			if (ferror(stdin))
				perror("fread");
			return -1;
		}

		switch (type->key) {
		case int16le:
		case uint16le:
			if (endianness == big)
				reverseNumber(&number.uintmax, 2);
			break;

		case int16be:
		case uint16be:
			if (endianness == little)
				reverseNumber(&number.uintmax, 2);
			break;

		case int32le:
		case uint32le:
			if (endianness == big)
				reverseNumber(&number.uintmax, 4);
			break;

		case int32be:
		case uint32be:
			if (endianness == little)
				reverseNumber(&number.uintmax, 4);
			break;

		case int64le:
		case uint64le:
			if (endianness == big)
				reverseNumber(&number.uintmax, 8);
			break;

		case int64be:
		case uint64be:
			if (endianness == little)
				reverseNumber(&number.uintmax, 8);
			break;

		default:
			break;
		}

		switch (type->key) {
		case int8:
			printf(config.format, number.int8);
			break;

		case uint8:
			printf(config.format, number.uint8);
			break;

		case int16le:
			printf(config.format, number.int16le);
			break;

		case int16be:
			printf(config.format, number.int16be);
			break;

		case int16:
			printf(config.format, number.int16);
			break;

		case uint16le:
			printf(config.format, number.uint16le);
			break;

		case uint16be:
			printf(config.format, number.uint16be);
			break;

		case uint16:
			printf(config.format, number.uint16);
			break;

		case int32le:
			printf(config.format, number.int32le);
			break;

		case int32be:
			printf(config.format, number.int32be);
			break;

		case int32:
			printf(config.format, number.int32);
			break;

		case uint32le:
			printf(config.format, number.uint32le);
			break;

		case uint32be:
			printf(config.format, number.uint32be);
			break;

		case uint32:
			printf(config.format, number.uint32);
			break;

		case int64le:
			printf(config.format, number.int64le);
			break;

		case int64be:
			printf(config.format, number.int64be);
			break;

		case int64:
			printf(config.format, number.int64);
			break;

		case uint64le:
			printf(config.format, number.uint64le);
			break;

		case uint64be:
			printf(config.format, number.uint64be);
			break;

		case uint64:
			printf(config.format, number.uint64);
			break;

		default:
			break;
		}

		putchar('\n');
	}

	return 0;
}

static int format(void)
{
	struct type *type = config.type;

	switch (type->key) {
	case string:
		if (printString() == -1)
			return -1;
		break;

	default:
		if (printNumber() == -1)
			return -1;
		break;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	endianness = testEndianness();

	if (parseArguments(argc, argv) == -1)
		return EXIT_FAILURE;

	if (format() == -1)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
