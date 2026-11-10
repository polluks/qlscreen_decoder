/*
 * test_qlscreen.c - Unit tests for Sinclair QL screen decoder routines
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef uint8_t  UBYTE;
typedef uint16_t UWORD;
typedef uint32_t ULONG;
typedef int32_t  LONG;

#define QL_SCREEN_SIZE         32768
#define QL_SCREEN_LINES        256
#define QL_LINE_BYTES          128

#define QL_MODE_4              4
#define QL_MODE_8              8

#define QL_MODE4_WIDTH         512
#define QL_MODE4_HEIGHT        256
#define QL_MODE4_COLORS        4

#define QL_MODE8_WIDTH         256
#define QL_MODE8_HEIGHT        256
#define QL_MODE8_COLORS        8

#define QL_MAX_LINE_PIXELS     512

/*
 * Sinclair QL 4-colour palette (Mode 4):
 *  0: Black  (#000000)
 *  1: Red    (#FF0000)
 *  2: Green  (#00FF00)
 *  3: White  (#FFFFFF)
 */
static const ULONG QL_Palette_Mode4[QL_MODE4_COLORS] = {
	0xFF000000,
	0xFFFF0000,
	0xFF00FF00,
	0xFFFFFFFF
};

/*
 * Sinclair QL 8-colour palette (Mode 8):
 *  0: Black   (#000000)
 *  1: Blue    (#0000FF)
 *  2: Red     (#FF0000)
 *  3: Magenta (#FF00FF)
 *  4: Green   (#00FF00)
 *  5: Cyan    (#00FFFF)
 *  6: Yellow  (#FFFF00)
 *  7: White   (#FFFFFF)
 */
static const ULONG QL_Palette_Mode8[QL_MODE8_COLORS] = {
	0xFF000000,
	0xFF0000FF,
	0xFFFF0000,
	0xFFFF00FF,
	0xFF00FF00,
	0xFF00FFFF,
	0xFFFFFF00,
	0xFFFFFFFF
};

/* Decoding functions under test (from qlscreen.c) */
void QL_DecodeMode4LineARGB(const UBYTE *src, ULONG *dst)
{
	int w;
	for (w = 0; w < QL_LINE_BYTES; w += 2)
	{
		UBYTE g = src[w];
		UBYTE r = src[w + 1];
		int bit;
		for (bit = 7; bit >= 0; bit--)
		{
			UBYTE pixel = (UBYTE)((((g >> bit) & 1) << 1) | ((r >> bit) & 1));
			*dst++ = QL_Palette_Mode4[pixel];
		}
	}
}

void QL_DecodeMode8LineARGB(const UBYTE *src, ULONG *dst)
{
	int w;
	for (w = 0; w < QL_LINE_BYTES; w += 2)
	{
		UBYTE gb = src[w];
		UBYTE rb = src[w + 1];
		int pair;
		for (pair = 6; pair >= 0; pair -= 2)
		{
			UBYTE g = (gb >> (pair + 1)) & 1;
			UBYTE r = (rb >> (pair + 1)) & 1;
			UBYTE b = (rb >> pair) & 1;
			UBYTE pixel = (UBYTE)((g << 2) | (r << 1) | b);
			*dst++ = QL_Palette_Mode8[pixel];
		}
	}
}

ULONG QL_DetectModeFromBuffer(const UBYTE *buf, ULONG len)
{
	ULONG flash_count = 0;
	ULONG total_tested = 0;
	ULONG i;

	for (i = 0; i < len; i += 2)
	{
		UBYTE gb = buf[i];
		flash_count += ((gb >> 6) & 1) + ((gb >> 4) & 1) + ((gb >> 2) & 1) + (gb & 1);
		total_tested += 4;
	}

	if (total_tested > 0 && (flash_count * 100 / total_tested) >= 5)
	{
		return QL_MODE_4;
	}
	return QL_MODE_8;
}

void test_mode4_decoding(void)
{
	UBYTE src[128];
	ULONG dst[512];
	int i;

	memset(src, 0, sizeof(src));
	memset(dst, 0, sizeof(dst));

	/* Test all black */
	QL_DecodeMode4LineARGB(src, dst);
	for (i = 0; i < 512; i++)
	{
		assert(dst[i] == 0xFF000000);
	}

	/* Test all red (g=0, r=0xFF) */
	src[0] = 0x00;
	src[1] = 0xFF;
	QL_DecodeMode4LineARGB(src, dst);
	for (i = 0; i < 8; i++)
	{
		assert(dst[i] == 0xFFFF0000);
	}
	for (i = 8; i < 512; i++)
	{
		assert(dst[i] == 0xFF000000);
	}

	/* Test all green (g=0xFF, r=0x00) */
	src[0] = 0xFF;
	src[1] = 0x00;
	QL_DecodeMode4LineARGB(src, dst);
	for (i = 0; i < 8; i++)
	{
		assert(dst[i] == 0xFF00FF00);
	}

	/* Test all white (g=0xFF, r=0xFF) */
	src[0] = 0xFF;
	src[1] = 0xFF;
	QL_DecodeMode4LineARGB(src, dst);
	for (i = 0; i < 8; i++)
	{
		assert(dst[i] == 0xFFFFFFFF);
	}

	/* Test alternating bits (g=0b10101010=0xAA, r=0b01010101=0x55) */
	/* Pixels: Green (2), Red (1), Green (2), Red (1)... */
	src[0] = 0xAA;
	src[1] = 0x55;
	QL_DecodeMode4LineARGB(src, dst);
	for (i = 0; i < 8; i++)
	{
		if (i % 2 == 0)
		{
			assert(dst[i] == 0xFF00FF00);
		}
		else
		{
			assert(dst[i] == 0xFFFF0000);
		}
	}

	printf("PASS: Mode 4 line decoding into ARGB32\n");
}

