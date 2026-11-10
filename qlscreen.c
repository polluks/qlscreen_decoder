/*
 * qlscreen.c - MorphOS Reggae decoder class for Sinclair QL screen images
 *
 * Decodes Sinclair QL screen video streams (MMF_VIDEO_QLSCREEN) into
 * standard 32-bit ARGB pixels (MMF_VIDEO_ARGB32).
 *
 * Supports both Sinclair QL screen modes:
 *  - Mode 4: 512 x 256, 4 colours (2 bpp)
 *  - Mode 8: 256 x 256, 8 colours (3 bpp)
 *
 * A Sinclair QL screen image is 32768 bytes (256 lines of 128 bytes).
 */

#define __NOLIBBASE__
#define SYSTEM_PRIVATE

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/multimedia.h>
#include <emul/emulregs.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <clib/alib_protos.h>
#include <clib/debug_protos.h>
#include <classes/multimedia/multimedia.h>
#include <classes/multimedia/video.h>

#include "class_version.h"

#define SUPERCLASS "multimedia.class"

/* Sinclair QL Screen Constants */
#define QL_SCREEN_SIZE         32768
#define QL_SCREEN_LINES        256
#define QL_LINE_BYTES          128

#define QL_MODE_UNKNOWN        0
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

/* Library bases */
struct Library *SysBase, *IntuitionBase, *UtilityBase, *MultimediaBase;

struct ClassBase
{
	struct Library          LibNode;
	Class                  *LibClass;
	APTR                    Seglist;
	struct SignalSemaphore  BaseLock;
	BOOL                    InitFlag;
	const struct TagItem   *Attributes;
};

struct ObjData
{
	Object       *StreamObj;
	UBYTE        *ScreenData;      /* 32768 bytes raw QL screen buffer */
	ULONG        *ScanlineBuffer;  /* 512 ULONGs ARGB32 line buffer */
	ULONG         Width;           /* 512 or 256 */
	ULONG         Height;          /* 256 */
	ULONG         Mode;            /* QL_MODE_4 or QL_MODE_8 */
	ULONG         CurrentLine;     /* Current scanline (0..255) */
	ULONG         PixelsLeft;      /* ARGB32 pixels left in ScanlineBuffer */
	ULONG        *PixelPtr;        /* Current read pointer in ScanlineBuffer */
	BOOL          SetupDone;
};

#define GET_BASE struct ClassBase *cb = (struct ClassBase*)cl->cl_UserData
#define GET_DATA struct ObjData *d = (struct ObjData*)INST_DATA(cl, obj)

static const ULONG InputFormats[] = {
	MMF_VIDEO_QLSCREEN,
	0
};

static const ULONG OutputFormats[] = {
	MMF_VIDEO_ARGB32,
	0
};

static inline ULONG xget(Object *obj, ULONG attr)
{
	ULONG val = 0;
	DoMethod(obj, OM_GET, attr, (IPTR)&val);
	return val;
}

#define OBJNAME(obj) xget(obj, MMA_ObjectName)

#ifdef DEBUG
#define MLOG(level, format, ...) MLog(format, ##__VA_ARGS__)
#define MLOGV(level, format, ...) MLog(format, ##__VA_ARGS__)
#else
#define MLOG(level, format, ...) do {} while (0)
#define MLOGV(level, format, ...) do {} while (0)
#endif

#define SETERR(err) MediaSetPort(obj, 0, MMA_ErrorCode, (err))

/* Forward declarations */
LONG ClassDispatcher(void);
struct Library* LibInit(struct Library *unused, APTR seglist, struct Library *sysbase);
struct Library* LibOpen(void);
ULONG LibClose(void);
APTR LibExpunge(void);
ULONG LibReserved(void);
Class* GetClass(void);
APTR lib_expunge(struct ClassBase *cb);

/* ROM Tag */
static const char LibName[] = CLASSNAME;
static const char LibIdString[] = VSTRING;

const struct Resident ROMTag = {
	RTC_MATCHWORD,
	(struct Resident*)&ROMTag,
	(struct Resident*)&ROMTag + 1,
	RTF_AUTOINIT,
	VERSION,
	NT_LIBRARY,
	0,
	(char*)LibName,
	(char*)LibIdString,
	(APTR)LibInit
};

