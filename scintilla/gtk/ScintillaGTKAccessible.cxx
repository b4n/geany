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

// FIXME: report characters rather than bytes

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
#include <utility>
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
	std::vector<std::pair<Position, Position>> old_sels;

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
			throw;
		}

		ScintillaGTKAccessible *self = SCINTILLA_OBJECT_ACCESSIBLE_GET_PRIVATE(accessible)->pscin;
		if (! self)
			throw;

		return self;
	}

	gchar *GetTextRange(Position start_offset, Position end_offset);
	gchar *GetTextAfterOffset(int offset, AtkTextBoundary boundary_type, int *start_offset, int *end_offset);
	gchar *GetTextBeforeOffset(int offset, AtkTextBoundary boundary_type, int *start_offset, int *end_offset);
	gchar *GetTextAtOffset(int offset, AtkTextBoundary boundary_type, int *start_offset, int *end_offset);
	gchar *GetStringAtOffset(int offset, AtkTextGranularity granularity, int *start_offset, int *end_offset);
	gunichar GetCharacterAtOffset(gint offset);
	gint GetCharacterCount();
	gint GetCaretOffset();
	gboolean SetCaretOffset(gint offset);
	gint GetOffsetAtPoint(gint x, gint y, AtkCoordType coords);
	void GetCharacterExtents(gint offset, gint *x, gint *y, gint *width, gint *height, AtkCoordType coords);
	AtkAttributeSet *GetAttributesForStyle(unsigned int style);
	AtkAttributeSet *GetRunAttributes(gint offset, gint *start_offset, gint *end_offset);
	AtkAttributeSet *GetDefaultAttributes();
	gint GetNSelections();
	gchar *GetSelection(gint selection_num, gint *start_pos, gint *end_pos);
	gboolean AddSelection(gint start, gint end);
	gboolean RemoveSelection(gint selection_num);
	gboolean SetSelection(gint selection_num, gint start, gint end);

	/* atkeditabletext.h */
	void SetTextContents(const gchar *contents);
	void InsertText(const gchar *contents, gint length, gint *position);
	void CopyText(gint start, gint end);
	void CutText(gint start, gint end);
	void DeleteText(gint start, gint end);
	void PasteText(gint position);

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
			WRAPPER_METHOD_BODY(text, GetTextRange(start_offset, end_offset), NULL);
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
	ChangeDocument(nullptr);
}

gchar *ScintillaGTKAccessible::GetTextRange(Position start_offset, Position end_offset)  {
	struct Sci_TextRange range;

	g_return_val_if_fail(start_offset >= 0, NULL);
	// FIXME: should we swap start/end if necessary?
	g_return_val_if_fail(end_offset >= start_offset, NULL);

	range.chrg.cpMin = start_offset;
	range.chrg.cpMax = end_offset;
	range.lpstrText = (char *) g_malloc(end_offset - start_offset + 1);
	sci->WndProc(SCI_GETTEXTRANGE, 0, (sptr_t) &range);

	return range.lpstrText;
}

gchar *ScintillaGTKAccessible::GetTextAfterOffset(int offset,
		AtkTextBoundary boundary_type, int *start_offset, int *end_offset) {
	g_return_val_if_fail(offset >= 0, NULL);

	switch (boundary_type) {
		case ATK_TEXT_BOUNDARY_CHAR:
			*start_offset = sci->WndProc(SCI_POSITIONAFTER, offset, 0);
			*end_offset = sci->WndProc(SCI_POSITIONAFTER, *start_offset, 0);
			break;

		case ATK_TEXT_BOUNDARY_WORD_START:
			*start_offset = sci->WndProc(SCI_WORDENDPOSITION, offset, 1);
			*start_offset = sci->WndProc(SCI_WORDENDPOSITION, *start_offset, 0);
			*end_offset = sci->WndProc(SCI_WORDENDPOSITION, *start_offset, 1);
			*end_offset = sci->WndProc(SCI_WORDENDPOSITION, *end_offset, 0);
			break;

		case ATK_TEXT_BOUNDARY_WORD_END:
			*start_offset = sci->WndProc(SCI_WORDENDPOSITION, offset, 0);
			*start_offset = sci->WndProc(SCI_WORDENDPOSITION, *start_offset, 1);
			*end_offset = sci->WndProc(SCI_WORDENDPOSITION, *start_offset, 0);
			*end_offset = sci->WndProc(SCI_WORDENDPOSITION, *end_offset, 1);
			break;

		case ATK_TEXT_BOUNDARY_LINE_START: {
			int line = sci->WndProc(SCI_LINEFROMPOSITION, offset, 0);
			*start_offset = sci->WndProc(SCI_POSITIONFROMLINE, line + 1, 0);
			*end_offset = sci->WndProc(SCI_POSITIONFROMLINE, line + 2, 0);
			break;
		}

		case ATK_TEXT_BOUNDARY_LINE_END: {
			int line = sci->WndProc(SCI_LINEFROMPOSITION, offset, 0);
			*start_offset = sci->WndProc(SCI_GETLINEENDPOSITION, line, 0);
			*end_offset = sci->WndProc(SCI_GETLINEENDPOSITION, line + 1, 0);
			break;
		}

		default:
			*start_offset = *end_offset = -1;
			return NULL;
	}

	return GetTextRange(*start_offset, *end_offset);
}

