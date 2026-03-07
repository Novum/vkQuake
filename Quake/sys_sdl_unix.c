/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2005 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "arch_def.h"
#include "quakedef.h"

#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <execinfo.h>
#include <signal.h>
#ifdef PLATFORM_OSX
#include <libgen.h> /* dirname() and basename() */
#include <sys/sysctl.h>
#endif
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#ifdef DO_USERDIRS
#include <pwd.h>
#endif

#if defined(PLATFORM_UNIX) && !defined(PLATFORM_OSX) && !defined(PLATFORM_BSD) && !defined(TASK_AFFINITY_NOT_AVAILABLE)
#include <sched.h>
#include <pthread.h>
#endif

qboolean isDedicated;

static double counter_freq;

COMPILE_TIME_ASSERT (CHECK_LARGE_FILE_SUPPORT, sizeof (off_t) >= sizeof (qfileofs_t));

int Sys_fseek (FILE *file, qfileofs_t ofs, int origin)
{
	return fseeko (file, (off_t)ofs, origin);
}

qfileofs_t Sys_ftell (FILE *file)
{
	return (qfileofs_t)ftello (file);
}

int Sys_FileType (const char *path)
{
	/*
	if (access (path, R_OK) == -1)
		return 0;
	*/
	struct stat st;

	if (stat (path, &st) != 0)
		return FS_ENT_NONE;
	if (S_ISDIR (st.st_mode))
		return FS_ENT_DIRECTORY;
	if (S_ISREG (st.st_mode))
		return FS_ENT_FILE;

	return FS_ENT_NONE;
}

static char cwd[MAX_OSPATH];
#ifdef DO_USERDIRS
static char userdir[MAX_OSPATH];
#ifdef PLATFORM_OSX
#define SYS_USERDIR "Library/Application Support/vkQuake"
#else
#define SYS_USERDIR ".vkquake"
#endif

static qboolean Sys_GetUserdirArgs (int argc, char **argv, char *dst, size_t dstsize)
{
	int i = 1;
	for (; i < argc - 1; ++i)
	{
		if (strcmp (argv[i], "-userdir") == 0)
		{
			char	   *p = dst;
			const char *arg = argv[i + 1];
			const int	n = (int)strlen (arg);
			if (n < 1)
				Sys_Error ("Bad argument to -userdir");
			if (q_strlcpy (dst, arg, dstsize) >= dstsize)
				Sys_Error ("Insufficient array size for userspace directory");
			if (dst[n - 1] == '/')
				dst[n - 1] = 0;
			if (*p == '/')
				p++;
			for (; *p; p++)
			{
				const char c = *p;
				if (c == '/')
				{
					*p = 0;
					Sys_mkdir (dst);
					*p = c;
				}
			}
			return true;
		}
	}
	return false;
}

static void Sys_GetUserdir (int argc, char **argv, char *dst, size_t dstsize)
{
	size_t		   n;
	const char	  *home_dir = NULL;
	struct passwd *pwent;

	if (Sys_GetUserdirArgs (argc, argv, dst, dstsize))
		return;

	pwent = getpwuid (getuid ());
	if (pwent == NULL)
		perror ("getpwuid");
	else
		home_dir = pwent->pw_dir;
	if (home_dir == NULL)
		home_dir = getenv ("HOME");
	if (home_dir == NULL)
		Sys_Error ("Couldn't determine userspace directory");

	/* what would be a maximum path for a file in the user's directory...
	 * $HOME/SYS_USERDIR/game_dir/dirname1/dirname2/dirname3/filename.ext
	 * still fits in the MAX_OSPATH == 256 definition, but just in case :
	 */
	n = strlen (home_dir) + strlen (SYS_USERDIR) + 50;
	if (n >= dstsize)
		Sys_Error ("Insufficient array size for userspace directory");

	q_snprintf (dst, dstsize, "%s/%s", home_dir, SYS_USERDIR);
}
#endif /* DO_USERDIRS */

