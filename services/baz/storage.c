#include "storage.h"
#include "num.h"

#define MAXITEMS 4096
#define MAXNODES 409600
#define KEY_SIZE 32
#define LEGACY_FLAG_SIZE 32
#define TEAM_FLAG_SIZE 40
#define VALUE_SIZE (TEAM_FLAG_SIZE + 1)

typedef struct 
{
	char key[KEY_SIZE];
	char value[VALUE_SIZE];
} slot;

typedef struct
{
	uint32 trans[26];
	uint32 value;
	uint32 used; 
} node;

node nodes[MAXNODES];
slot slots[MAXITEMS];
uint64 current_item;
uint64 current_node;

int32 logfd;


bool store_item_impl(slot *item);
bool is_legacy_flag(const char *flag);
bool is_team_flag(const char *flag);
char *decode_team_flag(uint192 n, char *buffer);
char *encode_legacy_flag(const char *recipe, char *buffer);
char *hash_legacy_flag(const char *flag, char *buffer);
char *hash_team_flag(const char *flag, char *buffer);

void init_storage()
{
	current_item = 0;
	current_node = 1;
	memzero(nodes, sizeof(nodes));
	memzero(slots, sizeof(slots));
	nodes[0].used = true;

	if ((logfd = openrw("storage-v2")) < 0)
	{
		print("fuck storage\n");
		exit(1);
	}

	uint64 items_read = 0;
	slot item;
	while (true)
	{
		int32 bytesRead = read(logfd, &item, sizeof(item));

		if (!bytesRead)
			break;

		if (bytesRead < sizeof(item) || (!is_legacy_flag(item.value) && !is_team_flag(item.value)))
		{
			print("Item ");nprint(items_read);print(" is corrupt\n");
			break;
		}

		store_item_impl(&item);
		items_read++;
	}
}

uint64 allocate_node()
{
	uint64 i;
	for (i = 0; i < MAXNODES; i++)
	{
		uint64 idx = (i + current_node) % MAXNODES;
		if (nodes[idx].used)
			continue;
		nodes[idx].used = true;
		current_node = idx + 1;
		return idx;
	}
	return -1;
}

void delete_item(slot *item)
{
	uint64 current = 0;
	char *key;
	uint32 path[32] = { 0 };
	uint64 i = 0;
	for (key = item->key; *key && key < item->value; key++)
	{
		uint64 n = *key - 'A';
		path[i++] = current;
		current = nodes[current].trans[n] - 1;
		if (current > MAXNODES)
			return;
	}

	nodes[current].value = 0;

	uint64 deleted = 0;
	for (i--; i > 0; i--)
	{
		if (nodes[current].value)
			break;

		uint64 j;
		for (j = 0; j < 26; j++)
		{
			if (nodes[current].trans[j])
				break;
		}

		if (j < 26)
			break;

		key--;
		uint64 n = *key - 'A';

		memzero(&nodes[current], sizeof(node));
		current = path[i];
		nodes[current].trans[n] = 0;
		deleted++;
	}
}

uint32 add_item(const slot *item)
{
	current_item = (current_item + 1) % MAXITEMS;
	if (slots[current_item].key[0])
		delete_item(&slots[current_item]);

	slots[current_item] = *item;
	return current_item;
}

void copy_bytes(const void *src, void *dest, uint64 length)
{
	const byte *s = (const byte *)src;
	byte *d = (byte *)dest;
	uint64 i;
	for (i = 0; i < length; i++)
		d[i] = s[i];
}

bool store_item_impl(slot *item)
{
	uint64 current = 0;

	uint32 new_item = add_item(item);

	char *key;
	for (key = item->key; *key && key < item->value; key++)
	{
		uint64 n = *key - 'A';
		if (n >= 26)
			return false;
		if (!nodes[current].trans[n])
		{
			uint64 nd = allocate_node();
			if (nd == -1)
			{
				return false;
			}
			nodes[current].trans[n] = nd + 1;
		}
		current = nodes[current].trans[n] - 1;
	}

	if (nodes[current].value)
		return false;

	nodes[current].value = new_item + 1;

	return true;
}

char * store_item(char *key, char *value)
{
	if (!key || !value)
		return 0;
	slot item;
	memzero(&item, sizeof(item));
	uint64 i;
	for (i = 0; i < VALUE_SIZE; i++)
	{
		item.value[i] = value[i];
		if (!value[i])
			break;
	}
	if (!name_flag(key, item.key))
		return 0;

	if (store_item_impl(&item))
	{
		write(logfd, &item, sizeof(item));
		return key;
	}

	return 0;
}

