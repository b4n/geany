/* Scintilla source code edit control */
/* ScintillaGTKAccessible.c - GTK+ accessibility for ScintillaGTK */
/* Copyright 2016 by Colomban Wendling <colomban@geany.org>
 * The License.txt file describes the conditions under which this software may be distributed. */

// On GTK < 3.2, we need to use the AtkObjectFactory.  We need to query
// the factory to see what type we should derive from, thus making use of
// dynamic inheritance.  It's tricky, but it works so long as it's done
// carefully enough.
//
// On GTK 3.2 through 3.6, we need to hack around because GTK stopped
// registering its accessible types in the factory, so we can't query
// them that way.  Unfortunately, the accessible types aren't exposed
// yet (not until 3.8), so there's no proper way to know which type to
// inherit from.  To work around this, we instantiate the parent's
// AtkObject temporarily, and use it's type.  It means creating an extra
// throwaway object and being able to pass the type information up to the
// type registration code, but it's the only solution I could find.
//
// On GTK 3.8 onward, we use the proper exposed GtkContainerAccessible as
// parent, and so a straightforward class.
//
// To hide and contain the complexity in type creation arising from the
// hackish support for GTK 3.2 to 3.8, the actual implementation for the
// widget's get_accessible() is located in the accessibility layer itself.

// Initially based on GtkTextViewAccessible from GTK 3.20
// Inspiration for the GTK < 3.2 part comes from Evince 2.24, thanks.

// FIXME: optimize character/byte offset conversion (with a cache?)

#include <sys/types.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

// whether we have widget_set() and widget_unset()
#define HAVE_WIDGET_SET_UNSET (GTK_CHECK_VERSION(3, 3, 6))
// whether GTK accessibility is available through the ATK factory
#define HAVE_GTK_FACTORY (! GTK_CHECK_VERSION(3, 1, 9))
// whether we have gtk-a11y.h and the public GTK accessible types
#define HAVE_GTK_A11Y_H (GTK_CHECK_VERSION(3, 7, 6))

#if HAVE_GTK_A11Y_H
# include <gtk/gtk-a11y.h>
#endif

#define GTK

// ScintillaGTK.h and stuff it needs
#include <stdexcept>
#include <new>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include "Platform.h"

#include "ILexer.h"
#include "Scintilla.h"
#include "ScintillaWidget.h"
#ifdef SCI_LEXER
#include "SciLexer.h"
#endif
#include "StringCopy.h"
#ifdef SCI_LEXER
#include "LexerModule.h"
#endif
#include "Position.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "CallTip.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "XPM.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "CaseConvert.h"
#include "UniConversion.h"
#include "UnicodeFromUTF8.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "AutoComplete.h"
#include "ScintillaBase.h"

#include "Scintilla.h"
#include "ScintillaWidget.h"
#include "ScintillaGTK.h"
#include "ScintillaGTKAccessible.h"

class ScintillaGTKAccessible;

struct ScintillaObjectAccessiblePrivate {
	ScintillaGTKAccessible *pscin;
};

typedef GtkAccessible ScintillaObjectAccessible;
typedef GtkAccessibleClass ScintillaObjectAccessibleClass;

#define SCINTILLA_OBJECT_ACCESSIBLE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), SCINTILLA_TYPE_OBJECT_ACCESSIBLE, ScintillaObjectAccessible))
#define SCINTILLA_TYPE_OBJECT_ACCESSIBLE (scintilla_object_accessible_get_type(0))

// We can't use priv member because of dynamic inheritance, so we don't actually know the offset.  Meh.
#define SCINTILLA_OBJECT_ACCESSIBLE_GET_PRIVATE(inst) (G_TYPE_INSTANCE_GET_PRIVATE((inst), SCINTILLA_TYPE_OBJECT_ACCESSIBLE, ScintillaObjectAccessiblePrivate))

static GType scintilla_object_accessible_get_type(GType parent_type);

#ifdef SCI_NAMESPACE
using namespace Scintilla;
#endif

class ScintillaGTKAccessible {
private:
	GtkAccessible *accessible;
	ScintillaGTK *sci;

	// local state for comparing
	Document *old_doc = nullptr;
	bool old_readonly = false;
	Position old_pos = -1;
	std::vector<SelectionRange> old_sels;

	void UpdateCursor();
	void ChangeDocument(Document *new_doc);
	void Notify(GtkWidget *widget, gint code, SCNotification *nt);
	static void SciNotify(GtkWidget *widget, gint code, SCNotification *nt, gpointer data) {
		try {
			reinterpret_cast<ScintillaGTKAccessible*>(data)->Notify(widget, code, nt);
		} catch (...) {}
	}

	static ScintillaGTKAccessible *ThisFromAccessible(GtkAccessible *accessible) {
		// FIXME: do we need the check below?  GTK checks that in all methods, so maybe
		GtkWidget *widget = gtk_accessible_get_widget(accessible);
		if (! widget) {
			throw std::runtime_error("Defunct accessible object");
		}

		ScintillaGTKAccessible *self = SCINTILLA_OBJECT_ACCESSIBLE_GET_PRIVATE(accessible)->pscin;
		if (! self)
			throw std::runtime_error("Invalid accessible object");

		return self;
	}

	Position ByteOffsetFromCharacterOffset(Position startByte, int characterOffset) {
		Position pos = sci->pdoc->GetRelativePosition(startByte, characterOffset);
		if (pos == INVALID_POSITION) {
			// clamp invalid positions inside the document
			if (characterOffset > 0) {
				return sci->pdoc->Length();
			} else {
				return 0;
			}
		}
		return pos;
	}

	Position ByteOffsetFromCharacterOffset(int characterOffset) {
		return ByteOffsetFromCharacterOffset(0, characterOffset);
	}

	int CharacterOffsetFromByteOffset(Position byteOffset) {
		return sci->pdoc->CountCharacters(0, byteOffset);
	}

	void CharacterRangeFromByteRange(Position startByte, Position endByte, int *startChar, int *endChar) {
		*startChar = CharacterOffsetFromByteOffset(startByte);
		*endChar = *startChar + sci->pdoc->CountCharacters(startByte, endByte);
	}

	void ByteRangeFromCharacterRange(int startChar, int endChar, Position& startByte, Position& endByte) {
		startByte = ByteOffsetFromCharacterOffset(startChar);
		endByte = ByteOffsetFromCharacterOffset(startByte, endChar - startChar);
	}

	Position PositionBefore(Position pos) {
		return sci->pdoc->MovePositionOutsideChar(pos - 1, -1, true);
	}

	Position PositionAfter(Position pos) {
		return sci->pdoc->MovePositionOutsideChar(pos + 1, 1, true);
	}

