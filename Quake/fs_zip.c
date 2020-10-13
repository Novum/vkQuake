#include "quakedef.h"

#ifdef USE_ZLIB
#include <zlib.h>
#else
#pragma message("zips supported but with no zlib")
#endif

//ZIP features:
//zip64 for huge zips, except that quakespasm's filesystem calls are all otherwise 31bit.
//utf-8 encoding support (non-utf-8 is always ibm437)
//compression mode: store
//compression mode: deflate (via zlib)
//bigendian cpus. everything misaligned.

//NOT supported:
//symlink flag (files are ignored completely)
//compression mode: deflate64
//other compression modes
//split archives
//if central dir is crypted/compressed, the archive will fail to open
//if a file is crypted/compressed, the file will (internally) be marked as corrupt
//crc verification
//infozip utf-8 name override.
//other 'extra' fields.

#define qofs_t long long
#define qofs_Make(low,high) ((unsigned int)(low) | ((qofs_t)(high)<<32))

struct zipinfo
{
	unsigned int	thisdisk;					//this disk number
	unsigned int	centraldir_startdisk;		//central directory starts on this disk
	qofs_t			centraldir_numfiles_disk;	//number of files in the centraldir on this disk
	qofs_t			centraldir_numfiles_all;	//number of files in the centraldir on all disks
	qofs_t			centraldir_size;			//total size of the centraldir
	qofs_t			centraldir_offset;			//start offset of the centraldir with respect to the first disk that contains it
	unsigned short	commentlength;				//length of the comment at the end of the archive.

	unsigned int	zip64_centraldirend_disk;	//zip64 weirdness
	qofs_t			zip64_centraldirend_offset;
	unsigned int	zip64_diskcount;
	qofs_t			zip64_eocdsize;
	unsigned short	zip64_version_madeby;
	unsigned short	zip64_version_needed;

	unsigned short	centraldir_compressionmethod;
	unsigned short	centraldir_algid;

	qofs_t			centraldir_end;
	qofs_t			zipoffset;
};
struct zipcentralentry
{
	unsigned char	*fname;
	qofs_t			cesize;
	unsigned int	flags;
	time_t			mtime;

	//PK12
	unsigned short	version_madeby;
	unsigned short	version_needed;
	unsigned short	gflags;
	unsigned short	cmethod;
	unsigned short	lastmodfiletime;
	unsigned short	lastmodfiledate;
	unsigned int	crc32;
	qofs_t			csize;
	qofs_t			usize;
	unsigned short	fnane_len;
	unsigned short	extra_len;
	unsigned short	comment_len;
	unsigned int	disknum;
	unsigned short	iattributes;
	unsigned int	eattributes;
	qofs_t	localheaderoffset;	//from start of disk
};

struct ziplocalentry
{
	//PK34
	unsigned short	version_needed;
	unsigned short	gpflags;
	unsigned short	cmethod;
	unsigned short	lastmodfiletime;
	unsigned short	lastmodfiledate;
	unsigned int	crc32;
	qofs_t			csize;
	qofs_t			usize;
	unsigned short	fname_len;
	unsigned short	extra_len;
};

#define LittleU2FromPtr(p) (unsigned int)((p)[0] | ((p)[1]<<8u))
#define LittleU4FromPtr(p) (unsigned int)((p)[0] | ((p)[1]<<8u) | ((p)[2]<<16u) | ((p)[3]<<24u))
#define LittleU8FromPtr(p) qofs_Make(LittleU4FromPtr(p), LittleU4FromPtr((p)+4))

#define SIZE_LOCALENTRY 30
#define SIZE_ENDOFCENTRALDIRECTORY 22
#define SIZE_ZIP64ENDOFCENTRALDIRECTORY_V1 56
#define SIZE_ZIP64ENDOFCENTRALDIRECTORY_V2 84
#define SIZE_ZIP64ENDOFCENTRALDIRECTORY SIZE_ZIP64ENDOFCENTRALDIRECTORY_V2
#define SIZE_ZIP64ENDOFCENTRALDIRECTORYLOCATOR 20


