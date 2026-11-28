/*
 * Mod catalogue and installation support.
 * Community catalogue format and download flow from Ironwail, Copyright (C) 2022 A. Drexler.
 * Distributed under the GNU General Public License, version 2 or later.
 */
#include "quakedef.h"
#include "addons.h"
#include "miniz.h"
#include <curl/curl.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#define ADDON_MAX_MANIFEST (4 * 1024 * 1024)

static const char *const addon_servers[] = {"https://kexquake.s3.amazonaws.com", "https://q1tools.github.io"};
static const char *const addon_manifests[] = {"content.json", "mods/moddownloads.json"};
static const char *const addon_names[] = {"KEX", "Q1Tools"};
static char				 addon_urls[ADDON_SOURCE_COUNT][2048];
static qboolean			 addon_disabled;

typedef struct
{
	json_t	*json;
	addon_t *items;
	char	 message[192];
} addon_catalog_t;
static addon_catalog_t addon_catalogs[ADDON_SOURCE_COUNT];
static unsigned int	   addon_revision;
static int			   addon_next_source;

/* A job owns all its mutable data until the worker signals done. Only the
 * cancel/progress/done fields are read or written concurrently. */
static struct
{
	SDL_Thread	   *thread;
	atomic_uint32_t cancel, progress, done;
	int				source;
	qboolean		install, update, success;
	addon_t			addon;
	char			basedir[MAX_OSPATH];
	char			url[2048];
	char			stage[MAX_OSPATH];
	char			destination[MAX_OSPATH];
	char			recovery[MAX_OSPATH];
	char			previous_revision[17];
	char			error[192];
	char		   *manifest;
	size_t			manifest_size;
	FILE		   *file;
	uint64_t		received, offset, total;
	size_t			part_index, part_count;
} addon_job;

static qboolean Addons_Error (const char *message)
{
	q_strlcpy (addon_job.error, message, sizeof (addon_job.error));
	return false;
}

/* Use portable directory names, including on case-insensitive Windows volumes. */
static qboolean Addons_Component (const char *name, size_t len)
{
	char   stem[16];
	size_t i = 0;
	if (!len || len > 255 || name[len - 1] == '.' || name[len - 1] == ' ')
		return false;
#ifdef _WIN32
	if (!MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, name, (int)len, NULL, 0))
		return false;
#endif
	for (size_t j = 0; j < len; j++)
		if ((unsigned char)name[j] < 32 || strchr ("\\/:*?\"<>|;", name[j]))
			return false;
	while (i < len && i < sizeof (stem) - 1 && name[i] != '.')
	{
		stem[i] = name[i];
		i++;
	}
	stem[i] = 0;
	if (!q_strcasecmp (stem, "con") || !q_strcasecmp (stem, "prn") || !q_strcasecmp (stem, "aux") || !q_strcasecmp (stem, "nul"))
		return false;
	if (i == 4 && (!q_strncasecmp (stem, "com", 3) || !q_strncasecmp (stem, "lpt", 3)) && stem[3] >= '0' && stem[3] <= '9')
		return false;
	return true;
}

static qboolean Addons_Path (const char *path)
{
	const char *p = path;
	int			depth = 0;
	if (!*p || strlen (p) >= MAX_OSPATH)
		return false;
	while (*p)
	{
		const char *end = strchr (p, '/');
		size_t		len = end ? (size_t)(end - p) : strlen (p);
		if (++depth > 32 || !Addons_Component (p, len))
			return false;
		p += len;
		if (*p)
			p++;
	}
	return true;
}

static qboolean Addons_Dir (const char *dir)
{
	return dir && dir[0] != '.' && !strstr (dir, "..") && strlen (dir) < MAX_QPATH && Addons_Component (dir, strlen (dir)) && q_strcasecmp (dir, GAMENAME) &&
		   q_strcasecmp (dir, "rerelease");
}

static qboolean Addons_URL (const char *url)
{
	return url && !q_strncasecmp (url, "https://", 8) && url[8] && !strpbrk (url, "\r\n\t ");
}

static qboolean Addons_HashValid (const char *hash)
{
	if (!hash || !*hash)
		return true; // hashes are optional in the catalogue
	if (strlen (hash) != 64)
		return false;
	for (int i = 0; i < 64; i++)
		if (!strchr ("0123456789abcdefABCDEF", hash[i]))
			return false;
	return true;
}

static qboolean Addons_AssetValid (const jsonentry_t *asset)
{
	return asset && asset->type == JSON_OBJECT && Addons_URL (JSON_FindString (asset, "url")) && Addons_HashValid (JSON_FindString (asset, "sha256"));
}

static void Addons_FreeCatalog (addon_catalog_t *catalog)
{
	for (size_t i = 0; i < VEC_SIZE (catalog->items); i++)
	{
		addon_t *item = &catalog->items[i];
		for (size_t j = 0; j < VEC_SIZE (item->parts); j++)
			Mem_Free (item->parts[j].url);
		VEC_FREE (item->parts);
	}
	VEC_FREE (catalog->items);
	if (catalog->json)
		JSON_Free (catalog->json);
	catalog->json = NULL;
}

static void Addons_AddPart (addon_t *item, const char *url, const char *sha256)
{
	addon_part_t part;
	part.url = Mem_Alloc (strlen (url) + 1);
	strcpy (part.url, url);
	part.sha256 = sha256;
	VEC_PUSH (item->parts, part);
}

static void Addons_SetRevision (addon_t *item)
{
	uint64_t hash = 14695981039346656037ULL;
	for (size_t i = 0; i <= VEC_SIZE (item->parts); i++)
	{
		const char *text = i == VEC_SIZE (item->parts) ? item->version : item->parts[i].url;
		for (const char *p = text; *p; p++)
			hash = (hash ^ (byte)*p) * 1099511628211ULL;
		hash = (hash ^ 0xff) * 1099511628211ULL;
		if (i < VEC_SIZE (item->parts) && item->parts[i].sha256)
			for (const char *p = item->parts[i].sha256; *p; p++)
				hash = (hash ^ (byte)*p) * 1099511628211ULL;
	}
	/* Some KEX entries omit a version; date/size changes still identify a new package. */
	for (const char *p = item->date; *p; p++)
		hash = (hash ^ (byte)*p) * 1099511628211ULL;
	if (item->sha256)
		for (const char *p = item->sha256; *p; p++)
			hash = (hash ^ (byte)*p) * 1099511628211ULL;
	hash = (hash ^ item->bytes) * 1099511628211ULL;
	q_snprintf (item->revision, sizeof (item->revision), "%016llx", (unsigned long long)hash);
}