	gchar *GetTextRangeUTF8(Position startByte, Position endByte);
	gchar *GetText(int startChar, int endChar);
	gchar *GetTextAfterOffset(int charOffset, AtkTextBoundary boundaryType, int *startChar, int *endChar);
	gchar *GetTextBeforeOffset(int charOffset, AtkTextBoundary boundaryType, int *startChar, int *endChar);
	gchar *GetTextAtOffset(int charOffset, AtkTextBoundary boundaryType, int *startChar, int *endChar);
	gchar *GetStringAtOffset(int charOffset, AtkTextGranularity granularity, int *startChar, int *endChar);
	gunichar GetCharacterAtOffset(int charOffset);
	gint GetCharacterCount();
	gint GetCaretOffset();
	gboolean SetCaretOffset(int charOffset);
	gint GetOffsetAtPoint(gint x, gint y, AtkCoordType coords);
	void GetCharacterExtents(int charOffset, gint *x, gint *y, gint *width, gint *height, AtkCoordType coords);
	AtkAttributeSet *GetAttributesForStyle(unsigned int style);
	AtkAttributeSet *GetRunAttributes(int charOffset, int *startChar, int *endChar);
	AtkAttributeSet *GetDefaultAttributes();
	gint GetNSelections();
	gchar *GetSelection(gint selection_num, int *startChar, int *endChar);
	gboolean AddSelection(int startChar, int endChar);
	gboolean RemoveSelection(int selection_num);
	gboolean SetSelection(gint selection_num, int startChar, int endChar);

	/* atkeditabletext.h */
	void SetTextContents(const gchar *contents);
	void InsertText(const gchar *contents, int lengthBytes, int *charPosition);
	void CopyText(int startChar, int endChar);
	void CutText(int startChar, int endChar);
	void DeleteText(int startChar, int endChar);
	void PasteText(int charPosition);

public:
	ScintillaGTKAccessible(GtkAccessible *accessible, GtkWidget *widget);
	~ScintillaGTKAccessible();

	// ATK methods

	// wraps a call from the accessible object to the ScintillaGTKAccessible, and avoid leaking any exception
	#define WRAPPER_METHOD_BODY(accessible, call, defret) \
		try { \
			return ThisFromAccessible(reinterpret_cast<GtkAccessible*>(accessible))->call; \
		} catch (...) { \
			return defret; \
		}

	class AtkTextIface {
	public:
		static void init(::AtkTextIface *iface);

	private:
		AtkTextIface();

		static gchar *GetText(AtkText *text, int start_offset, int end_offset) {
			WRAPPER_METHOD_BODY(text, GetText(start_offset, end_offset), NULL);
		}
		static gchar *GetTextAfterOffset(AtkText *text, int offset, AtkTextBoundary boundary_type, int *start_offset, int *end_offset) {
			WRAPPER_METHOD_BODY(text, GetTextAfterOffset(offset, boundary_type, start_offset, end_offset), NULL)
		}
		static gchar *GetTextBeforeOffset(AtkText *text, int offset, AtkTextBoundary boundary_type, int *start_offset, int *end_offset) {
			WRAPPER_METHOD_BODY(text, GetTextBeforeOffset(offset, boundary_type, start_offset, end_offset), NULL)
		}
		static gchar *GetTextAtOffset(AtkText *text, gint offset, AtkTextBoundary boundary_type, gint *start_offset, gint *end_offset) {
			WRAPPER_METHOD_BODY(text, GetTextAtOffset(offset, boundary_type, start_offset, end_offset), NULL)
		}
		static gchar *GetStringAtOffset(AtkText *text, gint offset, AtkTextGranularity granularity, gint *start_offset, gint *end_offset) {
			WRAPPER_METHOD_BODY(text, GetStringAtOffset(offset, granularity, start_offset, end_offset), NULL)
		}
		static gunichar GetCharacterAtOffset(AtkText *text, gint offset) {
			WRAPPER_METHOD_BODY(text, GetCharacterAtOffset(offset), 0)
		}
		static gint GetCharacterCount(AtkText *text) {
			WRAPPER_METHOD_BODY(text, GetCharacterCount(), 0)
		}
		static gint GetCaretOffset(AtkText *text) {
			WRAPPER_METHOD_BODY(text, GetCaretOffset(), 0)
		}
		static gboolean SetCaretOffset(AtkText *text, gint offset) {
			WRAPPER_METHOD_BODY(text, SetCaretOffset(offset), FALSE)
		}
		static gint GetOffsetAtPoint(AtkText *text, gint x, gint y, AtkCoordType coords) {
			WRAPPER_METHOD_BODY(text, GetOffsetAtPoint(x, y, coords), -1)
		}
		static void GetCharacterExtents(AtkText *text, gint offset, gint *x, gint *y, gint *width, gint *height, AtkCoordType coords) {
			WRAPPER_METHOD_BODY(text, GetCharacterExtents(offset, x, y, width, height, coords), )
		}
		static AtkAttributeSet *GetRunAttributes(AtkText *text, gint offset, gint *start_offset, gint *end_offset) {
			WRAPPER_METHOD_BODY(text, GetRunAttributes(offset, start_offset, end_offset), NULL)
		}
		static AtkAttributeSet *GetDefaultAttributes(AtkText *text) {
			WRAPPER_METHOD_BODY(text, GetDefaultAttributes(), NULL)
		}
		static gint GetNSelections(AtkText *text) {
			WRAPPER_METHOD_BODY(text, GetNSelections(), 0)
		}
		static gchar *GetSelection(AtkText *text, gint selection_num, gint *start_pos, gint *end_pos) {
			WRAPPER_METHOD_BODY(text, GetSelection(selection_num, start_pos, end_pos), NULL)
		}
		static gboolean AddSelection(AtkText *text, gint start, gint end) {
			WRAPPER_METHOD_BODY(text, AddSelection(start, end), FALSE)
		}
		static gboolean RemoveSelection(AtkText *text, gint selection_num) {
			WRAPPER_METHOD_BODY(text, RemoveSelection(selection_num), FALSE)
		}
		static gboolean SetSelection(AtkText *text, gint selection_num, gint start, gint end) {
			WRAPPER_METHOD_BODY(text, SetSelection(selection_num, start, end), FALSE)
		}
	};
	class AtkEditableTextIface {
	public:
		static void init(::AtkEditableTextIface *iface);

	private:
		AtkEditableTextIface();

		static void SetTextContents(AtkEditableText *text, const gchar *contents) {
			WRAPPER_METHOD_BODY(text, SetTextContents(contents), )
		}
		static void InsertText(AtkEditableText *text, const gchar *contents, gint length, gint *position) {
			WRAPPER_METHOD_BODY(text, InsertText(contents, length, position), )
		}
		static void CopyText(AtkEditableText *text, gint start, gint end) {
			WRAPPER_METHOD_BODY(text, CopyText(start, end), )
		}
		static void CutText(AtkEditableText *text, gint start, gint end) {
			WRAPPER_METHOD_BODY(text, CutText(start, end), )
		}
		static void DeleteText(AtkEditableText *text, gint start, gint end) {
			WRAPPER_METHOD_BODY(text, DeleteText(start, end), )
		}
		static void PasteText(AtkEditableText *text, gint position) {
			WRAPPER_METHOD_BODY(text, PasteText(position), )
		}
	};
};

ScintillaGTKAccessible::ScintillaGTKAccessible(GtkAccessible *accessible_, GtkWidget *widget_) :
		accessible(accessible_),
		sci(ScintillaFromWidget(widget_)) {
	g_signal_connect(widget_, "sci-notify", G_CALLBACK(SciNotify), this);
}

