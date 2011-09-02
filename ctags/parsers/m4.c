/*
 *   Copyright (c) 2011, Colomban Wendling
 *
 *   This source code is released for free distribution under the terms of the
 *   GNU General Public License.
 *
 *   This module contains functions for generating tags for M4 and Autoconf files.
 */

#include "general.h"	/* must always come first */

#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "parse.h"
#include "read.h"
#include "vstring.h"



static void setQuotes(char openQuote, char closeQuote);


enum {
	MACRO_KIND,
	VARIABLE_KIND
};

static kindOption M4Kinds[] = {
	{ TRUE, 'd', "macro", "macros" },
	{ TRUE, 'v', "variable", "variables" }
};

/* "language" selection */

enum {
	LANG_M4,
	LANG_AC
};

static int Current_lang = 0;

#define isLang(l)	((l) == Current_lang)

static void setLang(int l)
{
	Current_lang = l;

	/*fprintf(stderr, "lang set to %d\n", Current_lang);*/
	if (isLang(LANG_AC))
		setQuotes('[', ']');
	else
		setQuotes('`', '\'');
}


/* tag creation */

static void makeM4Tag(unsigned int t, const vString *const name)
{
	tagEntryInfo e;

	if (vStringLength(name) <= 0)
		return;

	initTagEntry (&e, vStringValue(name));

	e.kindName = M4Kinds[t].name;
	e.kind = M4Kinds[t].letter;

	/*fprintf(stderr, "making tag `%s' of type %c\n", e.name, e.kind);*/
	makeTagEntry(&e);
}


/* parser */

#define IS_WORD(c) (isalnum(c) || (c) == '_')

static char Quote_open = 0;
static char Quote_close = 0;

static void setQuotes(char openQuote, char closeQuote)
{
	Quote_open = openQuote;
	Quote_close = closeQuote;
	/*fprintf(stderr, "quotes set to: %c %c\n", Quote_open, Quote_close);*/
}

/* gets the close quote corresponding to openQuote.
 * return 0 if openQuote is not a valid open quote */
static int getCloseQuote(int openQuote)
{
	if (openQuote == Quote_open)
		return Quote_close;
	return 0;

	switch (openQuote)
	{
		case '[': return ']';
		case '`': return '\'';
		case '\'':
		case '"': return openQuote;

		default: return 0;
	}
}

static void skipQuotes(int c)
{
	unsigned int depth = 0;
	int openQuote = 0, closeQuote = 0;

	closeQuote = getCloseQuote(c);
	if (! closeQuote)
		return;
	else
		openQuote = c;

	for (; c != EOF; c = fileGetc())
	{
		if (c == closeQuote)
			depth --;
		else if (c == openQuote)
			depth ++;
		if (depth == 0)
			break;
	}
}

/* reads a possibly quoted word.  word characters are those passing IS_WORD() */
static void readQuotedWord(vString *const name)
{
	unsigned int depth = 0;
	int openQuote = 0, closeQuote = 0;
	int c = fileGetc();

	closeQuote = getCloseQuote(c);
	if (closeQuote != 0)
	{
		openQuote = c;
		depth ++;
		c = fileGetc();
	}

	for (; c != EOF; c = fileGetc())
	{
		/* don't allow embedded NULs, and prevents to match when quote == 0 (aka none) */
		if (c == 0)
			break;
		/* close before open to support open and close characters to be the same */
		else if (c == closeQuote)
			depth --;
		else if (c == openQuote)
			depth ++;
		else if (IS_WORD(c) || depth > 0)
			vStringPut(name, c);
		else
		{
			fileUngetc(c);
			break;
		}
	}
}

static void skipBlanks(void)
{
	int c;

	while ((c = fileGetc()) != EOF)
	{
		if (! isspace(c))
		{
			fileUngetc(c);
			break;
		}
	}
}

static boolean skipLineEnding(int c)
{
	if (c == '\n')
		return TRUE;
	else if (c == '\r')
	{
		/* try to eat the `\n' of a `\r\n' sequence */
		c = fileGetc();
		if (c != '\n')
			fileUngetc(c);
		return TRUE;
	}

	return FALSE;
}

static void skipToCharacter(int ch, boolean oneLine)
{
	int c;

	while ((c = fileGetc()) != EOF)
	{
		if (c == ch)
			break;
		else if (oneLine && skipLineEnding(c))
			break;
	}
}

static void skipLine(int c)
{
	for (; c != EOF; c = fileGetc())
	{
		if (skipLineEnding(c))
			break;
	}
}

