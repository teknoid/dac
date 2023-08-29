/**
 *
 * extract from mp3gain's apetag.c
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <memory.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "mp3gain.h"

int ReadMP3ID3v1Tag(FILE *fi, unsigned char **tagbuff, long *tag_offset) {
    char tmp[128];

	if ( *tag_offset < 128 ) return 0;
	if ( fseek(fi, *tag_offset - 128,SEEK_SET) ) return 0;
	if ( fread(tmp, 1, 128, fi) != 128 ) return 0;
    if ( memcmp (tmp, "TAG", 3) ) return 0;

    //we have tag, so store it in buffer
	if (*tagbuff)
		free(*tagbuff);
	*tagbuff = (unsigned char *)malloc(128);
    memcpy(*tagbuff,tmp,128);
    *tag_offset -= 128;

    return 1;
}


/*
static int Lyrics3GetNumber5 ( const unsigned char* string )
{
	return ( string[0] - '0') * 10000 +
		   ( string[1] - '0') * 1000 +
		   ( string[2] - '0') * 100 +
		   ( string[3] - '0') * 10 +
		   ( string[4] - '0') * 1;
}
*/


static int Lyrics3GetNumber6 ( const unsigned char* string )
{
	if (string[0] < '0' || string[0] > '9') return 0;
	if (string[1] < '0' || string[1] > '9') return 0;
	if (string[2] < '0' || string[2] > '9') return 0;
	if (string[3] < '0' || string[3] > '9') return 0;
	if (string[4] < '0' || string[4] > '9') return 0;
	if (string[5] < '0' || string[5] > '9') return 0;
	return ( string[0] - '0') * 100000 +
		   ( string[1] - '0') * 10000 +
		   ( string[2] - '0') * 1000 +
		   ( string[3] - '0') * 100 +
		   ( string[4] - '0') * 10 +
		   ( string[5] - '0') * 1;
}

struct Lyrics3TagFooterStruct {
    unsigned char   Length  [6];
    unsigned char   ID      [9];
};

struct Lyrics3TagField {
	unsigned char   ID      [3];
	unsigned char   Length  [5];
};

// Reads Lyrics3 v2.0 tag
static int ReadMP3Lyrics3v2Tag ( FILE *fp, unsigned char **tagbuff, unsigned long *tagSize, unsigned char **id3tagbuff, long *tag_offset )
{
	int                                 len;
	struct Lyrics3TagFooterStruct       T;
    char                                tmpid3[128];
    char                                tmp[11];
    long                                taglen;

	if ( *tag_offset < 128 ) return 0;
    if ( fseek (fp, *tag_offset - 128, SEEK_SET) ) return 0;
    if ( fread (tmpid3, 1, 128, fp) != 128 ) return 0;
    // check for id3-tag
    if ( memcmp (tmpid3, "TAG", 3) ) return 0;
    //if we have id3-tag, put it in the id3tagbuff
	if (*id3tagbuff)
		free(*id3tagbuff);
	*id3tagbuff = (unsigned char *)malloc(128);
    memcpy(*id3tagbuff,tmpid3,128);
	if ( *tag_offset < (128 + (long)(sizeof(T))) ) return 0;
    if ( fseek (fp, *tag_offset - 128 - sizeof (T), SEEK_SET) ) return 0;
    if ( fread (&T, 1, sizeof (T), fp) != sizeof (T) ) return 0;
    // check for lyrics3 v2.00 tag
    if ( memcmp (T.ID, "LYRICS200", sizeof (T.ID)) ) return 0;
	len = Lyrics3GetNumber6 (T.Length);
	if (*tag_offset < (128 + (long)(sizeof(T)) + len)) return 0;
	if ( fseek (fp, *tag_offset - 128 - (long)sizeof (T) - len, SEEK_SET) ) return 0;
    if ( fread  (tmp, 1, 11, fp) != 11 ) return 0;
    if ( memcmp (tmp, "LYRICSBEGIN", 11) ) return 0;

    taglen = 128 + Lyrics3GetNumber6(T.Length) + sizeof(T);

    *tag_offset -= taglen;
    if (*tagbuff != NULL) {
        free(*tagbuff);
    }
    *tagbuff = (unsigned char *)malloc(taglen);
    fseek(fp,*tag_offset,SEEK_SET);
    fread(*tagbuff,1,taglen,fp);
	*tagSize = taglen;
    return 1;
}