static const APTR JumpTable[] = {
	(APTR)LibOpen,
	(APTR)LibClose,
	(APTR)LibExpunge,
	(APTR)LibReserved,
	(APTR)GetClass,
	(APTR)-1
};

Class *init_class(struct ClassBase *cb)
{
	Class *cl;

	cl = MakeClass(CLASSNAME, SUPERCLASS, NULL, sizeof(struct ObjData), 0);
	if (cl)
	{
		cl->cl_Dispatcher.h_Entry = (HOOKFUNC)ClassDispatcher;
		cl->cl_UserData = (IPTR)cb;
		AddClass(cl);
		cb->LibClass = cl;
	}
	return cl;
}

BOOL InitResources(struct ClassBase *cb)
{
	if (!(IntuitionBase = OpenLibrary("intuition.library", 50))) return FALSE;
	if (!(UtilityBase = OpenLibrary("utility.library", 50))) return FALSE;
	if (!(MultimediaBase = OpenLibrary("Multimedia/multimedia.class", 50))) return FALSE;
	if (!(init_class(cb))) return FALSE;
	return TRUE;
}

void FreeResources(struct ClassBase *cb)
{
	(void)cb;
	if (MultimediaBase) CloseLibrary(MultimediaBase);
	if (UtilityBase) CloseLibrary(UtilityBase);
	if (IntuitionBase) CloseLibrary(IntuitionBase);
}

struct ClassBase *lib_init(struct ClassBase *cb, APTR seglist, struct Library *sysbase)
{
	InitSemaphore(&cb->BaseLock);
	cb->Seglist = seglist;
	cb->Attributes = 0;
	(void)sysbase;
	return cb;
}

struct Library *LibInit(struct Library *unused, APTR seglist, struct Library *sysbase)
{
	(void)unused;
	SysBase = sysbase;

	return (NewCreateLibraryTags(
		LIBTAG_FUNCTIONINIT, (ULONG)JumpTable,
		LIBTAG_LIBRARYINIT,  (ULONG)lib_init,
		LIBTAG_MACHINE,      MACHINE_PPC,
		LIBTAG_BASESIZE,     sizeof(struct ClassBase),
		LIBTAG_SEGLIST,      (ULONG)seglist,
		LIBTAG_TYPE,         NT_LIBRARY,
		LIBTAG_NAME,         (ULONG)ROMTag.rt_Name,
		LIBTAG_IDSTRING,     (ULONG)ROMTag.rt_IdString,
		LIBTAG_FLAGS,        LIBF_CHANGED | LIBF_SUMUSED,
		LIBTAG_VERSION,      VERSION,
		LIBTAG_REVISION,     REVISION,
		LIBTAG_PUBLIC,       TRUE,
	TAG_END));
}

struct Library *LibOpen(void)
{
	struct ClassBase *cb = (struct ClassBase*)REG_A6;
	struct Library *lib = (struct Library*)cb;

	ObtainSemaphore(&cb->BaseLock);
	if (!cb->InitFlag)
	{
		if (InitResources(cb)) cb->InitFlag = TRUE;
		else
		{
			FreeResources(cb);
			lib = NULL;
		}
	}

	if (lib)
	{
		cb->LibNode.lib_Flags &= ~LIBF_DELEXP;
		cb->LibNode.lib_OpenCnt++;
	}
	ReleaseSemaphore(&cb->BaseLock);
	return lib;
}

ULONG LibClose(void)
{
	struct ClassBase *cb = (struct ClassBase*)REG_A6;
	ULONG ret = 0;

	ObtainSemaphore(&cb->BaseLock);
	if (--cb->LibNode.lib_OpenCnt == 0)
	{
		if (cb->LibNode.lib_Flags & LIBF_DELEXP) ret = (ULONG)lib_expunge(cb);
	}
	ReleaseSemaphore(&cb->BaseLock);

	return ret;
}

APTR LibExpunge(void)
{
	struct ClassBase *cb = (struct ClassBase*)REG_A6;
	return lib_expunge(cb);
}