gchar *ScintillaGTKAccessible::GetTextBeforeOffset(int offset,
		AtkTextBoundary boundary_type, int *start_offset, int *end_offset) {
	g_return_val_if_fail(offset >= 0, NULL);

	switch (boundary_type) {
		case ATK_TEXT_BOUNDARY_CHAR:
			*end_offset = sci->WndProc(SCI_POSITIONBEFORE, offset, 0);
			*start_offset = sci->WndProc(SCI_POSITIONBEFORE, *end_offset, 0);
			break;

		case ATK_TEXT_BOUNDARY_WORD_START:
			*end_offset = sci->WndProc(SCI_WORDSTARTPOSITION, offset, 0);
			*end_offset = sci->WndProc(SCI_WORDSTARTPOSITION, *end_offset, 1);
			*start_offset = sci->WndProc(SCI_WORDSTARTPOSITION, *end_offset, 0);
			*start_offset = sci->WndProc(SCI_WORDSTARTPOSITION, *start_offset, 1);
			break;

		case ATK_TEXT_BOUNDARY_WORD_END:
			*end_offset = sci->WndProc(SCI_WORDSTARTPOSITION, offset, 1);
			*end_offset = sci->WndProc(SCI_WORDSTARTPOSITION, *end_offset, 0);
			*start_offset = sci->WndProc(SCI_WORDSTARTPOSITION, *end_offset, 1);
			*start_offset = sci->WndProc(SCI_WORDSTARTPOSITION, *start_offset, 0);
			break;

		case ATK_TEXT_BOUNDARY_LINE_START: {
			int line = sci->WndProc(SCI_LINEFROMPOSITION, offset, 0);
			*end_offset = sci->WndProc(SCI_POSITIONFROMLINE, line, 0);
			if (line > 0) {
				*start_offset = sci->WndProc(SCI_POSITIONFROMLINE, line - 1, 0);
			} else {
				*start_offset = *end_offset;
			}
			break;
		}

		case ATK_TEXT_BOUNDARY_LINE_END: {
			int line = sci->WndProc(SCI_LINEFROMPOSITION, offset, 0);
			if (line > 0) {
				*end_offset = sci->WndProc(SCI_GETLINEENDPOSITION, line - 1, 0);
			} else {
				*end_offset = 0;
			}
			if (line > 1) {
				*start_offset = sci->WndProc(SCI_GETLINEENDPOSITION, line - 2, 0);
			} else {
				*start_offset = *end_offset;
			}
			break;
		}

		default:
			*start_offset = *end_offset = -1;
			return NULL;
	}

	return GetTextRange(*start_offset, *end_offset);
}