ScintillaGTKAccessible::~ScintillaGTKAccessible() {
	if (old_doc) {
		old_doc->Release();
	}
}

gchar *ScintillaGTKAccessible::GetTextRangeUTF8(Position startByte, Position endByte) {
	g_return_val_if_fail(startByte >= 0, NULL);
	// FIXME: should we swap start/end if necessary?
	g_return_val_if_fail(endByte >= startByte, NULL);

	gchar *utf8Text = NULL;
	const char *charSetBuffer;

	// like TargetAsUTF8, but avoids a double conversion
	if (sci->IsUnicodeMode() || ! *(charSetBuffer = sci->CharacterSetID())) {
		int len = endByte - startByte;
		utf8Text = (char *) g_malloc(len + 1);
		sci->pdoc->GetCharRange(utf8Text, startByte, len);
		utf8Text[len] = '\0';
	} else {
		// Need to convert
		std::string s = sci->RangeText(startByte, endByte);
		std::string tmputf = ConvertText(&s[0], s.length(), "UTF-8", charSetBuffer, false);
		size_t len = tmputf.length();
		utf8Text = (char *) g_malloc(len + 1);
		memcpy(utf8Text, tmputf.c_str(), len);
		utf8Text[len] = '\0';
	}

	return utf8Text;
}

gchar *ScintillaGTKAccessible::GetText(int startChar, int endChar) {
	Position startByte, endByte;
	if (endChar == -1) {
		startByte = ByteOffsetFromCharacterOffset(startChar);
		endByte = sci->pdoc->Length();
	} else {
		ByteRangeFromCharacterRange(startChar, endChar, startByte, endByte);
	}
	return GetTextRangeUTF8(startByte, endByte);
}

gchar *ScintillaGTKAccessible::GetTextAfterOffset(int charOffset,
		AtkTextBoundary boundaryType, int *startChar, int *endChar) {
	g_return_val_if_fail(charOffset >= 0, NULL);

	Position startByte, endByte;
	Position byteOffset = ByteOffsetFromCharacterOffset(charOffset);

	switch (boundaryType) {
		case ATK_TEXT_BOUNDARY_CHAR:
			startByte = PositionAfter(byteOffset);
			endByte = PositionAfter(startByte);
			// FIXME: optimize conversion back, as we can reasonably assume +1 char?
			break;

		case ATK_TEXT_BOUNDARY_WORD_START:
			startByte = sci->WndProc(SCI_WORDENDPOSITION, byteOffset, 1);
			startByte = sci->WndProc(SCI_WORDENDPOSITION, startByte, 0);
			endByte = sci->WndProc(SCI_WORDENDPOSITION, startByte, 1);
			endByte = sci->WndProc(SCI_WORDENDPOSITION, endByte, 0);
			break;

		case ATK_TEXT_BOUNDARY_WORD_END:
			startByte = sci->WndProc(SCI_WORDENDPOSITION, byteOffset, 0);
			startByte = sci->WndProc(SCI_WORDENDPOSITION, startByte, 1);
			endByte = sci->WndProc(SCI_WORDENDPOSITION, startByte, 0);
			endByte = sci->WndProc(SCI_WORDENDPOSITION, endByte, 1);
			break;

		case ATK_TEXT_BOUNDARY_LINE_START: {
			int line = sci->WndProc(SCI_LINEFROMPOSITION, byteOffset, 0);
			startByte = sci->WndProc(SCI_POSITIONFROMLINE, line + 1, 0);
			endByte = sci->WndProc(SCI_POSITIONFROMLINE, line + 2, 0);
			break;
		}

		case ATK_TEXT_BOUNDARY_LINE_END: {
			int line = sci->WndProc(SCI_LINEFROMPOSITION, byteOffset, 0);
			startByte = sci->WndProc(SCI_GETLINEENDPOSITION, line, 0);
			endByte = sci->WndProc(SCI_GETLINEENDPOSITION, line + 1, 0);
			break;
		}

		default:
			*startChar = *endChar = -1;
			return NULL;
	}

	CharacterRangeFromByteRange(startByte, endByte, startChar, endChar);
	return GetTextRangeUTF8(startByte, endByte);
}

gchar *ScintillaGTKAccessible::GetTextBeforeOffset(int charOffset,
		AtkTextBoundary boundaryType, int *startChar, int *endChar) {
	g_return_val_if_fail(charOffset >= 0, NULL);

	Position startByte, endByte;
	Position byteOffset = ByteOffsetFromCharacterOffset(charOffset);

	switch (boundaryType) {
		case ATK_TEXT_BOUNDARY_CHAR:
			endByte = PositionBefore(byteOffset);
			startByte = PositionBefore(endByte);
			break;

		case ATK_TEXT_BOUNDARY_WORD_START:
			endByte = sci->WndProc(SCI_WORDSTARTPOSITION, byteOffset, 0);
			endByte = sci->WndProc(SCI_WORDSTARTPOSITION, endByte, 1);
			startByte = sci->WndProc(SCI_WORDSTARTPOSITION, endByte, 0);
			startByte = sci->WndProc(SCI_WORDSTARTPOSITION, startByte, 1);
			break;

		case ATK_TEXT_BOUNDARY_WORD_END:
			endByte = sci->WndProc(SCI_WORDSTARTPOSITION, byteOffset, 1);
			endByte = sci->WndProc(SCI_WORDSTARTPOSITION, endByte, 0);
			startByte = sci->WndProc(SCI_WORDSTARTPOSITION, endByte, 1);
			startByte = sci->WndProc(SCI_WORDSTARTPOSITION, startByte, 0);
			break;

		case ATK_TEXT_BOUNDARY_LINE_START: {
			int line = sci->WndProc(SCI_LINEFROMPOSITION, byteOffset, 0);
			endByte = sci->WndProc(SCI_POSITIONFROMLINE, line, 0);
			if (line > 0) {
				startByte = sci->WndProc(SCI_POSITIONFROMLINE, line - 1, 0);
			} else {
				startByte = endByte;
			}
			break;
		}

		case ATK_TEXT_BOUNDARY_LINE_END: {
			int line = sci->WndProc(SCI_LINEFROMPOSITION, byteOffset, 0);
			if (line > 0) {
				endByte = sci->WndProc(SCI_GETLINEENDPOSITION, line - 1, 0);
			} else {
				endByte = 0;
			}
			if (line > 1) {
				startByte = sci->WndProc(SCI_GETLINEENDPOSITION, line - 2, 0);
			} else {
				startByte = endByte;
			}
			break;
		}

		default:
			*startChar = *endChar = -1;
			return NULL;
	}

	CharacterRangeFromByteRange(startByte, endByte, startChar, endChar);
	return GetTextRangeUTF8(startByte, endByte);
}