typedef struct
{
	char			name[MAX_QPATH];
	qofs_t			localpos;
	qofs_t			filelen;
	unsigned int	crc;
	unsigned int	flags;
//	time_t			time;
} zpackfile_t;

typedef struct
{
	int raw;
	qofs_t rawsize;

	size_t numfiles;
	zpackfile_t *files;
} zipfile_t;
#define ZFL_DEFLATED	1	//need to use zlib
#define ZFL_STORED		2	//direct access is okay
#define ZFL_SYMLINK		4	//file is a symlink
#define ZFL_CORRUPT		8	//file is corrupt or otherwise unreadable.
#define ZFL_WEAKENCRYPT	16	//traditional zip encryption


//for zip->quake charsets
unsigned short ibmtounicode[256] =
{
	0x0000,0x263A,0x263B,0x2665,0x2666,0x2663,0x2660,0x2022,0x25D8,0x25CB,0x25D9,0x2642,0x2640,0x266A,0x266B,0x263C,	//0x00(non-ascii, display-only)
	0x25BA,0x25C4,0x2195,0x203C,0x00B6,0x00A7,0x25AC,0x21A8,0x2191,0x2193,0x2192,0x2190,0x221F,0x2194,0x25B2,0x25BC,	//0x10(non-ascii, display-only)
	0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027,0x0028,0x0029,0x002A,0x002B,0x002C,0x002D,0x002E,0x002F,	//0x20(ascii)
	0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037,0x0038,0x0039,0x003A,0x003B,0x003C,0x003D,0x003E,0x003F,	//0x30(ascii)
	0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047,0x0048,0x0049,0x004A,0x004B,0x004C,0x004D,0x004E,0x004F,	//0x40(ascii)
	0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057,0x0058,0x0059,0x005A,0x005B,0x005C,0x005D,0x005E,0x005F,	//0x50(ascii)
	0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067,0x0068,0x0069,0x006A,0x006B,0x006C,0x006D,0x006E,0x006F,	//0x60(ascii)
	0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077,0x0078,0x0079,0x007A,0x007B,0x007C,0x007D,0x007E,0x2302,	//0x70(mostly ascii, one display-only)
	0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,	//0x80(non-ascii, printable)
	0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,	//0x90(non-ascii, printable)
	0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,	//0xa0(non-ascii, printable)
	0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,	//0xb0(box-drawing, printable)
	0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,	//0xc0(box-drawing, printable)
	0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,	//0xd0(box-drawing, printable)
	0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,	//0xe0(maths(greek), printable)
	0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0,	//0xf0(maths, printable)
};


