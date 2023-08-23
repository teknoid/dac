#define MPD_HOST		"localhost"
#define MPD_PORT		6600

#ifndef MUSIC
#define MUSIC			"/var/lib/mpd/music/"
#endif

typedef struct plist {
	int key;
	int pos;
	char *name;
	char *path;
} plist;

void mpdclient_handle(int);
