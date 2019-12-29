/* MP3 TAGS STUFF -- put together using public specs.
 * Copyright (C) 2018-2019 O. Sezer <sezero@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "quakedef.h"

#if defined(USE_CODEC_MP3)
#include "snd_codec.h"
#include "q_ctype.h"

static inline qboolean is_id3v1(const unsigned char *data, long length) {
    /* http://id3.org/ID3v1 :  3 bytes "TAG" identifier and 125 bytes tag data */
    if (length < 128 || memcmp(data,"TAG",3) != 0) {
        return false;
    }
    return true;
}
static qboolean is_id3v2(const unsigned char *data, size_t length) {
    /* ID3v2 header is 10 bytes:  http://id3.org/id3v2.4.0-structure */
    /* bytes 0-2: "ID3" identifier */
    if (length < 10 || memcmp(data,"ID3",3) != 0) {
        return false;
    }
    /* bytes 3-4: version num (major,revision), each byte always less than 0xff. */
    if (data[3] == 0xff || data[4] == 0xff) {
        return false;
    }
    /* bytes 6-9 are the ID3v2 tag size: a 32 bit 'synchsafe' integer, i.e. the
     * highest bit 7 in each byte zeroed.  i.e.: 7 bit information in each byte ->
     * effectively a 28 bit value.  */
    if (data[6] >= 0x80 || data[7] >= 0x80 || data[8] >= 0x80 || data[9] >= 0x80) {
        return false;
    }
    return true;
}
static long get_id3v2_len(const unsigned char *data, long length) {
    /* size is a 'synchsafe' integer (see above) */
    long size = (long)((data[6]<<21) + (data[7]<<14) + (data[8]<<7) + data[9]);
    size += 10; /* header size */
    /* ID3v2 header[5] is flags (bits 4-7 only, 0-3 are zero).
     * bit 4 set: footer is present (a copy of the header but
     * with "3DI" as ident.)  */
    if (data[5] & 0x10) {
        size += 10; /* footer size */
    }
    /* optional padding (always zeroes) */
    while (size < length && data[size] == 0) {
        ++size;
    }
    return size;
}
static qboolean is_apetag(const unsigned char *data, size_t length) {
    /* http://wiki.hydrogenaud.io/index.php?title=APEv2_specification
     * Header/footer is 32 bytes: bytes 0-7 ident, bytes 8-11 version,
     * bytes 12-17 size. bytes 24-31 are reserved: must be all zeroes. */
    unsigned int v;

    if (length < 32 || memcmp(data,"APETAGEX",8) != 0) {
        return false;
    }
    v = (unsigned)((data[11]<<24) | (data[10]<<16) | (data[9]<<8) | data[8]); /* version */
    if (v != 2000U && v != 1000U) {
        return false;
    }
    v = 0; /* reserved bits : */
    if (memcmp(&data[24],&v,4) != 0 || memcmp(&data[28],&v,4) != 0) {
        return false;
    }
    return true;
}
static long get_ape_len(const unsigned char *data) {
    unsigned int flags, version;
    long size = (long)((data[15]<<24) | (data[14]<<16) | (data[13]<<8) | data[12]);
    version = (unsigned)((data[11]<<24) | (data[10]<<16) | (data[9]<<8) | data[8]);
    flags = (unsigned)((data[23]<<24) | (data[22]<<16) | (data[21]<<8) | data[20]);
    if (version == 2000U && (flags & (1U<<31))) size += 32; /* header present. */
    return size;
}
static inline int is_lyrics3tag(const unsigned char *data, long length) {
    /* http://id3.org/Lyrics3
     * http://id3.org/Lyrics3v2 */
    if (length < 15) return 0;
    if (memcmp(data+6,"LYRICS200",9) == 0) return 2; /* v2 */
    if (memcmp(data+6,"LYRICSEND",9) == 0) return 1; /* v1 */
    return 0;
}
static long get_lyrics3v1_len(snd_stream_t *stream) {
    const char *p; long i, len;
    char buf[5104];
    /* needs manual search:  http://id3.org/Lyrics3 */
    if (stream->fh.length < 20) return -1;
    len = (stream->fh.length > 5109)? 5109 : stream->fh.length;
    FS_fseek(&stream->fh, -len, SEEK_END);
    FS_fread(buf, 1, (len -= 9), &stream->fh); /* exclude footer */
    /* strstr() won't work here. */
    for (i = len - 11, p = buf; i >= 0; --i, ++p) {
        if (memcmp(p, "LYRICSBEGIN", 11) == 0)
            break;
    }
    if (i < 0) return -1;
    return len - (long)(p - buf) + 9 /* footer */;
}
static inline long get_lyrics3v2_len(const unsigned char *data, long length) {
    /* 6 bytes before the end marker is size in decimal format -
     * does not include the 9 bytes end marker and size field. */
    if (length != 6) return 0;
    return strtol((const char *)data, NULL, 10) + 15;
}
static inline qboolean verify_lyrics3v2(const unsigned char *data, long length) {
    if (length < 11) return false;
    if (memcmp(data,"LYRICSBEGIN",11) == 0) return true;
    return false;
}
#define MMTAG_PARANOID
static qboolean is_musicmatch(const unsigned char *data, long length) {
  /* From docs/musicmatch.txt in id3lib: https://sourceforge.net/projects/id3lib/
     Overall tag structure:

      +-----------------------------+
      |           Header            |
      |    (256 bytes, OPTIONAL)    |
      +-----------------------------+
      |  Image extension (4 bytes)  |
      +-----------------------------+
      |        Image binary         |
      |  (var. length >= 4 bytes)   |
      +-----------------------------+
      |      Unused (4 bytes)       |
      +-----------------------------+
      |  Version info (256 bytes)   |
      +-----------------------------+
      |       Audio meta-data       |
      | (var. length >= 7868 bytes) |
      +-----------------------------+
      |   Data offsets (20 bytes)   |
      +-----------------------------+
      |      Footer (48 bytes)      |
      +-----------------------------+
     */
    if (length < 48) return false;
    /* sig: 19 bytes company name + 13 bytes space */
    if (memcmp(data,"Brava Software Inc.             ",32) != 0) {
        return false;
    }
    /* 4 bytes version: x.xx */
    if (!q_isdigit(data[32]) || data[33] != '.' ||
        !q_isdigit(data[34]) ||!q_isdigit(data[35])) {
        return false;
    }
    #ifdef MMTAG_PARANOID
    /* [36..47]: 12 bytes trailing space */
    for (length = 36; length < 48; ++length) {
        if (data[length] != ' ') return false;
    }
    #endif
    return true;
}
static long get_musicmatch_len(snd_stream_t *stream) {
    const int metasizes[4] = { 7868, 7936, 8004, 8132 };
    const unsigned char syncstr[10] = {'1','8','2','7','3','6','4','5',0,0};
    unsigned char buf[256];
    int i, j, imgext_ofs, version_ofs;
    long len;

    FS_fseek(&stream->fh, -68, SEEK_END);
    FS_fread(buf, 1, 20, &stream->fh);
    imgext_ofs  = (int)((buf[3] <<24) | (buf[2] <<16) | (buf[1] <<8) | buf[0] );
    version_ofs = (int)((buf[15]<<24) | (buf[14]<<16) | (buf[13]<<8) | buf[12]);
    if (version_ofs <= imgext_ofs) return -1;
    if (version_ofs <= 0 || imgext_ofs <= 0) return -1;
    /* Try finding the version info section:
     * Because metadata section comes after it, and because metadata section
     * has different sizes across versions (format ver. <= 3.00: always 7868
     * bytes), we can _not_ directly calculate using deltas from the offsets
     * section. */
    for (i = 0; i < 4; ++i) {
    /* 48: footer, 20: offsets, 256: version info */
        len = metasizes[i] + 48 + 20 + 256;
        if (stream->fh.length < len) return -1;
        FS_fseek(&stream->fh, -len, SEEK_END);
        FS_fread(buf, 1, 256, &stream->fh);
        /* [0..9]: sync string, [30..255]: 0x20 */
        #ifdef MMTAG_PARANOID
        for (j = 30; j < 256; ++j) {
            if (buf[j] != ' ') break;
        }
        if (j < 256) continue;
        #endif
        if (memcmp(buf, syncstr, 10) == 0) {
            break;
        }
    }
    if (i == 4) return -1; /* no luck. */
    #ifdef MMTAG_PARANOID
    /* unused section: (4 bytes of 0x00) */
    FS_fseek(&stream->fh, -(len + 4), SEEK_END);
    FS_fread(buf, 1, 4, &stream->fh); j = 0;
    if (memcmp(buf, &j, 4) != 0) return -1;
    #endif
    len += (version_ofs - imgext_ofs);
    if (stream->fh.length < len) return -1;
    FS_fseek(&stream->fh, -len, SEEK_END);
    FS_fread(buf, 1, 8, &stream->fh);
    j = (int)((buf[7] <<24) | (buf[6] <<16) | (buf[5] <<8) | buf[4]);
    if (j < 0) return -1;
    /* verify image size: */
    /* without this, we may land at a wrong place. */
    if (j + 12 != version_ofs - imgext_ofs) return -1;
    /* try finding the optional header */
    if (stream->fh.length < len + 256) return len;
    FS_fseek(&stream->fh, -(len + 256), SEEK_END);
    FS_fread(buf, 1, 256, &stream->fh);
    /* [0..9]: sync string, [30..255]: 0x20 */
    if (memcmp(buf, syncstr, 10) != 0) {
        return len;
    }
    #ifdef MMTAG_PARANOID
    for (j = 30; j < 256; ++j) {
        if (buf[j] != ' ') return len;
    }
    #endif
    return len + 256; /* header is present. */
}