gchar *ScintillaGTKAccessible::GetTextAtOffset(int charOffset,
		AtkTextBoundary boundaryType, int *startChar, int *endChar) {
	g_return_val_if_fail(charOffset >= 0, NULL);

	Position startByte, endByte;
	Position byteOffset = ByteOffsetFromCharacterOffset(charOffset);

	switch (boundaryType) {
		case ATK_TEXT_BOUNDARY_CHAR:
			startByte = byteOffset;
			endByte = sci->WndProc(SCI_POSITIONAFTER, byteOffset, 0);
			break;

		case ATK_TEXT_BOUNDARY_WORD_START:
			startByte = sci->WndProc(SCI_WORDSTARTPOSITION, byteOffset, 1);
			endByte = sci->WndProc(SCI_WORDENDPOSITION, byteOffset, 1);
			if (! sci->WndProc(SCI_ISRANGEWORD, startByte, endByte)) {
				// if the cursor was not on a word, forward back
				startByte = sci->WndProc(SCI_WORDSTARTPOSITION, startByte, 0);
				startByte = sci->WndProc(SCI_WORDSTARTPOSITION, startByte, 1);
			}
			endByte = sci->WndProc(SCI_WORDENDPOSITION, endByte, 0);
			break;

		case ATK_TEXT_BOUNDARY_WORD_END:
			startByte = sci->WndProc(SCI_WORDSTARTPOSITION, byteOffset, 1);
			endByte = sci->WndProc(SCI_WORDENDPOSITION, byteOffset, 1);
			if (! sci->WndProc(SCI_ISRANGEWORD, startByte, endByte)) {
				// if the cursor was not on a word, forward back
				endByte = sci->WndProc(SCI_WORDENDPOSITION, endByte, 0);
				endByte = sci->WndProc(SCI_WORDENDPOSITION, endByte, 1);
			}
			startByte = sci->WndProc(SCI_WORDSTARTPOSITION, startByte, 0);
			break;

		case ATK_TEXT_BOUNDARY_LINE_START: {
			int line = sci->WndProc(SCI_LINEFROMPOSITION, byteOffset, 0);
			startByte = sci->WndProc(SCI_POSITIONFROMLINE, line, 0);
			endByte = sci->WndProc(SCI_POSITIONFROMLINE, line + 1, 0);
			break;
		}

		case ATK_TEXT_BOUNDARY_LINE_END: {
			int line = sci->WndProc(SCI_LINEFROMPOSITION, byteOffset, 0);
			if (line > 0) {
				startByte = sci->WndProc(SCI_GETLINEENDPOSITION, line - 1, 0);
			} else {
				startByte = 0;
			}
			endByte = sci->WndProc(SCI_GETLINEENDPOSITION, line, 0);
			break;
		}

		default:
			*startChar = *endChar = -1;
			return NULL;
	}

	CharacterRangeFromByteRange(startByte, endByte, startChar, endChar);
	return GetTextRangeUTF8(startByte, endByte);
}

gchar *ScintillaGTKAccessible::GetStringAtOffset(int charOffset,
		AtkTextGranularity granularity, int *startChar, int *endChar) {
	g_return_val_if_fail(charOffset >= 0, NULL);

	Position startByte, endByte;
	Position byteOffset = ByteOffsetFromCharacterOffset(charOffset);

	switch (granularity) {
		case ATK_TEXT_GRANULARITY_CHAR:
			startByte = byteOffset;
			endByte = sci->WndProc(SCI_POSITIONAFTER, byteOffset, 0);
			break;
		case ATK_TEXT_GRANULARITY_WORD:
			startByte = sci->WndProc(SCI_WORDSTARTPOSITION, byteOffset, 1);
			endByte = sci->WndProc(SCI_WORDENDPOSITION, byteOffset, 1);
			break;
		case ATK_TEXT_GRANULARITY_LINE: {
			gint line = sci->WndProc(SCI_LINEFROMPOSITION, byteOffset, 0);
			startByte = sci->WndProc(SCI_POSITIONFROMLINE, line, 0);
			endByte = sci->WndProc(SCI_GETLINEENDPOSITION, line, 0);
			break;
		}
		default:
			*startChar = *endChar = -1;
			return NULL;
	}

	CharacterRangeFromByteRange(startByte, endByte, startChar, endChar);
	return GetTextRangeUTF8(startByte, endByte);
}

gunichar ScintillaGTKAccessible::GetCharacterAtOffset(int charOffset) {
	g_return_val_if_fail(charOffset >= 0, 0);

	Position startByte = ByteOffsetFromCharacterOffset(charOffset);
	Position endByte = PositionAfter(startByte);
	gchar *ch = GetTextRangeUTF8(startByte, endByte);
	gunichar unichar = g_utf8_get_char_validated(ch, -1);
	g_free(ch);

	return unichar;
}

gint ScintillaGTKAccessible::GetCharacterCount() {
	return sci->pdoc->CountCharacters(0, sci->pdoc->Length());
}

gint ScintillaGTKAccessible::GetCaretOffset() {
	return CharacterOffsetFromByteOffset(sci->WndProc(SCI_GETCURRENTPOS, 0, 0));
}

gboolean ScintillaGTKAccessible::SetCaretOffset(int charOffset) {
	// FIXME: do we need to scroll explicitly?  it has to happen, but need to check if
	// SCI_SETCURRENTPOS does it
	sci->WndProc(SCI_SETCURRENTPOS, ByteOffsetFromCharacterOffset(charOffset), 0);
	return TRUE;
}

gint ScintillaGTKAccessible::GetOffsetAtPoint(gint x, gint y, AtkCoordType coords) {
	gint x_widget, y_widget, x_window, y_window;
	GtkWidget *widget = gtk_accessible_get_widget(accessible);

	GdkWindow *window = gtk_widget_get_window(widget);
	gdk_window_get_origin(window, &x_widget, &y_widget);
	if (coords == ATK_XY_SCREEN) {
		x = x - x_widget;
		y = y - y_widget;
	} else if (coords == ATK_XY_WINDOW) {
		window = gdk_window_get_toplevel(window);
		gdk_window_get_origin(window, &x_window, &y_window);

		x = x - x_widget + x_window;
		y = y - y_widget + y_window;
	} else {
		return -1;
	}

	// FIXME: should we handle scrolling?
	return CharacterOffsetFromByteOffset(sci->WndProc(SCI_CHARPOSITIONFROMPOINTCLOSE, x, y));
}

