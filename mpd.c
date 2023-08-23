#include <ctype.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mpd/audio_format.h>
#include <mpd/connection.h>
#include <mpd/database.h>
#include <mpd/error.h>
#include <mpd/idle.h>
#include <mpd/player.h>
#include <mpd/playlist.h>
#include <mpd/queue.h>
#include <mpd/song.h>
#include <mpd/status.h>
#include <mpd/tag.h>

#include <linux/input-event-codes.h>

#include "replaygain.h"
#include "utils.h"
#include "dac.h"
#include "mpd.h"
#include "mcp.h"

static int current_song = -1;
static int playlist_mode = 1;

static struct plist playlists[] = {
		{ 0, 0, "00_incoming", 		"00 incoming" },
		{ 1, 0, "01_top", 			"01 sortiert/01 top" },
		{ 2, 0, "02_aktuell", 		"01 sortiert/02 aktuell" },
		{ 3, 0, "03_modern", 		"01 sortiert/03 modern" },
		{ 4, 0, "04_eurodance", 	"01 sortiert/04 eurodance" },
		{ 5, 0, "05_umz", 			"01 sortiert/05 umz" },
		{ 6, 0, "06_extended", 		"01 sortiert/06 extended" },
		{ 7, 0, "07_chill", 		"01 sortiert/07 chill" },
		{ 8, 0, "08_slow", 			"01 sortiert/08 slow" },
		{ 9, 0, "09_house", 		"01 sortiert/09 house" },
};

static struct mpd_connection *conn;

static pthread_t thread_mpdclient;

static struct mpd_connection* mpdclient_get_connection() {
	// wait for mpd connect success
	int timeout = 10;
	while (1) {
		struct mpd_connection *connection = mpd_connection_new(MPD_HOST, MPD_PORT, 1000);
		if (!connection) {
			xlog("Out of memory");
			return NULL;
		}
		if (mpd_connection_get_error(connection) == MPD_ERROR_SUCCESS) {
			const unsigned int *v = mpd_connection_get_server_version(connection);
			xlog("connected to MPD on %s Version %d.%d.%d", MPD_HOST, v[0], v[1], v[2]);
			return connection;
		}
		if (--timeout == 0) {
			xlog("error connecting to MPD: %s", mpd_connection_get_error_message(connection));
			return NULL;
		}
		xlog("waiting for MPD connection %d", timeout);
		mpd_connection_free(connection);
		sleep(1);
	}
	return NULL;
}

static void upper(char *s) {
	while (*s != 0x00) {
		*s = toupper(*s);
		s++;
	}
}

static const char* get_filename_ext(const char *filename) {
	const char *dot = strrchr(filename, '.');
	if (!dot || dot == filename)
		return "";
	return dot + 1;
}

static void external(char *key) {
	char command[64];
	xlog("external %s", key);
	strcpy(command, EXTERNAL);
	strcat(command, " ");
	strcat(command, key);
	system(command);
}

// find active playlist by path of current song
static struct plist* find_current_playlist() {
	struct mpd_song *song = mpd_run_current_song(conn);
	if (song) {
		const char *path = mpd_song_get_uri(song);
		for (int i = 0; i <= 9; i++) {
			struct plist *playlist = &playlists[i];
			if (starts_with(playlist->path, path, strlen(path))) {
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
	xlog("[%d] generated new playlist from %s", playlist->key, playlist->path);
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
	xlog("loaded playlist %s", playlist->name);
}

// update database for list 0, load list 0 and start playing from beginning
static void load_incoming() {
	struct plist *playlist = &playlists[0];
	mpd_run_stop(conn);
	mpd_run_clear(conn);
	mpd_run_update(conn, playlist->path);

	//	while (mpd_run_idle_mask(conn, MPD_IDLE_UPDATE) != 0) {
//		xlog("sleep");
//		sleep(1);
//	}

// bug? mpd_run_idle_mask hangs? - workaround
	int count = 11;
	while (count-- > 0) {
		sleep(1);
		xlog("wait %d", count);
	}
	xlog("[0] updated %s", playlist->path);
	mpd_run_add(conn, playlist->path);
	mpd_run_play_pos(conn, 0);
	xlog("loaded playlist %s", playlist->name);
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
	// apply replaygain to DAC from file id3tag data
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
			if (starts_with(playlist->path, path, strlen(path))) {
				plist_key = playlist->key;
				playlist->pos = pos;
			}
		}
	}

	const char *artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
	const char *title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
	const char *album = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);

