#include <stdio.h>
#include <assert.h>

#include "bitmap.h"

int
bitmap_get(void* bm, int ii)
{
	short val = 0;
	short* start_addr = (short*)(bm);
	int bits = 8 * sizeof(short);
	
	start_addr += ii / bits;
	val = *start_addr;

	return (val >> (ii % bits)) & 0x1;
}

void
bitmap_put(void* bm, int ii, int vv)
{
	assert(vv == 0 || vv == 1);

	short val = 0;
	short* start_addr = (short*)(bm);
	int bits = 8 * sizeof(short);

	start_addr += ii / bits;

	if (vv == 0) {
		val = ~(0x1 << (ii % bits));
		*(start_addr) &= val;
	}
	else if (vv == 1) {
		val = 0x1 << (ii % bits);
		*(start_addr) |= val;	
	}

}

void
bitmap_print(void* bm, int size)
{
	short val = 0;
	short* start_addr = (short*)(bm);
	int bits = 8 * sizeof(short);

	long i = 0;

	do {
		val = *start_addr;

		printf("%d", ((val >> (i % bits)) & 0x1));

		i++;
		if (i % bits == 0) {
			start_addr++;
			fflush(stdout);
		}

	} while (i < size);

}
