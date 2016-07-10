/*
 * stupid test tool that reports the type sizes and
 * their alignment offsets in structures, and the byte
 * order as detected at runtime and compile time.
 */

/*
 * endianness stuff: <sys/types.h> is supposed
 * to succeed in locating the correct endian.h
 * this BSD style may not work everywhere.
 */

#undef ENDIAN_GUESSED_SAFE
#undef ENDIAN_ASSUMED_UNSAFE

#include <sys/types.h>

#include <stddef.h>
#include <limits.h>
#include <stdio.h>

/* include more if it didn't work: */
#if !defined(BYTE_ORDER)

# if defined(__linux__) || defined(__linux)
#	include <endian.h>
# elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#	include <machine/endian.h>
# elif defined(__sun) || defined(__svr4__)
#	include <sys/byteorder.h>
# elif defined(_AIX)
#	include <sys/machine.h>
# elif defined(sgi)
#	include <sys/endian.h>
# elif defined(__DJGPP__)
#	include <machine/endian.h>
# endif

#endif	/* endian includes */


#if defined(__BYTE_ORDER) && !defined(BYTE_ORDER)
#define	BYTE_ORDER	__BYTE_ORDER
#endif	/* __BYTE_ORDER */

#if !defined(PDP_ENDIAN)
#if defined(__PDP_ENDIAN)
#define	PDP_ENDIAN	__PDP_ENDIAN
#else
#define	PDP_ENDIAN	3412
#endif
#endif	/* NUXI endian (not supported) */

#if defined(__LITTLE_ENDIAN) && !defined(LITTLE_ENDIAN)
#define	LITTLE_ENDIAN	__LITTLE_ENDIAN
#endif	/* __LITTLE_ENDIAN */

#if defined(__BIG_ENDIAN) && !defined(BIG_ENDIAN)
#define	BIG_ENDIAN	__BIG_ENDIAN
#endif	/* __LITTLE_ENDIAN */


#if defined(BYTE_ORDER) && defined(LITTLE_ENDIAN) && defined(BIG_ENDIAN)

# if (BYTE_ORDER != LITTLE_ENDIAN) && (BYTE_ORDER != BIG_ENDIAN)
# error "Unsupported endianness."
# endif

#else	/* one of the definitions is mising. */

# undef BYTE_ORDER
# undef LITTLE_ENDIAN
# undef BIG_ENDIAN
# undef PDP_ENDIAN
# define LITTLE_ENDIAN	1234
# define BIG_ENDIAN	4321
# define PDP_ENDIAN	3412

#endif	/* byte order defs */


#if !defined(BYTE_ORDER)
/* supposedly safe assumptions: these may actually
 * be OS dependant and listing all possible compiler
 * macros here is impossible (the ones here are gcc
 * flags, mostly.) so, proceed carefully..
 */

# if defined(__DJGPP__) || defined(MSDOS) || defined(__MSDOS__)
#	define	BYTE_ORDER	LITTLE_ENDIAN	/* DOS */

# elif defined(__sun) || defined(__svr4__)	/* solaris */
#   if defined(_LITTLE_ENDIAN)	/* x86 */
#	define	BYTE_ORDER	LITTLE_ENDIAN
#   elif defined(_BIG_ENDIAN)	/* sparc */
#	define	BYTE_ORDER	BIG_ENDIAN
#   endif

# elif defined(__i386) || defined(__i386__) || defined(__386__) || defined(_M_IX86)
#	define	BYTE_ORDER	LITTLE_ENDIAN	/* any x86 */

# elif defined(__amd64) || defined(__x86_64__) || defined(_M_X64)
#	define	BYTE_ORDER	LITTLE_ENDIAN	/* any x64 */

# elif defined(_M_IA64)
#	define	BYTE_ORDER	LITTLE_ENDIAN	/* ia64 / Visual C */

# elif defined (__ppc__) || defined(__POWERPC__) || defined(_M_PPC)
#	define	BYTE_ORDER	BIG_ENDIAN	/* PPC: big endian */

# elif (defined(__alpha__) || defined(__alpha)) || defined(_M_ALPHA)
#	define	BYTE_ORDER	LITTLE_ENDIAN	/* should be safe */

# elif defined(_WIN32) || defined(_WIN64)	/* windows : */
#	define	BYTE_ORDER	LITTLE_ENDIAN	/* should be safe */

# elif defined(__hppa) || defined(__hppa__) || defined(__sparc) || defined(__sparc__)	/* others: check! */
#	define	BYTE_ORDER	BIG_ENDIAN

# endif

# if defined(BYTE_ORDER)
  /* raise a flag, just in case: */
#	define	ENDIAN_GUESSED_SAFE	BYTE_ORDER
# endif

#endif	/* supposedly safe assumptions */


#if !defined(BYTE_ORDER)

/* brain-dead fallback: default to little endian.
 * change if necessary!!!!
 */
