/*
    mdfour.c

    An implementation of MD4 designed for use in the samba SMB
    authentication protocol

    Copyright (C) 1997-1998  Andrew Tridgell

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

    See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to:

        Free Software Foundation, Inc.
        59 Temple Place - Suite 330
        Boston, MA  02111-1307, USA

    $Id: mdfour.c 2557 2007-08-01 14:55:28Z acceptthis $
*/

#include <string.h> /* XoXus: needed for memset call */

#ifndef _MDFOUR_H
#define _MDFOUR_H

#ifndef int32
#define int32 int
#endif

#if SIZEOF_INT > 4
#define LARGE_INT32
#endif

#ifndef uint32
#define uint32 unsigned int32
#endif

struct mdfour
{
	uint32 A, B, C, D;
	uint32 totalN;
};

static void mdfour_begin (struct mdfour *md);                            // old: MD4Init
static void mdfour_update (struct mdfour *md, unsigned char *in, int n); // old: MD4Update
static void mdfour_result (struct mdfour *md, unsigned char *out);       // old: MD4Final
static void mdfour (unsigned char *out, unsigned char *in, int n);

#endif // _MDFOUR_H

/* NOTE: This code makes no attempt to be fast!

   It assumes that a int is at least 32 bits long
*/

#define F(X, Y, Z) (((X) & (Y)) | ((~(X)) & (Z)))
#define G(X, Y, Z) (((X) & (Y)) | ((X) & (Z)) | ((Y) & (Z)))
#define H(X, Y, Z) ((X) ^ (Y) ^ (Z))
#ifdef LARGE_INT32
#define lshift(x, s) ((((x) << (s)) & 0xFFFFFFFF) | (((x) >> (32 - (s))) & 0xFFFFFFFF))
#else
#define lshift(x, s) (((x) << (s)) | ((x) >> (32 - (s))))
#endif

#define ROUND1(a, b, c, d, k, s) a = lshift (a + F (b, c, d) + X[k], s)
#define ROUND2(a, b, c, d, k, s) a = lshift (a + G (b, c, d) + X[k] + 0x5A827999, s)
#define ROUND3(a, b, c, d, k, s) a = lshift (a + H (b, c, d) + X[k] + 0x6ED9EBA1, s)

/* this applies md4 to 64 byte chunks */
static void mdfour64 (struct mdfour *m, uint32 *M)
{
	int    j;
	uint32 AA, BB, CC, DD;
	uint32 X[16];
	uint32 A, B, C, D;

	for (j = 0; j < 16; j++)
		X[j] = M[j];

	A = m->A;
	B = m->B;
	C = m->C;
	D = m->D;
	AA = A;
	BB = B;
	CC = C;
	DD = D;

	ROUND1 (A, B, C, D, 0, 3);
	ROUND1 (D, A, B, C, 1, 7);
	ROUND1 (C, D, A, B, 2, 11);
	ROUND1 (B, C, D, A, 3, 19);
	ROUND1 (A, B, C, D, 4, 3);
	ROUND1 (D, A, B, C, 5, 7);
	ROUND1 (C, D, A, B, 6, 11);
	ROUND1 (B, C, D, A, 7, 19);
	ROUND1 (A, B, C, D, 8, 3);
	ROUND1 (D, A, B, C, 9, 7);
	ROUND1 (C, D, A, B, 10, 11);
	ROUND1 (B, C, D, A, 11, 19);
	ROUND1 (A, B, C, D, 12, 3);
	ROUND1 (D, A, B, C, 13, 7);
	ROUND1 (C, D, A, B, 14, 11);
	ROUND1 (B, C, D, A, 15, 19);

	ROUND2 (A, B, C, D, 0, 3);
	ROUND2 (D, A, B, C, 4, 5);
	ROUND2 (C, D, A, B, 8, 9);
	ROUND2 (B, C, D, A, 12, 13);
	ROUND2 (A, B, C, D, 1, 3);
	ROUND2 (D, A, B, C, 5, 5);
	ROUND2 (C, D, A, B, 9, 9);
	ROUND2 (B, C, D, A, 13, 13);
	ROUND2 (A, B, C, D, 2, 3);
	ROUND2 (D, A, B, C, 6, 5);
	ROUND2 (C, D, A, B, 10, 9);
	ROUND2 (B, C, D, A, 14, 13);
	ROUND2 (A, B, C, D, 3, 3);
	ROUND2 (D, A, B, C, 7, 5);
	ROUND2 (C, D, A, B, 11, 9);
	ROUND2 (B, C, D, A, 15, 13);

	ROUND3 (A, B, C, D, 0, 3);
	ROUND3 (D, A, B, C, 8, 9);
	ROUND3 (C, D, A, B, 4, 11);
	ROUND3 (B, C, D, A, 12, 15);
	ROUND3 (A, B, C, D, 2, 3);
	ROUND3 (D, A, B, C, 10, 9);
	ROUND3 (C, D, A, B, 6, 11);
	ROUND3 (B, C, D, A, 14, 15);
	ROUND3 (A, B, C, D, 1, 3);
	ROUND3 (D, A, B, C, 9, 9);
	ROUND3 (C, D, A, B, 5, 11);
	ROUND3 (B, C, D, A, 13, 15);
	ROUND3 (A, B, C, D, 3, 3);
	ROUND3 (D, A, B, C, 11, 9);
	ROUND3 (C, D, A, B, 7, 11);
	ROUND3 (B, C, D, A, 15, 15);

	A += AA;
	B += BB;
	C += CC;
	D += DD;

#ifdef LARGE_INT32
	A &= 0xFFFFFFFF;
	B &= 0xFFFFFFFF;
	C &= 0xFFFFFFFF;
	D &= 0xFFFFFFFF;
#endif

	for (j = 0; j < 16; j++)
		X[j] = 0;

	m->A = A;
	m->B = B;
	m->C = C;
	m->D = D;
}