static qboolean FSZIP_FindEndCentralDirectory(zipfile_t *zip, struct zipinfo *info)
{
	qboolean result = false;
	//zip comment is capped to 65k or so, so we can use a single buffer for this
	byte traildata[0x10000 + SIZE_ENDOFCENTRALDIRECTORY+SIZE_ZIP64ENDOFCENTRALDIRECTORYLOCATOR];
	byte *magic;
	unsigned int trailsize = 0x10000 + SIZE_ENDOFCENTRALDIRECTORY+SIZE_ZIP64ENDOFCENTRALDIRECTORYLOCATOR;
	if ((qofs_t)trailsize > zip->rawsize)
		trailsize = zip->rawsize;
	//FIXME: do in a loop to avoid a huge block of stack use
	Sys_FileSeek(zip->raw, zip->rawsize - trailsize);
	Sys_FileRead(zip->raw, traildata, trailsize);

	memset(info, 0, sizeof(*info));

	for (magic = traildata+trailsize-SIZE_ENDOFCENTRALDIRECTORY; magic >= traildata; magic--)
	{
		if (magic[0] == 'P' && 
			magic[1] == 'K' && 
			magic[2] == 5 && 
			magic[3] == 6)
		{
			info->centraldir_end = (zip->rawsize-trailsize)+(magic-traildata);

			info->thisdisk					= LittleU2FromPtr(magic+4);
			info->centraldir_startdisk		= LittleU2FromPtr(magic+6);
			info->centraldir_numfiles_disk	= LittleU2FromPtr(magic+8);
			info->centraldir_numfiles_all	= LittleU2FromPtr(magic+10);
			info->centraldir_size			= LittleU4FromPtr(magic+12);
			info->centraldir_offset			= LittleU4FromPtr(magic+16);
			info->commentlength				= LittleU2FromPtr(magic+20);

			result = true;
			break;
		}
	}

	if (!result)
		Con_Printf("zip: unable to find end-of-central-directory\n");
	else

	//now look for a zip64 header.
	//this gives more larger archives, more files, larger files, more spanned disks.
	//note that the central directory itself is the same, it just has a couple of extra attributes on files that need them.
	for (magic -= SIZE_ZIP64ENDOFCENTRALDIRECTORYLOCATOR; magic >= traildata; magic--)
	{
		if (magic[0] == 'P' && 
			magic[1] == 'K' && 
			magic[2] == 6 && 
			magic[3] == 7)
		{
			byte z64eocd[SIZE_ZIP64ENDOFCENTRALDIRECTORY];

			info->zip64_centraldirend_disk		= LittleU4FromPtr(magic+4);
			info->zip64_centraldirend_offset	= LittleU8FromPtr(magic+8);
			info->zip64_diskcount				= LittleU4FromPtr(magic+16);

			if (info->zip64_diskcount != 1 || info->zip64_centraldirend_disk != 0)
			{
				Con_Printf("zip: archive is spanned\n");
				return false;
			}

			Sys_FileSeek(zip->raw, info->zip64_centraldirend_offset);
			Sys_FileRead(zip->raw, z64eocd, sizeof(z64eocd));

			if (z64eocd[0] == 'P' &&
				z64eocd[1] == 'K' &&
				z64eocd[2] == 6 &&
				z64eocd[3] == 6)
			{
				info->zip64_eocdsize						= LittleU8FromPtr(z64eocd+4) + 12;
				info->zip64_version_madeby					= LittleU2FromPtr(z64eocd+12);
				info->zip64_version_needed					= LittleU2FromPtr(z64eocd+14);
				info->thisdisk								= LittleU4FromPtr(z64eocd+16);
				info->centraldir_startdisk					= LittleU4FromPtr(z64eocd+20);
				info->centraldir_numfiles_disk				= LittleU8FromPtr(z64eocd+24);
				info->centraldir_numfiles_all				= LittleU8FromPtr(z64eocd+32);
				info->centraldir_size						= LittleU8FromPtr(z64eocd+40);
				info->centraldir_offset						= LittleU8FromPtr(z64eocd+48);

				if (info->zip64_eocdsize >= 84)
				{
					info->centraldir_compressionmethod		= LittleU2FromPtr(z64eocd+56);
//					info->zip64_2_centraldir_csize			= LittleU8FromPtr(z64eocd+58);
//					info->zip64_2_centraldir_usize			= LittleU8FromPtr(z64eocd+66);
					info->centraldir_algid					= LittleU2FromPtr(z64eocd+74);
//					info.zip64_2_bitlen						= LittleU2FromPtr(z64eocd+76);
//					info->zip64_2_flags						= LittleU2FromPtr(z64eocd+78);
//					info->zip64_2_hashid					= LittleU2FromPtr(z64eocd+80);
//					info->zip64_2_hashlength				= LittleU2FromPtr(z64eocd+82);
					//info->zip64_2_hashdata				= LittleUXFromPtr(z64eocd+84, info->zip64_2_hashlength);
				}
			}
			else
			{
				Con_Printf("zip: zip64 end-of-central directory at unknown offset.\n");
				result = false;
			}

			break;
		}
	}

	if (info->thisdisk || info->centraldir_startdisk || info->centraldir_numfiles_disk != info->centraldir_numfiles_all)
	{
		Con_Printf("zip: archive is spanned\n");
		result = false;
	}
	if (info->centraldir_compressionmethod || info->centraldir_algid)
	{
		Con_Printf("zip: encrypted centraldir\n");
		result = false;
	}

	return result;
}

