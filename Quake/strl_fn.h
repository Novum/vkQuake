/*	header file for BSD strlcat and strlcpy		*/

#ifndef __STRLFUNCS_H
#define __STRLFUNCS_H

/* use our own copies of strlcpy and strlcat taken from OpenBSD */
extern size_t q_strlcpy (char *dst, const char *src, size_t size);
extern size_t q_strlcat (char *dst, const char *src, size_t size);

#endif	/* __STRLFUNCS_H */