static int probe_id3v1(snd_stream_t *stream, unsigned char *buf, int atend) {
    if (stream->fh.length >= 128) {
        FS_fseek(&stream->fh, -128, SEEK_END);
        if (FS_fread(buf, 1, 128, &stream->fh) != 128)
            return -1;
        if (is_id3v1(buf, 128)) {
            if (!atend) { /* possible false positive? */
                if (is_musicmatch(buf + 128 - 48, 48) ||
                    is_apetag    (buf + 128 - 32, 32) ||
                    is_lyrics3tag(buf + 128 - 15, 15)) {
                    return 0;
                }
            }
            stream->fh.length -= 128;
            Con_DPrintf("MP3: skipped %ld bytes ID3v1 tag\n", 128L);
            return 1;
            /* FIXME: handle possible double-ID3v1 tags? */
        }
    }
    return 0;
}
static int probe_mmtag(snd_stream_t *stream, unsigned char *buf) {
    long len;
    if (stream->fh.length >= 68) {
        FS_fseek(&stream->fh, -48, SEEK_END);
        if (FS_fread(buf, 1, 48, &stream->fh) != 48)
            return -1;
        if (is_musicmatch(buf, 48)) {
            len = get_musicmatch_len(stream);
            if (len < 0) return -1;
            if (len >= stream->fh.length) return -1;
            stream->fh.length -= len;
            Con_DPrintf("MP3: skipped %ld bytes MusicMatch tag\n", len);
            return 1;
        }
    }
    return 0;
}
static int probe_apetag(snd_stream_t *stream, unsigned char *buf) {
    long len;
    if (stream->fh.length >= 32) {
        FS_fseek(&stream->fh, -32, SEEK_END);
        if (FS_fread(buf, 1, 32, &stream->fh) != 32)
            return -1;
        if (is_apetag(buf, 32)) {
            len = get_ape_len(buf);
            if (len >= stream->fh.length) return -1;
            stream->fh.length -= len;
            Con_DPrintf("MP3: skipped %ld bytes APE tag\n", len);
            return 1;
        }
    }
    return 0;
}
static int probe_lyrics3(snd_stream_t *stream, unsigned char *buf) {
    long len;
    if (stream->fh.length >= 15) {
        FS_fseek(&stream->fh, -15, SEEK_END);
        if (FS_fread(buf, 1, 15, &stream->fh) != 15)
            return -1;
        len = is_lyrics3tag(buf, 15);
        if (len == 2) {
            len = get_lyrics3v2_len(buf, 6);
            if (len >= stream->fh.length) return -1;
            if (len < 15) return -1;
            FS_fseek(&stream->fh, -len, SEEK_END);
            if (FS_fread(buf, 1, 11, &stream->fh) != 11)
                return -1;
            if (!verify_lyrics3v2(buf, 11)) return -1;
            stream->fh.length -= len;
            Con_DPrintf("MP3: skipped %ld bytes Lyrics3 tag\n", len);
            return 1;
        }
        else if (len == 1) {
            len = get_lyrics3v1_len(stream);
            if (len < 0) return -1;
            stream->fh.length -= len;
            Con_DPrintf("MP3: skipped %ld bytes Lyrics3 tag\n", len);
            return 1;
        }
    }
    return 0;
}

