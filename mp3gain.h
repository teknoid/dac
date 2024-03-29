#define MMSYSERR_NOERROR  0
#define MMSYSERR_ERROR    01
#define WAVERR_BADFORMAT  32

#define MP3GAIN_NOERROR					 MMSYSERR_NOERROR
#define MP3GAIN_UNSPECIFED_ERROR         MMSYSERR_ERROR
#define MP3GAIN_FILEFORMAT_NOTSUPPORTED  WAVERR_BADFORMAT
#define MP3GAIN_CANCELLED                2006

#define _stricmp strcasecmp

struct MP3GainTagInfo {
	int haveTrackGain;
	int haveTrackPeak;
	int haveAlbumGain;
	int haveAlbumPeak;
	int haveUndo;
	int haveMinMaxGain;
	int haveAlbumMinMaxGain;
	double trackGain;
	double trackPeak;
	double albumGain;
	double albumPeak;
	int undoLeft;
	int undoRight;
	int undoWrap;
	/* undoLeft and undoRight will be the same 95% of the time.
	 mp3gain DOES have a command-line switch to adjust the gain on just
	 one channel, though.
	 The "undoWrap" field indicates whether or not "wrapping" was turned on
	 when the mp3 was adjusted
	 */
	unsigned char minGain;
	unsigned char maxGain;
	unsigned char albumMinGain;
	unsigned char albumMaxGain;
	/* minGain and maxGain are the current minimum and maximum values of
	 the "global gain" fields in the frames of the mp3 file
	 */
	int dirty; /* flag if data changes after loaded from file */
	int recalc; /* Used to signal if recalculation is required */
};

struct APEFieldStruct {
	unsigned long vsize;
	unsigned long isize;
	unsigned long flags;
	char *name;
	char *value;
};

struct APETagFooterStruct {
	char ID[8];
	char Version[4];
	char Length[4];
	char TagCount[4];
	char Flags[4];
	char Reserved[8];
};

struct APETagStruct {
	unsigned long originalTagSize;
	int haveHeader;
	struct APETagFooterStruct header;
	struct APETagFooterStruct footer;
	unsigned char *otherFields; /* i.e. other than MP3Gain */
	unsigned long otherFieldsSize;
};

struct FileTagsStruct {
	long tagOffset;
	struct APETagStruct *apeTag;
	unsigned char *lyrics3tag;
	unsigned long lyrics3TagSize;
	unsigned char *id31tag;
};

typedef unsigned int MMRESULT;

int ReadMP3GainID3Tag(char *filename, struct MP3GainTagInfo *info);

int ReadMP3GainAPETag(char *filename, struct MP3GainTagInfo *info, struct FileTagsStruct *fileTags);
