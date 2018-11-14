#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <linux/input.h>

#include <mpd/connection.h>
#include <mpd/message.h>
#include <mpd/client.h>
#include <mpd/entity.h>
#include <mpd/search.h>
#include <mpd/idle.h>
#include <mpd/tag.h>

#include "mcp.h"
#include "playlists.h"

int current_song = -1;
int playlist_mode = 1;

struct mpd_connection *conn;

// find active playlist by path of current song
static struct plist * find_current_playlist() {
	struct mpd_song *song = mpd_run_current_song(conn);
	if (song) {
		const char* path = mpd_song_get_uri(song);
		for (int i = 0; i <= 9; i++) {
			struct plist *playlist = &playlists[i];
			if (startsWith(playlist->path, path)) {
				return playlist;
			}
		}
		mpd_song_free(song);
	}
	return NULL;
}

// create new list from playlist's source directory
static void create_playlist(struct plist *playlist) {
	mpd_run_clear(conn);
	mpd_run_add(conn, playlist->path);
	mpd_run_shuffle(conn);
	mpd_run_rm(conn, playlist->name);
	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
		mpd_connection_clear_error(conn);
	}
	mpd_run_save(conn, playlist->name);
	playlist->pos = 0;
	mcplog("[%d] generated new playlist from %s", playlist->key, playlist->path);
}

// load list into queue and start playing from saved position
static void load_playlist(struct plist *playlist) {
	mpd_run_stop(conn);
	mpd_run_clear(conn);
	mpd_run_load(conn, playlist->name);
	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
		mpd_connection_clear_error(conn);
		create_playlist(playlist);
	}
	mpd_run_play_pos(conn, playlist->pos);
	mcplog("[%d] loaded playlist", playlist->key);
}

// update database for list 0, load list 0 and start playing from beginning
static void load_incoming() {
	struct plist *playlist = &playlists[0];
	mpd_run_stop(conn);
	mpd_run_clear(conn);
	mpd_run_update(conn, playlist->path);

	//	while (mpd_run_idle_mask(conn, MPD_IDLE_UPDATE) != 0) {
//		mcplog("sleep");
//		sleep(1);
//	}

// bug? mpd_run_idle_mask hangs? - workaround
	int count = 11;
	while (count-- > 0) {
		sleep(1);
		mcplog("wait %d", count);
	}
	mcplog("[0] updated %s", playlist->path);
	mpd_run_add(conn, playlist->path);
	mpd_run_play_pos(conn, 0);
	mcplog("[0] loaded playlist");
}

static void toggle_pause() {
	struct mpd_status *status = mpd_run_status(conn);
	enum mpd_state state = mpd_status_get_state(status);
	if (state == MPD_STATE_STOP || state == MPD_STATE_PAUSE) {
		mpd_run_play(conn);
		dac_unmute();
	} else if (state == MPD_STATE_PLAY) {
		dac_mute();
		mpd_run_toggle_pause(conn);
	}
	mpd_status_free(status);
}

static void process_song(struct mpd_song *song, int pos) {
	// apply replaygain to DAC from file metadata
	char filename[512];
	const char *path = mpd_song_get_uri(song);
	strcpy(filename, MUSIC);
	strcat(filename, path);
	replaygain(filename);

	// find current playlist and save position
	int plist_key = -1;
	if (playlist_mode) {
		for (int i = 0; i <= 9; i++) {
			struct plist *playlist = &playlists[i];
			if (startsWith(playlist->path, path)) {
				plist_key = playlist->key;
				playlist->pos = pos;
			}
		}
	}

	// print status
	const char *artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
	const char *title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
	const char *album = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);

	int valid = artist != NULL && title != NULL;
	if (valid) {
		mcplog("[%d:%d] %s - %s", plist_key, pos, artist, title);
		strcpy(mcp->artist, artist);
		strcpy(mcp->title, title);
		if (album != NULL) {
			strcpy(mcp->album, album);
		}
	} else {
		mcplog("[%d:%d] %s", plist_key, pos, path);
	}

	dac_update();
}