gchar *ScintillaGTKAccessible::GetTextAtOffset(gint offset,
		AtkTextBoundary boundary_type, gint *start_offset, gint *end_offset) {
	g_return_val_if_fail(offset >= 0, NULL);

	switch (boundary_type) {
		case ATK_TEXT_BOUNDARY_CHAR:
			*start_offset = offset;
			*end_offset = sci->WndProc(SCI_POSITIONAFTER, offset, 0);
			break;

		case ATK_TEXT_BOUNDARY_WORD_START:
			*start_offset = sci->WndProc(SCI_WORDSTARTPOSITION, offset, 1);
			*end_offset = sci->WndProc(SCI_WORDENDPOSITION, offset, 1);
			if (! sci->WndProc(SCI_ISRANGEWORD, *start_offset, *end_offset)) {
				// if the cursor was not on a word, forward back
				*start_offset = sci->WndProc(SCI_WORDSTARTPOSITION, *start_offset, 0);
				*start_offset = sci->WndProc(SCI_WORDSTARTPOSITION, *start_offset, 1);
			}
			*end_offset = sci->WndProc(SCI_WORDENDPOSITION, *end_offset, 0);
			break;

		case ATK_TEXT_BOUNDARY_WORD_END:
			*start_offset = sci->WndProc(SCI_WORDSTARTPOSITION, offset, 1);
			*end_offset = sci->WndProc(SCI_WORDENDPOSITION, offset, 1);
			if (! sci->WndProc(SCI_ISRANGEWORD, *start_offset, *end_offset)) {
				// if the cursor was not on a word, forward back
				*end_offset = sci->WndProc(SCI_WORDENDPOSITION, *end_offset, 0);
				*end_offset = sci->WndProc(SCI_WORDENDPOSITION, *end_offset, 1);
			}
			*start_offset = sci->WndProc(SCI_WORDSTARTPOSITION, *start_offset, 0);
			break;

		case ATK_TEXT_BOUNDARY_LINE_START: {
			int line = sci->WndProc(SCI_LINEFROMPOSITION, offset, 0);
			*start_offset = sci->WndProc(SCI_POSITIONFROMLINE, line, 0);
			*end_offset = sci->WndProc(SCI_POSITIONFROMLINE, line + 1, 0);
			break;
		}

		case ATK_TEXT_BOUNDARY_LINE_END: {
			int line = sci->WndProc(SCI_LINEFROMPOSITION, offset, 0);
			if (line > 0) {
				*start_offset = sci->WndProc(SCI_GETLINEENDPOSITION, line - 1, 0);
			} else {
				*start_offset = 0;
			}
			*end_offset = sci->WndProc(SCI_GETLINEENDPOSITION, line, 0);
			break;
		}

		default:
			*start_offset = *end_offset = -1;
			return NULL;
	}

	return GetTextRange(*start_offset, *end_offset);
}

gchar *ScintillaGTKAccessible::GetStringAtOffset(gint offset,
		AtkTextGranularity granularity, gint *start_offset, gint *end_offset) {
	g_return_val_if_fail(offset >= 0, NULL);

	switch (granularity) {
		case ATK_TEXT_GRANULARITY_CHAR:
			*start_offset = offset;
			*end_offset = sci->WndProc(SCI_POSITIONAFTER, offset, 0);
			break;
		case ATK_TEXT_GRANULARITY_WORD:
			*start_offset = sci->WndProc(SCI_WORDSTARTPOSITION, offset, 1);
			*end_offset = sci->WndProc(SCI_WORDENDPOSITION, offset, 1);
			break;
		case ATK_TEXT_GRANULARITY_LINE: {
			gint line = sci->WndProc(SCI_LINEFROMPOSITION, offset, 0);
			*start_offset = sci->WndProc(SCI_POSITIONFROMLINE, line, 0);
			*end_offset = sci->WndProc(SCI_GETLINEENDPOSITION, line, 0);
			break;
		}
		default:
			*start_offset = *end_offset = -1;
			return NULL;
	}

	return GetTextRange(*start_offset, *end_offset);
}

gunichar ScintillaGTKAccessible::GetCharacterAtOffset(gint offset) {
	g_return_val_if_fail(offset >= 0, 0);

	// FIXME: support real Unicode character, not bytes?
	return sci->WndProc(SCI_GETCHARAT, offset, 0);
}

gint ScintillaGTKAccessible::GetCharacterCount() {
	// FIXME: return characters, not bytes?
	return sci->WndProc(SCI_GETLENGTH, 0, 0);
}

gint ScintillaGTKAccessible::GetCaretOffset() {
	return sci->WndProc(SCI_GETCURRENTPOS, 0, 0);
}

gboolean ScintillaGTKAccessible::SetCaretOffset(gint offset) {
	// FIXME: do we need to scroll explicitly?  it has to happen, but need to check if
	// SCI_SETCURRENTPOS does it
	sci->WndProc(SCI_SETCURRENTPOS, offset, 0);
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
	return sci->WndProc(SCI_CHARPOSITIONFROMPOINTCLOSE, x, y);
}