void ScintillaGTKAccessible::GetCharacterExtents(int charOffset,
		gint *x, gint *y, gint *width, gint *height, AtkCoordType coords) {
	*x = *y = *height = *width = 0;

	Position byteOffset = ByteOffsetFromCharacterOffset(charOffset);

	// FIXME: should we handle scrolling?
	*x = sci->WndProc(SCI_POINTXFROMPOSITION, 0, byteOffset);
	*y = sci->WndProc(SCI_POINTYFROMPOSITION, 0, byteOffset);

	int line = sci->WndProc(SCI_LINEFROMPOSITION, byteOffset, 0);
	*height = sci->WndProc(SCI_TEXTHEIGHT, line, 0);

	int nextByteOffset = PositionAfter(byteOffset);
	int next_x = sci->WndProc(SCI_POINTXFROMPOSITION, 0, nextByteOffset);
	if (next_x > *x) {
		*width = next_x - *x;
	} else if (nextByteOffset > byteOffset) {
		/* maybe next position was on the next line or something.
		 * just compute the expected character width */
		int style = sci->pdoc->StyleAt(byteOffset);
		char ch[nextByteOffset - byteOffset];
		sci->pdoc->GetCharRange(ch, byteOffset, nextByteOffset);
		*width = sci->TextWidth(style, ch);
	} else {
		// possibly the last position on the document, so no character here.
		*x = *y = *height = *width = 0;
		return;
	}

	GtkWidget *widget = gtk_accessible_get_widget(accessible);
	GdkWindow *window = gtk_widget_get_window(widget);
	int x_widget, y_widget;
	gdk_window_get_origin(window, &x_widget, &y_widget);
	if (coords == ATK_XY_SCREEN) {
		*x += x_widget;
		*y += y_widget;
	} else if (coords == ATK_XY_WINDOW) {
		window = gdk_window_get_toplevel(window);
		int x_window, y_window;
		gdk_window_get_origin(window, &x_window, &y_window);

		*x += x_widget - x_window;
		*y += y_widget - y_window;
	} else {
		*x = *y = *height = *width = 0;
	}
}

static AtkAttributeSet *AddTextAttribute(AtkAttributeSet *attributes, AtkTextAttribute attr, gchar *value) {
	AtkAttribute *at = g_new(AtkAttribute, 1);
	at->name = g_strdup(atk_text_attribute_get_name(attr));
	at->value = value;

	return g_slist_prepend(attributes, at);
}

static AtkAttributeSet *AddTextIntAttribute(AtkAttributeSet *attributes, AtkTextAttribute attr, gint i) {
	return AddTextAttribute(attributes, attr, g_strdup(atk_text_attribute_get_value(attr, i)));
}

static AtkAttributeSet *AddTextColorAttribute(AtkAttributeSet *attributes, AtkTextAttribute attr, const ColourDesired &colour) {
	return AddTextAttribute(attributes, attr,
		g_strdup_printf("%u,%u,%u", colour.GetRed() * 257, colour.GetGreen() * 257, colour.GetBlue() * 257));
}

