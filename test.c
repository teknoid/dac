#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <magic.h>
#include <libmpd.h>
#include <FLAC/metadata.h>
#include <id3tag.h>
#include "mp3gain.h"

#define RED "\x1b[31;01m"
#define DARKRED "\x1b[31;06m"
#define RESET "\x1b[0m"
#define GREEN "\x1b[32;06m"
#define YELLOW "\x1b[33;06m"

#define MUSIC "/public/music/"

#define PACKET_SIZE 256
#define LIRC_DEV "/dev/lircd"
#define AUDIOPHONICS "audiophonics"

double rgcurrent = -6;
int lirc = -1;

magic_t magic;

int lirc_socket() {
	struct sockaddr_un addr_un;

	int lirc = socket(AF_UNIX, SOCK_STREAM, 0);
	if (lirc == -1) {
		syslog(LOG_ERR, "could not open LIRC socket");
	}

	addr_un.sun_family = AF_UNIX;
	strcpy(addr_un.sun_path, LIRC_DEV);
	if (connect(lirc, (struct sockaddr *) &addr_un, sizeof(addr_un)) == -1) {
		syslog(LOG_ERR, "could not connect to LIRC socket");
	}
	return lirc;
}

void lirc_send(int lirc, const char *remote, const char *command) {
	char buffer[PACKET_SIZE];
	int done, todo;

	memset(buffer, '\0', PACKET_SIZE);
	sprintf(buffer, "SEND_ONCE %s %s\n", remote, command);
	todo = strlen(buffer);
	char *data = buffer;
	while (todo > 0) {
		done = write(lirc, (void *) data, todo);
		if (done < 0) {
			syslog(LOG_ERR, "could not send LIRC packet");
			return;
		}
		data += done;
		todo -= done;
	}
}

void lirc_volume_up() {
	lirc_send(lirc, AUDIOPHONICS, "KEY_VOLUMEUP");
	printf(YELLOW"VOL++\n"RESET);
}

void lirc_volume_down() {
	lirc_send(lirc, AUDIOPHONICS, "KEY_VOLUMEDOWN");
	printf(YELLOW"VOL--\n"RESET);
}

const char* flac_get_tag_utf8(const FLAC__StreamMetadata *tags, const char *name) {
	const int i = FLAC__metadata_object_vorbiscomment_find_entry_from(tags, /*offset=*/0, name);
	return (i < 0 ? 0 : strchr(tags->data.vorbis_comment.comments[i].entry, '=') + 1);
}

double flac_get_replaygain(const char *filename) {
	FLAC__StreamMetadata *tags = NULL;
	double rgvalue;

	if (!FLAC__metadata_get_tags(filename, &tags))
		if (0 == (tags = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT)))
			return 0;

	const char *trackgain = flac_get_tag_utf8(tags, "REPLAYGAIN_TRACK_GAIN");
	if (trackgain) {
		sscanf(trackgain, "%lf dB", &rgvalue);
		FLAC__metadata_object_delete(tags);
		return rgvalue;
	}

	const char *albumgain = flac_get_tag_utf8(tags, "REPLAYGAIN_ALBUM_GAIN");
	if (albumgain) {
		sscanf(albumgain, "%lf dB", &rgvalue);
		FLAC__metadata_object_delete(tags);
		return rgvalue;
	}
	return 0;
}

double mp3_get_replaygain(const char *filename) {
	struct MP3GainTagInfo *taginfo;
	taginfo = malloc(sizeof(struct MP3GainTagInfo));

	ReadMP3GainID3Tag((char*) filename, taginfo);
	if (taginfo->haveTrackGain) {
		fprintf(stdout, "Recommended \"Track\" dB change: %f\n", taginfo->trackGain);
		return taginfo->trackGain;
	}
	if (taginfo->haveAlbumGain) {
		fprintf(stdout, "Recommended \"Album\" dB change: %f\n", taginfo->albumGain);
		return taginfo->albumGain;
	}
	return 0;
}

void replaygain(mpd_Song *song) {
	int i;
	double rgnew = 0;
	char buf[256];

	strcpy(buf, MUSIC);
	strcat(buf, song->file);
	const char *mime = magic_file(magic, buf);
	if (strstr(mime, "flac") != NULL) {
		rgnew = flac_get_replaygain(buf);
	} else if (strstr(mime, "mpeg") != NULL) {
		rgnew = mp3_get_replaygain(buf);
	} else {
		printf("replaygain not supported %s\n", buf);
	}

	if (rgnew == 0) {
		return;
	} else if (rgnew < -12) {
		printf("limiting replaygain to -12 (%f)\n", rgnew);
		rgnew = -12;
	} else if (rgnew > 6) {
		printf("limiting replaygain to +6 (%f)\n", rgnew);
		rgnew = 6;
	}

	double rgdiff = rgnew - rgcurrent;
	sprintf(buf, "RG current %lf  new %lf  adjust %lf", rgcurrent, rgnew, rgdiff);
	printf("%s\n", buf);

	int count = (int) abs(rint(rgdiff));
	if (count != 0 && rgdiff < 0) {
		for (i = 0; i < count; i++) {
			lirc_volume_down();
		}
		rgcurrent = rgnew;
	} else if (count != 0 && rgdiff > 0) {
		for (i = 0; i < count; i++) {
			lirc_volume_up();
		}
		rgcurrent = rgnew;
	}
}

void status_changed(MpdObj *mi, ChangedStatusType what) {
	if (what & MPD_CST_SONGID) {
		mpd_Song *song = mpd_playlist_get_current_song(mi);
		if (song) {
			char filename[128];
			strcpy(filename, MUSIC);
			strcat(filename, song->file);

			printf(GREEN"Song:"RESET" %s - %s\n", song->artist, song->title);
			replaygain(song);
		}
	}
}

int main() {
	int run = 1, iport = 6600;
	char *hostname = getenv("MPD_HOST");
	char *port = getenv("MPD_PORT");
	char *password = getenv("MPD_PASSWORD");
	MpdObj *obj = NULL;

	if (!hostname) {
		hostname = "localhost";
	}
	if (port) {
		iport = atoi(port);
	}

	magic = magic_open(MAGIC_CONTINUE | MAGIC_MIME);
	magic_load(magic, NULL);

	obj = mpd_new(hostname, iport, password);

	mpd_signal_connect_status_changed(obj, (StatusChangedCallback) status_changed, NULL);
	mpd_set_connection_timeout(obj, 10);

	lirc = lirc_socket();

	if (!mpd_connect(obj)) {
		mpd_send_password(obj);
		do {
			mpd_status_update(obj);
		} while (!usleep(500 * 1000) && run);
	}
	return 1;
}
