/* Scintilla source code edit control */
/* ScintillaGTKAccessible.h - GTK+ accessibility for ScintillaGTK */
/* Copyright 2016 by Colomban Wendling <colomban@geany.org>
 * The License.txt file describes the conditions under which this software may be distributed. */

#ifndef SCINTILLAGTKACCESSIBLE_H
#define SCINTILLAGTKACCESSIBLE_H

#ifdef SCI_NAMESPACE
namespace Scintilla {
#endif

class ScintillaGTKAccessible {
private:
	GtkAccessible *accessible;
	ScintillaGTK *sci;

	// local state for comparing
	bool old_readonly = false;
	Position old_pos = -1;
	std::vector<SelectionRange> old_sels;

	void UpdateCursor();
	void Notify(GtkWidget *widget, gint code, SCNotification *nt);
	static void SciNotify(GtkWidget *widget, gint code, SCNotification *nt, gpointer data) {
		try {
			reinterpret_cast<ScintillaGTKAccessible*>(data)->Notify(widget, code, nt);
		} catch (...) {}
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

	// For AtkText
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
	// for AtkEditableText
	bool InsertStringUTF8(Position bytePos, const gchar *utf8, int lengthBytes);
	void SetTextContents(const gchar *contents);
	void InsertText(const gchar *contents, int lengthBytes, int *charPosition);
	void CopyText(int startChar, int endChar);
	void CutText(int startChar, int endChar);
	void DeleteText(int startChar, int endChar);
	void PasteText(int charPosition);

public:
	ScintillaGTKAccessible(GtkAccessible *accessible, GtkWidget *widget);
	~ScintillaGTKAccessible();

	static ScintillaGTKAccessible *FromAccessible(GtkAccessible *accessible);
	// So ScintillaGTK can notify us
	void ChangeDocument(Document *oldDoc, Document *newDoc);

	// ATK methods

	// wraps a call from the accessible object to the ScintillaGTKAccessible, and avoid leaking any exception
	#define WRAPPER_METHOD_BODY(accessible, call, defret) \
		try { \
			ScintillaGTKAccessible *thisAccessible = FromAccessible(reinterpret_cast<GtkAccessible*>(accessible)); \
			if (thisAccessible) { \
				return thisAccessible->call; \
			} else { \
				return defret; \
			} \
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

AtkObject *scintilla_object_accessible_widget_get_accessible_impl(GtkWidget *widget, AtkObject **cache, gpointer widget_parent_class);

#ifdef SCI_NAMESPACE
}
#endif


#endif /* SCINTILLAGTKACCESSIBLE_H */
