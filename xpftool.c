#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#define makedir(n) _mkdir(n)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define makedir(n) mkdir(n, 0777)
#endif

#ifdef _MSC_VER
#define __builtin_bswap32(x) _byteswap_ulong(x)
#endif

typedef struct
{
	char     magic[4];
	uint32_t dataOffset;
	uint32_t numFiles;
	char     padding[4];
} xpfHeader;

typedef struct
{
	char     filename[24];
	uint32_t offset;
	uint32_t length;
} xpfEntry;

static const char *g_CustomOutputFolderName;

void makeDirIfFolder(char *name)
{
	char fn[512], *p = name;

	/* loop until end of string or '/' found */
	while (*p != '\0' && *p++ != '/') ;

	/* not a directory */
	if (*p == '\0') return;

	/* make the directory */
	if (g_CustomOutputFolderName)
	{
		name[p - name] = '\0';
		sprintf(fn, "%s/%s", g_CustomOutputFolderName, name);
		name[p - name] = '/';

		makedir(fn);
	}
	else
	{
		name[p - name] = '\0';
		makedir(name);
		name[p - name] = '/';
	}
}

/* messy decompilation, work needs to be done to clean it up */
int decompress(uint8_t *out, uint8_t *in)
{
	char ch;
	int copyLen, distance, decSz, flag, maskA, maskB, unk;
	uint8_t *data, *next;

	decSz = (*(int *)in >> 8);
	decSz = (decSz >> 16) | (decSz & 0xff00) | ((decSz & 0xff) << 16);

	flag =  in[4];
	data = &in[5];

	maskA = 0x80;

	do
	{
		while (1)
		{
			maskB = maskA;
			unk   = flag & maskA;

			if (maskA == 0)
			{
				flag  = *data++;
				maskB = 0x80;
				unk   = flag & 0x80;
			}

			maskB >>= 1;
			maskA = maskB;

			if (unk)
				break;

			*out++ = *data++;
		}

		unk = flag & maskA;

		if (maskA == 0)
		{
			flag  = *data++;
			maskB = 0x80;
			unk   = flag & 0x80;
		}

		ch = *data;

		if (unk == 0)
		{
			if (ch == 0)
				return decSz;

			distance = ch | 0xffffff00;
			data++;
		}
		else
		{
			maskA  = maskB >> 1;
			next   = data + 1;

			if (maskA == 0)
			{
				flag  = *next;
				maskA = 0x80;
				next  = data + 2;
			}

			copyLen = (ch | 0xffffff00) << 1;

			if (flag & maskA)
				copyLen++;

			maskB = maskA >> 1;
			maskA = flag & maskB;

			if (maskB == 0)
			{
				flag  = *next++;
				maskB = 0x80;
				maskA = flag & 0x80;
			}

			copyLen <<= 1;

			if (maskA)
				copyLen++;

			maskB >>= 1;
			maskA = flag & maskB;

			if (maskB == 0)
			{
				flag  = *next++;
				maskB = 0x80;
				maskA = flag & 0x80;
			}

			copyLen <<= 1;

			if (maskA)
				copyLen++;

			maskB >>= 1;
			maskA = flag & maskB;

			if (maskB == 0)
			{
				flag  = *next++;
				maskB = 0x80;
				maskA = flag & 0x80;
			}

			copyLen <<= 1;

			if (maskA)
				copyLen++;

			distance = copyLen - 0xff;
			data     = next;
		}

		copyLen = 1;

		while (1)
		{
			maskB >>= 1;

			maskA = flag & maskB;

			if (maskB == 0)
			{
				flag  = *data++;
				maskB = 0x80;
				maskA = flag & 0x80;
			}

			maskB >>= 1;

			if (maskA == 0)
				break;

			maskA = flag & maskB;

			if (maskB == 0)
			{
				flag  = *data++;
				maskB = 0x80;
				maskA = flag & 0x80;
			}

			copyLen <<= 1;

			if (maskA)
				copyLen++;
		}

		maskA = maskB;

		while (copyLen-- > -1)
			*out++ = out[distance];
	}
	while (1);
}

int main(int argc, char **argv)
{
	char      fn[512];
	FILE     *in, *out;
	xpfHeader hdr;
	xpfEntry *entries;
	uint8_t  *data, *buffer;
	long      dataSz = 0, decSz;

	if (argc < 2)
	{
		printf("Usage: %s <input.xpf> [output directory (optional)]\n", argv[0]);
		return 0;
	}

	if ((in = fopen(argv[1], "rb")) == NULL)
	{
		perror("Error opening input");
		return 1;
	}

	g_CustomOutputFolderName = argc > 2 ? argv[2] : NULL;

	if (g_CustomOutputFolderName)
		makedir(g_CustomOutputFolderName);

	fread(&hdr, sizeof(xpfHeader), 1, in);

	entries = malloc(sizeof(xpfEntry) * hdr.numFiles);
	fread(entries, sizeof(xpfEntry), hdr.numFiles, in);

	for (uint32_t i = 0; i < hdr.numFiles; i++)
		dataSz += entries[i].length;

	data = malloc(dataSz);

	fread(data, 1, dataSz, in);
	fclose(in);

	for (uint32_t i = 0; i < hdr.numFiles; i++)
	{
		if (g_CustomOutputFolderName)
			sprintf(fn, "%s/%s", g_CustomOutputFolderName, entries[i].filename);

		printf("Extracting %s...\n", entries[i].filename);

		makeDirIfFolder(entries[i].filename);

		if ((out = fopen(g_CustomOutputFolderName ? fn : entries[i].filename, "wb")) == NULL)
		{
			perror("Error opening output");
			return 1;
		}

		/* decompressed size is stored big endian for whatever reason */
		decSz  = __builtin_bswap32(*(uint32_t *)(data + entries[i].offset));
		buffer = malloc(decSz);

		decompress(buffer, data + entries[i].offset);

		fwrite(buffer, 1, decSz, out);
		fclose(out);

		free(buffer);
	}

	free(entries);
	free(data);

	puts("\nDone!");

	return 0;
}