static qboolean FSZIP_ReadCentralEntry(zipfile_t *zip, byte *data, struct zipcentralentry *entry)
{
	entry->flags = 0;
	entry->fname = (unsigned char*)"";
	entry->fnane_len = 0;

	if (data[0] != 'P' ||
		data[1] != 'K' ||
		data[2] != 1 ||
		data[3] != 2)
		return false;	//verify the signature

	//if we read too much, we'll catch it after.
	entry->version_madeby = LittleU2FromPtr(data+4);
	entry->version_needed = LittleU2FromPtr(data+6);
	entry->gflags = LittleU2FromPtr(data+8);
	entry->cmethod = LittleU2FromPtr(data+10);
	entry->lastmodfiletime = LittleU2FromPtr(data+12);
	entry->lastmodfiledate = LittleU2FromPtr(data+12);
	entry->crc32 = LittleU4FromPtr(data+16);
	entry->csize = LittleU4FromPtr(data+20);
	entry->usize = LittleU4FromPtr(data+24);
	entry->fnane_len = LittleU2FromPtr(data+28);
	entry->extra_len = LittleU2FromPtr(data+30);
	entry->comment_len = LittleU2FromPtr(data+32);
	entry->disknum = LittleU2FromPtr(data+34);
	entry->iattributes = LittleU2FromPtr(data+36);
	entry->eattributes = LittleU4FromPtr(data+38);
	entry->localheaderoffset = LittleU4FromPtr(data+42);
	entry->cesize = 46;

	//mark the filename position
	entry->fname = data+entry->cesize;
	entry->cesize += entry->fnane_len;

	entry->mtime = 0;

	//parse extra
	if (entry->extra_len)
	{
		byte *extra = data + entry->cesize;
		byte *extraend = extra + entry->extra_len;
		unsigned short extrachunk_tag;
		unsigned short extrachunk_len;
		while(extra+4 < extraend)
		{
			extrachunk_tag = LittleU2FromPtr(extra+0);
			extrachunk_len = LittleU2FromPtr(extra+2);
			if (extra + extrachunk_len > extraend)
				break;	//error
			extra += 4;

			switch(extrachunk_tag)
			{
			case 1:	//zip64 extended information extra field. the attributes are only present if the reegular file info is nulled out with a -1
				if (entry->usize == 0xffffffffu)
				{
					entry->usize = LittleU8FromPtr(extra);
					extra += 8;
				}
				if (entry->csize == 0xffffffffu)
				{
					entry->csize = LittleU8FromPtr(extra);
					extra += 8;
				}
				if (entry->localheaderoffset == 0xffffffffu)
				{
					entry->localheaderoffset = LittleU8FromPtr(extra);
					extra += 8;
				}
				if (entry->disknum == 0xffffu)
				{
					entry->disknum = LittleU4FromPtr(extra);
					extra += 4;
				}
				break;
#if !defined(_MSC_VER) || _MSC_VER > 1200
			case 0x000a:	//NTFS extra field
				//0+4: reserved
				//4+2: subtag(must be 1, for times)
				//6+2: subtagsize(times: must be == 8*3+4)
				//8+8: mtime
				//16+8: atime
				//24+8: ctime
				if (extrachunk_len >= 32 && LittleU2FromPtr(extra+4) == 1 && LittleU2FromPtr(extra+6) == 8*3)	
					entry->mtime = LittleU8FromPtr(extra+8) / 10000000ULL - 11644473600ULL;
				else
					Con_Printf("zip: unsupported ntfs subchunk %x\n", extrachunk_tag);
				extra += extrachunk_len;
				break;
#endif
			case 0x5455:
				if (extra[0] & 1)
					entry->mtime = LittleU4FromPtr(extra+1);
				//access and creation do NOT exist in the central header.
				extra += extrachunk_len;
				break;
			default:
/*				Con_Printf("Unknown chunk %x\n", extrachunk_tag);
			case 0x5455:	//extended timestamp
			case 0x7875:	//unix uid/gid
			case 0x9901:	//aes crypto
*/				extra += extrachunk_len;
				break;
			}
		}
		entry->cesize += entry->extra_len;
	}

	//parse comment
	entry->cesize += entry->comment_len;

	//check symlink flags
	{
		byte madeby = entry->version_madeby>>8;	//system
		//vms, unix, or beos file attributes includes a symlink attribute.
		//symlinks mean the file contents is just the name of another file.
		if (madeby == 2 || madeby == 3 || madeby == 16)
		{
			unsigned short unixattr = entry->eattributes>>16;
			if ((unixattr & 0xF000) == 0xA000)//fa&S_IFMT==S_IFLNK 
				entry->flags |= ZFL_SYMLINK;
			else if ((unixattr & 0xA000) == 0xA000)//fa&S_IFMT==S_IFLNK 
				entry->flags |= ZFL_SYMLINK;
		}
	}

	if (entry->gflags & (1u<<0))	//encrypted
	{
#ifdef ZIPCRYPT
		entry->flags |= ZFL_WEAKENCRYPT;
#else
		entry->flags |= ZFL_CORRUPT;
#endif
	}


	if (entry->gflags & (1u<<5))	//is patch data
		entry->flags |= ZFL_CORRUPT;
	else if (entry->gflags & (1u<<6))	//strong encryption
		entry->flags |= ZFL_CORRUPT;
	else if (entry->gflags & (1u<<13))	//strong encryption
		entry->flags |= ZFL_CORRUPT;
	else if (entry->cmethod == 0)
		entry->flags |= ZFL_STORED;
	//1: shrink
	//2-5: reduce
	//6: implode
	//7: tokenize
	else if (entry->cmethod == 8)
		entry->flags |= ZFL_DEFLATED;
	//8: deflate64 - patented. sometimes written by microsoft's crap, so this might be problematic. only minor improvements.
	//10: implode
	//12: bzip2
//	else if (entry->cmethod == 12)
//		entry->flags |= ZFL_BZIP2;
//	else if (entry->cmethod == 14)
//		entry->flags |= ZFL_LZMA;
	//19: lz77
	//97: wavpack
	//98: ppmd
	else 
		entry->flags |= ZFL_CORRUPT;	//unsupported compression method.

	if ((entry->flags & ZFL_WEAKENCRYPT) && !(entry->flags & ZFL_DEFLATED))
		entry->flags |= ZFL_CORRUPT;	//only support decryption with deflate.
	return true;
}

