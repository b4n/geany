/*
*   Copyright (c) 1998-2003, Darren Hiebert
*
*   This source code is released for free distribution under the terms of the
*   GNU General Public License version 2 or (at your option) any later version.
*
*   Provides the general (non-ctags-specific) environment assumed by all.
*/
#ifndef CTAGS_MAIN_GENERAL_H
#define CTAGS_MAIN_GENERAL_H

/*
*   INCLUDE FILES
*/
#if defined (HAVE_CONFIG_H)
# include <config.h>
#endif
#ifdef macintosh
# include "mac.h"
#endif

/* include unistd.h preventively because at least under MacOSX it is needed for off_t */
#include <unistd.h>

#include <stdbool.h>

/*
*   MACROS
*/

/*  This is a helpful internal feature of later versions (> 2.7) of GCC
 *  to prevent warnings about unused variables.
 */
#if (__GNUC__ > 2  ||  (__GNUC__ == 2  &&  __GNUC_MINOR__ >= 7)) && !(defined (__APPLE_CC__) || defined (__GNUG__))
# define CTAGS_ATTR_UNUSED __attribute__((unused))
# define CTAGS_ATTR_PRINTF(s,f)  __attribute__((format (printf, s, f)))
#else
# define CTAGS_ATTR_UNUSED
# define CTAGS_ATTR_PRINTF(s,f)
#endif


/*  MS-DOS doesn't allow manipulation of standard error, so we send it to
 *  stdout instead.
 */
#if defined (WIN32)
# define errout stdout
#else
# define errout stderr
#endif

#if defined (__CYGWIN__)
# define UNIX_PATH_SEPARATOR 1
# define MSDOS_STYLE_PATH
#endif

#if defined (WIN32)
# define CASE_INSENSITIVE_FILENAMES
# define MSDOS_STYLE_PATH
# define HAVE_DOS_H 1
# define HAVE_FCNTL_H 1
# define HAVE_IO_H 1
# define HAVE_STDLIB_H 1
# define HAVE_SYS_STAT_H 1
# define HAVE_SYS_TYPES_H 1
# define HAVE_TIME_H 1
# define HAVE_CLOCK 1
# define HAVE_CHSIZE 1
# define HAVE_FGETPOS 1
# define HAVE_STRICMP 1
# define HAVE_STRNICMP 1
# define HAVE_STRSTR 1
# define HAVE_STRERROR 1
# define HAVE_FINDNEXT 1
# ifdef _MSC_VER
#   define HAVE__FINDFIRST 1
#   define HAVE_DIRECT_H 1
# elif defined (__MINGW32__)
#  define HAVE_DIR_H 1
#  define HAVE_DIRENT_H 1
#  define HAVE__FINDFIRST 1
#  define ffblk _finddata_t
#  define FA_DIREC _A_SUBDIR
#  define ff_name name
# endif
/* provide the prototype for cross-compiling/Windows */
char *lrealpath(const char *filename);
#endif

#ifndef HAVE_FNMATCH_H
/* provide the prototype for cross-compiling/Windows */
int fnmatch(const char *pattern, const char *string, int flags);
#endif

#if defined (__MWERKS__) && defined (__MACINTOSH__)
# define HAVE_STAT_H 1
#endif


#ifdef __FreeBSD__
#include <sys/types.h>
#endif /* __FreeBSD__ */

/* Define regex if supported */
#if (defined (HAVE_REGCOMP) && !defined (REGCOMP_BROKEN)) || defined (HAVE_RE_COMPILE_PATTERN)
# define HAVE_REGEX 1
#endif


/* fake debug statement macro */
#define DebugStatement(x)      ;
#define PrintStatus(x)         ;
/* wrap g_warning so we don't include glib.h for all parsers, to keep compat with CTags */
void utils_warn(const char *msg);
#define Assert(x) if (!(x)) utils_warn("Assert(" #x ") failed!")
/*
*   DATA DECLARATIONS
*/

#if ! defined (HAVE_FGETPOS) && ! defined (fpos_t)
# define fpos_t long
#endif

/* Work-around for broken implementation of fgetpos()/fsetpos() on Mingw32 */
#if defined (__MINGW32__) && defined (__MSVCRT__)
# undef HAVE_FGETPOS
#endif

/*
*   FUNCTION PROTOTYPES
*/

#if defined (NEED_PROTO_REMOVE) && defined (HAVE_REMOVE)
extern int remove (const char *);
#endif

#if defined (NEED_PROTO_UNLINK) && ! defined (HAVE_REMOVE)
extern void *unlink (const char *);
#endif

#ifdef NEED_PROTO_GETENV
extern char *getenv (const char *);
#endif

#endif  /* CTAGS_MAIN_GENERAL_H */