char * load_item(const char *key, char *buffer)
{
	if (!key || !buffer)
		return 0;

	uint64 current = 0;

	while (*key)
	{
		uint64 n = *key - 'A';
		if (n >= 26)
			return 0;
		if (!nodes[current].trans[n])
			return 0;
		current = nodes[current].trans[n] - 1;
		key++;
	}

	if (!nodes[current].value)
		return 0;

	copy_bytes(slots[nodes[current].value - 1].value, buffer, VALUE_SIZE);
	buffer[VALUE_SIZE] = 0; 

	return buffer;
}

char * list_items(uint64 skip, uint64 take, char *buffer, uint64 length)
{
	if (!buffer)
		return 0;

	if (take * 33 > length)
		return 0;

	buffer[0] = 0;

	uint64 i;
	for (i = skip; i < skip + take; i++)
	{
		int64 idx = (current_item + MAXITEMS - i) % MAXITEMS;

		strncat(buffer, slots[idx].key, 32);
		strcat(buffer, "\n");
	}

	return buffer;
}

#define CSPIRITS 33
#define MAXRECIPE 32
char *spirits[] = {
"Ale",
"Porter",
"Stout",
"Lager",
"Cider",
"Mead",
"Wine",
"Port",
"Sherry",
"Vermouth",
"Vinsanto",
"Sangria",
"Champagne",
"Sake",
"Brandy",
"Cognac",
"Armagnac",
"Schnapps",
"Gin",
"Horilka",
"Metaxa",
"Rakia",
"Rum",
"Shochu",
"Soju",
"Tequila",
"Vodka",
"Bourbon",
"Whiskey",
"Absinthe",
"Juice",
"Cola",
"Water"
};

uint192 magic = { .i0 = 0xb0b8bad, .i1 = 0xbeefdefec87edfec, .i2 = 0xe5f100deda11dead };

bool is_legacy_flag(const char *flag)
{
	if (!flag)
		return false;

	uint64 i;
	for (i = 0; i < LEGACY_FLAG_SIZE - 1; i++)
	{
		char c = flag[i];
		bool is_digit = c >= '0' && c <= '9';
		bool is_upper = c >= 'A' && c <= 'Z';
		if (!is_digit && !is_upper)
			return false;
	}

	return flag[LEGACY_FLAG_SIZE - 1] == '=' && !flag[LEGACY_FLAG_SIZE];
}

bool is_team_flag(const char *flag)
{
	if (!flag)
		return false;
	if (flag[0] != 'T' || flag[1] != 'E' || flag[2] != 'A' || flag[3] != 'M')
		return false;
	if (flag[7] != '_')
		return false;

	uint64 i;
	for (i = 4; i < 7; i++)
	{
		if (flag[i] < '0' || flag[i] > '9')
			return false;
	}
	for (i = 8; i < TEAM_FLAG_SIZE; i++)
	{
		char c = flag[i];
		bool is_digit = c >= '0' && c <= '9';
		bool is_upper = c >= 'A' && c <= 'Z';
		if (!is_digit && !is_upper)
			return false;
	}

	return !flag[TEAM_FLAG_SIZE];
}

uint64 flag_char_to_digit(char c)
{
	return c > '9' ? c - 'A' + 10 : c - '0';
}

char digit_to_flag_char(uint32 digit)
{
	return digit > 9 ? 'A' + digit - 10 : '0' + digit;
}

char *decode_team_flag(uint192 n, char *buffer)
{
	if (!buffer)
		return 0;

	uint32 rem;
	int64 i;
	for (i = TEAM_FLAG_SIZE - 1; i >= 8; i--)
	{
		n = divmod(n, 36, &rem);
		buffer[i] = digit_to_flag_char(rem);
	}

	if (n.i2 >= 1000 || n.i1 || n.i0)
		return 0;

	buffer[0] = 'T';
	buffer[1] = 'E';
	buffer[2] = 'A';
	buffer[3] = 'M';
	buffer[4] = '0' + (n.i2 / 100);
	buffer[5] = '0' + ((n.i2 / 10) % 10);
	buffer[6] = '0' + (n.i2 % 10);
	buffer[7] = '_';
	buffer[TEAM_FLAG_SIZE] = 0;
	return buffer;
}