static qboolean FSZIP_EnumerateCentralDirectory(zipfile_t *zip, struct zipinfo *info, const char *prefix)
{
	qboolean success = false;
	zpackfile_t		*f;
	struct zipcentralentry entry;
	qofs_t ofs = 0;
	unsigned int i;
	//lazily read the entire thing.
	byte *centraldir = malloc(info->centraldir_size);
	if (centraldir)
	{
		Sys_FileSeek(zip->raw, info->centraldir_offset+info->zipoffset);
		if ((qofs_t)Sys_FileRead(zip->raw, centraldir, info->centraldir_size) == info->centraldir_size)
		{
			zip->numfiles = info->centraldir_numfiles_disk;
			zip->files = f = Z_Malloc (zip->numfiles * sizeof(*f));

			for (i = 0; i < zip->numfiles; i++)
			{
				if (!FSZIP_ReadCentralEntry(zip, centraldir+ofs, &entry) || ofs + entry.cesize > info->centraldir_size)
					break;

				f->crc = entry.crc32;

				//copy out the filename and lowercase it
				if (entry.gflags & (1u<<11))
				{	//already utf-8 encoding
					if (entry.fnane_len > sizeof(f->name)-1)
						entry.fnane_len = sizeof(f->name)-1;
					memcpy(f->name, entry.fname, entry.fnane_len);
					f->name[entry.fnane_len] = 0;
				}
				else
				{	//legacy charset
					int i;
					int nlen = 0;
					int cl;
					for (i = 0; i < entry.fnane_len; i++)
					{
#if 1
						cl = ibmtounicode[entry.fname[i]];
						if (cl > 127)
							f->name[nlen] = '?';	//we can't encode non-ascii chars
						else
							f->name[nlen] = cl;
						cl = 1;
#else
						cl = utf8_encode(f->name+nlen, ibmtounicode[entry.fname[i]], sizeof(f->name)-1 - nlen);
#endif
						if (!cl)	//overflowed, truncate cleanly.
							break;
						nlen += cl;
					}
					f->name[nlen] = 0;

				}

				if (prefix && *prefix)
				{
					if (!strcmp(prefix, ".."))
					{
						char *c; 
						for (c = f->name; *c; )
						{
							if (*c++ == '/')
								break;
						}
						memmove(f->name, c, strlen(c)+1);
					}
					else
					{
						size_t prelen = strlen(prefix);
						size_t oldlen = strlen(f->name);
						if (prelen+1+oldlen+1 > sizeof(f->name))
							*f->name = 0;
						else
						{
							memmove(f->name+prelen+1, f->name, oldlen);
							f->name[prelen] = '/';
							memmove(f->name, prefix, prelen);
						}
					}
				}
				q_strlwr(f->name);

				f->filelen = *f->name?entry.usize:0;
				f->localpos = entry.localheaderoffset+info->zipoffset;
				f->flags = entry.flags;
//				f->mtime = entry.mtime;

				ofs += entry.cesize;
				f++;
			}

			success = i == zip->numfiles;
			if (!success)
			{
				free(zip->files);
				zip->files = NULL;
				zip->numfiles = 0;
			}
		}
	}

	free(centraldir);
	return success;
}