int mp3_skiptags(snd_stream_t *stream)
{
    unsigned char buf[128];
    long len; size_t readsize;
    int c_id3, c_ape, c_lyr, c_mm;
    int rc = -1;
    /* failsafe */
    long oldlength = stream->fh.length;
    long oldstart = stream->fh.start;

    /* MP3 standard has no metadata format, so everyone invented
     * their own thing, even with extensions, until ID3v2 became
     * dominant: Hence the impossible mess here.
     *
     * Note: I don't yet care about freaky broken mp3 files with
     * double tags. -- O.S.
     */

    readsize = FS_fread(buf, 1, 128, &stream->fh);
    if (!readsize || FS_ferror(&stream->fh)) goto fail;

    /* ID3v2 tag is at the start */
    if (is_id3v2(buf, readsize)) {
        len = get_id3v2_len(buf, (long)readsize);
        if (len >= stream->fh.length) goto fail;
        stream->fh.start += len;
        stream->fh.length -= len;
        Con_DPrintf("MP3: skipped %ld bytes ID3v2 tag\n", len);
    }
    /* APE tag _might_ be at the start (discouraged
     * but not forbidden, either.)  read the header. */
    else if (is_apetag(buf, readsize)) {
        len = get_ape_len(buf);
        if (len >= stream->fh.length) goto fail;
        stream->fh.start += len;
        stream->fh.length -= len;
        Con_DPrintf("MP3: skipped %ld bytes APE tag\n", len);
    }

    /* it's not impossible that _old_ MusicMatch tag
     * placing itself after ID3v1. */
    if ((c_mm = probe_mmtag(stream, buf)) < 0) {
        goto fail;
    }
    /* ID3v1 tag is at the end */
    if ((c_id3 = probe_id3v1(stream, buf, !c_mm)) < 0) {
        goto fail;
    }
    /* we do not know the order of ape or lyrics3
     * or musicmatch tags, hence the loop here.. */
    c_ape = 0;
    c_lyr = 0;
    for (;;) {
        if (!c_lyr) {
        /* care about mp3s with double Lyrics3 tags? */
            if ((c_lyr = probe_lyrics3(stream, buf)) < 0)
                goto fail;
            if (c_lyr) continue;
        }
        if (!c_mm) {
            if ((c_mm = probe_mmtag(stream, buf)) < 0)
                goto fail;
            if (c_mm) continue;
        }
        if (!c_ape) {
            if ((c_ape = probe_apetag(stream, buf)) < 0)
                goto fail;
            if (c_ape) continue;
        }
        break;
    } /* for (;;) */

    rc = (stream->fh.length > 0)? 0 : -1;
    fail:
    if (rc < 0) {
        stream->fh.start = oldstart;
        stream->fh.length = oldlength;
    }
    FS_rewind(&stream->fh);
    return rc;
}
#endif /* USE_CODEC_MP3 */
