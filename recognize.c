/*
 * recognize.c - Class recognition attributes for Sinclair QL screen decoder (.dtcode section)
 */

#define SYSTEM_PRIVATE

#include <proto/intuition.h>
#include <proto/multimedia.h>
#include <clib/alib_protos.h>
#include <classes/multimedia/multimedia.h>
#include <classes/multimedia/video.h>

#include "class_version.h"

static const ULONG InputFormats[] = {
	MMF_VIDEO_QLSCREEN,
	0
};

const struct TagItem ClassTags[] = {
	{MMA_ClassType,        MMCLASS_DECODER},
	{MMA_MediaType,        MMT_PICTURE},
	{MMA_SupportedFormats, (IPTR)InputFormats},
	{TAG_END,              0}
};

const struct TagItem* ClassAttributes(void)
{
	return ClassTags;
}