APTR lib_expunge(struct ClassBase *cb)
{
	APTR seglist = NULL;

	ObtainSemaphore(&cb->BaseLock);
	if (cb->LibNode.lib_OpenCnt == 0)
	{
		if (!cb->LibClass || FreeClass(cb->LibClass))
		{
			cb->LibClass = NULL;
			Forbid();
			Remove((struct Node*)cb);
			Permit();
			FreeResources(cb);
			seglist = cb->Seglist;
			FreeMem((UBYTE*)cb - cb->LibNode.lib_NegSize, cb->LibNode.lib_NegSize + cb->LibNode.lib_PosSize);
			cb = NULL;
		}
		if (cb && cb->LibClass) AddClass(cb->LibClass);
	}
	else
	{
		cb->LibNode.lib_Flags |= LIBF_DELEXP;
	}

	if (cb) ReleaseSemaphore(&cb->BaseLock);
	return seglist;
}

ULONG LibReserved(void)
{
	return 0;
}

Class *GetClass(void)
{
	struct ClassBase *cb = (struct ClassBase*)REG_A6;
	return cb->LibClass;
}

/*
 * Decode Mode 4 line (128 bytes -> 512 ARGB32 pixels)
 * Each word pair (green_byte, red_byte) gives 8 pixels.
 */
static void QL_DecodeMode4LineARGB(const UBYTE *src, ULONG *dst)
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

/*
 * Decode Mode 8 line (128 bytes -> 256 ARGB32 pixels)
 * Each word pair (gb_byte, rb_byte) gives 4 pixels.
 */
static void QL_DecodeMode8LineARGB(const UBYTE *src, ULONG *dst)
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

/*
 * Decode current scanline into ScanlineBuffer.
 * Returns 0 on success, or error code (e.g. MMERR_END_OF_DATA).
 */
static LONG QL_DecodeScanline(struct ObjData *d)
{
	const UBYTE *line_src;

	if (d->CurrentLine >= QL_SCREEN_LINES)
	{
		return MMERR_END_OF_DATA;
	}

	line_src = d->ScreenData + (d->CurrentLine * QL_LINE_BYTES);

	if (d->Mode == QL_MODE_8)
	{
		QL_DecodeMode8LineARGB(line_src, d->ScanlineBuffer);
		d->PixelsLeft = QL_MODE8_WIDTH;
	}
	else
	{
		QL_DecodeMode4LineARGB(line_src, d->ScanlineBuffer);
		d->PixelsLeft = QL_MODE4_WIDTH;
	}

	d->PixelPtr = d->ScanlineBuffer;
	d->CurrentLine++;
	return 0;
}

/*
 * Detect Sinclair QL mode from screen data if upstream width is unknown:
 * Mode 8 graphics typically have flash bits (even byte bits 6, 4, 2, 0) cleared (0).
 * In Mode 4, the even byte is a continuous green bitplane, so flash bits are set >= 5%.
 */
static ULONG QL_DetectModeFromBuffer(const UBYTE *buf, ULONG len)
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

/* BOOPSI methods */

LONG New(Class *cl, Object *obj, struct opSet *msg)
{
	LONG newobj = 0;

	if ((obj = (Object*)DoSuperMethodA(cl, obj, (Msg)msg)))
	{
		GET_DATA;
		DoMethod(obj, MMM_LockObject);

		d->StreamObj = (Object*)GetTagData(MMA_Stream, 0, msg->ops_AttrList);
		d->Width = QL_MODE4_WIDTH;
		d->Height = QL_MODE4_HEIGHT;
		d->Mode = QL_MODE_4;
		d->CurrentLine = 0;
		d->PixelsLeft = 0;
		d->PixelPtr = NULL;
		d->SetupDone = FALSE;

		/* Input port: Port 0 (MMF_VIDEO_QLSCREEN) */
		DoMethod(obj, MMM_AddPort, 0);
		DoMethod(obj, MMM_SetPort, 0, MMA_Port_Type, MDP_TYPE_INPUT);
		DoMethod(obj, MMM_SetPort, 0, MMA_Port_FormatsTable, (IPTR)InputFormats);
		DoMethod(obj, MMM_SetPort, 0, MMA_Port_Format, MMF_VIDEO_QLSCREEN);

		/* Output port: Port 1 (MMF_VIDEO_ARGB32) */
		DoMethod(obj, MMM_AddPort, 1);
		DoMethod(obj, MMM_SetPort, 1, MMA_Port_Type, MDP_TYPE_OUTPUT);
		DoMethod(obj, MMM_SetPort, 1, MMA_Port_FormatsTable, (IPTR)OutputFormats);
		DoMethod(obj, MMM_SetPort, 1, MMA_Port_Format, MMF_VIDEO_ARGB32);

		/* Allocate screen buffer (32768 bytes) and scanline buffer (512 * 4 = 2048 bytes) */
		d->ScreenData = (UBYTE*)MediaAllocVec(QL_SCREEN_SIZE);
		d->ScanlineBuffer = (ULONG*)MediaAllocVec(QL_MAX_LINE_PIXELS * sizeof(ULONG));

		if (d->ScreenData && d->ScanlineBuffer)
		{
			newobj = (LONG)obj;
		}

		DoMethod(obj, MMM_UnlockObject);
	}

	if (!newobj) CoerceMethod(cl, obj, OM_DISPOSE);
	else MLOGV(LOG_INFO, "Object \"%s\" created at $%08lX.", OBJNAME((Object*)newobj), newobj);

	return newobj;
}