static qboolean Addons_Parse (addon_catalog_t *catalog, const char *text, int source)
{
	json_t			  *json = JSON_Parse (text);
	const jsonentry_t *array = json ? JSON_Find (json->root, source ? "mods" : "addons", JSON_ARRAY) : NULL;
	addon_t			  *items = NULL;
	if (!array)
	{
		if (json)
			JSON_Free (json);
		return false;
	}
	for (const jsonentry_t *entry = array->firstchild; entry; entry = entry->next)
	{
		const char *dir = JSON_FindString (entry, source ? "install_dir" : "gamedir");
		const char *name = JSON_FindString (entry, source ? "title" : "name");
		addon_t		item = {0};
		qboolean	valid = Addons_Dir (dir) && name && *name;
		if (source)
		{
			const jsonentry_t *parts = JSON_Find (entry, "parts", JSON_ARRAY);
			if (parts)
			{
				int count = 0;
				for (const jsonentry_t *part = parts->firstchild; part; part = part->next)
				{
					valid = valid && Addons_AssetValid (part);
					count++;
				}
				valid = valid && count > 0 && Addons_HashValid (JSON_FindString (entry, "sha256"));
			}
			else
				valid = valid && Addons_AssetValid (JSON_Find (entry, "asset", JSON_OBJECT));
		}
		else
		{
			const char *download = JSON_FindString (entry, "download");
			valid = valid && download && Addons_Path (download);
		}
		if (!valid)
			continue;
		for (size_t i = 0; i < VEC_SIZE (items); i++)
			if (!q_strcasecmp (items[i].dir, dir))
				valid = false;
		if (!valid || VEC_SIZE (items) >= 4096)
			continue;
		q_strlcpy (item.dir, dir, sizeof (item.dir));
		item.source = source;
		UTF8_ToQuake (item.name, sizeof (item.name), name);
		const char *author = JSON_FindString (entry, "author");
		const char *date = JSON_FindString (entry, "date");
		if (author)
			UTF8_ToQuake (item.author, sizeof (item.author), author);
		if (date)
			q_strlcpy (item.date, date, sizeof (item.date));
		if (source)
		{
			const char *size = JSON_FindString (entry, "size");
			if (size)
				q_strlcpy (item.size, size, sizeof (item.size));
		}
		else
		{
			const double *size = JSON_FindNumber (entry, "size");
			if (size && *size > 0)
				q_snprintf (item.size, sizeof (item.size), "%.1f MB", *size / 1048576.0);
		}
		const char *version = JSON_FindString (entry, source ? "tag" : "version");
		q_strlcpy (item.version, version && *version ? version : "Unspecified", sizeof (item.version));
		item.zip = source != 0;
		if (source)
		{
			const jsonentry_t *parts = JSON_Find (entry, "parts", JSON_ARRAY);
			const jsonentry_t *asset = JSON_Find (entry, "asset", JSON_OBJECT);
			for (const jsonentry_t *part = parts ? parts->firstchild : asset; part; part = parts ? part->next : NULL)
				Addons_AddPart (&item, JSON_FindString (part, "url"), JSON_FindString (part, "sha256"));
			item.sha256 = parts ? JSON_FindString (entry, "sha256") : NULL;
		}
		else
		{
			char		  url[2048];
			const double *bytes = JSON_FindNumber (entry, "size");
			q_snprintf (url, sizeof (url), "%s/%s", addon_urls[source], JSON_FindString (entry, "download"));
			Addons_AddPart (&item, url, NULL);
			if (bytes && *bytes > 0 && *bytes < (double)INT64_MAX)
				item.bytes = (uint64_t)*bytes;
		}
		Addons_SetRevision (&item);
		VEC_PUSH (items, item);
	}
	if (!VEC_SIZE (items))
	{
		JSON_Free (json);
		VEC_FREE (items);
		return false;
	}
	Addons_FreeCatalog (catalog);
	catalog->json = json;
	catalog->items = items;
	return true;
}

static size_t Addons_Write (void *data, size_t size, size_t count, void *unused)
{
	if (size && count > SIZE_MAX / size)
		return 0;
	size_t	 bytes = size * count;
	uint64_t limit = addon_job.install ? INT64_MAX : ADDON_MAX_MANIFEST;
	if (Atomic_LoadUInt32 (&addon_job.cancel) || bytes > limit || addon_job.offset + addon_job.received > limit - bytes)
		return 0;
	if (addon_job.file)
	{
		if (fwrite (data, 1, bytes, addon_job.file) != bytes)
			return 0;
	}
	else
	{
		char *manifest = Mem_Realloc (addon_job.manifest, addon_job.manifest_size + bytes + 1);
		if (!manifest)
			return 0;
		addon_job.manifest = manifest;
		memcpy (addon_job.manifest + addon_job.manifest_size, data, bytes);
		addon_job.manifest_size += bytes;
		addon_job.manifest[addon_job.manifest_size] = 0;
	}
	addon_job.received += bytes;
	if (addon_job.total)
		Atomic_StoreUInt32 (
			&addon_job.progress,
			(uint32_t)q_min (99.0, (addon_job.part_index + (double)addon_job.received / (double)addon_job.total) * 100.0 / q_max (1, addon_job.part_count)));
	return bytes;
}

static int Addons_CurlProgress (void *unused, curl_off_t total, curl_off_t now, curl_off_t upload_total, curl_off_t upload_now)
{
	if (!addon_job.total && total > 0 && addon_job.install)
		addon_job.total = total;
	return Atomic_LoadUInt32 (&addon_job.cancel) != 0;
}