static boolean tokenMatches(const vString *const token, const char *name)
{
	return strcmp(vStringValue(token), name) == 0;
}

static boolean tokenStartMatches(const vString *const token, const char *start)
{
	const char *name = vStringValue(token);

	while (*start && *name == *start)
	{
		name++;
		start++;
	}

	return *start == 0;
}

/* reads everything in a macro argument
 * return TRUE if there are more args, FALSE otherwise */
static boolean readMacroArgument(vString *const arg)
{
	int c;

	/* discard leading blanks */
	while ((c = fileGetc()) != EOF && isspace(c))
		;

	for (; c != EOF; c = fileGetc())
	{
		if (c == ',' || c == ')')
		{
			fileUngetc(c);
			return c == ',';
		}
		else if (getCloseQuote(c) != 0)
		{
			fileUngetc(c);
			readQuotedWord(arg);
		}
		else
			vStringPut(arg, c);
	}

	return FALSE;
}

static void handleChangequote(void)
{
	vString *const arg = vStringNew();
	char args[2] = {0,0};
	int i, n = (sizeof(args) / sizeof(args[0]));
	boolean more = TRUE;

	for (i = 0; more && i < n; i++)
	{
		const char *v;

		vStringClear(arg);
		more = readMacroArgument(arg);
		if (more)
			fileGetc();
		vStringTerminate(arg);
		v = vStringValue(arg);
		fprintf(stderr, "got arg: %s\n", v);
		if (! v[0] || v[1])
			break;
		else
			args[i] = *v;
	}

	if (! more && args[0] && args[1])
		setQuotes(args[0], args[1]);

	vStringDelete(arg);
}

static void findTags(void)
{
	vString *const name = vStringNew();
	vString *const token = vStringNew();
	int c;

	while ((c = fileGetc()) != EOF)
	{
		if (c == '#' || tokenMatches(token, "dnl"))
			skipLine(c);
		else if (c == Quote_open)
			skipQuotes(c);
		else if (isLang(LANG_AC) && (c == '"' || c == '"' || c == '`')) /* AutoConf quotes */
			skipToCharacter(c, FALSE);
#if 0
		else if (isLang(LANG_AC) && c == '=') /* shell-style variables for Autoconf */
			makeM4Tag(VARIABLE_KIND, token);
#endif
		else if (c == '(' && vStringLength(token) > 0) /* catch a few macro calls */
		{
			/* assume AC/AM/AS prefixes means we're in an AutoConf file */
			if (tokenStartMatches(token, "AC_") ||
				tokenStartMatches(token, "AM_") ||
				tokenStartMatches(token, "AS_"))
				setLang(LANG_AC);

			if (tokenMatches(token, "define") ||
				tokenMatches(token, "m4_define") ||
				tokenMatches(token, "m4_defun") ||
				tokenMatches(token, "AC_DEFUN") ||
				tokenMatches(token, "AU_ALIAS"))
			{
				skipBlanks();
				vStringClear(name);
				readMacroArgument(name);
				vStringTerminate(name);
				makeM4Tag(MACRO_KIND, name);
			}
			else if (tokenMatches(token, "changequote") ||
					 tokenMatches(token, "m4_changequote"))
				handleChangequote();
		}

		vStringClear(token);
		if (IS_WORD(c))
		{
			fileUngetc(c);
			readQuotedWord(token);
			vStringTerminate(token);
		}
	}
	vStringDelete(token);
	vStringDelete(name);
}

static void findM4Tags(void)
{
	setLang(LANG_M4);
	findTags();
}

static void findAutoConfTags(void)
{
	setLang(LANG_AC);
	findTags();
}

extern parserDefinition* M4Parser (void)
{
	static const char *const patterns [] = { "*.m4", NULL };
	static const char *const extensions [] = { "m4", NULL };
	parserDefinition* const def = parserNew("M4");

	def->kinds = M4Kinds;
	def->kindCount = KIND_COUNT(M4Kinds);
	def->patterns = patterns;
	def->extensions = extensions;
	def->parser = findM4Tags;
	return def;
}

extern parserDefinition* AutoConfParser (void)
{
	static const char *const patterns [] = { "*.ac", "configure.in", NULL };
	static const char *const extensions [] = { "ac", NULL };
	parserDefinition* const def = parserNew("AutoConf");

	def->kinds = M4Kinds;
	def->kindCount = KIND_COUNT(M4Kinds);
	def->patterns = patterns;
	def->extensions = extensions;
	def->parser = findAutoConfTags;
	return def;
}