#ifdef PLATFORM_OSX
static char *OSX_StripAppBundle (char *dir)
{ /* based on the ioquake3 project at icculus.org. */
	static char osx_path[MAX_OSPATH];

	q_strlcpy (osx_path, dir, sizeof (osx_path));
	if (strcmp (basename (osx_path), "MacOS"))
		return dir;
	q_strlcpy (osx_path, dirname (osx_path), sizeof (osx_path));
	if (strcmp (basename (osx_path), "Contents"))
		return dir;
	q_strlcpy (osx_path, dirname (osx_path), sizeof (osx_path));
	if (!strstr (basename (osx_path), ".app"))
		return dir;
	q_strlcpy (osx_path, dirname (osx_path), sizeof (osx_path));
	return osx_path;
}

static void Sys_GetBasedir (char *argv0, char *dst, size_t dstsize)
{
	char *tmp;

	if (realpath (argv0, dst) == NULL)
	{
		perror ("realpath");
		if (getcwd (dst, dstsize - 1) == NULL)
		_fail:
			Sys_Error ("Couldn't determine current directory");
	}
	else
	{
		/* strip off the binary name */
		if (!(tmp = strdup (dst)))
			goto _fail;
		q_strlcpy (dst, dirname (tmp), dstsize);
		free (tmp);
	}

	tmp = OSX_StripAppBundle (dst);
	if (tmp != dst)
		q_strlcpy (dst, tmp, dstsize);
}
#else
static void Sys_GetBasedir (char *argv0, char *dst, size_t dstsize)
{
	char *tmp;

	if (getcwd (dst, dstsize - 1) == NULL)
		Sys_Error ("Couldn't determine current directory");

	tmp = dst;
	while (*tmp != 0)
		tmp++;
	while (*tmp == 0 && tmp != dst)
	{
		--tmp;
		if (tmp != dst && *tmp == '/')
			*tmp = 0;
	}
}
#endif

void Sys_Init (void)
{
	memset (cwd, 0, sizeof (cwd));
	Sys_GetBasedir (host_parms->argv[0], cwd, sizeof (cwd));
	host_parms->basedir = cwd;
#ifndef DO_USERDIRS
	host_parms->userdir = host_parms->basedir; /* code elsewhere relies on this ! */
#else
	memset (userdir, 0, sizeof (userdir));
	Sys_GetUserdir (host_parms->argc, host_parms->argv, userdir, sizeof (userdir));
	Sys_mkdir (userdir);
	host_parms->userdir = userdir;
#endif

	counter_freq = (double)SDL_GetPerformanceFrequency ();
}

void Sys_mkdir (const char *path)
{
	int rc = mkdir (path, 0777);
	if (rc != 0 && errno == EEXIST)
	{
		struct stat st;
		if (stat (path, &st) == 0 && S_ISDIR (st.st_mode))
			rc = 0;
	}
	if (rc != 0)
	{
		rc = errno;
		Sys_Error ("Unable to create directory %s: %s", path, strerror (rc));
	}
}

static const char errortxt1[] = "\nERROR-OUT BEGIN\n\n";
static const char errortxt2[] = "\nQUAKE ERROR: ";

void Sys_Error (const char *error, ...)
{
	va_list argptr;

	char *text = NULL;

	host_parms->errstate++;

	va_start (argptr, error);
	text = q_vstrcatf (text, error, argptr);
	va_end (argptr);

	Sys_DebugBreak ();

	if (!Sys_IsInDebugger ())
	{
		const char *captured_stack_trace = Sys_StackTrace ();

		text = q_strcatf (text, "\nSTACK TRACE:\n%s", captured_stack_trace);

		Mem_Free (captured_stack_trace);
	}

	PR_SwitchQCVM (NULL);

	fputs (errortxt1, stdout);
	Host_Shutdown ();
	fputs (errortxt2, stdout);

	Sys_Printf ("%s\n\n", text);

	if (!isDedicated && !Sys_IsInDebugger ())
		PL_ErrorDialog (text);

	Mem_Free (text);

	exit (1);
}

void Sys_Printf (const char *fmt, ...)
{
	va_list argptr;

	va_start (argptr, fmt);
	vprintf (fmt, argptr);
	va_end (argptr);
}

void Sys_Quit (void)
{
	Host_Shutdown ();

	exit (0);
}

double Sys_DoubleTime (void)
{
	return (double)SDL_GetPerformanceCounter () / counter_freq;
}