static qboolean FSZIP_ValidateLocalHeader(zipfile_t *zip, zpackfile_t *zfile, qofs_t *datastart, qofs_t *datasize)
{
	struct ziplocalentry local;
	byte localdata[SIZE_LOCALENTRY];
	qofs_t localstart = zfile->localpos;

	Sys_FileSeek(zip->raw, localstart);
	Sys_FileRead(zip->raw, localdata, sizeof(localdata));

	//make sure we found the right sort of table.
	if (localdata[0] != 'P' ||
		localdata[1] != 'K' ||
		localdata[2] != 3 ||
		localdata[3] != 4)
		return false;

	local.version_needed = LittleU2FromPtr(localdata+4);
	local.gpflags = LittleU2FromPtr(localdata+6);
	local.cmethod = LittleU2FromPtr(localdata+8);
	local.lastmodfiletime = LittleU2FromPtr(localdata+10);
	local.lastmodfiledate = LittleU2FromPtr(localdata+12);
	local.crc32 = LittleU4FromPtr(localdata+14);
	local.csize = LittleU4FromPtr(localdata+18);
	local.usize = LittleU4FromPtr(localdata+22);
	local.fname_len = LittleU2FromPtr(localdata+26);
	local.extra_len = LittleU2FromPtr(localdata+28);

	localstart += SIZE_LOCALENTRY;
	localstart += local.fname_len;

	//parse extra
	if (local.usize == 0xffffffffu || local.csize == 0xffffffffu)	//don't bother otherwise.
	if (local.extra_len)
	{
		byte extradata[65536];
		byte *extra = extradata;
		byte *extraend = extradata + local.extra_len;
		unsigned short extrachunk_tag;
		unsigned short extrachunk_len;

		Sys_FileSeek(zip->raw, localstart);
		Sys_FileRead(zip->raw, extradata, sizeof(extradata));

		while(extra+4 < extraend)
		{
			extrachunk_tag = LittleU2FromPtr(extra+0);
			extrachunk_len = LittleU2FromPtr(extra+2);
			if (extra + extrachunk_len > extraend)
				break;	//error
			extra += 4;

			switch(extrachunk_tag)
			{
			case 1:	//zip64 extended information extra field. the attributes are only present if the reegular file info is nulled out with a -1
				if (local.usize == 0xffffffffu)
				{
					local.usize = LittleU8FromPtr(extra);
					extra += 8;
				}
				if (local.csize == 0xffffffffu)
				{
					local.csize = LittleU8FromPtr(extra);
					extra += 8;
				}
				break;
			default:
/*				Con_Printf("Unknown chunk %x\n", extrachunk_tag);
			case 0x000a:	//NTFS (timestamps)
			case 0x5455:	//extended timestamp
			case 0x7875:	//unix uid/gid
*/				extra += extrachunk_len;
				break;
			}
		}
	}
	localstart += local.extra_len;
	*datastart = localstart;	//this is the end of the local block, and the start of the data block (well, actually, should be encryption, but we don't support that).
	*datasize = local.csize;

	if (local.gpflags & (1u<<3))
	{
		//crc, usize, and csize were not known at the time the file was compressed.
		//there is a 'data descriptor' after the file data, but to parse that we would need to decompress the file.
		//instead, just depend upon upon the central directory and don't bother checking.
	}
	else
	{
		if (local.crc32 != zfile->crc)
			return false;
		if (local.usize != zfile->filelen)
			return false;
	}

	//FIXME: with pure paths, we still don't bother checking the crc (again, would require decompressing the entire file in advance).

	if (local.cmethod == 0)
		return (zfile->flags & (ZFL_STORED|ZFL_CORRUPT|ZFL_DEFLATED)) == ZFL_STORED;
	if (local.cmethod == 8)
		return (zfile->flags & (ZFL_STORED|ZFL_CORRUPT|ZFL_DEFLATED)) == ZFL_DEFLATED;
	return false;	//some other method that we don't know.
}