AtkAttributeSet *ScintillaGTKAccessible::GetAttributesForStyle(unsigned int styleNum) {
	AtkAttributeSet *attr_set = NULL;

	if (styleNum >= sci->vs.styles.size())
		return NULL;
	Style &style = sci->vs.styles[styleNum];

	attr_set = AddTextAttribute(attr_set, ATK_TEXT_ATTR_FAMILY_NAME, g_strdup(style.fontName));
	attr_set = AddTextAttribute(attr_set, ATK_TEXT_ATTR_SIZE, g_strdup_printf("%d", style.size / SC_FONT_SIZE_MULTIPLIER));
	attr_set = AddTextIntAttribute(attr_set, ATK_TEXT_ATTR_WEIGHT, CLAMP(style.weight, 100, 1000));
	attr_set = AddTextIntAttribute(attr_set, ATK_TEXT_ATTR_STYLE, style.italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
	attr_set = AddTextIntAttribute(attr_set, ATK_TEXT_ATTR_UNDERLINE, style.underline ? PANGO_UNDERLINE_SINGLE : PANGO_UNDERLINE_NONE);
	attr_set = AddTextColorAttribute(attr_set, ATK_TEXT_ATTR_FG_COLOR, style.fore);
	attr_set = AddTextColorAttribute(attr_set, ATK_TEXT_ATTR_BG_COLOR, style.back);
	attr_set = AddTextIntAttribute(attr_set, ATK_TEXT_ATTR_INVISIBLE, style.visible ? 0 : 1);
	attr_set = AddTextIntAttribute(attr_set, ATK_TEXT_ATTR_EDITABLE, style.changeable ? 1 : 0);

	return attr_set;
}

AtkAttributeSet *ScintillaGTKAccessible::GetRunAttributes(int charOffset, int *startChar, int *endChar) {
	g_return_val_if_fail(charOffset >= -1, NULL);

	Position byteOffset;
	if (charOffset == -1) {
		byteOffset = sci->WndProc(SCI_GETCURRENTPOS, 0, 0);
	} else {
		byteOffset = ByteOffsetFromCharacterOffset(charOffset);
	}
	int length = sci->pdoc->Length();

	g_return_val_if_fail(byteOffset < length, NULL);

	const char style = sci->pdoc->StyleAt(byteOffset);
	// compute the range for this style
	Position startByte = byteOffset;
	while (startByte > 0 && sci->pdoc->StyleAt((startByte) - 1) == style)
		(startByte)--;
	Position endByte = byteOffset;
	while ((endByte) + 1 < length && sci->pdoc->StyleAt((endByte) + 1) == style)
		(endByte)++;

	CharacterRangeFromByteRange(startByte, endByte, startChar, endChar);
	return GetAttributesForStyle((unsigned int) style);
}

AtkAttributeSet *ScintillaGTKAccessible::GetDefaultAttributes() {
	return GetAttributesForStyle(0);
}

gint ScintillaGTKAccessible::GetNSelections() {
	return sci->sel.Empty() ? 0 : sci->sel.Count();
}

gchar *ScintillaGTKAccessible::GetSelection(gint selection_num, int *startChar, int *endChar) {
	if (selection_num < 0 || (unsigned int) selection_num >= sci->sel.Count())
		return NULL;

	Position startByte = sci->sel.Range(selection_num).Start().Position();
	Position endByte = sci->sel.Range(selection_num).End().Position();

	CharacterRangeFromByteRange(startByte, endByte, startChar, endChar);
	return GetTextRangeUTF8(startByte, endByte);
}

gboolean ScintillaGTKAccessible::AddSelection(int startChar, int endChar) {
	size_t n_selections = sci->sel.Count();
	Position startByte, endByte;
	ByteRangeFromCharacterRange(startChar, endChar, startByte, endByte);
	// use WndProc() to set the selections so it notifies as needed
	if (n_selections > 1 || ! sci->sel.Empty()) {
		sci->WndProc(SCI_ADDSELECTION, startByte, endByte);
	} else {
		sci->WndProc(SCI_SETSELECTION, startByte, endByte);
	}

	return TRUE;
}

gboolean ScintillaGTKAccessible::RemoveSelection(gint selection_num) {
	size_t n_selections = sci->sel.Count();
	if (selection_num < 0 || (unsigned int) selection_num >= n_selections)
		return FALSE;

	if (n_selections > 1) {
		sci->WndProc(SCI_DROPSELECTIONN, selection_num, 0);
	} else if (sci->sel.Empty()) {
		return FALSE;
	} else {
		sci->WndProc(SCI_CLEARSELECTIONS, 0, 0);
	}

	return TRUE;
}

gboolean ScintillaGTKAccessible::SetSelection(gint selection_num, int startChar, int endChar) {
	if (selection_num < 0 || (unsigned int) selection_num >= sci->sel.Count())
		return FALSE;

	Position startByte, endByte;
	ByteRangeFromCharacterRange(startChar, endChar, startByte, endByte);

	sci->WndProc(SCI_SETSELECTIONNSTART, selection_num, startByte);
	sci->WndProc(SCI_SETSELECTIONNEND, selection_num, endByte);

	return TRUE;
}

void ScintillaGTKAccessible::AtkTextIface::init(::AtkTextIface *iface) {
	iface->get_text = GetText;
	iface->get_text_after_offset = GetTextAfterOffset;
	iface->get_text_at_offset = GetTextAtOffset;
	iface->get_text_before_offset = GetTextBeforeOffset;
#if ATK_CHECK_VERSION(2, 10, 0)
	iface->get_string_at_offset = GetStringAtOffset;
#endif
	iface->get_character_at_offset = GetCharacterAtOffset;
	iface->get_character_count = GetCharacterCount;
	iface->get_caret_offset = GetCaretOffset;
	iface->set_caret_offset = SetCaretOffset;
	iface->get_offset_at_point = GetOffsetAtPoint;
	iface->get_character_extents = GetCharacterExtents;
	iface->get_n_selections = GetNSelections;
	iface->get_selection = GetSelection;
	iface->add_selection = AddSelection;
	iface->remove_selection = RemoveSelection;
	iface->set_selection = SetSelection;
	iface->get_run_attributes = GetRunAttributes;
	iface->get_default_attributes = GetDefaultAttributes;
}

/* atkeditabletext.h */

void ScintillaGTKAccessible::SetTextContents(const gchar *contents) {
	// FIXME: it's probably useless to check for READONLY here, SETTEXT probably does it just fine?
	if (! sci->WndProc(SCI_GETREADONLY, 0, 0)) {
		sci->WndProc(SCI_SETTEXT, 0, (sptr_t) contents);
	}
}

static bool DocumentInsertStringUTF8(Document *doc, const char *charSet, Position bytePos, const gchar *utf8, int lengthBytes) {
	if (doc->IsReadOnly()) {
		return false;
	}

	// like EncodedFromUTF8(), but avoids an extra copy
	// FIXME: update target?
	if (SC_CP_UTF8 == doc->dbcsCodePage || ! *charSet) {
		doc->InsertString(bytePos, utf8, lengthBytes);
	} else {
		// conversion needed
		std::string encoded = ConvertText(utf8, lengthBytes, charSet, "UTF-8", true);
		doc->InsertString(bytePos, encoded.c_str(), encoded.length());
	}

	return true;
}

void ScintillaGTKAccessible::InsertText(const gchar *text, int lengthBytes, int *charPosition) {
	Position bytePosition = ByteOffsetFromCharacterOffset(*charPosition);

	// FIXME: should we update the target?
	if (DocumentInsertStringUTF8(sci->pdoc, sci->CharacterSetID(), bytePosition, text, lengthBytes)) {
		(*charPosition) += sci->pdoc->CountCharacters(bytePosition, lengthBytes);
	}
}

void ScintillaGTKAccessible::CopyText(int startChar, int endChar) {
	Position startByte, endByte;
	ByteRangeFromCharacterRange(startChar, endChar, startByte, endByte);
	sci->WndProc(SCI_COPYRANGE, startByte, endByte);
}

void ScintillaGTKAccessible::CutText(int startChar, int endChar) {
	g_return_if_fail(endChar >= startChar);

	if (! sci->WndProc(SCI_GETREADONLY, 0, 0)) {
		// FIXME: have a byte variant of those and convert only once?
		CopyText(startChar, endChar);
		DeleteText(startChar, endChar);
	}
}

void ScintillaGTKAccessible::DeleteText(int startChar, int endChar) {
	g_return_if_fail(endChar >= startChar);

	if (! sci->WndProc(SCI_GETREADONLY, 0, 0)) {
		int old_target[2] = {
			(int) sci->WndProc(SCI_GETTARGETSTART, 0, 0),
			(int) sci->WndProc(SCI_GETTARGETEND, 0, 0)
		};

		Position startByte, endByte;
		ByteRangeFromCharacterRange(startChar, endChar, startByte, endByte);

		sci->WndProc(SCI_SETTARGETRANGE, startByte, endByte);
		sci->WndProc(SCI_REPLACETARGET, 0, (sptr_t) "");

		// restore the old target, compensating for the removed range
		for (int i = 0; i < 2; i++) {
			if (old_target[i] > endByte)
				old_target[i] -= endByte - startByte;
			else if (old_target[i] > startByte) // start was in the middle of removed range
				old_target[i] = startByte;
		}

		sci->WndProc(SCI_SETTARGETRANGE, old_target[0], old_target[1]);
	}
}

struct PasteData {
	Document *doc;
	const char *charSet;
	int bytePosition;

	PasteData(Document *doc_, const char *charSet_, int bytePos_) :
		doc(doc_),
		charSet(charSet_), // static string
		bytePosition(bytePos_) {
		doc->AddRef();
	}

	~PasteData() {
		doc->Release();
	}

	void TextReceivedThis(GtkClipboard *, const gchar *text) {
		if (text) {
			DocumentInsertStringUTF8(doc, charSet, bytePosition, text, (int) strlen(text));
		}
	}

	static void TextReceived(GtkClipboard *clipboard, const gchar *text, gpointer data) {
		reinterpret_cast<PasteData*>(data)->TextReceivedThis(clipboard, text);
	}
};

void ScintillaGTKAccessible::PasteText(int charPosition) {
	// FIXME: use ScintillaGTK::Paste() somehow (it'd need a way to set the position)

	if (sci->pdoc->IsReadOnly())
		return;

	PasteData paste(sci->pdoc, sci->CharacterSetID(), ByteOffsetFromCharacterOffset(charPosition));

	GtkWidget *widget = gtk_accessible_get_widget(accessible);
	GtkClipboard *clipboard = gtk_widget_get_clipboard(widget, GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_request_text(clipboard, paste.TextReceived, &paste);
}

void ScintillaGTKAccessible::AtkEditableTextIface::init(::AtkEditableTextIface *iface) {
	iface->set_text_contents = SetTextContents;
	iface->insert_text = InsertText;
	iface->copy_text = CopyText;
	iface->cut_text = CutText;
	iface->delete_text = DeleteText;
	iface->paste_text = PasteText;
	//~ iface->set_run_attributes = SetRunAttributes;
}

// Callbacks

void ScintillaGTKAccessible::UpdateCursor() {
	Position pos = sci->WndProc(SCI_GETCURRENTPOS, 0, 0);
	if (old_pos != pos) {
		int charPosition = CharacterOffsetFromByteOffset(pos);
		g_signal_emit_by_name(accessible, "text-caret-moved", charPosition);
		old_pos = pos;
	}

	size_t n_selections = sci->sel.Count();
	size_t prev_n_selections = old_sels.size();
	bool selection_changed = n_selections != prev_n_selections;

	old_sels.resize(n_selections);
	for (size_t i = 0; i < n_selections; i++) {
		SelectionRange &sel = sci->sel.Range(i);

		if (i < prev_n_selections && ! selection_changed) {
			SelectionRange &old_sel = old_sels[i];
			// do not consider a caret move to be a selection change
			selection_changed = ((! old_sel.Empty() || ! sel.Empty()) && ! (old_sel == sel));
		}

		old_sels[i] = sel;
	}

	if (selection_changed)
		g_signal_emit_by_name(accessible, "text-selection-changed");
}

// FIXME: this doesn't seem to really work, Orca doesn't read nothing when the document changes
//        OTOH, GtkTextView has the same problem, so maybe it's Orca's fault?
void ScintillaGTKAccessible::ChangeDocument(Document *new_doc) {
	if (new_doc == old_doc) {
		return;
	}

	if (old_doc) {
		int charLength = old_doc->CountCharacters(0, old_doc->Length());
		g_signal_emit_by_name(accessible, "text-changed::delete", 0, charLength);
		old_doc->Release();
	}

	if (new_doc) {
		int charLength = new_doc->CountCharacters(0, new_doc->Length());
		g_signal_emit_by_name(accessible, "text-changed::insert", 0, charLength);

		// FIXME: should we really reinit readonly here?  we probably should notify the accessible
		old_readonly = new_doc->IsReadOnly();

		// update cursor and selection
		old_pos = -1;
		old_sels.clear();
		UpdateCursor();

		new_doc->AddRef();
	}

	old_doc = new_doc;
}

void ScintillaGTKAccessible::Notify(GtkWidget *, gint, SCNotification *nt) {
	switch (nt->nmhdr.code) {
		case SCN_MODIFIED: {
			if (nt->modificationType & SC_MOD_INSERTTEXT) {
				int startChar = CharacterOffsetFromByteOffset(nt->position);
				int lengthChar = sci->pdoc->CountCharacters(nt->position, nt->position + nt->length);
				g_signal_emit_by_name(accessible, "text-changed::insert", startChar, lengthChar);
				UpdateCursor();
			}
			if (nt->modificationType & SC_MOD_DELETETEXT) {
				int startChar = CharacterOffsetFromByteOffset(nt->position);
				int lengthChar = sci->pdoc->CountCharacters(nt->position, nt->position + nt->length);
				g_signal_emit_by_name(accessible, "text-changed::delete", startChar, lengthChar);
				UpdateCursor();
			}
			if (nt->modificationType & SC_MOD_CHANGESTYLE) {
				g_signal_emit_by_name(accessible, "text-attributes-changed");
			}
		} break;
		case SCN_UPDATEUI: {
			if (nt->updated & SC_UPDATE_SELECTION) {
				UpdateCursor();

				// SC_UPDATE_SELECTION is the only signal we get when DOCPOINTER changes, so check here
				if (sci->pdoc != old_doc) {
					ChangeDocument(sci->pdoc);
				}
			}

			bool readonly = sci->pdoc->IsReadOnly();
			if (old_readonly != readonly) {
				atk_object_notify_state_change(ATK_OBJECT(accessible), ATK_STATE_EDITABLE, ! readonly);
				old_readonly = readonly;
			}
		} break;
	}
}

// GObject glue

#if HAVE_GTK_FACTORY
static GType scintilla_object_accessible_factory_get_type(void);
#endif

static void scintilla_object_accessible_init(ScintillaObjectAccessible *accessible);
static void scintilla_object_accessible_class_init(ScintillaObjectAccessibleClass *klass);
static gpointer scintilla_object_accessible_parent_class = NULL;


// @p parent_type is only required on GTK 3.2 to 3.6, and only on the first call
static GType scintilla_object_accessible_get_type(GType parent_type) {
	static volatile gsize type_id_result = 0;

	if (g_once_init_enter(&type_id_result)) {
		GTypeInfo tinfo = {
			0,															/* class size */
			(GBaseInitFunc) NULL,										/* base init */
			(GBaseFinalizeFunc) NULL,									/* base finalize */
			(GClassInitFunc) scintilla_object_accessible_class_init,	/* class init */
			(GClassFinalizeFunc) NULL,									/* class finalize */
			NULL,														/* class data */
			0,															/* instance size */
			0,															/* nb preallocs */
			(GInstanceInitFunc) scintilla_object_accessible_init,		/* instance init */
			NULL														/* value table */
		};

		const GInterfaceInfo atk_text_info = {
			(GInterfaceInitFunc) ScintillaGTKAccessible::AtkTextIface::init,
			(GInterfaceFinalizeFunc) NULL,
			NULL
		};

		const GInterfaceInfo atk_editable_text_info = {
			(GInterfaceInitFunc) ScintillaGTKAccessible::AtkEditableTextIface::init,
			(GInterfaceFinalizeFunc) NULL,
			NULL
		};

#if HAVE_GTK_A11Y_H
		// good, we have gtk-a11y.h, we can use that
		GType derived_atk_type = GTK_TYPE_CONTAINER_ACCESSIBLE;
		tinfo.class_size = sizeof (GtkContainerAccessibleClass);
		tinfo.instance_size = sizeof (GtkContainerAccessible);
#else // ! HAVE_GTK_A11Y_H
# if HAVE_GTK_FACTORY
		// Figure out the size of the class and instance we are deriving from through the registry
		GType derived_type = g_type_parent(SCINTILLA_TYPE_OBJECT);
		AtkObjectFactory *factory = atk_registry_get_factory(atk_get_default_registry(), derived_type);
		GType derived_atk_type = atk_object_factory_get_accessible_type(factory);
# else // ! HAVE_GTK_FACTORY
		// We're kind of screwed and can't determine the parent (no registry, and no public type)
		// Hack your way around by requiring the caller to give us our parent type.  The caller
		// might be able to trick its way into doing that, by e.g. instantiating the parent's
		// accessible type and get its GType.  It's ugly but we can't do better on GTK 3.2 to 3.6.
		g_assert(parent_type != 0);

		GType derived_atk_type = parent_type;
# endif // ! HAVE_GTK_FACTORY

		GTypeQuery query;
		g_type_query(derived_atk_type, &query);
		tinfo.class_size = query.class_size;
		tinfo.instance_size = query.instance_size;
#endif // ! HAVE_GTK_A11Y_H

		GType type_id = g_type_register_static(derived_atk_type, "ScintillaObjectAccessible", &tinfo, (GTypeFlags) 0);
		g_type_add_interface_static(type_id, ATK_TYPE_TEXT, &atk_text_info);
		g_type_add_interface_static(type_id, ATK_TYPE_EDITABLE_TEXT, &atk_editable_text_info);

		g_once_init_leave(&type_id_result, type_id);
	}

	return type_id_result;
}

static AtkObject *scintilla_object_accessible_new(GType parent_type, GObject *obj) {
	g_return_val_if_fail(SCINTILLA_IS_OBJECT(obj), NULL);

	AtkObject *accessible = (AtkObject *) g_object_new(scintilla_object_accessible_get_type(parent_type),
#if HAVE_WIDGET_SET_UNSET
		"widget", obj,
#endif
		NULL);
	atk_object_initialize(accessible, obj);

	return accessible;
}

// implementation for get_widget_get_accessible().
// See the comment at the top of the file for details on the implementation
// @p widget the widget.
// @p cache pointer to store the AtkObject between repeated calls.  Might or might not be filled.
// @p widget_parent_class pointer to the widget's parent class (to chain up method calls).
AtkObject *scintilla_object_accessible_widget_get_accessible_impl(GtkWidget *widget, AtkObject **cache, gpointer widget_parent_class) {
#if HAVE_GTK_A11Y_H // just instantiate the accessible
	if (*cache == NULL) {
		*cache = scintilla_object_accessible_new(0, G_OBJECT(widget));
	}
	return *cache;
#elif HAVE_GTK_FACTORY // register in the factory and let GTK instantiate
	static volatile gsize registered = 0;

	if (g_once_init_enter(&registered)) {
		// Figure out whether accessibility is enabled by looking at the type of the accessible
		// object which would be created for the parent type of ScintillaObject.
		GType derived_type = g_type_parent(SCINTILLA_TYPE_OBJECT);

		AtkRegistry *registry = atk_get_default_registry();
		AtkObjectFactory *factory = atk_registry_get_factory(registry, derived_type);
		GType derived_atk_type = atk_object_factory_get_accessible_type(factory);
		if (g_type_is_a(derived_atk_type, GTK_TYPE_ACCESSIBLE)) {
			atk_registry_set_factory_type(registry, SCINTILLA_TYPE_OBJECT,
			                              scintilla_object_accessible_factory_get_type());
		}
		g_once_init_leave(&registered, 1);
	}
	return GTK_WIDGET_CLASS(widget_parent_class)->get_accessible(widget);
#else // no public API, no factory, so guess from the parent and instantiate
	if (*cache == NULL) {
		static GType parent_atk_type = 0;

		if (parent_atk_type == 0) {
			AtkObject *parent_obj = GTK_WIDGET_CLASS(widget_parent_class)->get_accessible(widget);
			if (parent_obj) {
				GType parent_atk_type = G_OBJECT_TYPE(parent_obj);

				// Figure out whether accessibility is enabled by looking at the type of the accessible
				// object which would be created for the parent type of ScintillaObject.
				if (g_type_is_a(parent_atk_type, GTK_TYPE_ACCESSIBLE)) {
					*cache = scintilla_object_accessible_new(parent_atk_type, G_OBJECT(widget));
					g_object_unref(parent_obj);
				} else {
					*cache = parent_obj;
				}
			}
		}
	}
	return *cache;
#endif
}

static AtkStateSet *scintilla_object_accessible_ref_state_set(AtkObject *accessible) {
	AtkStateSet *state_set = ATK_OBJECT_CLASS(scintilla_object_accessible_parent_class)->ref_state_set(accessible);

	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(accessible));
	if (widget == NULL) {
		atk_state_set_add_state(state_set, ATK_STATE_DEFUNCT);
	} else {
		if (scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_GETREADONLY, 0, 0))
			atk_state_set_add_state(state_set, ATK_STATE_READ_ONLY);
		else
			atk_state_set_add_state(state_set, ATK_STATE_EDITABLE);
		atk_state_set_add_state(state_set, ATK_STATE_MULTI_LINE);
		atk_state_set_add_state(state_set, ATK_STATE_MULTISELECTABLE);
		atk_state_set_add_state(state_set, ATK_STATE_SELECTABLE_TEXT);
		/*atk_state_set_add_state(state_set, ATK_STATE_SUPPORTS_AUTOCOMPLETION);*/
	}

	return state_set;
}