char *encode_legacy_flag(const char *recipe, char *buffer)
{
	if (!recipe || !buffer)
		return 0;

	uint192 n = expand(0);

	char buf[64];
	buf[0] = 0;
	char *bptr = buf;
	while (*recipe && bptr < buf + sizeof(buf)) // TODO check recipe length
	{
		char c = *recipe;
		if (c == ',' || !recipe[1])
		{
			if (!recipe[1])
			{
				*bptr = *recipe;
				bptr++;
			}
			*bptr = 0;
			uint64 i;
			for (i = 0; i < CSPIRITS; i++)
			{
				if (!strcmp(buf, spirits[i]))
					break;
			}

			if (i >= CSPIRITS)
				return 0;

			n = multiply(n, CSPIRITS);
			n = add(n, i);

			buf[0] = 0;
			bptr = buf;
		}
		else
		{
			*bptr = *recipe;
			bptr++;
		}
		recipe++;
	}

	//n = not(n);
	n = xor(n, magic);
	
	uint64 length = 0;
	while (!is_zero(n) && length < sizeof(buf))
	{
		uint32 rem;
		n = divmod(n, 36, &rem);
		buf[length] = rem > 9 ? 'A' + rem - 10 : '0' + rem;
		length++;
	}

	while (length < 31)
	{
		buf[length] = '0';
		length++;
	}

	buffer[32] = 0;
	buffer[31] = '=';
	uint64 i;
	for (i = 0; i < 31; i++)
	{
		buffer[i] = buf[--length];
	}

	return buffer;
}

char * hash_flag(const char *flag, char *buffer)
{
	if (!flag || !buffer)
		return 0;

	if (is_team_flag(flag))
		return hash_team_flag(flag, buffer);

	if (!is_legacy_flag(flag))
		return 0;

	return hash_legacy_flag(flag, buffer);
}

char *hash_legacy_flag(const char *flag, char *buffer)
{
	char flag_copy[LEGACY_FLAG_SIZE];
	uint64 i;
	for (i = 0; i < LEGACY_FLAG_SIZE; i++)
		flag_copy[i % 2 ? LEGACY_FLAG_SIZE - i : i] = flag[i] > '9' ? flag[i] - 'A' + 10 : flag[i] - '0';

	const uint64 seed = 0x60d15dead;
	const uint64 m = 0xc6a4a7935bd1e995;
	const int32 r = 47;

	uint64 h = seed ^ (LEGACY_FLAG_SIZE * m);

	const uint64 *data = (const uint64 *)flag_copy;
	const uint64 *end = 4 + data;

	while (data != end)
	{
		uint64 k = *data++;

		k *= m;
		k ^= k >> r;
		k *= m;

		h ^= k;
		h *= m;
	}

	h *= m;

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	if (!(h >> 63))
		h = ~h;

	to_string_hex(h, buffer, 32);
	return buffer;
}

uint64 load_uint64_le(const char *data)
{
	uint64 v = 0;
	uint64 i;
	for (i = 0; i < 8; i++)
		v |= ((uint64)(byte)data[i]) << (8 * i);
	return v;
}

char *hash_team_flag(const char *flag, char *buffer)
{
	const uint64 seed = 0x60d15dead;
	const uint64 m = 0xc6a4a7935bd1e995;
	const int32 r = 47;

	uint64 h = seed ^ (TEAM_FLAG_SIZE * m);
	uint64 i;
	for (i = 0; i < TEAM_FLAG_SIZE / 8; i++)
	{
		uint64 k = load_uint64_le(flag + i * 8);

		k *= m;
		k ^= k >> r;
		k *= m;

		h ^= k;
		h *= m;
	}

	h *= m;

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	if (!(h >> 63))
		h = ~h;

	to_string_hex(h, buffer, 32);
	return buffer;
}

char * encode_flag(const char *recipe, char *buffer)
{
	if (!recipe || !buffer)
		return 0;
	const char *original_recipe = recipe;

	uint192 n = expand(0);

	char buf[64];
	buf[0] = 0;
	char *bptr = buf;
	while (*recipe && bptr < buf + sizeof(buf))
	{
		char c = *recipe;
		if (c == ',' || !recipe[1])
		{
			if (!recipe[1])
			{
				*bptr = *recipe;
				bptr++;
			}
			*bptr = 0;
			uint64 i;
			for (i = 0; i < CSPIRITS; i++)
			{
				if (!strcmp(buf, spirits[i]))
					break;
			}

			if (i >= CSPIRITS)
				return 0;

			n = multiply(n, CSPIRITS);
			n = add(n, i);

			buf[0] = 0;
			bptr = buf;
		}
		else
		{
			*bptr = *recipe;
			bptr++;
		}
		recipe++;
	}

	n = xor(n, magic);
	if (decode_team_flag(n, buffer))
		return buffer;

	return encode_legacy_flag(original_recipe, buffer);
}

char * name_flag(const char *flag, char *buffer)
{
	if (!flag || !buffer)
		return 0;
	uint64 length = 0;
	while (*flag)
	{
		char c = *flag;
		flag++;
		if (c < 'A' || c > 'Z')
			continue;
		*buffer = c;
		buffer++;
		length++;
	}

	if (!length)
		return 0;

	*buffer = 0;

	return buffer - length;
}