LONG Dispose(Class *cl, Object *obj, Msg msg)
{
	GET_DATA;

	DoMethod(obj, MMM_LockObject);
	if (d->ScreenData)
	{
		MediaFreeVec(d->ScreenData);
		d->ScreenData = NULL;
	}
	if (d->ScanlineBuffer)
	{
		MediaFreeVec(d->ScanlineBuffer);
		d->ScanlineBuffer = NULL;
	}
	MLOGV(LOG_INFO, "Object \"%s\" disposed.", OBJNAME(obj));
	DoMethod(obj, MMM_UnlockObject);

	return DoSuperMethodA(cl, obj, msg);
}

LONG Setup(Class *cl, Object *obj, struct mmopSetup *msg)
{
	GET_DATA;
	LONG rv = 0;

	MLOGV(LOG_VERBOSE, "Setup started for port %lu.", msg->Port);

	if (msg->Port == 0)
	{
		Object *stream_obj = NULL;
		ULONG bytes_read = 0;

		DoMethod(obj, MMM_GetPort, 0, MMA_Stream, (IPTR)&stream_obj);
		if (!stream_obj) stream_obj = d->StreamObj;

		if (!stream_obj)
		{
			SETERR(MMERR_NO_STREAM);
			return 0;
		}

		if (d->ScreenData)
		{
			/* Pull full 32KB Sinclair QL screen from Port 0 (handled by multimedia.class) */
			bytes_read = DoMethod(obj, MMM_Pull, 0, (IPTR)d->ScreenData, QL_SCREEN_SIZE);
			if (bytes_read == QL_SCREEN_SIZE)
			{
				ULONG upstream_width = 0;

				/* Query upstream width on Port 0 if available */
				DoMethod(obj, MMM_GetPort, 0, MMA_Video_Width, (IPTR)&upstream_width);
				if (upstream_width == QL_MODE8_WIDTH)
				{
					d->Mode = QL_MODE_8;
					d->Width = QL_MODE8_WIDTH;
				}
				else if (upstream_width == QL_MODE4_WIDTH)
				{
					d->Mode = QL_MODE_4;
					d->Width = QL_MODE4_WIDTH;
				}
				else
				{
					/* Auto-detect mode from screen content */
					ULONG detected = QL_DetectModeFromBuffer(d->ScreenData, QL_SCREEN_SIZE);
					if (detected == QL_MODE_8)
					{
						d->Mode = QL_MODE_8;
						d->Width = QL_MODE8_WIDTH;
					}
					else
					{
						d->Mode = QL_MODE_4;
						d->Width = QL_MODE4_WIDTH;
					}
				}

				d->Height = QL_SCREEN_LINES;
				d->CurrentLine = 0;
				d->PixelsLeft = 0;
				d->PixelPtr = NULL;
				d->SetupDone = TRUE;
				rv = 1;
			}
			else
			{
				ULONG err = 0;
				DoMethod(obj, MMM_GetPort, 0, MMA_ErrorCode, (IPTR)&err);
				SETERR(err);
				rv = 0;
			}
		}
		else
		{
			SETERR(MMERR_OUT_OF_MEMORY);
			rv = 0;
		}
	}
	else if (msg->Port == 1)
	{
		if (!d->SetupDone)
		{
			rv = DoMethod(obj, MMM_Setup, 0);
		}
		else
		{
			rv = 1;
		}
	}
	else
	{
		MLOGV(LOG_ERRORS, "Setup on non existing port %lu.", msg->Port);
		rv = 0;
	}

	return rv;
}