static qboolean Addons_Download (const char *url)
{
	CURL	*curl = curl_easy_init ();
	CURLcode result;
	long	 status = 0;
	addon_job.received = 0;
	if (!curl)
		return Addons_Error ("Could not initialize HTTPS");
	curl_easy_setopt (curl, CURLOPT_URL, url);
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt (curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt (curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
	curl_easy_setopt (curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
	curl_easy_setopt (curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
	curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt (curl, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt (curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt (curl, CURLOPT_USERAGENT, "vkQuake");
	curl_easy_setopt (curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
	curl_easy_setopt (curl, CURLOPT_PROXY_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
	curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt (curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt (curl, CURLOPT_LOW_SPEED_TIME, 10L);
	curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, Addons_Write);
	curl_easy_setopt (curl, CURLOPT_XFERINFOFUNCTION, Addons_CurlProgress);
	curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 0L);
	result = curl_easy_perform (curl);
	curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &status);
	if (result != CURLE_OK)
		q_snprintf (addon_job.error, sizeof (addon_job.error), "Download failed: %s", curl_easy_strerror (result));
	else if (status != 200)
		q_snprintf (addon_job.error, sizeof (addon_job.error), "Download failed (HTTP %ld)", status);
	curl_easy_cleanup (curl);
	return result == CURLE_OK && status == 200;
}

/* SHA-256, applied to each part as well as the joined archive when supplied. */
static uint32_t Addons_Rotate (uint32_t x, int n)
{
	return (x >> n) | (x << (32 - n));
}

static void Addons_SHA256Block (uint32_t h[8], const byte block[64])
{
	static const uint32_t k[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
								   0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
								   0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
								   0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
								   0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
								   0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
								   0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
	uint32_t			  w[64], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], z = h[7];
	for (int i = 0; i < 16; i++)
		w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) | ((uint32_t)block[i * 4 + 2] << 8) | block[i * 4 + 3];
	for (int i = 16; i < 64; i++)
	{
		uint32_t x = w[i - 15], y = w[i - 2];
		w[i] = w[i - 16] + (Addons_Rotate (x, 7) ^ Addons_Rotate (x, 18) ^ (x >> 3)) + w[i - 7] + (Addons_Rotate (y, 17) ^ Addons_Rotate (y, 19) ^ (y >> 10));
	}
	for (int i = 0; i < 64; i++)
	{
		uint32_t t1 = z + (Addons_Rotate (e, 6) ^ Addons_Rotate (e, 11) ^ Addons_Rotate (e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
		uint32_t t2 = (Addons_Rotate (a, 2) ^ Addons_Rotate (a, 13) ^ Addons_Rotate (a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		z = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	h[0] += a;
	h[1] += b;
	h[2] += c;
	h[3] += d;
	h[4] += e;
	h[5] += f;
	h[6] += g;
	h[7] += z;
}

static qboolean Addons_Verify (FILE *file, uint64_t start, uint64_t length, const char *expected)
{
	uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	byte	 buffer[65536], tail[128] = {0};
	uint64_t remaining = length, bits = length * 8;
	char	 hash[65];
	if (!expected || !*expected)
		return true;
	if (fflush (file) || Sys_fseek (file, (qfileofs_t)start, SEEK_SET))
		return Addons_Error ("Could not read downloaded file");
	while (remaining >= 64)
	{
		size_t size = (size_t)q_min ((uint64_t)sizeof (buffer), remaining & ~63ULL);
		if (Atomic_LoadUInt32 (&addon_job.cancel) || fread (buffer, 1, size, file) != size)
			return Addons_Error ("Could not verify downloaded file");
		for (size_t i = 0; i < size; i += 64)
			Addons_SHA256Block (h, buffer + i);
		remaining -= size;
	}
	if (fread (tail, 1, (size_t)remaining, file) != remaining)
		return Addons_Error ("Could not verify downloaded file");
	tail[remaining] = 0x80;
	size_t padded = remaining < 56 ? 64 : 128;
	for (int i = 0; i < 8; i++)
		tail[padded - 1 - i] = (byte)(bits >> (8 * i));
	Addons_SHA256Block (h, tail);
	if (padded == 128)
		Addons_SHA256Block (h, tail + 64);
	for (int i = 0; i < 8; i++)
		q_snprintf (hash + i * 8, 9, "%08x", h[i]);
	if (q_strcasecmp (hash, expected))
		return Addons_Error ("Download checksum mismatch");
	return Sys_fseek (file, 0, SEEK_END) == 0;
}

/* Refuse links/reparse points rather than following them out of a mod directory. */
static qboolean Addons_IsLink (const char *path)
{
#ifdef _WIN32
	wchar_t wide[MAX_OSPATH];
	if (!MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, countof (wide)))
		return true;
	DWORD attributes = GetFileAttributesW (wide);
	return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
	struct stat st;
	return lstat (path, &st) || (!S_ISREG (st.st_mode) && !S_ISDIR (st.st_mode));
#endif
}

/* Unlike menu enumeration, an unreadable directory must not look empty here:
 * omitting even one local file while updating could destroy user data. */
typedef qboolean (*addon_visit_t) (const char *name, const char *path, qboolean directory, void *context);
static qboolean Addons_Visit (const char *path, addon_visit_t visit, void *context)
{
	qboolean ok = true;
#ifdef _WIN32
	char			 pattern[MAX_OSPATH];
	wchar_t			 wide[MAX_OSPATH];
	WIN32_FIND_DATAW data;
	if ((size_t)q_snprintf (pattern, sizeof (pattern), "%s/*", path) >= sizeof (pattern) ||
		!MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, pattern, -1, wide, countof (wide)))
		return false;
	HANDLE handle = FindFirstFileW (wide, &data);
	if (handle == INVALID_HANDLE_VALUE)
		return GetLastError () == ERROR_FILE_NOT_FOUND && Sys_FileType (path) == FS_ENT_DIRECTORY;
	do
	{
		char name[MAX_OSPATH];
		if (!WideCharToMultiByte (CP_UTF8, 0, data.cFileName, -1, name, sizeof (name), NULL, NULL))
		{
			ok = false;
			break;
		}
		qboolean directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
	DIR *handle = opendir (path);
	if (!handle)
		return false;
	for (;;)
	{
		errno = 0;
		struct dirent *entry = readdir (handle);
		if (!entry)
		{
			ok = errno == 0;
			break;
		}
		const char *name = entry->d_name;
#endif
		if (!strcmp (name, ".") || !strcmp (name, ".."))
			continue;
		char child[MAX_OSPATH];
		if ((size_t)q_snprintf (child, sizeof (child), "%s/%s", path, name) >= sizeof (child))
		{
			ok = false;
			break;
		}
#ifndef _WIN32
		struct stat st;
		if (lstat (child, &st))
		{
			ok = false;
			break;
		}
		qboolean directory = S_ISDIR (st.st_mode);
#endif
		if (!visit (name, child, directory, context))
		{
			ok = false;
			break;
		}
#ifdef _WIN32
	} while (FindNextFileW (handle, &data));
	if (ok)
		ok = GetLastError () == ERROR_NO_MORE_FILES;
	FindClose (handle);
#else
	}
	closedir (handle);
#endif
	return ok;
}

static qboolean Addons_Mkdir (const char *path)
{
#ifdef _WIN32
	wchar_t wide[MAX_OSPATH];
	return MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, countof (wide)) && CreateDirectoryW (wide, NULL);
#else
	return mkdir (path, 0700) == 0;
#endif
}

static qboolean Addons_Rmdir (const char *path)
{
#ifdef _WIN32
	wchar_t wide[MAX_OSPATH];
	return MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, countof (wide)) && RemoveDirectoryW (wide);
#else
	return rmdir (path) == 0;
#endif
}

static qboolean Addons_RemoveStage (const char *path);
static qboolean Addons_RemoveFile (const char *name, const char *path, qboolean directory, void *context)
{
	if (Addons_IsLink (path))
		return false;
	return directory ? Addons_RemoveStage (path) : Sys_remove (path) == 0;
}

/* Only remove this job's staging directory after committing or rolling back. */
static qboolean Addons_RemoveStage (const char *path)
{
	return !Addons_IsLink (path) && Addons_Visit (path, Addons_RemoveFile, NULL) && Addons_Rmdir (path);
}

static size_t Addons_ZipRead (void *opaque, mz_uint64 offset, void *buffer, size_t size)
{
	FILE *file = opaque;
	return Sys_fseek (file, (qfileofs_t)offset, SEEK_SET) ? 0 : fread (buffer, 1, size, file);
}

static size_t Addons_ZipWrite (void *opaque, mz_uint64 offset, const void *buffer, size_t size)
{
	if (Atomic_LoadUInt32 (&addon_job.cancel))
		return 0;
	return fwrite (buffer, 1, size, (FILE *)opaque);
}

static qboolean Addons_Extract (FILE *file, const char *target)
{
	mz_zip_archive			 zip = {0};
	mz_zip_archive_file_stat stat;
	uint64_t				 total = 0;
	qboolean				 ok = false;
	zip.m_pRead = Addons_ZipRead;
	zip.m_pIO_opaque = file;
	if (!mz_zip_reader_init (&zip, (mz_uint64)Sys_filelength (file), 0))
		return Addons_Error ("Invalid ZIP archive");
	mz_uint count = mz_zip_reader_get_num_files (&zip);
	if (count > 100000)
		goto done;
	for (mz_uint i = 0; i < count; i++)
	{
		char path[MAX_OSPATH];
		if (!mz_zip_reader_file_stat (&zip, i, &stat) || !Addons_Path (stat.m_filename) ||
			mz_zip_reader_get_filename (&zip, i, NULL, 0) > sizeof (stat.m_filename) || stat.m_is_encrypted ||
			(stat.m_external_attr >> 16 & 0170000) == 0120000 || stat.m_uncomp_size > INT64_MAX - total)
			goto done;
		total += stat.m_uncomp_size;
		if (stat.m_is_directory)
			continue;
		if (!stat.m_is_supported || (size_t)q_snprintf (path, sizeof (path), "%s/%s", target, stat.m_filename) >= sizeof (path))
			goto done;
		FILE *out = Sys_fopen (path, "wbx");
		if (!out)
			goto done;
		qboolean extracted = mz_zip_reader_extract_to_callback (&zip, i, Addons_ZipWrite, out, 0);
		if (fclose (out))
			extracted = false;
		if (!extracted)
			goto done;
	}
	ok = true;
done:
	mz_zip_reader_end (&zip);
	return ok || Addons_Error ("Could not extract ZIP (invalid paths, data, or insufficient disk space)");
}

static qboolean Addons_IsGameDir (const char *path)
{
	const char *const markers[] = {"pak0.pak", "progs.dat", "csprogs.dat", "maps"};
	for (size_t i = 0; i < countof (markers); i++)
	{
		char marker[MAX_OSPATH];
		if ((size_t)q_snprintf (marker, sizeof (marker), "%s/%s", path, markers[i]) < sizeof (marker) &&
			Sys_FileType (marker) == (i == countof (markers) - 1 ? FS_ENT_DIRECTORY : FS_ENT_FILE))
			return true;
	}
	return false;
}

static uint32_t Addons_ReadLE32 (const byte *data)
{
	return (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

/* Validate the PAKs the engine will mount, for either download source. A valid
 * header alone does not rule out truncated data or an unsupported directory. */
static qboolean Addons_CheckPak (const char *name, const char *path, qboolean directory, void *context)
{
	if (directory || q_strncasecmp (name, "pak", 3) || q_strcasecmp (COM_FileGetExtension (name), "pak"))
		return true;
	FILE *file = Sys_fopen (path, "rb");
	if (!file)
		return false;
	byte		header[12], entry[64];
	qfilesize_t size = Sys_filelength (file);
	qboolean	ok = size >= (qfilesize_t)sizeof (header) && fread (header, 1, sizeof (header), file) == sizeof (header) && !memcmp (header, "PACK", 4);
	if (ok)
	{
		uint32_t offset = Addons_ReadLE32 (header + 4), length = Addons_ReadLE32 (header + 8);
		ok = offset >= sizeof (header) && offset <= INT32_MAX && length > 0 && length % sizeof (entry) == 0 && length / sizeof (entry) <= MAX_FILES_IN_PACK &&
			 (uint64_t)offset + length <= (uint64_t)size && !Sys_fseek (file, offset, SEEK_SET);
		for (uint32_t i = 0; ok && i < length / sizeof (entry); i++)
		{
			ok = !Atomic_LoadUInt32 (&addon_job.cancel) && fread (entry, 1, sizeof (entry), file) == sizeof (entry);
			if (ok)
			{
				uint32_t position = Addons_ReadLE32 (entry + 56), bytes = Addons_ReadLE32 (entry + 60);
				ok = memchr (entry, 0, 56) && position <= INT32_MAX && bytes <= INT32_MAX && (uint64_t)position + bytes <= (uint64_t)size;
			}
		}
	}
	fclose (file);
	return ok;
}

/* Archives may contain loose game files, a named game directory, or a wrapper
 * directory. Select a unique game root; never flatten several mods together. */
static int Addons_FindGameDir (const char *path, char *result, size_t size, int depth)
{
	int count = 0;
	if (Addons_IsGameDir (path))
	{
		q_strlcpy (result, path, size);
		return 1;
	}
	if (!depth)
		return 0;
	for (findfile_t *find = Sys_FindFirst (path, NULL); find; find = Sys_FindNext (find))
	{
		char child[MAX_OSPATH];
		if (!(find->attribs & FA_DIRECTORY) || find->name[0] == '.')
			continue;
		if ((size_t)q_snprintf (child, sizeof (child), "%s/%s", path, find->name) < sizeof (child))
			count += Addons_FindGameDir (child, result, size, depth - 1);
	}
	return count;
}

#define ADDON_RECEIPT	  ".vkquake-addon.json"
#define ADDON_MAX_RECEIPT (16 * 1024 * 1024)

static json_t *Addons_ReadReceipt (const char *directory)
{
	char path[MAX_OSPATH];
	if ((size_t)q_snprintf (path, sizeof (path), "%s/" ADDON_RECEIPT, directory) >= sizeof (path))
		return NULL;
	FILE *file = Sys_fopen (path, "rb");
	if (!file)
		return NULL;
	qfilesize_t length = Sys_filelength (file);
	if (length <= 0 || length > ADDON_MAX_RECEIPT)
	{
		fclose (file);
		return NULL;
	}
	char *text = Mem_Alloc ((size_t)length + 1);
	if (!text)
	{
		fclose (file);
		return NULL;
	}
	size_t got = fread (text, 1, (size_t)length, file);
	fclose (file);
	text[got] = 0;
	json_t *json = got == (size_t)length ? JSON_Parse (text) : NULL;
	Mem_Free (text);
	return json;
}

static qboolean Addons_FindInstalledPath (const char *dir, char path[MAX_OSPATH])
{
	for (int i = com_numbasedirs - 1; i >= 0; i--)
	{
		if ((size_t)q_snprintf (path, MAX_OSPATH, "%s/%s", com_basedirs[i], dir) < MAX_OSPATH && Sys_FileType (path))
			return true;
	}
	return false;
}

int Addons_InstalledInfo (const char *dir, char *version, size_t size, char revision[17])
{
	char path[MAX_OSPATH];
	int	 source = -1;
	q_strlcpy (version, "Unknown", size);
	revision[0] = 0;
	if (!Addons_FindInstalledPath (dir, path))
		return -1;
	json_t *json = Addons_ReadReceipt (path);
	if (json)
	{
		const double *format = JSON_FindNumber (json->root, "format");
		const double *provider = JSON_FindNumber (json->root, "source");
		const char	 *tag = JSON_FindString (json->root, "version");
		const char	 *key = JSON_FindString (json->root, "revision");
		if (format && *format == 1 && provider && (*provider == 0 || *provider == 1) && tag && key && strlen (key) == 16 &&
			JSON_Find (json->root, "files", JSON_ARRAY))
		{
			source = (int)*provider;
			q_strlcpy (version, tag, size);
			q_strlcpy (revision, key, 17);
		}
		JSON_Free (json);
	}
	return source;
}

qboolean Addons_UpdateAvailable (const addon_t *addon)
{
	char				   version[64], revision[17];
	const filelist_item_t *installed = Addons_FindInstalled (addon);
	return Addons_InstalledInfo (installed ? installed->name : addon->dir, version, sizeof (version), revision) == addon->source &&
		   strcmp (revision, addon->revision);
}

static qboolean Addons_FileCRC (const char *path, uint32_t *crc, uint64_t *size)
{
	byte   buffer[65536];
	size_t got;
	FILE  *file = Sys_fopen (path, "rb");
	if (!file)
		return false;
	*crc = 0;
	*size = 0;
	while ((got = fread (buffer, 1, sizeof (buffer), file)) != 0)
	{
		if (Atomic_LoadUInt32 (&addon_job.cancel))
		{
			fclose (file);
			return false;
		}
		*crc = (uint32_t)mz_crc32 (*crc, buffer, got);
		*size += got;
	}
	qboolean ok = !ferror (file);
	fclose (file);
	return ok;
}

static void Addons_WriteString (FILE *file, const char *text)
{
	fputc ('"', file);
	for (const byte *p = (const byte *)text; *p; p++)
	{
		if (*p < 32 || *p == '"' || *p == '\\')
			fprintf (file, "\\u%04x", *p);
		else
			fputc (*p, file);
	}
	fputc ('"', file);
}

typedef struct
{
	FILE	   *receipt;
	const char *root;
	int			depth, count;
} addon_receipt_t;

static qboolean Addons_WriteFile (const char *name, const char *path, qboolean directory, void *context)
{
	addon_receipt_t *receipt = context;
	if (!receipt->depth && !q_strcasecmp (name, ADDON_RECEIPT))
		return true;
	if (Atomic_LoadUInt32 (&addon_job.cancel) || Addons_IsLink (path))
		return false;
	if (directory)
	{
		if (++receipt->depth > 32)
			return false;
		qboolean ok = Addons_Visit (path, Addons_WriteFile, receipt);
		receipt->depth--;
		return ok;
	}
	uint32_t crc;
	uint64_t size;
	if (++receipt->count > 100000 || !Addons_FileCRC (path, &crc, &size))
		return false;
	fprintf (receipt->receipt, "%s{\"path\":", receipt->count > 1 ? ",\n" : "");
	Addons_WriteString (receipt->receipt, path + strlen (receipt->root) + 1);
	fprintf (receipt->receipt, ",\"size\":%llu,\"crc\":%u}", (unsigned long long)size, crc);
	return !ferror (receipt->receipt) && Sys_ftell (receipt->receipt) < ADDON_MAX_RECEIPT - 8;
}

static qboolean Addons_WriteReceipt (const char *game)
{
	char path[MAX_OSPATH];
	if ((size_t)q_snprintf (path, sizeof (path), "%s/" ADDON_RECEIPT, game) >= sizeof (path))
		return false;
	FILE *file = Sys_fopen (path, "wbx");
	if (!file)
		return false;
	fprintf (file, "{\"format\":1,\"source\":%d,\"version\":", addon_job.source);
	Addons_WriteString (file, addon_job.addon.version);
	fprintf (file, ",\"revision\":\"%s\",\"files\":[\n", addon_job.addon.revision);
	addon_receipt_t receipt = {file, game, 0, 0};
	qboolean		ok = Addons_Visit (game, Addons_WriteFile, &receipt);
	fprintf (file, "\n]}\n");
	if (fclose (file))
		ok = false;
	return ok;
}

typedef struct
{
	const char *path;
	uint64_t	size;
	uint32_t	crc;
} addon_file_t;
static int Addons_FileCompare (const void *left, const void *right)
{
#ifdef _WIN32
	return q_strcasecmp (((const addon_file_t *)left)->path, ((const addon_file_t *)right)->path);
#else
	return strcmp (((const addon_file_t *)left)->path, ((const addon_file_t *)right)->path);
#endif
}

typedef struct
{
	const char	 *old, *game;
	addon_file_t *files;
	int			  depth;
} addon_copy_t;

static qboolean Addons_CopyLocalFile (const char *name, const char *from, qboolean directory, void *context)
{
	addon_copy_t *copy = context;
	const char	 *child = from + strlen (copy->old) + 1;
	char		  to[MAX_OSPATH];
	if (!copy->depth && !q_strcasecmp (name, ADDON_RECEIPT))
		return true;
	if (Atomic_LoadUInt32 (&addon_job.cancel) || Addons_IsLink (from) || (size_t)q_snprintf (to, sizeof (to), "%s/%s", copy->game, child) >= sizeof (to))
		return false;
	if (directory)
	{
		if (++copy->depth > 32 || (!Addons_Mkdir (to) && (Sys_FileType (to) != FS_ENT_DIRECTORY || Addons_IsLink (to))))
			return false;
		qboolean ok = Addons_Visit (from, Addons_CopyLocalFile, copy);
		copy->depth--;
		return ok;
	}
	addon_file_t		key = {child, 0, 0};
	const addon_file_t *owned = VEC_SIZE (copy->files) ? bsearch (&key, copy->files, VEC_SIZE (copy->files), sizeof (*copy->files), Addons_FileCompare) : NULL;
	const char		   *extension = COM_FileGetExtension (child);
	if (owned && q_strcasecmp (extension, "cfg") && q_strcasecmp (extension, "sav"))
	{
		uint32_t crc;
		uint64_t size;
		if (!Addons_FileCRC (from, &crc, &size))
			return false;
		if (crc == owned->crc && size == owned->size)
			return true;
	}
	FILE	*input = Sys_fopen (from, "rb");
	FILE	*output = input ? Sys_fopen (to, "wb") : NULL;
	qboolean ok = input && output;
	if (ok)
	{
		byte   buffer[65536];
		size_t got;
		while ((got = fread (buffer, 1, sizeof (buffer), input)) != 0)
			if (Atomic_LoadUInt32 (&addon_job.cancel) || fwrite (buffer, 1, got, output) != got)
			{
				ok = false;
				break;
			}
		ok = ok && !ferror (input);
	}
	if (input)
		fclose (input);
	if (output && fclose (output))
		ok = false;
	return ok;
}

static qboolean Addons_PreserveLocalFiles (const char *game)
{
	json_t			  *receipt = Addons_ReadReceipt (addon_job.destination);
	const jsonentry_t *array = receipt ? JSON_Find (receipt->root, "files", JSON_ARRAY) : NULL;
	addon_file_t	  *files = NULL;
	qboolean		   ok = array != NULL;
	if (array)
		for (const jsonentry_t *entry = array->firstchild; entry; entry = entry->next)
		{
			const char	 *path = JSON_FindString (entry, "path");
			const double *size = JSON_FindNumber (entry, "size");
			const double *crc = JSON_FindNumber (entry, "crc");
			if (!path || !Addons_Path (path) || !size || !(*size >= 0 && *size < (double)INT64_MAX) || *size != floor (*size) || !crc ||
				!(*crc >= 0 && *crc <= UINT32_MAX) || *crc != floor (*crc) || VEC_SIZE (files) >= 100000)
			{
				ok = false;
				break;
			}
			addon_file_t file = {path, (uint64_t)*size, (uint32_t)*crc};
			VEC_PUSH (files, file);
		}
	if (ok)
	{
		if (VEC_SIZE (files) > 1)
			qsort (files, VEC_SIZE (files), sizeof (*files), Addons_FileCompare);
		for (size_t i = 1; i < VEC_SIZE (files); i++)
			if (!Addons_FileCompare (&files[i - 1], &files[i]))
				ok = false;
		addon_copy_t copy = {addon_job.destination, game, files, 0};
		ok = ok && !Addons_IsLink (copy.old) && Addons_Visit (copy.old, Addons_CopyLocalFile, &copy);
	}
	VEC_FREE (files);
	if (receipt)
		JSON_Free (receipt);
	return ok || Addons_Error ("Could not preserve local mod files; installation left unchanged");
}

/* Include dangling links and inaccessible paths in the existence check. */
static qboolean Addons_Exists (const char *path)
{
#ifdef _WIN32
	wchar_t wide[MAX_OSPATH];
	if (!MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, countof (wide)))
		return true;
	if (GetFileAttributesW (wide) != INVALID_FILE_ATTRIBUTES)
		return true;
	DWORD error = GetLastError ();
	return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
#else
	struct stat st;
	return lstat (path, &st) == 0 || errno != ENOENT;
#endif
}

/* Publish a complete directory without replacing an existing destination. */
static qboolean Addons_Move (const char *from, const char *to)
{
	if (Addons_Exists (to))
		return false;
#ifdef _WIN32
	wchar_t source[MAX_OSPATH], destination[MAX_OSPATH];
	return MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, from, -1, source, countof (source)) &&
		   MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, to, -1, destination, countof (destination)) && MoveFileExW (source, destination, 0);
#else
	return rename (from, to) == 0;
#endif
}

static qboolean Addons_InstallWorker (void)
{
	char	 archive[MAX_OSPATH], extracted[MAX_OSPATH], game[MAX_OSPATH], destination[MAX_OSPATH], url[MAX_OSPATH];
	qboolean ok = false;
	for (int i = 0; i < 100; i++)
	{
		if ((size_t)q_snprintf (
				addon_job.stage, sizeof (addon_job.stage), "%s/.vkquake-download-%llx-%d", addon_job.basedir, (unsigned long long)SDL_GetPerformanceCounter (),
				i) >= sizeof (addon_job.stage))
			break;
		if (Addons_Mkdir (addon_job.stage))
			goto created;
	}
	addon_job.stage[0] = 0;
	return Addons_Error ("Could not create download directory");
created:
	if ((size_t)q_snprintf (archive, sizeof (archive), "%s/download", addon_job.stage) >= sizeof (archive) ||
		(size_t)q_snprintf (extracted, sizeof (extracted), "%s/files", addon_job.stage) >= sizeof (extracted) || !addon_job.destination[0])
		return Addons_Error ("Installation path is too long");
	q_strlcpy (destination, addon_job.destination, sizeof (destination));
	if (!addon_job.update && Addons_Exists (destination))
		return Addons_Error ("Mod directory already exists");
	addon_job.file = Sys_fopen (archive, "w+bx");
	if (!addon_job.file)
		return Addons_Error ("Could not create download file");
	addon_job.part_count = VEC_SIZE (addon_job.addon.parts);
	for (size_t i = 0; i < VEC_SIZE (addon_job.addon.parts); i++)
	{
		const addon_part_t *part = &addon_job.addon.parts[i];
		addon_job.part_index = i;
		addon_job.total = 0;
		if (!Addons_Download (part->url) || !Addons_Verify (addon_job.file, addon_job.offset, addon_job.received, part->sha256))
			goto done;
		addon_job.offset += addon_job.received;
	}
	if (!Addons_Verify (addon_job.file, 0, addon_job.offset, addon_job.addon.sha256))
	{
		if (!addon_job.error[0])
			Addons_Error ("Incomplete download");
		goto done;
	}
	Atomic_StoreUInt32 (&addon_job.progress, 100);
	if (addon_job.addon.zip)
	{
		if (fflush (addon_job.file) || !Addons_Extract (addon_job.file, extracted))
			goto done;
		if (Addons_FindGameDir (extracted, game, sizeof (game), 3) != 1)
		{
			Addons_Error ("Archive does not contain a single mod directory");
			goto done;
		}
	}
	else
		q_strlcpy (game, extracted, sizeof (game));
	if (fclose (addon_job.file))
	{
		addon_job.file = NULL;
		return Addons_Error ("Could not finish writing download");
	}
	addon_job.file = NULL;
	if (!addon_job.addon.zip)
	{
		if (!Addons_Mkdir (game))
			return Addons_Error ("Could not create mod directory");
		if ((size_t)q_snprintf (url, sizeof (url), "%s/pak0.pak", game) >= sizeof (url) || !Addons_Move (archive, url))
			return Addons_Error ("Could not install PAK");
	}
	/* Keep the catalogue name available when the catalogue is unavailable. */
	if ((size_t)q_snprintf (url, sizeof (url), "%s/descript.ion", game) >= sizeof (url))
		return Addons_Error ("Installation path is too long");
	if (!Sys_FileType (url))
	{
		FILE *description = Sys_fopen (url, "wbx");
		if (description)
		{
			fprintf (description, "%s\n", addon_job.addon.name);
			fclose (description);
		}
	}
	if (!Addons_Visit (game, Addons_CheckPak, NULL))
		return Addons_Error ("Invalid or unsupported PAK in download");
	if (!Addons_WriteReceipt (game))
		return Addons_Error ("Could not record installed version");
	if (addon_job.update)
	{
		json_t	   *receipt = Addons_ReadReceipt (destination);
		const char *revision = receipt ? JSON_FindString (receipt->root, "revision") : NULL;
		qboolean	unchanged = revision && !strcmp (revision, addon_job.previous_revision);
		if (receipt)
			JSON_Free (receipt);
		if (!unchanged)
			return Addons_Error ("Installed version changed while preparing the update");
		if (!Addons_PreserveLocalFiles (game))
			return false;
	}
	if (Atomic_LoadUInt32 (&addon_job.cancel))
		return false;
	if (addon_job.update)
	{
		char backup[MAX_OSPATH];
		if ((size_t)q_snprintf (backup, sizeof (backup), "%s/previous", addon_job.stage) >= sizeof (backup) || !Addons_Move (destination, backup))
			return Addons_Error ("Could not move previous version; close the mod before updating");
		if (!Addons_Move (game, destination))
		{
			if (!Addons_Move (backup, destination))
			{
				q_strlcpy (addon_job.recovery, backup, sizeof (addon_job.recovery));
				addon_job.stage[0] = 0; // never clean up a backup that could not be restored
				return Addons_Error ("Update failed; previous version retained in backup directory");
			}
			return Addons_Error ("Update failed; previous version restored");
		}
	}
	else if (!Addons_Move (game, destination))
		return Addons_Error ("Could not install mod; destination may already exist");
	ok = true;
done:
	if (addon_job.file)
	{
		fclose (addon_job.file);
		addon_job.file = NULL;
	}
	return ok;
}

static int Addons_Worker (void *unused)
{
	addon_job.success = addon_job.install ? Addons_InstallWorker () : Addons_Download (addon_job.url);
	if (addon_job.stage[0])
		Addons_RemoveStage (addon_job.stage);
	Atomic_StoreUInt32 (&addon_job.done, 1);
	return 0;
}

const char *Addons_SourceName (int source)
{
	return addon_names[source];
}
unsigned int Addons_Revision (void)
{
	return addon_revision;
}
int Addons_Progress (void)
{
	return (int)Atomic_LoadUInt32 (&addon_job.progress);
}
const char *Addons_Installing (void)
{
	return addon_job.thread && addon_job.install ? addon_job.addon.dir : NULL;
}

const addon_t *Addons_List (int source, int *count)
{
	addon_catalog_t *catalog = &addon_catalogs[source];
	*count = (int)VEC_SIZE (catalog->items);
	return catalog->items;
}

const char *Addons_Message (void)
{
	static char message[512];
	if (addon_disabled)
		return "Downloads disabled (-noaddons)";
	if (addon_next_source < ADDON_SOURCE_COUNT || (addon_job.thread && !addon_job.install))
		return "Loading mod lists...";
	message[0] = 0;
	for (int source = 0; source < ADDON_SOURCE_COUNT; source++)
		if (addon_catalogs[source].message[0])
		{
			if (message[0])
				q_strlcat (message, " / ", sizeof (message));
			q_strlcat (message, addon_names[source], sizeof (message));
			q_strlcat (message, ": ", sizeof (message));
			q_strlcat (message, addon_catalogs[source].message, sizeof (message));
		}
	return message;
}

const filelist_item_t *Addons_FindInstalled (const addon_t *addon)
{
	for (filelist_item_t *item = modlist; item; item = item->next)
		if (!q_strcasecmp (item->name, addon->dir))
			return item;
	// Catalogue directories can differ from existing installs (e.g. alkaline/alk).
	// Accept an exact display name only when it identifies a single local mod.
	const filelist_item_t *match = NULL;
	for (filelist_item_t *item = modlist; item; item = item->next)
	{
		const char *name = Modlist_GetFullName (item);
		if (name && !q_strcasecmp (name, addon->name))
		{
			if (match)
				return NULL;
			match = item;
		}
	}
	return match;
}

static void Addons_StartJob (void)
{
	addon_job.thread = SDL_CreateThread (Addons_Worker, "Mod download", NULL);
	if (!addon_job.thread)
		q_strlcpy (addon_catalogs[addon_job.source].message, "Could not start download", sizeof (addon_catalogs[0].message));
	addon_revision++;
}

static void Addons_Preload (void)
{
	if (addon_disabled || addon_job.thread)
		return;
	while (addon_next_source < (int)countof (addon_catalogs))
	{
		int source = addon_next_source++;
		memset (&addon_job, 0, sizeof (addon_job));
		addon_job.source = source;
		q_snprintf (addon_job.url, sizeof (addon_job.url), "%s/%s", addon_urls[source], addon_manifests[source]);
		Addons_StartJob ();
		if (addon_job.thread)
			return;
	}
}

qboolean Addons_Install (const addon_t *addon)
{
	char path[MAX_OSPATH];
	if (addon_disabled || addon_job.thread || !addon)
		return false;
	int					   source = addon->source;
	const filelist_item_t *installed = Addons_FindInstalled (addon);
	const char			  *dir = installed ? installed->name : addon->dir;
	qboolean			   exists = Addons_FindInstalledPath (dir, path);
	if (exists && Addons_IsLink (path))
	{
		q_strlcpy (addon_catalogs[source].message, "Cannot update a linked mod directory", sizeof (addon_catalogs[source].message));
		return false;
	}
	if (exists && !Addons_UpdateAvailable (addon))
	{
		q_strlcpy (addon_catalogs[source].message, "Existing mod is not an update from this source", sizeof (addon_catalogs[source].message));
		return false;
	}
	// Loaded PAKs may be open, and gameplay can write saves/configs during a download.
	if (exists)
		for (searchpath_t *search = com_searchpaths; search; search = search->next)
		{
			const char *filename = search->pack ? search->pack->filename : search->filename;
			size_t		len = strlen (path);
			if (!q_strncasecmp (filename, path, len) && (filename[len] == '/' || !filename[len]))
			{
				q_strlcpy (addon_catalogs[source].message, "Switch to another mod before updating", sizeof (addon_catalogs[source].message));
				return false;
			}
		}
	memset (&addon_job, 0, sizeof (addon_job));
	addon_job.install = true;
	addon_job.update = exists;
	addon_job.source = source;
	addon_job.addon = *addon;
	q_strlcpy (addon_job.addon.dir, dir, sizeof (addon_job.addon.dir));
	if (exists)
	{
		char version[64];
		Addons_InstalledInfo (dir, version, sizeof (version), addon_job.previous_revision);
		q_strlcpy (addon_job.destination, path, sizeof (addon_job.destination));
		q_strlcpy (addon_job.basedir, path, sizeof (addon_job.basedir));
		char *slash = strrchr (addon_job.basedir, '/');
		if (!slash)
			return false;
		*slash = 0;
	}
	else
	{
		q_strlcpy (addon_job.basedir, com_basedirs[com_numbasedirs - 1], sizeof (addon_job.basedir));
		if ((size_t)q_snprintf (addon_job.destination, sizeof (addon_job.destination), "%s/%s", addon_job.basedir, addon->dir) >=
			sizeof (addon_job.destination))
			return false;
	}
	addon_catalogs[source].message[0] = 0;
	Addons_StartJob ();
	return addon_job.thread != NULL;
}

void Addons_Cancel (void)
{
	Atomic_StoreUInt32 (&addon_job.cancel, 1);
}

void Addons_Poll (void)
{
	if (!addon_job.thread || !Atomic_LoadUInt32 (&addon_job.done))
		return;
	SDL_WaitThread (addon_job.thread, NULL);
	addon_job.thread = NULL;
	addon_catalog_t *catalog = &addon_catalogs[addon_job.source];
	qboolean		 cancelled = Atomic_LoadUInt32 (&addon_job.cancel) != 0;
	if (addon_job.success && !addon_job.install && !cancelled)
	{
		addon_job.success = Addons_Parse (catalog, addon_job.manifest ? addon_job.manifest : "", addon_job.source);
		if (!addon_job.success)
			Addons_Error ("Invalid mod catalogue");
	}
	q_strlcpy (
		catalog->message, cancelled && !(addon_job.success && addon_job.install) && !addon_job.recovery[0] ? "Download cancelled" : addon_job.error,
		sizeof (catalog->message));
	if (addon_job.manifest)
		Mem_Free (addon_job.manifest);
	addon_job.manifest = NULL;
	addon_revision++;
	if (addon_job.recovery[0])
		Con_Printf ("Unfinished mod installation retained at %s\n", addon_job.recovery);
	if (addon_job.success && addon_job.install)
	{
		Modlist_Init ();
		Con_Printf ("Installed %s\n", addon_job.addon.name);
	}
	else if (catalog->message[0])
		Con_Printf ("%s: %s\n", addon_names[addon_job.source], catalog->message);
	Addons_Preload ();
}

void Addons_Init (void)
{
	addon_disabled = COM_CheckParm ("-noaddons") != 0;
	for (int i = 0; i < ADDON_SOURCE_COUNT; i++)
	{
		int			p = COM_CheckParm (i ? "-q1tools" : "-addons");
		const char *url = p && p + 1 < com_argc ? com_argv[p + 1] : addon_servers[i];
		if (!Addons_URL (url) || strlen (url) >= sizeof (addon_urls[i]) - 32)
			url = addon_servers[i];
		q_strlcpy (addon_urls[i], url, sizeof (addon_urls[i]));
		size_t len = strlen (addon_urls[i]);
		while (len && addon_urls[i][len - 1] == '/')
			addon_urls[i][--len] = 0;
	}
	if (curl_global_init (CURL_GLOBAL_DEFAULT) != CURLE_OK)
		addon_disabled = true;
	Addons_Preload ();
}

void Addons_Shutdown (void)
{
	Addons_Cancel ();
	if (addon_job.thread)
		SDL_WaitThread (addon_job.thread, NULL);
	addon_job.thread = NULL;
	if (addon_job.manifest)
		Mem_Free (addon_job.manifest);
	addon_job.manifest = NULL;
	for (int i = 0; i < ADDON_SOURCE_COUNT; i++)
	{
		Addons_FreeCatalog (&addon_catalogs[i]);
	}
	curl_global_cleanup ();
}
