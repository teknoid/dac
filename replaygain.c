#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <magic.h>
#include <linux/input.h>
#include <FLAC/metadata.h>

#include "mp3gain.h"
#include "mcp.h"

double current_replaygain = -6;

magic_t magic = NULL;

static const char* flac_get_tag_utf8(const FLAC__StreamMetadata *tags, const char *name) {
	const int i = FLAC__metadata_object_vorbiscomment_find_entry_from(tags, /*offset=*/0, name);
	if (i < 0)
		return 0;

	const char *entry = (const char *) tags->data.vorbis_comment.comments[i].entry;
	const char *value = strchr(entry, '=') + 1;
	return value;
}

static double flac_get_replaygain(const char *filename) {
	FLAC__StreamMetadata *tags = NULL;
	double rgvalue = 0;

	if (!FLAC__metadata_get_tags(filename, &tags))
		return 0;

	if (FLAC__METADATA_TYPE_VORBIS_COMMENT != tags->type)
		return 0;

//	const char *albumgain = flac_get_tag_utf8(tags, "REPLAYGAIN_ALBUM_GAIN");
//	if (albumgain) {
//		sscanf(albumgain, "%lf dB", &rgvalue);
//		FLAC__metadata_object_delete(tags);
//		return rgvalue;
//	}
	const char *trackgain = flac_get_tag_utf8(tags, "REPLAYGAIN_TRACK_GAIN");
	if (trackgain) {
		sscanf(trackgain, "%lf dB", &rgvalue);
		FLAC__metadata_object_delete(tags);
		return rgvalue;
	}
	return 0;
}

static double mp3_get_replaygain(const char *filename) {
	struct MP3GainTagInfo *taginfo;
	taginfo = malloc(sizeof(struct MP3GainTagInfo));

	ReadMP3GainID3Tag((char*) filename, taginfo);
//	if (taginfo->haveAlbumGain) {
//		return taginfo->albumGain;
//	}
	if (taginfo->haveTrackGain) {
		return taginfo->trackGain;
	}
	return 0;
}

void replaygain(const char *filename) {
	double this_replaygain = 0;
	char buf[256];
	int i;

	if (magic == NULL) {
		magic = magic_open(MAGIC_CONTINUE | MAGIC_MIME);
		magic_load(magic, NULL);
	}

	const char *mime = magic_file(magic, filename);
	if (strstr(mime, "flac") != NULL) {
		this_replaygain = flac_get_replaygain(filename);
	} else if (strstr(mime, "mpeg") != NULL) {
		this_replaygain = mp3_get_replaygain(filename);
	} else {
		mcplog("replaygain not supported for %s", filename);
		return;
	}

	if (this_replaygain < -12.0) {
		mcplog("limiting replaygain to -12 (%f)", this_replaygain);
		this_replaygain = -12;
	} else if (this_replaygain > 0.0) {
		mcplog("limiting replaygain to 0 (%f)", this_replaygain);
		this_replaygain = 0;
	}

	double diff = this_replaygain - current_replaygain;
	sprintf(buf, "current %5.2f | new %5.2f | adjust %5.2f", current_replaygain, this_replaygain, diff);
	mcplog("%s", buf);

	int count = (int) abs(rint(diff));
	if (count != 0 && diff < 0) {
		for (i = 0; i < count; i++) {
			dac_volume_down();
		}
		current_replaygain = this_replaygain;
	} else if (count != 0 && diff > 0) {
		for (i = 0; i < count; i++) {
			dac_volume_up();
		}
		current_replaygain = this_replaygain;
	}
}
