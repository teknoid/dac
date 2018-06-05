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

int ReadMP3GainID3Tag(char *filename, struct MP3GainTagInfo *info);