void ScintillaGTKAccessible::GetCharacterExtents(gint offset,
		gint *x, gint *y, gint *width, gint *height, AtkCoordType coords) {
	*x = *y = *height = *width = 0;

	// FIXME: should we handle scrolling?
	*x = sci->WndProc(SCI_POINTXFROMPOSITION, 0, offset);
	*y = sci->WndProc(SCI_POINTYFROMPOSITION, 0, offset);

	int line = sci->WndProc(SCI_LINEFROMPOSITION, offset, 0);
	*height = sci->WndProc(SCI_TEXTHEIGHT, line, 0);

	int next_pos = sci->WndProc(SCI_POSITIONAFTER, offset, 0);
	int next_x = sci->WndProc(SCI_POINTXFROMPOSITION, 0, next_pos);
	if (next_x > *x) {
		*width = next_x - *x;
	} else if (next_pos > offset) {
		/* maybe next position was on the next line or something.
		 * just compute the expected character width */
		int style = sci->WndProc(SCI_GETSTYLEAT, offset, 0);
		gchar *ch = GetTextRange(offset, next_pos);
		*width = sci->WndProc(SCI_TEXTWIDTH, style, (sptr_t) ch);
		g_free(ch);
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

AtkAttributeSet *ScintillaGTKAccessible::GetRunAttributes(gint offset, gint *start_offset, gint *end_offset) {
	char style = 0;

	int length = sci->pdoc->Length();
	if (offset > 0 && offset < length) {
		style = sci->pdoc->StyleAt(offset);

		// compute the range for this style
		*start_offset = offset;
		while (*start_offset > 0 && sci->pdoc->StyleAt((*start_offset) - 1) == style)
			(*start_offset)--;
		*end_offset = offset;
		while ((*end_offset) + 1 < length && sci->pdoc->StyleAt((*end_offset) + 1) == style)
			(*end_offset)++;
	}

	return GetAttributesForStyle((unsigned int) style);
}

AtkAttributeSet *ScintillaGTKAccessible::GetDefaultAttributes() {
	return GetAttributesForStyle(0);
}

gint ScintillaGTKAccessible::GetNSelections() {
	if (sci->WndProc(SCI_GETSELECTIONEMPTY, 0, 0))
		return 0;
	else
		return sci->WndProc(SCI_GETSELECTIONS, 0, 0);
}

gchar *ScintillaGTKAccessible::GetSelection(gint selection_num, gint *start_pos, gint *end_pos) {
	if (selection_num >= sci->WndProc(SCI_GETSELECTIONS, 0, 0))
		return NULL;

	*start_pos = sci->WndProc(SCI_GETSELECTIONNSTART, selection_num, 0);
	*end_pos = sci->WndProc(SCI_GETSELECTIONNEND, selection_num, 0);

	return GetTextRange(*start_pos, *end_pos);
}

gboolean ScintillaGTKAccessible::AddSelection(gint start, gint end) {
	int n_selections = sci->WndProc(SCI_GETSELECTIONS, 0, 0);
	if (n_selections > 1 || ! sci->WndProc(SCI_GETSELECTIONEMPTY, 0, 0)) {
		sci->WndProc(SCI_ADDSELECTION, start, end);
	} else {
		sci->WndProc(SCI_SETSELECTION, start, end);
	}

	return TRUE;
}

gboolean ScintillaGTKAccessible::RemoveSelection(gint selection_num) {
	int n_selections = sci->WndProc(SCI_GETSELECTIONS, 0, 0);
	if (selection_num >= n_selections)
		return FALSE;

	if (n_selections > 1) {
		sci->WndProc(SCI_DROPSELECTIONN, selection_num, 0);
	} else if (sci->WndProc(SCI_GETSELECTIONEMPTY, 0, 0)) {
		return FALSE;
	} else {
		sci->WndProc(SCI_CLEARSELECTIONS, 0, 0);
	}

	return TRUE;
}

gboolean ScintillaGTKAccessible::SetSelection(gint selection_num, gint start, gint end) {
	if (selection_num >= sci->WndProc(SCI_GETSELECTIONS, 0, 0))
		return FALSE;

	sci->WndProc(SCI_SETSELECTIONNSTART, selection_num, start);
	sci->WndProc(SCI_SETSELECTIONNEND, selection_num, end);

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

void ScintillaGTKAccessible::InsertText(const gchar *text, gint length, gint *position) {
	if (! sci->WndProc(SCI_GETREADONLY, 0, 0)) {
		int old_target[2] = {
			(int) sci->WndProc(SCI_GETTARGETSTART, 0, 0),
			(int) sci->WndProc(SCI_GETTARGETEND, 0, 0)
		};

		sci->WndProc(SCI_SETTARGETRANGE, *position, *position);
		sci->WndProc(SCI_REPLACETARGET, length, (sptr_t) text);
		(*position) += length;

		// restore the old target
		for (int i = 0; i < 2; i++) {
			if (old_target[i] > *position)
				old_target[i] += length;
		}
		sci->WndProc(SCI_SETTARGETRANGE, old_target[0], old_target[1]);
	}
}

void ScintillaGTKAccessible::CopyText(gint start, gint end) {
	sci->WndProc(SCI_COPYRANGE, start, end);
}

void ScintillaGTKAccessible::CutText(gint start, gint end) {
	g_return_if_fail(end >= start);

	if (! sci->WndProc(SCI_GETREADONLY, 0, 0)) {
		CopyText(start, end);
		DeleteText(start, end);
	}
}

void ScintillaGTKAccessible::DeleteText(gint start, gint end) {
	g_return_if_fail(end >= start);

	if (! sci->WndProc(SCI_GETREADONLY, 0, 0)) {
		int old_target[2] = {
			(int) sci->WndProc(SCI_GETTARGETSTART, 0, 0),
			(int) sci->WndProc(SCI_GETTARGETEND, 0, 0)
		};

		sci->WndProc(SCI_SETTARGETRANGE, start, end);
		sci->WndProc(SCI_REPLACETARGET, 0, (sptr_t) "");

		// restore the old target, compensating for the removed range
		for (int i = 0; i < 2; i++) {
			if (old_target[i] > end)
				old_target[i] -= end - start;
			else if (old_target[i] > start) // start was in the middle of removed range
				old_target[i] = start;
		}

		sci->WndProc(SCI_SETTARGETRANGE, old_target[0], old_target[1]);
	}
}

struct PasteData {
	Document *doc;
	gint position;

	PasteData(Document *doc_, int pos_) :
		doc(doc_),
		position(pos_) {
		doc->AddRef();
	}

	~PasteData() {
		doc->Release();
	}

	void TextReceivedThis(GtkClipboard *, const gchar *text) {
		if (text) {
			doc->InsertString(position, text, (int) strlen(text));
		}
	}

	static void TextReceived(GtkClipboard *clipboard, const gchar *text, gpointer data) {
		reinterpret_cast<PasteData*>(data)->TextReceivedThis(clipboard, text);
	}
};

void ScintillaGTKAccessible::PasteText(gint position) {
	if (sci->pdoc->IsReadOnly())
		return;

	PasteData paste(sci->pdoc, position);

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
		g_signal_emit_by_name(accessible, "text-caret-moved", (gint) pos);
		old_pos = pos;
	}

	int n_selections = sci->WndProc(SCI_GETSELECTIONS, 0, 0);
	int prev_n_selections = old_sels.size();
	bool selection_changed = n_selections != prev_n_selections;

	old_sels.resize(n_selections);
	for (int i = 0; i < n_selections; i++) {
		Position start = sci->WndProc(SCI_GETSELECTIONNSTART, i, 0);
		Position end = sci->WndProc(SCI_GETSELECTIONNEND, i, 0);

		if (i < prev_n_selections && ! selection_changed) {
			// do not consider a caret move to be a selection change
			selection_changed = ((old_sels[i].first != old_sels[i].second || start != end) &&
			                     (old_sels[i].first != start || old_sels[i].second != end));
		}

		old_sels[i] = std::pair<Position, Position>(start, end);
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
		g_signal_emit_by_name(accessible, "text-changed::delete", 0, old_doc->Length());
		old_doc->Release();
	}

	if (new_doc)
	{
		g_signal_emit_by_name(accessible, "text-changed::insert", 0, new_doc->Length());

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
				g_signal_emit_by_name(accessible, "text-changed::insert", (gint) nt->position, (gint) nt->length);
				UpdateCursor();
			}
			if (nt->modificationType & SC_MOD_DELETETEXT) {
				g_signal_emit_by_name(accessible, "text-changed::delete", (gint) nt->position, (gint) nt->length);
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