static unsigned long Read_LE_Uint32_unsigned ( const unsigned char* p )
{
    return ((unsigned long)p[0] <<  0) |
           ((unsigned long)p[1] <<  8) |
           ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

static unsigned long Read_LE_Uint32 ( const char* p ) {return Read_LE_Uint32_unsigned((const unsigned char*)p);}


static void Write_LE_Uint32 ( char* p, const unsigned long value )
{
    p[0] = (unsigned char) (value >>  0);
    p[1] = (unsigned char) (value >>  8);
    p[2] = (unsigned char) (value >> 16);
    p[3] = (unsigned char) (value >> 24);
}

enum {
	MAX_FIELD_SIZE = 1024*1024 //treat bigger fields as errors
};

unsigned long strlen_max(const char * ptr, unsigned long max) {
	unsigned long n = 0;
	while (ptr[n] && n < max) n++;
	return n;
}

// Reads APE v1.0/2.0 tag
int ReadMP3APETag ( FILE *fp,  struct MP3GainTagInfo *info, struct APETagStruct **apeTag, long *tag_offset )
{
    unsigned long               vsize;
    unsigned long               isize;
    unsigned long               flags;
	unsigned long				remaining;
    char*                       buff;
    char*                       p;
    char*                       value;
    char*                       vp;
    char*                       end;
    struct APETagFooterStruct   T;
    unsigned long               TagLen;
    unsigned long               TagCount;
    unsigned long               origTagCount, otherFieldsCount;
    unsigned long               curFieldNum;
    unsigned long               Ver;
    char*                       name;
    int                         is_info;
	char						tmpString[10];

	if ( *tag_offset < (long)(sizeof(T)) ) return 0;
    if ( fseek(fp,*tag_offset - sizeof(T),SEEK_SET) ) return 0;
    if ( fread (&T, 1, sizeof(T), fp) != sizeof(T) ) return 0;
    if ( memcmp (T.ID, "APETAGEX", sizeof(T.ID)) ) return 0;
    Ver = Read_LE_Uint32 (T.Version);
    if ( (Ver != 1000) && (Ver != 2000) ) return 0;
    if ( (TagLen = Read_LE_Uint32 (T.Length)) < sizeof (T) ) return 0;
	if (*tag_offset < TagLen) return 0;
    if ( fseek (fp, *tag_offset - TagLen, SEEK_SET) ) return 0;
    buff = (char *)malloc (TagLen);
    if ( fread (buff, 1, TagLen - sizeof (T), fp) != (TagLen - sizeof (T)) ) {
        free (buff);
        return 0;
    }

    if (*apeTag) {
        if ((*apeTag)->otherFields)
            free((*apeTag)->otherFields);
		free(*apeTag);
    }
	*apeTag = (struct APETagStruct *)malloc(sizeof(struct APETagStruct));
	(*apeTag)->haveHeader = 0;
	(*apeTag)->otherFields = (unsigned char *)malloc(TagLen - sizeof(T));
    (*apeTag)->otherFieldsSize = 0;

	memcpy(&((*apeTag)->footer),&T,sizeof(T));

    origTagCount = TagCount = Read_LE_Uint32 (T.TagCount);
    otherFieldsCount = 0;


    end = buff + TagLen - sizeof (T);
	curFieldNum = 0;
    for ( p = buff; p < end && TagCount--; ) {
		if (end - p < 8) break;
        vsize = Read_LE_Uint32 (p); p += 4;
        flags = Read_LE_Uint32 (p); p += 4;

		remaining = (unsigned long) (end - p);
        isize = strlen_max (p, remaining);
		if (isize >= remaining || vsize > MAX_FIELD_SIZE || isize + 1 + vsize > remaining) break;

        name = (char*)malloc(isize+1);
		memcpy(name, p, isize);
		name[isize] = 0;
        value = (char*)malloc(vsize+1);
        memcpy(value, p+isize+1, vsize);
        value[vsize] = 0;

		is_info = 0;

		{
            if (!_stricmp (name, "REPLAYGAIN_TRACK_GAIN")) {
                info->haveTrackGain = !0;
                info->trackGain = atof(value);
            } else if (!_stricmp(name,"REPLAYGAIN_TRACK_PEAK")) {
                info->haveTrackPeak = !0;
                info->trackPeak = atof(value);
            } else if (!_stricmp(name,"REPLAYGAIN_ALBUM_GAIN")) {
                info->haveAlbumGain = !0;
                info->albumGain = atof(value);
            } else if (!_stricmp(name,"REPLAYGAIN_ALBUM_PEAK")) {
                info->haveAlbumPeak = !0;
                info->albumPeak = atof(value);
            } else if (!_stricmp(name,"MP3GAIN_UNDO")) {
				/* value should be something like "+003,+003,W" */
                info->haveUndo = !0;
                vp = value;
				memcpy(tmpString,vp,4);
				tmpString[4] = '\0';
                info->undoLeft = atoi(tmpString);
                vp = vp + 5; /* skip the comma, too */
				memcpy(tmpString,vp,4);
				tmpString[4] = '\0';
                info->undoRight = atoi(tmpString);
                vp = vp + 5; /* skip the comma, too */
                if ((*vp == 'w')||(*vp == 'W')) {
                    info->undoWrap = !0;
                } else {
                    info->undoWrap = 0;
                }
            } else if (!_stricmp(name,"MP3GAIN_MINMAX")) {
				/* value should be something like "001,153" */
                info->haveMinMaxGain = !0;
                vp = value;
				memcpy(tmpString,vp,3);
				tmpString[3] = '\0';
                info->minGain = atoi(tmpString);
                vp = vp + 4; /* skip the comma, too */
				memcpy(tmpString,vp,3);
				tmpString[3] = '\0';
                info->maxGain = atoi(tmpString);
            } else if (!_stricmp(name,"MP3GAIN_ALBUM_MINMAX")) {
				/* value should be something like "001,153" */
                info->haveAlbumMinMaxGain = !0;
                vp = value;
				memcpy(tmpString,vp,3);
				tmpString[3] = '\0';
                info->albumMinGain = atoi(tmpString);
                vp = vp + 4; /* skip the comma, too */
				memcpy(tmpString,vp,3);
				tmpString[3] = '\0';
                info->albumMaxGain = atoi(tmpString);
            } else {
                memcpy((*apeTag)->otherFields + (*apeTag)->otherFieldsSize, p - 8, 8 + isize + 1 + vsize);
                (*apeTag)->otherFieldsSize += 8 + isize + 1 + vsize;
                otherFieldsCount++;
            }
		}

        if ( isize > 0 && vsize > 0 ) {
            if (is_info) {
            } else {
            }
        }
        free(value);
		free(name);
        p += isize + 1 + vsize;
    }

    free (buff);

	*tag_offset -= TagLen;
	(*apeTag)->originalTagSize = TagLen;

    if ( Read_LE_Uint32 (T.Flags) & (1<<31) ) {  // Tag contains header
		if (*tag_offset < (long)(sizeof(T))) return 0;
        *tag_offset -= sizeof (T);

		fseek (fp, *tag_offset, SEEK_SET);
		fread (&((*apeTag)->header),1,sizeof(T),fp);
		(*apeTag)->haveHeader = !0;
		(*apeTag)->originalTagSize += sizeof(T);
	}

    if (otherFieldsCount != origTagCount) {
         Write_LE_Uint32((*apeTag)->footer.Length, sizeof(T) + (*apeTag)->otherFieldsSize);
         Write_LE_Uint32((*apeTag)->footer.TagCount, otherFieldsCount);
         if ((*apeTag)->haveHeader) {
             Write_LE_Uint32((*apeTag)->header.Length, sizeof(T) + (*apeTag)->otherFieldsSize);
             Write_LE_Uint32((*apeTag)->header.TagCount, otherFieldsCount);
         }
    }

    return 1;
}


/**
 * Read gain information from an APE tag.
 *
 * Look for an APE tag at the end of the MP3 file, and extract
 * gain information from it. Any ID3v1 or Lyrics3v2 tags at the end
 * of the file are read and stored, but not processed.
 */
int ReadMP3GainAPETag (char *filename, struct MP3GainTagInfo *info, struct FileTagsStruct *fileTags) {
    FILE *fi;
    long tag_offset, offs_bk, file_size;

    fi = fopen(filename, "rb");
    if (fi == NULL)
		return 0;

	fseek(fi, 0, SEEK_END);
    tag_offset = file_size = ftell(fi);

	fileTags->lyrics3TagSize = 0;

    do {
		offs_bk = tag_offset;
		ReadMP3APETag ( fi, info, &(fileTags->apeTag), &tag_offset );
        ReadMP3Lyrics3v2Tag ( fi, &(fileTags->lyrics3tag), &(fileTags->lyrics3TagSize), &(fileTags->id31tag), &tag_offset );
		ReadMP3ID3v1Tag ( fi, &(fileTags->id31tag), &tag_offset );
	} while ( offs_bk != tag_offset );

	if (tag_offset >= 0 && tag_offset <= file_size) {
		fileTags->tagOffset = tag_offset;
	} else { //Corrupt tag information, simply default to end-of-file
		fileTags->tagOffset = file_size;
	}

    fclose(fi);

    return 1;
};



int mp3gain_ape_main(int argc, char **argv) {
	struct MP3GainTagInfo *taginfo;
	taginfo = malloc(sizeof(struct MP3GainTagInfo));

	struct FileTagsStruct *filetags;
	filetags = malloc(sizeof(struct FileTagsStruct));

	ReadMP3GainAPETag(argv[1], taginfo, filetags);
	if (taginfo->haveAlbumGain)
		printf("Album Gain %f\n", taginfo->albumGain);

	if (taginfo->haveTrackGain)
		printf("Track Gain %f\n", taginfo->trackGain);

	return 0;
}

#ifdef MP3GAIN_MAIN
int main(int argc, char **argv) {
	return mp3gain_ape_main(argc, argv);
}
#endif