LONG Get(Class *cl, Object *obj, struct opGet *msg)
{
	GET_DATA;

	switch (msg->opg_AttrID)
	{
		case MMA_Video_Width:
			*msg->opg_Storage = d->Width;
			return TRUE;

		case MMA_Video_Height:
			*msg->opg_Storage = d->Height;
			return TRUE;

		case MMA_Video_BitsPerPixel:
			*msg->opg_Storage = 32;
			return TRUE;

		case MMA_Video_FrameCount:
			*(UQUAD*)msg->opg_Storage = 1;
			return TRUE;

		case MMA_MediaType:
			*msg->opg_Storage = MMT_PICTURE;
			return TRUE;

		default:
			return DoSuperMethodA(cl, obj, (Msg)msg);
	}
}

LONG GetPort(Class *cl, Object *obj, struct mmopGetPort *msg)
{
	GET_DATA;

	if (msg->Port > 1)
	{
		MLOGV(LOG_ERRORS, "Port %lu does not exist in object $%08lX.", msg->Port, (IPTR)obj);
		return 0;
	}

	switch (msg->Attribute)
	{
		case MMA_Video_Width:
			*msg->Storage = d->Width;
			return TRUE;

		case MMA_Video_Height:
			*msg->Storage = d->Height;
			return TRUE;

		case MMA_Video_BitsPerPixel:
			*msg->Storage = 32;
			return TRUE;

		case MMA_Video_FrameCount:
			*(UQUAD*)msg->Storage = 1;
			return TRUE;

		case MMA_MediaType:
			*msg->Storage = MMT_PICTURE;
			return TRUE;

		default:
			return DoSuperMethodA(cl, obj, (Msg)msg);
	}
}

LONG Pull(Class *cl, Object *obj, struct mmopPull *msg)
{
	GET_DATA;
	LONG super_rv;

	/* Let superclass handle Port 0 or default */
	super_rv = DoSuperMethodA(cl, obj, (Msg)msg);
	if (super_rv != -2)
	{
		return super_rv;
	}

	/* Handle Output Port 1 */
	if (msg->Port == 1)
	{
		ULONG *out_ptr = (ULONG*)msg->Buffer;
		LONG pixels_needed = msg->Length >> 2;
		LONG bytes_pulled = 0;
		LONG err = 0;

		DoMethod(obj, MMM_LockObject);

		while (pixels_needed > 0)
		{
			if (d->PixelsLeft == 0)
			{
				err = QL_DecodeScanline(d);
				if (err != 0)
				{
					break;
				}
			}

			if (d->PixelsLeft > 0)
			{
				LONG chunk = pixels_needed;
				if (chunk > (LONG)d->PixelsLeft)
				{
					chunk = (LONG)d->PixelsLeft;
				}

				CopyMem(d->PixelPtr, out_ptr, chunk * sizeof(ULONG));

				d->PixelPtr += chunk;
				d->PixelsLeft -= chunk;
				out_ptr += chunk;
				pixels_needed -= chunk;
				bytes_pulled += chunk * sizeof(ULONG);
			}
		}

		if (err != 0 && bytes_pulled == 0)
		{
			SETERR(err);
		}

		DoMethod(obj, MMM_UnlockObject);
		return bytes_pulled;
	}

	SETERR(MMERR_WRONG_ARGUMENTS);
	return 0;
}

LONG ClassDispatcher(void)
{
	Class *cl = (Class*)REG_A0;
	Object *obj = (Object*)REG_A2;
	Msg msg = (Msg)REG_A1;

	switch (msg->MethodID)
	{
		case OM_NEW:      return New(cl, obj, (struct opSet*)msg);
		case OM_DISPOSE:  return Dispose(cl, obj, msg);
		case OM_GET:      return Get(cl, obj, (struct opGet*)msg);
		case MMM_Pull:    return Pull(cl, obj, (struct mmopPull*)msg);
		case MMM_Setup:   return Setup(cl, obj, (struct mmopSetup*)msg);
		case MMM_GetPort: return GetPort(cl, obj, (struct mmopGetPort*)msg);
		default:          return DoSuperMethodA(cl, obj, msg);
	}
}