pack_t *FSZIP_LoadArchive (const char *packfile)
{
	size_t		i;
	packfile_t	*newfiles;
	int			numpackfiles;
	pack_t		*pack;

	zipfile_t zip;
	struct zipinfo info;

#ifndef USE_ZLIB
	qboolean zlibneeded = false;
#endif

	zip.rawsize = Sys_FileOpenRead(packfile, &zip.raw);
	if (zip.raw < 0)
		return NULL;

	//try to find the header
	if (!FSZIP_FindEndCentralDirectory(&zip, &info))
	{
		Sys_FileClose(zip.raw);
		return NULL;
	}

	//now try to read it.
	if (!FSZIP_EnumerateCentralDirectory(&zip, &info, "") && !info.zip64_diskcount)
	{
		//uh oh... the central directory wasn't where it was meant to be!
		//assuming that the endofcentraldir is packed at the true end of the centraldir (and that we're not zip64 and thus don't have an extra block), then we can guess based upon the offset difference
		info.zipoffset = info.centraldir_end - (info.centraldir_offset+info.centraldir_size);
		if (!FSZIP_EnumerateCentralDirectory(&zip, &info, ""))
		{
			Sys_FileClose(zip.raw);
			Con_Printf ("zipfile \"%s\" appears to be missing its central directory\n", packfile);
			return NULL;
		}
	}

	//lame zone.
	//copy the files into something compatible with quake's pak support.
	//ignore compressed / corrupt / unusable files
	pack = (pack_t *) Z_Malloc (sizeof (pack_t));
	q_strlcpy (pack->filename, packfile, sizeof(pack->filename));
	pack->handle = zip.raw;

	numpackfiles = 0;
	newfiles = NULL;

	newfiles = Z_Malloc(sizeof(*newfiles) * zip.numfiles);
	for (numpackfiles = 0, i = 0; i < zip.numfiles; i++)
	{
		qofs_t startpos, datasize;
		zpackfile_t *zp = &zip.files[i];
		if (zp->flags & ZFL_CORRUPT)
			continue;	//we can't cope with this.
		if (zp->flags & ZFL_SYMLINK)
			continue;	//file data is just a filename

		if (!FSZIP_ValidateLocalHeader(&zip, zp, &startpos, &datasize))
			continue;	//local header is corrupt

		if (zp->flags & ZFL_DEFLATED)
		{
#ifndef USE_ZLIB
			zlibneeded = true;
			continue;
#endif
		}
		else if (zp->flags & ZFL_STORED)
		{
			if (datasize != zp->filelen)
				continue;
			datasize = 0;
		}
		else
				continue;

		//usable file.
		memcpy(newfiles[numpackfiles].name, zp->name, MAX_QPATH-1);
		newfiles[numpackfiles].name[MAX_QPATH-1] = 0;
		newfiles[numpackfiles].filelen = zp->filelen;
		newfiles[numpackfiles].filepos = startpos;
		newfiles[numpackfiles].deflatedsize = datasize;
		numpackfiles++;
	}
	pack->numfiles = numpackfiles;
	pack->files = newfiles;

	//we don't need this stuff now.
	Z_Free(zip.files);

#ifndef USE_ZLIB
	if (zlibneeded)
		Con_Printf ("zipfile \"%s\" contains compressed files, but zlib was disabled at compile time.\n", packfile);
#endif
	//Sys_Printf ("Added packfile %s (%i files)\n", packfile, numpackfiles);
	return pack;
}