const char *Sys_ConsoleInput (void)
{
	static char	   con_text[256];
	static int	   textlen;
	char		   c;
	fd_set		   set;
	struct timeval timeout;

	FD_ZERO (&set);
	FD_SET (0, &set); // stdin
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	while (select (1, &set, NULL, NULL, &timeout))
	{
		if (read (0, &c, 1) != 1)
			return NULL;
		if (c == '\n' || c == '\r')
		{
			con_text[textlen] = '\0';
			textlen = 0;
			return con_text;
		}
		else if (c == 8)
		{
			if (textlen)
			{
				textlen--;
				con_text[textlen] = '\0';
			}
			continue;
		}
		con_text[textlen] = c;
		textlen++;
		if (textlen < (int)sizeof (con_text))
			con_text[textlen] = '\0';
		else
		{
			// buffer is full
			textlen = 0;
			con_text[0] = '\0';
			Sys_Printf ("\nConsole input too long!\n");
			break;
		}
	}

	return NULL;
}

void Sys_Sleep (unsigned long msecs)
{
	/*	usleep (msecs * 1000);*/
	SDL_Delay (msecs);
}

void Sys_SendKeyEvents (void)
{
	IN_Commands (); // ericw -- allow joysticks to add keys so they can be used to confirm SCR_ModalMessage
	IN_SendKeyEvents ();
}

bool Sys_PinCurrentThread (int core_index)
{
#if defined(PLATFORM_UNIX) && !defined(PLATFORM_OSX) && !defined(PLATFORM_BSD) && !defined(TASK_AFFINITY_NOT_AVAILABLE)
#pragma message("Info : Pinned tasks support for *Nix enabled using pthread_setaffinity_np()...")

	// valid for *Nix with GNU pthread extension pthread_setaffinity_np()
	//  which apparently is not available on OSX so skip it in that case.
	cpu_set_t cpuset;
	CPU_ZERO (&cpuset);
	CPU_SET (core_index, &cpuset);

	pthread_t current_thread = pthread_self ();
	if (pthread_setaffinity_np (current_thread, sizeof (cpu_set_t), &cpuset) != 0)
	{
		return false;
	}

	return true;

#endif
	return false;
}

const char *Sys_StackTrace (void)
{
#define MAX_STACK_FRAMES 24

	char *output_buffer = NULL;

	void *buffer[MAX_STACK_FRAMES];

	int nb_frames = backtrace (buffer, MAX_STACK_FRAMES);

#if defined(PLATFORM_OSX)
	// display on 1 line to pass to atos easily on MacOS:
	for (int frame_index = 0; frame_index < nb_frames; frame_index++)
	{
		output_buffer = q_strcatf (output_buffer, "0x%" PRIxPTR " ", (uintptr_t)buffer[frame_index]);

		if (frame_index == nb_frames - 1)
			output_buffer = q_strcatf (output_buffer, "\n");
	}
#endif
	// Then print 1 frame per line, together with its symbol using backtrace_symbols()
	char **symbols = backtrace_symbols (buffer, nb_frames);

	for (int frame_index = 0; frame_index < nb_frames; frame_index++)
	{
		output_buffer = q_strcatf (output_buffer, "%-2i: %s\n", frame_index, (symbols && symbols[frame_index]) ? symbols[frame_index] : "[no symbols]");
	}

	free (symbols);

	return output_buffer;

#undef MAX_STACK_FRAMES
}

bool Sys_IsInDebugger (void)
{
#if !defined(PLATFORM_OSX)
	FILE *f = fopen ("/proc/self/status", "r");
	if (!f)
		return false;

	char line[256];

	while (fgets (line, sizeof (line), f))
	{
		if (strncmp (line, "TracerPid:", 10) == 0)
		{
			int pid = atoi (line + 10);
			fclose (f);
			return pid != 0;
		}
	}
	fclose (f);
#else
	int				  mib[4];
	struct kinfo_proc info;
	size_t			  size = sizeof (info);
	info.kp_proc.p_flag = 0;

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PID;
	mib[3] = getpid ();

	if (sysctl (mib, 4, &info, &size, NULL, 0) == -1)
		return false;

	return (info.kp_proc.p_flag & P_TRACED) != 0;
#endif

	return false;
}

void Sys_DebugBreak (void)
{
	if (Sys_IsInDebugger ())
		raise (SIGTRAP);
}
