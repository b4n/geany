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

#include "parse.h"
#include "read.h"
#include "vstring.h"


enum {
	FUNCTION_KIND,
	VARIABLE_KIND
};

static kindOption M4Kinds[] = {
	{ TRUE, 'f', "function", "macros" },
	{ TRUE, 'v', "variable", "variables" }
};



static void makeM4Tag(unsigned int t, const vString *const name)
{
	tagEntryInfo e;

	if (vStringLength(name) <= 0)
		return;

	initTagEntry (&e, vStringValue(name));

	e.kindName = M4Kinds[t].name;
	e.kind = M4Kinds[t].letter;

	makeTagEntry(&e);
}

#define IS_WORD(c) (isalnum(c) || (c) == '_')

/* gets the close quote corresponding to openQuote.
 * return 0 if openQuote is not a valid open quote */
static int getCloseQuote(int openQuote)
{
	switch (openQuote)
	{
		case '[': return ']';
		case '`': return '\'';
		case '\'':
		case '"': return openQuote;

		default: return 0;
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

static void skipLine(void)
{
	int c;

	while ((c = fileGetc()) != EOF)
	{
		if (skipLineEnding(c))
			break;
	}
}

static boolean tokenMatches(const vString *const token, const char *name)
{
	return strcmp(vStringValue(token), name) == 0;
}

static void findM4Tags(void)
{
	vString *const name = vStringNew();
	vString *const token = vStringNew();
	int c;

	while ((c = fileGetc()) != EOF)
	{
		if (c == '#' || tokenMatches(token, "dnl"))
			skipLine();
		else if (c == '"')
			skipToCharacter(c, FALSE);
		else if (c == '\'' || c == '`')
			/* skipping on one line only is a compromise to support both m4-style `' and
			 * shell-syle `` quotes to support both raw m4 and Autoconf */
			skipToCharacter(getCloseQuote(c), TRUE);
		else if (c == '=') /* shell-style variables for Autoconf */
			makeM4Tag(VARIABLE_KIND, token);
		else if (c == '(')
		{
			if (tokenMatches(token, "define") ||
				tokenMatches(token, "m4_define") ||
				tokenMatches(token, "AC_DEFUN"))
			{
				skipBlanks();
				vStringClear(name);
				readQuotedWord(name);
				vStringTerminate(name);
				makeM4Tag(FUNCTION_KIND, name);
			}
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

extern parserDefinition* M4Parser (void)
{
	static const char *const patterns [] = { "*.m4", "*.ac", "configure.in", NULL };
	static const char *const extensions [] = { "m4", "ac", NULL };
	parserDefinition* const def = parserNew("M4");

	def->kinds = M4Kinds;
	def->kindCount = KIND_COUNT(M4Kinds);
	def->patterns = patterns;
	def->extensions = extensions;
	def->parser = findM4Tags;
	return def;
}