static void scintilla_object_accessible_widget_set(GtkAccessible *accessible) {
	GtkWidget *widget = gtk_accessible_get_widget(accessible);
	if (widget == NULL)
		return;

	ScintillaObjectAccessiblePrivate *priv = SCINTILLA_OBJECT_ACCESSIBLE_GET_PRIVATE(accessible);
	if (priv->pscin != nullptr)
		delete priv->pscin;
	priv->pscin = new ScintillaGTKAccessible(accessible, widget);
}

#if HAVE_WIDGET_SET_UNSET
static void scintilla_object_accessible_widget_unset(GtkAccessible *accessible) {
	GtkWidget *widget = gtk_accessible_get_widget(accessible);
	if (widget == NULL)
		return;

	ScintillaObjectAccessiblePrivate *priv = SCINTILLA_OBJECT_ACCESSIBLE_GET_PRIVATE(accessible);
	delete priv->pscin;
	priv->pscin = nullptr;
}
#endif

static void scintilla_object_accessible_initialize(AtkObject *obj, gpointer data) {
	ATK_OBJECT_CLASS(scintilla_object_accessible_parent_class)->initialize(obj, data);

#if ! HAVE_WIDGET_SET_UNSET
	scintilla_object_accessible_widget_set(GTK_ACCESSIBLE(obj));
#endif

	obj->role = ATK_ROLE_TEXT;
}