void mpdclient_set_playlist_mode(int mode) {
	playlist_mode = mode;
}

void mpdclient_handle(int key) {
	struct plist *playlist;

	// check connection
	assert(conn != NULL);
	struct mpd_status *status = mpd_run_status(conn);
	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
		mcplog("status: %s", mpd_connection_get_error_message(conn));
		if (!mpd_connection_clear_error(conn)) {
			mpd_connection_free(conn);
			conn = mpd_connection_new(MPD_HOST, MPD_PORT, 0);
			if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
				mcplog("connect: %s", mpd_connection_get_error_message(conn));
				return;
			}
			// mcplog("reconnected");
		}
	}
	if (status != NULL) {
		mpd_status_free(status);
	}
	// mcplog("connection ok");

	if (key == KEY_0) {
		load_incoming();
		return;
	}

	if (key >= KEY_1 && key <= KEY_9) {
		playlist = &playlists[key - 1];
		load_playlist(playlist);
		playlist_mode = 1;
		return;
	}

	switch (key) {
	case KEY_STOP:
		mpd_run_stop(conn);
		break;
	case KEY_PAUSE:
		toggle_pause();
		break;
	case KEY_PLAY:
		mpd_run_play(conn);
		break;
	case KEY_CLEAR:
		playlist = find_current_playlist();
		if (playlist) {
			create_playlist(playlist);
			load_playlist(playlist);
		}
		break;
	case KEY_PREVIOUSSONG:
		mpd_run_previous(conn);
		break;
	case KEY_NEXTSONG:
		mpd_run_next(conn);
		break;
	case KEY_RECORD:
		mpd_run_update(conn, "/");
		break;
	case KEY_REWIND:
		mpd_run_seek_pos(conn, 0, -10);
		break;
	case KEY_FORWARD:
		mpd_run_seek_pos(conn, 0, 10);
		break;
	}
}

int mpdclient_init() {
	conn = mpd_connection_new(MPD_HOST, MPD_PORT, 0);
	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
		mcplog("%s", mpd_connection_get_error_message(conn));
		mpd_connection_free(conn);
	}

	// requires libmpdclient >= 2.10
	//mpd_connection_set_keepalive(conn, true);
	//mpd_connection_set_timeout(conn, 5000);

	return 0;
}

void mpdclient_close() {
}

void *mpdclient(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		mcplog("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

	struct mpd_connection *conn_status = mpd_connection_new(MPD_HOST, MPD_PORT, 0);
	if (mpd_connection_get_error(conn_status) != MPD_ERROR_SUCCESS) {
		mcplog("connect: %s", mpd_connection_get_error_message(conn_status));
		mpd_connection_free(conn_status);
		return (void *) 0;
	}

	while (1) {
		struct mpd_status *status = mpd_run_status(conn_status);
		int state = mpd_status_get_state(status);
		mcplog("MPD State %d", state);
		mcp->mpd_state = state;

		if (state == MPD_STATE_PAUSE) {
			mcplog("MPD State PAUSE");
		} else if (state == MPD_STATE_STOP) {
			mcplog("MPD State STOP");
		} else if (state == MPD_STATE_PLAY) {
			mcplog("MPD State PLAY");

			const struct mpd_audio_format *audio_format = mpd_status_get_audio_format(status);
			if (audio_format != NULL) {
				mcp->mpd_bits = audio_format->bits;
				mcp->mpd_rate = audio_format->sample_rate;
			}

			struct mpd_song *song = mpd_run_current_song(conn_status);
			unsigned int this_song = mpd_song_get_id(song);
			if (this_song != current_song) {
				int pos = mpd_status_get_song_pos(status);
				process_song(song, pos);
				current_song = this_song;
			}
			mpd_song_free(song);
		}
		mpd_run_idle(conn_status);
	}

	mpd_connection_free(conn_status);
	return (void *) 0;
}