	// update status
	mcp->plist_key = plist_key;
	mcp->plist_pos = pos;
	int valid = artist != NULL && title != NULL;
	if (valid) {
		xlog("[%d:%d] %s - %s", plist_key, pos, artist, title);
		strcpy(mcp->artist, artist);
		strcpy(mcp->title, title);
		if (album != NULL) {
			strcpy(mcp->album, album);
		}
		strcpy(mcp->extension, get_filename_ext(path));
		upper(mcp->extension);
	} else {
		xlog("[%d:%d] %s", plist_key, pos, path);
	}
}

void mpdclient_handle(int key) {
	struct plist *playlist;

	// check connection
	// assert(conn != NULL);
	struct mpd_status *status = mpd_run_status(conn);
	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
		xlog("status: %s", mpd_connection_get_error_message(conn));
		if (!mpd_connection_clear_error(conn)) {
			mpd_connection_free(conn);
			conn = mpd_connection_new(MPD_HOST, MPD_PORT, 0);
			if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
				xlog("connect: %s", mpd_connection_get_error_message(conn));
				return;
			}
			// xlog("reconnected");
		}
	}
	if (status != NULL) {
		mpd_status_free(status);
	}
	// xlog("connection ok");

	if (key == KEY_0) {
		load_incoming();
		current_song = -1;
		sleep(1);
		struct mpd_song *song = mpd_run_current_song(conn);
		if (song) {
			process_song(song, 0);
		}
		return;
	}

	if (key >= KEY_1 && key <= KEY_9) {
		playlist = &playlists[key - 1];
		load_playlist(playlist);
		playlist_mode = 1;
		current_song = -1;
		sleep(1);
		struct mpd_song *song = mpd_run_current_song(conn);
		if (song) {
			process_song(song, playlist->pos);
		}
		return;
	}

	switch (key) {
	case KEY_STOP:
		dac_mute();
		mpd_run_stop(conn);
		break;
	case KEY_PAUSE:
		toggle_pause();
		break;
	case KEY_PLAY:
		mpd_run_play(conn);
		dac_unmute();
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
	case KEY_EJECTCD:
		playlist_mode = 0;
		external("RANDOM");
		break;
	case KEY_SELECT:
		external("SELECT");
		break;
	case KEY_F1:
		external("F1");
		break;
	case KEY_F2:
		external("F2");
		break;
	case KEY_F3:
		external("F3");
		break;
	case KEY_F4:
		external("F4");
		break;
	}
}

static void* mpdclient(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	struct mpd_connection *conn_status = NULL;
	while (1) {
		msleep(500);

		if (!conn_status) {
			conn_status = mpdclient_get_connection();
			if (!conn_status) {
				xlog("!conn_status");
				return (void*) 0;
			}
		}

		enum mpd_idle idle = mpd_run_idle(conn_status);
		if (!idle) {
			xlog("!idle");
			conn_status = NULL;
			continue;
		}

		if (idle == MPD_IDLE_PLAYER || idle == MPD_IDLE_QUEUE) {
			struct mpd_status *status = mpd_run_status(conn_status);
			if (!status) {
				xlog("!status");
				conn_status = NULL;
				continue;
			}

			mcp->mpd_state = mpd_status_get_state(status);
			switch (mcp->mpd_state) {
			case MPD_STATE_PAUSE:
				xlog("MPD State PAUSE");
				break;
			case MPD_STATE_STOP:
				xlog("MPD State STOP");
				break;
			case MPD_STATE_PLAY:
				xlog("MPD State PLAY");
				break;
			default:
				xlog("MPD State UNKNOWN");
			}

			const struct mpd_audio_format *audio_format = mpd_status_get_audio_format(status);
			if (audio_format) {
				mcp->mpd_bits = audio_format->bits;
				mcp->mpd_rate = audio_format->sample_rate;
			}

			struct mpd_song *song = mpd_run_current_song(conn_status);
			if (song) {
				unsigned int this_song = mpd_song_get_id(song);
				if (this_song != current_song) {
					int pos = mpd_status_get_song_pos(status);
					process_song(song, pos);
					current_song = this_song;
				}
				mpd_song_free(song);
			}
		}
		mcp->dac_state_changed = 1;
	}

	mpd_connection_free(conn_status);
	return (void*) 0;
}

static int init() {

	// get connection for sending events
	conn = mpdclient_get_connection();
	if (!conn)
		return -1;

	// listen for mpd state changes
	if (pthread_create(&thread_mpdclient, NULL, &mpdclient, NULL))
		return xerr("Error creating thread_mpdclient");

	xlog("MPDCLIENT initialized");
	return 0;
}

static void stop() {
	if (pthread_cancel(thread_mpdclient))
		xlog("Error canceling thread_mpdclient");

	if (pthread_join(thread_mpdclient, NULL))
		xlog("Error joining thread_mpdclient");

	if (conn)
		mpd_connection_free(conn);
}

MCP_REGISTER(mpdclient, 4, &init, &stop);