FILE *FSZIP_Deflate(FILE *src, int srcsize, int outsize)
{
#ifdef USE_ZLIB
	byte inbuffer[65536];
	byte outbuffer[65536];
	z_stream strm;
	int ret;

	FILE *of;
#ifdef _WIN32
	/*warning: annother app might manage to open the file before we can. if the file is not opened exclusively then we can end up with issues
	on windows, fopen is typically exclusive anyway, but not on unix. but on unix, tmpfile is actually usable, so special-case the windows code and hope that its never an issue
	tmpfile isn't usable in windows. it creates the file in the root dir and requires admin rights, which is stupid.
	*/
	char *fname = _tempnam(NULL, "ftemp");
	of = fopen(fname, "w+bD");
#else
	of = tmpfile();
#endif

	if (!of)
	{
		fclose(src);
		return NULL;
	}

	memset(&strm, 0, sizeof(strm));
	strm.data_type = Z_UNKNOWN;
	inflateInit2(&strm, -MAX_WBITS);
	while ((ret=inflate(&strm, Z_SYNC_FLUSH)) != Z_STREAM_END)
	{
		if (strm.avail_in == 0 || strm.avail_out == 0)
		{
			if (strm.avail_in == 0)
			{
				strm.avail_in = fread(inbuffer, 1, sizeof(inbuffer), src);
				strm.next_in = inbuffer;
				if (!strm.avail_in)
					break;
			}
			if (strm.avail_out == 0)
			{
				strm.next_out = outbuffer;
				fwrite(outbuffer, 1, strm.total_out, of);
				strm.total_out = 0;
				strm.avail_out = sizeof(outbuffer);
			}
			continue;
		}

		//doh, it terminated for no reason
		if (ret != Z_STREAM_END)
		{
			inflateEnd(&strm);
			fclose(src);
			fclose(of);
			Con_Printf("Couldn't decompress file\n");
			return NULL;
		}
		
	}
	fwrite(outbuffer, 1, strm.total_out, of);
	inflateEnd(&strm);

	fclose(src);

	fseek(of, SEEK_SET, 0);
	return of;
#else
	fclose(src);
	return NULL;
#endif
}