# define BYTE_ORDER	LITTLE_ENDIAN
# define ENDIAN_ASSUMED_UNSAFE		BYTE_ORDER

#endif	/* fallback. */

#if defined(ENDIAN_ASSUMED_UNSAFE)
/*
# if (ENDIAN_ASSUMED_UNSAFE == LITTLE_ENDIAN)
#    warning "Cannot determine endianess. Using LIL endian as an UNSAFE default"
# elif (ENDIAN_ASSUMED_UNSAFE == PDP_ENDIAN)
#    warning "Cannot determine endianess. Using PDP (NUXI) as an UNSAFE default."
# elif (ENDIAN_ASSUMED_UNSAFE == BIG_ENDIAN)
#    warning "Cannot determine endianess. Using BIG endian as an UNSAFE default."
# endif
*/
#endif	/* ENDIAN_ASSUMED_UNSAFE */

#define	COMPILED_BYTEORDER	BYTE_ORDER

#include <stdio.h>
#include <stdlib.h>

int DetectByteorder (void)
{
	int	i = 0x12345678;
		/*    U N I X */

	/*
	BE_ORDER:  12 34 56 78
		   U  N  I  X

	LE_ORDER:  78 56 34 12
		   X  I  N  U

	PDP_ORDER: 34 12 78 56
		   N  U  X  I
	*/

	if ( *(char *)&i == 0x12 )
		return BIG_ENDIAN;
	else if ( *(char *)&i == 0x78 )
		return LITTLE_ENDIAN;
	else if ( *(char *)&i == 0x34 )
		return PDP_ENDIAN;

	return -1;
}

struct align_test_char		{ char dummy; char test; };
struct align_test_short		{ char dummy; short test; };
struct align_test_int		{ char dummy; int test; };
struct align_test_long		{ char dummy; long test; };
struct align_test_longlong	{ char dummy; long long test; };
struct align_test_float		{ char dummy; float test; };
struct align_test_double	{ char dummy; double test; };
struct align_test_longdouble	{ char dummy; long double test;};
struct align_test_voidptr	{ char dummy; void *test; };

int main (void)
{
	int	tmp = ((char) -1);

	printf ("char is signed type : %s - char is %s\n", (tmp < 0) ? "yes" : "no", (tmp < 0) ? "SIGNED" : "UNSIGNED");
	printf ("Type sizes and alignment within structures:\n");
	printf ("char       : %d, packing offset: %d\n", (int) sizeof(char),	 (int) ((size_t) &((struct align_test_char *)0)->test));
	printf ("short      : %d, packing offset: %d\n", (int) sizeof(short),	 (int) ((size_t) &((struct align_test_short *)0)->test));
	printf ("int        : %d, packing offset: %d\n", (int) sizeof(int),	 (int) ((size_t) &((struct align_test_int *)0)->test));
	printf ("long       : %d, packing offset: %d\n", (int) sizeof(long),	 (int) ((size_t) &((struct align_test_long *)0)->test));
	printf ("long long  : %d, packing offset: %d\n", (int) sizeof(long long),(int) ((size_t) &((struct align_test_longlong *)0)->test));
	printf ("void *ptr  : %d, packing offset: %d\n", (int) sizeof(void *),	 (int) ((size_t) &((struct align_test_voidptr *)0)->test));
	printf ("float      : %d, packing offset: %d\n", (int) sizeof(float),	 (int) ((size_t) &((struct align_test_float *)0)->test));
	printf ("double     : %d, packing offset: %d\n", (int) sizeof(double),	 (int) ((size_t) &((struct align_test_double *)0)->test));
	printf ("long double: %d, packing offset: %d\n", (int) sizeof(long double),(int)((size_t)&((struct align_test_longdouble *)0)->test));

	printf ("ENDIANNESS (BYTE ORDER):\n");

	tmp = DetectByteorder();
	printf ("Runtime detection     : ");
	switch (tmp)
	{
	case BIG_ENDIAN:
		printf ("Big Endian");
		break;
	case LITTLE_ENDIAN:
		printf ("Little Endian");
		break;
	case PDP_ENDIAN:
		printf ("PDP (NUXI) Endian");
		break;
	default:
		printf ("Unknown Endian");
		break;
	}
	printf ("\n");

	tmp = COMPILED_BYTEORDER;
	printf ("Compile time detection: ");
	switch (tmp)
	{
	case BIG_ENDIAN:
		printf ("Big Endian");
		break;
	case LITTLE_ENDIAN:
		printf ("Little Endian");
		break;
	case PDP_ENDIAN:
		printf ("PDP (NUXI) Endian");
		break;
	default:
		printf ("Unknown Endian");
		break;
	}
#if defined(ENDIAN_GUESSED_SAFE)
	printf (" (Safe guess)");
#elif defined(ENDIAN_ASSUMED_UNSAFE)
	printf (" (Unsafe assumption)");
#endif
	printf ("\n");

	return 0;
}