static void scintilla_object_accessible_finalize(GObject *object) {
	ScintillaObjectAccessiblePrivate *priv = SCINTILLA_OBJECT_ACCESSIBLE_GET_PRIVATE(object);

	if (priv->pscin) {
		delete priv->pscin;
		priv->pscin = nullptr;
	}

	G_OBJECT_CLASS(scintilla_object_accessible_parent_class)->finalize(object);
}

static void scintilla_object_accessible_class_init(ScintillaObjectAccessibleClass *klass) {
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	AtkObjectClass *object_class = ATK_OBJECT_CLASS(klass);

#if HAVE_WIDGET_SET_UNSET
	GtkAccessibleClass *accessible_class = GTK_ACCESSIBLE_CLASS(klass);
	accessible_class->widget_set = scintilla_object_accessible_widget_set;
	accessible_class->widget_unset = scintilla_object_accessible_widget_unset;
#endif

	object_class->ref_state_set = scintilla_object_accessible_ref_state_set;
	object_class->initialize = scintilla_object_accessible_initialize;

	gobject_class->finalize = scintilla_object_accessible_finalize;

	scintilla_object_accessible_parent_class = g_type_class_peek_parent(klass);

	g_type_class_add_private(klass, sizeof (ScintillaObjectAccessiblePrivate));
}

static void scintilla_object_accessible_init(ScintillaObjectAccessible *accessible) {
	ScintillaObjectAccessiblePrivate *priv = SCINTILLA_OBJECT_ACCESSIBLE_GET_PRIVATE(accessible);

	priv->pscin = nullptr;
}

#if HAVE_GTK_FACTORY
// Object factory
typedef AtkObjectFactory ScintillaObjectAccessibleFactory;
typedef AtkObjectFactoryClass ScintillaObjectAccessibleFactoryClass;

G_DEFINE_TYPE(ScintillaObjectAccessibleFactory, scintilla_object_accessible_factory, ATK_TYPE_OBJECT_FACTORY)

static void scintilla_object_accessible_factory_init(ScintillaObjectAccessibleFactory *) {
}

static GType scintilla_object_accessible_factory_get_accessible_type(void) {
	return SCINTILLA_TYPE_OBJECT_ACCESSIBLE;
}

static AtkObject *scintilla_object_accessible_factory_create_accessible(GObject *obj) {
	return scintilla_object_accessible_new(0, obj);
}

static void scintilla_object_accessible_factory_class_init(AtkObjectFactoryClass * klass) {
	klass->create_accessible = scintilla_object_accessible_factory_create_accessible;
	klass->get_accessible_type = scintilla_object_accessible_factory_get_accessible_type;
}
#endif