void test_mode8_decoding(void)
{
	UBYTE src[128];
	ULONG dst[256];
	int i;

	memset(src, 0, sizeof(src));
	memset(dst, 0, sizeof(dst));

	/* All black */
	QL_DecodeMode8LineARGB(src, dst);
	for (i = 0; i < 256; i++)
	{
		assert(dst[i] == 0xFF000000);
	}

	/* Test all 8 colors in first 8 pixels (2 word pairs, 4 bytes) */
	/* Colors 0..3: Black, Blue, Red, Magenta */
	src[0] = 0x00;                                /* G=0 for all 4 pixels */
	src[1] = (0 << 6) | (1 << 4) | (2 << 2) | 3;  /* R, B bits */

	/* Colors 4..7: Green, Cyan, Yellow, White */
	src[2] = 0xAA;                                /* G=1 (bits 7, 5, 3, 1 set) */
	src[3] = (0 << 6) | (1 << 4) | (2 << 2) | 3;  /* R, B bits */

	QL_DecodeMode8LineARGB(src, dst);

	assert(dst[0] == 0xFF000000); /* Black */
	assert(dst[1] == 0xFF0000FF); /* Blue */
	assert(dst[2] == 0xFFFF0000); /* Red */
	assert(dst[3] == 0xFFFF00FF); /* Magenta */
	assert(dst[4] == 0xFF00FF00); /* Green */
	assert(dst[5] == 0xFF00FFFF); /* Cyan */
	assert(dst[6] == 0xFFFFFF00); /* Yellow */
	assert(dst[7] == 0xFFFFFFFF); /* White */

	for (i = 8; i < 256; i++)
	{
		assert(dst[i] == 0xFF000000);
	}

	printf("PASS: Mode 8 line decoding into ARGB32\n");
}

void test_scanline_streaming_simulation(void)
{
	/* Simulate scanline buffer streaming to arbitrary chunk requests in Pull */
	ULONG scanline_buf[QL_MAX_LINE_PIXELS];
	ULONG out_buf[1024];
	ULONG *pixel_ptr = scanline_buf;
	ULONG pixels_left = 0;

	/* Setup a test line: Mode 4 (512 pixels) */
	int i;
	for (i = 0; i < 512; i++)
	{
		scanline_buf[i] = 0xFF000000 | i;
	}
	pixels_left = 512;
	pixel_ptr = scanline_buf;

	/* Simulate pulling in small chunks, e.g. 100 pixels at a time */
	ULONG total_pulled = 0;
	while (total_pulled < 512)
	{
		ULONG chunk = 100;
		if (chunk > (512 - total_pulled)) chunk = 512 - total_pulled;

		if (pixels_left > 0)
		{
			ULONG to_copy = (chunk < pixels_left) ? chunk : pixels_left;
			memcpy(&out_buf[total_pulled], pixel_ptr, to_copy * sizeof(ULONG));
			pixel_ptr += to_copy;
			pixels_left -= to_copy;
			total_pulled += to_copy;
		}
	}

	assert(total_pulled == 512);
	for (i = 0; i < 512; i++)
	{
		assert(out_buf[i] == (0xFF000000 | i));
	}

	printf("PASS: Scanline buffer streaming simulation\n");
}

void test_mode_detection(void)
{
	UBYTE buf[1024];

	/* Test buffer with no flash bits set -> Mode 8 */
	memset(buf, 0, sizeof(buf));
	assert(QL_DetectModeFromBuffer(buf, sizeof(buf)) == QL_MODE_8);

	/* Test buffer with regular green bitplane (bits 6, 4, 2, 0 set frequently) -> Mode 4 */
	memset(buf, 0x55, sizeof(buf)); /* 0b01010101 -> flash bits all 1 */
	assert(QL_DetectModeFromBuffer(buf, sizeof(buf)) == QL_MODE_4);

	printf("PASS: Mode detection heuristics\n");
}

int main(void)
{
	printf("Running Sinclair QL Decoder Unit Tests...\n");
	test_mode4_decoding();
	test_mode8_decoding();
	test_scanline_streaming_simulation();
	test_mode_detection();
	printf("All unit tests passed successfully!\n");
	return 0;
}