static void copy64 (uint32 *M, unsigned char *in)
{
	int i;

	for (i = 0; i < 16; i++)
		M[i] = ((uint32)in[i * 4 + 3] << 24) | ((uint32)in[i * 4 + 2] << 16) | ((uint32)in[i * 4 + 1] << 8) | ((uint32)in[i * 4 + 0] << 0);
}

static void copy4 (unsigned char *out, uint32 x)
{
	out[0] = x & 0xFF;
	out[1] = (x >> 8) & 0xFF;
	out[2] = (x >> 16) & 0xFF;
	out[3] = (x >> 24) & 0xFF;
}

static void mdfour_begin (struct mdfour *md)
{
	md->A = 0x67452301;
	md->B = 0xefcdab89;
	md->C = 0x98badcfe;
	md->D = 0x10325476;
	md->totalN = 0;
}

static void mdfour_tail (struct mdfour *m, unsigned char *in, int n)
{
	unsigned char buf[128];
	uint32        M[16];
	uint32        b;

	m->totalN += n;

	b = m->totalN * 8;

	memset (buf, 0, 128);
	if (n)
		memcpy (buf, in, n);
	buf[n] = 0x80;

	if (n <= 55)
	{
		copy4 (buf + 56, b);
		copy64 (M, buf);
		mdfour64 (m, M);
	}
	else
	{
		copy4 (buf + 120, b);
		copy64 (M, buf);
		mdfour64 (m, M);
		copy64 (M, buf + 64);
		mdfour64 (m, M);
	}
}

static void mdfour_update (struct mdfour *m, unsigned char *in, int n)
{
	uint32 M[16];

	//	if (n == 0) mdfour_tail(in, n); //Spike: This is where the bug was.

	while (n >= 64)
	{
		copy64 (M, in);
		mdfour64 (m, M);
		in += 64;
		n -= 64;
		m->totalN += 64;
	}

	mdfour_tail (m, in, n);
}

static void mdfour_result (struct mdfour *m, unsigned char *out)
{
	copy4 (out, m->A);
	copy4 (out + 4, m->B);
	copy4 (out + 8, m->C);
	copy4 (out + 12, m->D);
}

static void mdfour (unsigned char *out, unsigned char *in, int n)
{
	struct mdfour md;
	mdfour_begin (&md);
	mdfour_update (&md, in, n);
	mdfour_result (&md, out);
}

///////////////////////////////////////////////////////////////
//	MD4-based checksum utility functions
//
//	Copyright (C) 2000       Jeff Teunissen <d2deek@pmail.net>
//
//	Author: Jeff Teunissen	<d2deek@pmail.net>
//	Date: 01 Jan 2000

unsigned Com_BlockChecksum (void *buffer, int length)
{
	int      digest[4];
	unsigned val;

	mdfour ((unsigned char *)digest, (unsigned char *)buffer, length);

	val = digest[0] ^ digest[1] ^ digest[2] ^ digest[3];

	return val;
}

void Com_BlockFullChecksum (void *buffer, int len, unsigned char *outbuf)
{
	mdfour (outbuf, (unsigned char *)buffer, len);
}
