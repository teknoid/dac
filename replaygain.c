#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <magic.h>
#include <math.h>

#include <FLAC/format.h>
#include <FLAC/metadata.h>

#include "dac.h"
#include "mcp.h"
#include "utils.h"
#include "mp3gain.h"

static double current_replaygain = 0;

static magic_t magic = NULL;

static const char* flac_get_tag_utf8(const FLAC__StreamMetadata *tags, const char *name) {
	const int i = FLAC__metadata_object_vorbiscomment_find_entry_from(tags, /*offset=*/0, name);
	if (i < 0)
		return 0;

	const char *entry = (const char*) tags->data.vorbis_comment.comments[i].entry;
	const char *value = strchr(entry, '=') + 1;
	return value;
}

static int flac_get_replaygain(const char *filename, double *rgvalue) {
	FLAC__StreamMetadata *tags = NULL;

	if (!FLAC__metadata_get_tags(filename, &tags))
		return -1;

	if (FLAC__METADATA_TYPE_VORBIS_COMMENT != tags->type)
		return -1;

	const char *albumgain = flac_get_tag_utf8(tags, "REPLAYGAIN_ALBUM_GAIN");
	if (albumgain) {
		sscanf(albumgain, "%lf dB", rgvalue);
		FLAC__metadata_object_delete(tags);
		return 0;
	}

	const char *trackgain = flac_get_tag_utf8(tags, "REPLAYGAIN_TRACK_GAIN");
	if (trackgain) {
		sscanf(trackgain, "%lf dB", rgvalue);
		FLAC__metadata_object_delete(tags);
		return 0;
	}

	return -1;
}

static int mp3_get_replaygain(const char *filename, double *rgvalue) {
	struct MP3GainTagInfo *taginfo;
	taginfo = malloc(sizeof(struct MP3GainTagInfo));
	ZERO(taginfo);

	struct FileTagsStruct *filetags;
	filetags = malloc(sizeof(struct FileTagsStruct));
	ZERO(filetags);

	// 1st try APE
	ReadMP3GainAPETag((char*) filename, taginfo, filetags);

	if (taginfo->haveTrackGain) {
		*rgvalue = taginfo->trackGain;
		free(taginfo);
		free(filetags);
		return 0;
	}

	if (taginfo->haveAlbumGain) {
		*rgvalue = taginfo->albumGain;
		free(taginfo);
		free(filetags);
		return 0;
	}

	// 2nd try ID3
	ReadMP3GainID3Tag((char*) filename, taginfo);

	if (taginfo->haveTrackGain) {
		*rgvalue = taginfo->trackGain;
		free(taginfo);
		free(filetags);
		return 0;
	}

	if (taginfo->haveAlbumGain) {
		*rgvalue = taginfo->albumGain;
		free(taginfo);
		free(filetags);
		return 0;
	}

	return -1;
}

void replaygain(const char *filename) {
	double new_replaygain = 0;
	int res, i;

	if (magic == NULL) {
		magic = magic_open(MAGIC_CONTINUE | MAGIC_MIME | MAGIC_SYMLINK);
		magic_load(magic, NULL);
	}

	const char *mime = magic_file(magic, filename);
	if (strstr(mime, "flac") != NULL) {
		res = flac_get_replaygain(filename, &new_replaygain);
	} else if (strstr(mime, "mpeg") != NULL) {
		res = mp3_get_replaygain(filename, &new_replaygain);
	} else {
		xlog("replaygain not supported for %s", filename);
		return;
	}

	if (res < 0) {
		xlog("no replaygain found in %s", filename);
		return;
	}

	if (new_replaygain < -12.0) {
		xlog("limiting replaygain to -12 (%f)", new_replaygain);
		new_replaygain = -12;
	} else if (new_replaygain > 3.0) {
		xlog("limiting replaygain to +3 (%f)", new_replaygain);
		new_replaygain = 3;
	}

	double diff = new_replaygain - current_replaygain;
	xlog("current %5.2f | new %5.2f | adjust %5.2f", current_replaygain, new_replaygain, diff);

	int steps = (int) abs(rint(diff));
	if (steps != 0 && diff < 0) {
		for (i = 0; i < steps; i++)
			dac_volume_down();
		current_replaygain = new_replaygain;
	} else if (steps != 0 && diff > 0) {
		for (i = 0; i < steps; i++)
			dac_volume_up();
		current_replaygain = new_replaygain;
	}
}
