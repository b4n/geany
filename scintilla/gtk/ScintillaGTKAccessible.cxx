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

#include "ScintillaGTK.h"

#include "ScintillaGTKAccessible.h"
#include "Scintilla.h"
#include "ScintillaWidget.h"

typedef struct
{
	Document *doc;

	gboolean readonly;

	gint pos;
	GArray *carets;
	GArray *anchors;
}
ScintillaObjectAccessiblePrivate;

typedef GtkAccessible ScintillaObjectAccessible;
typedef GtkAccessibleClass ScintillaObjectAccessibleClass;

#define SCINTILLA_OBJECT_ACCESSIBLE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), SCINTILLA_TYPE_OBJECT_ACCESSIBLE, ScintillaObjectAccessible))
#define SCINTILLA_TYPE_OBJECT_ACCESSIBLE (scintilla_object_accessible_get_type(0))

// We can't use priv member because of dynamic inheritance, so we don't actually know the offset.  Meh.
#define SCINTILLA_OBJECT_ACCESSIBLE_GET_PRIVATE(inst) (G_TYPE_INSTANCE_GET_PRIVATE((inst), SCINTILLA_TYPE_OBJECT_ACCESSIBLE, ScintillaObjectAccessiblePrivate))


static void scintilla_object_accessible_update_cursor(ScintillaObjectAccessible *accessible, ScintillaObject *sci);

static void sci_notify_handler(GtkWidget *widget, gint code, SCNotification *nt, gpointer data);

static void atk_editable_text_interface_init(AtkEditableTextIface *iface);
static void atk_text_interface_init(AtkTextIface *iface);

#if HAVE_GTK_FACTORY
static GType scintilla_object_accessible_factory_get_type(void);
#endif

static void scintilla_object_accessible_init(ScintillaObjectAccessible *accessible);
static void scintilla_object_accessible_class_init(ScintillaObjectAccessibleClass *klass);
static gpointer scintilla_object_accessible_parent_class = NULL;


// @p parent_type is only required on GTK 3.2 to 3.6, and only on the first call
static GType scintilla_object_accessible_get_type(GType parent_type)
{
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
			(GInterfaceInitFunc) atk_text_interface_init,
			(GInterfaceFinalizeFunc) NULL,
			NULL
		};

		const GInterfaceInfo atk_editable_text_info = {
			(GInterfaceInitFunc) atk_editable_text_interface_init,
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

static AtkObject *scintilla_object_accessible_new(GType parent_type, GObject *obj)
{
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
AtkObject *scintilla_object_accessible_widget_get_accessible_impl(GtkWidget *widget, AtkObject **cache, gpointer widget_parent_class)
{
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

static AtkStateSet *scintilla_object_accessible_ref_state_set(AtkObject *accessible)
{
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

// FIXME: this doesn't seem to really work, Orca doesn't read nothing when the document changes
//        OTOH, GtkTextView has the same problem, so maybe it's Orca's fault?
static void scintilla_object_accessible_change_document(ScintillaObjectAccessible *accessible, ScintillaObject *sci, Document *new_doc)
{
	ScintillaObjectAccessiblePrivate *priv = SCINTILLA_OBJECT_ACCESSIBLE_GET_PRIVATE(accessible);

	if (new_doc == priv->doc) {
		return;
	}

	if (priv->doc) {
		// FIXME: we need to query the *previous* document, not the current one
		g_signal_emit_by_name(accessible, "text-changed::delete", 0, (gint) priv->doc->Length());
		priv->doc->Release();
	}

	if (new_doc)
	{
		new_doc->AddRef();

		g_signal_emit_by_name(accessible, "text-changed::insert", 0, (gint) new_doc->Length());

		// FIXME: should we really reinit readonly here?  we probably should notify the accessible
		priv->readonly = scintilla_send_message(sci, SCI_GETREADONLY, 0, 0);

		// update cursor and selection
		priv->pos = -1;
		g_array_set_size(priv->carets, 0);
		g_array_set_size(priv->anchors, 0);
		scintilla_object_accessible_update_cursor(accessible, sci);
	}

	priv->doc = new_doc;
}

static void scintilla_object_accessible_widget_set(GtkAccessible *accessible)
{
	GtkWidget *widget = gtk_accessible_get_widget(accessible);
	if (widget == NULL)
		return;

	scintilla_object_accessible_change_document(SCINTILLA_OBJECT_ACCESSIBLE(accessible), SCINTILLA_OBJECT(widget),
			ScintillaBaseFromWidget(widget)->pdoc);

	g_signal_connect(widget, "sci-notify", G_CALLBACK(sci_notify_handler), accessible);
}

#if HAVE_WIDGET_SET_UNSET
static void scintilla_object_accessible_widget_unset(GtkAccessible *accessible)
{
	GtkWidget *widget = gtk_accessible_get_widget(accessible);
	if (widget == NULL)
		return;

	scintilla_object_accessible_change_document(SCINTILLA_OBJECT_ACCESSIBLE(accessible), SCINTILLA_OBJECT(widget), NULL);
}
#endif

static void scintilla_object_accessible_initialize(AtkObject *obj, gpointer data)
{
	ATK_OBJECT_CLASS(scintilla_object_accessible_parent_class)->initialize(obj, data);

#if ! HAVE_WIDGET_SET_UNSET
	scintilla_object_accessible_widget_set(GTK_ACCESSIBLE(obj));
#endif

	obj->role = ATK_ROLE_TEXT;
}

static void scintilla_object_accessible_finalize(GObject *object)
{
	ScintillaObjectAccessiblePrivate *priv = SCINTILLA_OBJECT_ACCESSIBLE_GET_PRIVATE(object);

	g_array_free(priv->carets, TRUE);
	g_array_free(priv->anchors, TRUE);
}

static void scintilla_object_accessible_class_init(ScintillaObjectAccessibleClass *klass)
{
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

static void scintilla_object_accessible_init(ScintillaObjectAccessible *accessible)
{
	ScintillaObjectAccessiblePrivate *priv = SCINTILLA_OBJECT_ACCESSIBLE_GET_PRIVATE(accessible);

	priv->doc = NULL;

	priv->pos = 0;
	priv->readonly = FALSE;

	priv->carets = g_array_new(FALSE, FALSE, sizeof(int));
	priv->anchors = g_array_new(FALSE, FALSE, sizeof(int));
}

static gchar *get_text_range(ScintillaObject *sci, gint start_offset, gint end_offset)
{
	struct Sci_TextRange range;

	g_return_val_if_fail(start_offset >= 0, NULL);
	g_return_val_if_fail(end_offset >= start_offset, NULL);

	range.chrg.cpMin = start_offset;
	range.chrg.cpMax = end_offset;
	range.lpstrText = (char *) g_malloc(end_offset - start_offset + 1);
	scintilla_send_message(sci, SCI_GETTEXTRANGE, 0, (sptr_t) &range);

	return range.lpstrText;
}

static gchar *scintilla_object_accessible_get_text(AtkText *text, gint start_offset, gint end_offset)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (widget == NULL)
		return NULL;

	return get_text_range(SCINTILLA_OBJECT(widget), start_offset, end_offset);
}

static gchar *scintilla_object_accessible_get_text_after_offset(AtkText *text, gint offset,
		AtkTextBoundary boundary_type, gint *start_offset, gint *end_offset)
{
	g_return_val_if_fail(offset >= 0, NULL);

	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return NULL;

	ScintillaObject *sci = SCINTILLA_OBJECT(widget);
	switch (boundary_type) {
		case ATK_TEXT_BOUNDARY_CHAR:
			*start_offset = scintilla_send_message(sci, SCI_POSITIONAFTER, offset, 0);
			*end_offset = scintilla_send_message(sci, SCI_POSITIONAFTER, *start_offset, 0);
			break;

		case ATK_TEXT_BOUNDARY_WORD_START:
			*start_offset = scintilla_send_message(sci, SCI_WORDENDPOSITION, offset, 1);
			*start_offset = scintilla_send_message(sci, SCI_WORDENDPOSITION, *start_offset, 0);
			*end_offset = scintilla_send_message(sci, SCI_WORDENDPOSITION, *start_offset, 1);
			*end_offset = scintilla_send_message(sci, SCI_WORDENDPOSITION, *end_offset, 0);
			break;

		case ATK_TEXT_BOUNDARY_WORD_END:
			*start_offset = scintilla_send_message(sci, SCI_WORDENDPOSITION, offset, 0);
			*start_offset = scintilla_send_message(sci, SCI_WORDENDPOSITION, *start_offset, 1);
			*end_offset = scintilla_send_message(sci, SCI_WORDENDPOSITION, *start_offset, 0);
			*end_offset = scintilla_send_message(sci, SCI_WORDENDPOSITION, *end_offset, 1);
			break;

		case ATK_TEXT_BOUNDARY_LINE_START: {
			gint line = scintilla_send_message(sci, SCI_LINEFROMPOSITION, offset, 0);
			*start_offset = scintilla_send_message(sci, SCI_POSITIONFROMLINE, line + 1, 0);
			*end_offset = scintilla_send_message(sci, SCI_POSITIONFROMLINE, line + 2, 0);
			break;
		}

		case ATK_TEXT_BOUNDARY_LINE_END: {
			gint line = scintilla_send_message(sci, SCI_LINEFROMPOSITION, offset, 0);
			*start_offset = scintilla_send_message(sci, SCI_GETLINEENDPOSITION, line, 0);
			*end_offset = scintilla_send_message(sci, SCI_GETLINEENDPOSITION, line + 1, 0);
			break;
		}

		default:
			*start_offset = *end_offset = -1;
			return NULL;
	}

	return get_text_range(sci, *start_offset, *end_offset);
}

static gchar *scintilla_object_accessible_get_text_before_offset(AtkText *text, gint offset,
		AtkTextBoundary boundary_type, gint *start_offset, gint *end_offset)
{
	g_return_val_if_fail(offset >= 0, NULL);

	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return NULL;

	ScintillaObject *sci = SCINTILLA_OBJECT(widget);
	switch (boundary_type) {
		case ATK_TEXT_BOUNDARY_CHAR:
			*end_offset = scintilla_send_message(sci, SCI_POSITIONBEFORE, offset, 0);
			*start_offset = scintilla_send_message(sci, SCI_POSITIONBEFORE, *end_offset, 0);
			break;

		case ATK_TEXT_BOUNDARY_WORD_START:
			*end_offset = scintilla_send_message(sci, SCI_WORDSTARTPOSITION, offset, 0);
			*end_offset = scintilla_send_message(sci, SCI_WORDSTARTPOSITION, *end_offset, 1);
			*start_offset = scintilla_send_message(sci, SCI_WORDSTARTPOSITION, *end_offset, 0);
			*start_offset = scintilla_send_message(sci, SCI_WORDSTARTPOSITION, *start_offset, 1);
			break;

		case ATK_TEXT_BOUNDARY_WORD_END:
			*end_offset = scintilla_send_message(sci, SCI_WORDSTARTPOSITION, offset, 1);
			*end_offset = scintilla_send_message(sci, SCI_WORDSTARTPOSITION, *end_offset, 0);
			*start_offset = scintilla_send_message(sci, SCI_WORDSTARTPOSITION, *end_offset, 1);
			*start_offset = scintilla_send_message(sci, SCI_WORDSTARTPOSITION, *start_offset, 0);
			break;

		case ATK_TEXT_BOUNDARY_LINE_START: {
			gint line = scintilla_send_message(sci, SCI_LINEFROMPOSITION, offset, 0);
			*end_offset = scintilla_send_message(sci, SCI_POSITIONFROMLINE, line, 0);
			if (line > 0) {
				*start_offset = scintilla_send_message(sci, SCI_POSITIONFROMLINE, line - 1, 0);
			} else {
				*start_offset = *end_offset;
			}
			break;
		}

		case ATK_TEXT_BOUNDARY_LINE_END: {
			gint line = scintilla_send_message(sci, SCI_LINEFROMPOSITION, offset, 0);
			if (line > 0) {
				*end_offset = scintilla_send_message(sci, SCI_GETLINEENDPOSITION, line - 1, 0);
			} else {
				*end_offset = 0;
			}
			if (line > 1) {
				*start_offset = scintilla_send_message(sci, SCI_GETLINEENDPOSITION, line - 2, 0);
			} else {
				*start_offset = *end_offset;
			}
			break;
		}

		default:
			*start_offset = *end_offset = -1;
			return NULL;
	}

	return get_text_range(sci, *start_offset, *end_offset);
}

static gchar *scintilla_object_accessible_get_string_at_offset(AtkText *text, gint offset,
		AtkTextGranularity granularity, gint *start_offset, gint *end_offset)
{
	g_return_val_if_fail(offset >= 0, NULL);

	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return NULL;

	ScintillaObject *sci = SCINTILLA_OBJECT(widget);
	switch (granularity) {
		case ATK_TEXT_GRANULARITY_CHAR:
			*start_offset = offset;
			*end_offset = scintilla_send_message(sci, SCI_POSITIONAFTER, offset, 0);
			break;
		case ATK_TEXT_GRANULARITY_WORD:
			*start_offset = scintilla_send_message(sci, SCI_WORDSTARTPOSITION, offset, 1);
			*end_offset = scintilla_send_message(sci, SCI_WORDENDPOSITION, offset, 1);
			break;
		case ATK_TEXT_GRANULARITY_LINE: {
			gint line = scintilla_send_message(sci, SCI_LINEFROMPOSITION, offset, 0);
			*start_offset = scintilla_send_message(sci, SCI_POSITIONFROMLINE, line, 0);
			*end_offset = scintilla_send_message(sci, SCI_GETLINEENDPOSITION, line, 0);
			break;
		}
		default:
			*start_offset = *end_offset = -1;
			return NULL;
	}

	return get_text_range(sci, *start_offset, *end_offset);
}

static AtkTextGranularity boundary_to_granularity(AtkTextBoundary boundary_type)
{
	switch (boundary_type) {
		default:
		case ATK_TEXT_BOUNDARY_CHAR:
			return ATK_TEXT_GRANULARITY_CHAR;
		case ATK_TEXT_BOUNDARY_WORD_START:
		case ATK_TEXT_BOUNDARY_WORD_END:
			return ATK_TEXT_GRANULARITY_WORD;
		case ATK_TEXT_BOUNDARY_SENTENCE_START:
		case ATK_TEXT_BOUNDARY_SENTENCE_END:
			return ATK_TEXT_GRANULARITY_SENTENCE;
		case ATK_TEXT_BOUNDARY_LINE_START:
		case ATK_TEXT_BOUNDARY_LINE_END:
			return ATK_TEXT_GRANULARITY_LINE;
	}
}

static gchar *scintilla_object_accessible_get_text_at_offset(AtkText *text, gint offset,
		AtkTextBoundary boundary_type, gint *start_offset, gint *end_offset)
{
	// FIXME: this isn't totally what e.g. GtkTextViewAccessible does
	return scintilla_object_accessible_get_string_at_offset(text, offset,
			boundary_to_granularity(boundary_type), start_offset, end_offset);
}

static gunichar scintilla_object_accessible_get_character_at_offset(AtkText *text, gint offset)
{
	g_return_val_if_fail(offset >= 0, 0);

	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return 0;

	// FIXME: support real Unicode character, not bytes?
	return scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_GETCHARAT, offset, 0);
}

static gint scintilla_object_accessible_get_character_count(AtkText *text)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return 0;

	// FIXME: return characters, not bytes?
	return scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_GETLENGTH, 0, 0);
}

static gint scintilla_object_accessible_get_caret_offset(AtkText *text)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE (text));
	if (! widget)
		return 0;

	return scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_GETCURRENTPOS, 0, 0);
}

static gboolean scintilla_object_accessible_set_caret_offset(AtkText *text, gint offset)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE (text));
	if (! widget)
		return 0;

	// FIXME: do we need to scroll explicitly?  it has to happen, but need to check if
	// SCI_SETCURRENTPOS does it
	scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_SETCURRENTPOS, offset, 0);
	return TRUE;
}

static gint scintilla_object_accessible_get_offset_at_point(AtkText *text, gint x, gint y, AtkCoordType coords)
{
	gint x_widget, y_widget, x_window, y_window;
	GtkWidget *widget;
	GdkWindow *window;

	widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return -1;

	window = gtk_widget_get_window(widget);
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
	return scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_CHARPOSITIONFROMPOINTCLOSE, x, y);
}

static void scintilla_object_accessible_get_character_extents(AtkText *text, gint offset,
		gint *x, gint *y, gint *width, gint *height, AtkCoordType coords)
{
	*x = *y = *height = *width = 0;

	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return;
	ScintillaObject *sci = SCINTILLA_OBJECT(widget);

	// FIXME: should we handle scrolling?
	*x = scintilla_send_message(sci, SCI_POINTXFROMPOSITION, 0, offset);
	*y = scintilla_send_message(sci, SCI_POINTYFROMPOSITION, 0, offset);

	int line = scintilla_send_message(sci, SCI_LINEFROMPOSITION, offset, 0);
	*height = scintilla_send_message(sci, SCI_TEXTHEIGHT, line, 0);

	int next_pos = scintilla_send_message(sci, SCI_POSITIONAFTER, offset, 0);
	int next_x = scintilla_send_message(sci, SCI_POINTXFROMPOSITION, 0, next_pos);
	if (next_x > *x) {
		*width = next_x - *x;
	} else if (next_pos > offset) {
		/* maybe next position was on the next line or something.
		 * just compute the expected character width */
		int style = scintilla_send_message(sci, SCI_GETSTYLEAT, offset, 0);
		gchar *ch = get_text_range(sci, offset, next_pos);
		*width = scintilla_send_message(sci, SCI_TEXTWIDTH, style, (sptr_t) ch);
		g_free(ch);
	} else {
		// possibly the last position on the document, so no character here.
		*x = *y = *height = *width = 0;
		return;
	}

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

static AtkAttributeSet *add_text_attribute(AtkAttributeSet *attributes, AtkTextAttribute attr, gchar *value)
{
	AtkAttribute *at = g_new(AtkAttribute, 1);
	at->name = g_strdup(atk_text_attribute_get_name(attr));
	at->value = value;

	return g_slist_prepend(attributes, at);
}

static AtkAttributeSet *add_text_int_attribute(AtkAttributeSet *attributes, AtkTextAttribute attr, gint i)
{
	return add_text_attribute (attributes, attr, g_strdup(atk_text_attribute_get_value(attr, i)));
}

static AtkAttributeSet *get_attributes_for_style(ScintillaObject *sci, gint style)
{
	AtkAttributeSet *attr_set = NULL;

	const int font_len = scintilla_send_message(sci, SCI_STYLEGETFONT, style, 0);
	gchar *font = (char *) g_malloc(font_len + 1);
	scintilla_send_message(sci, SCI_STYLEGETFONT, style, (sptr_t) font);
	attr_set = add_text_attribute(attr_set, ATK_TEXT_ATTR_FAMILY_NAME, font);

	const int size = scintilla_send_message(sci, SCI_STYLEGETSIZE, style, 0);
	attr_set = add_text_attribute(attr_set, ATK_TEXT_ATTR_SIZE, g_strdup_printf("%d", size));

	const int weight = scintilla_send_message(sci, SCI_STYLEGETWEIGHT, style, 0);
	attr_set = add_text_int_attribute(attr_set, ATK_TEXT_ATTR_WEIGHT, CLAMP(weight, 100, 1000));

	const int italic = scintilla_send_message(sci, SCI_STYLEGETITALIC, style, 0);
	attr_set = add_text_int_attribute(attr_set, ATK_TEXT_ATTR_STYLE, italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);

	const int underline = scintilla_send_message(sci, SCI_STYLEGETUNDERLINE, style, 0);
	attr_set = add_text_int_attribute(attr_set, ATK_TEXT_ATTR_UNDERLINE, underline ? PANGO_UNDERLINE_SINGLE : PANGO_UNDERLINE_NONE);

	const int fg = scintilla_send_message(sci, SCI_STYLEGETFORE, style, 0);
	attr_set = add_text_attribute(attr_set, ATK_TEXT_ATTR_FG_COLOR,
	                              g_strdup_printf("%u,%u,%u",
	                                              (guint) (((fg >>  0) & 0xff) * 257),
	                                              (guint) (((fg >>  8) & 0xff) * 257),
	                                              (guint) (((fg >> 16) & 0xff) * 257)));

	const int bg = scintilla_send_message(sci, SCI_STYLEGETBACK, style, 0);
	attr_set = add_text_attribute(attr_set, ATK_TEXT_ATTR_BG_COLOR,
	                              g_strdup_printf("%u,%u,%u",
	                                              (guint) (((bg >>  0) & 0xff) * 257),
	                                              (guint) (((bg >>  8) & 0xff) * 257),
	                                              (guint) (((bg >> 16) & 0xff) * 257)));

	const int visible = scintilla_send_message(sci, SCI_STYLEGETVISIBLE, style, 0);
	attr_set = add_text_int_attribute(attr_set, ATK_TEXT_ATTR_INVISIBLE, ! visible);

	const int changeable = scintilla_send_message(sci, SCI_STYLEGETCHANGEABLE, style, 0);
	attr_set = add_text_int_attribute(attr_set, ATK_TEXT_ATTR_EDITABLE, changeable);

	return attr_set;
}

static AtkAttributeSet *scintilla_object_accessible_get_run_attributes(AtkText *text, gint offset, gint *start_offset, gint *end_offset)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return NULL;
	ScintillaObject *sci = SCINTILLA_OBJECT(widget);

	const gint style = scintilla_send_message(sci, SCI_GETSTYLEAT, offset, 0);

	/* compute the range for this style */
	*start_offset = offset;
	while (*start_offset > 0 && scintilla_send_message(sci, SCI_GETSTYLEAT, (*start_offset) - 1, 0) == style)
		(*start_offset)--;
	*end_offset = offset;
	while ((*end_offset) < scintilla_send_message(sci, SCI_GETLENGTH, (*end_offset) + 1, 0) &&
	       scintilla_send_message(sci, SCI_GETSTYLEAT, (*end_offset) - 1, 0) == style)
		(*end_offset)++;

	/* fill the style info */
	return get_attributes_for_style(sci, style);
}

static AtkAttributeSet *scintilla_object_accessible_get_default_attributes(AtkText *text)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return NULL;

	return get_attributes_for_style(SCINTILLA_OBJECT(widget), 0);
}

static gint scintilla_object_accessible_get_n_selections(AtkText *text)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return 0;

	if (scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_GETSELECTIONEMPTY, 0, 0))
		return 0;
	else
		return scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_GETSELECTIONS, 0, 0);
}

static gchar *scintilla_object_accessible_get_selection(AtkText *atk_text, gint selection_num, gint *start_pos, gint *end_pos)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE (atk_text));
	if (! widget)
		return NULL;

	ScintillaObject *sci = SCINTILLA_OBJECT(widget);
	if (selection_num >= scintilla_send_message(sci, SCI_GETSELECTIONS, 0, 0))
		return NULL;

	struct Sci_TextRange range;
	*start_pos = range.chrg.cpMin = scintilla_send_message(sci, SCI_GETSELECTIONNSTART, selection_num, 0);
	*end_pos = range.chrg.cpMax = scintilla_send_message(sci, SCI_GETSELECTIONNEND, selection_num, 0);
	if (range.chrg.cpMin == range.chrg.cpMax) {
		return NULL;
	} else if (range.chrg.cpMax < range.chrg.cpMin) {
		/* just in case */
		Sci_PositionCR tmp = range.chrg.cpMin;
		range.chrg.cpMin = range.chrg.cpMax;
		range.chrg.cpMax = tmp;
	}

	range.lpstrText = (char *) g_malloc(range.chrg.cpMax - range.chrg.cpMin + 1);
	scintilla_send_message(sci, SCI_GETTEXTRANGE, 0, (sptr_t) &range);

	return range.lpstrText;
}

static gboolean scintilla_object_accessible_add_selection(AtkText *text, gint start, gint end)
{
	GtkWidget *widget = gtk_accessible_get_widget (GTK_ACCESSIBLE (text));
	if (! widget)
		return FALSE;

	ScintillaObject *sci = SCINTILLA_OBJECT(widget);
	int n_selections = scintilla_send_message(sci, SCI_GETSELECTIONS, 0, 0);
	if (n_selections > 1 || ! scintilla_send_message(sci, SCI_GETSELECTIONEMPTY, 0, 0)) {
		scintilla_send_message(sci, SCI_ADDSELECTION, start, end);
	} else {
		scintilla_send_message(sci, SCI_SETSELECTION, start, end);
	}

	return TRUE;
}

static gboolean scintilla_object_accessible_remove_selection(AtkText *text, gint selection_num)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return FALSE;

	ScintillaObject *sci = SCINTILLA_OBJECT(widget);
	int n_selections = scintilla_send_message(sci, SCI_GETSELECTIONS, 0, 0);
	if (selection_num >= n_selections)
		return FALSE;

	if (n_selections > 1) {
		scintilla_send_message(sci, SCI_DROPSELECTIONN, selection_num, 0);
	} else if (scintilla_send_message(sci, SCI_GETSELECTIONEMPTY, 0, 0)) {
		return FALSE;
	} else {
		scintilla_send_message(sci, SCI_CLEARSELECTIONS, 0, 0);
	}

	return TRUE;
}

static gboolean scintilla_object_accessible_set_selection(AtkText *text, gint selection_num, gint start, gint end)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return FALSE;

	ScintillaObject *sci = SCINTILLA_OBJECT(widget);
	if (selection_num >= scintilla_send_message(sci, SCI_GETSELECTIONS, 0, 0))
		return NULL;

	scintilla_send_message(sci, SCI_SETSELECTIONNSTART, selection_num, start);
	scintilla_send_message(sci, SCI_SETSELECTIONNEND, selection_num, end);

	return TRUE;
}

static void atk_text_interface_init (AtkTextIface *iface)
{
	iface->get_text = scintilla_object_accessible_get_text;
	iface->get_text_after_offset = scintilla_object_accessible_get_text_after_offset;
	iface->get_text_at_offset = scintilla_object_accessible_get_text_at_offset;
	iface->get_text_before_offset = scintilla_object_accessible_get_text_before_offset;
#if ATK_CHECK_VERSION(2, 10, 0)
	iface->get_string_at_offset = scintilla_object_accessible_get_string_at_offset;
#endif
	iface->get_character_at_offset = scintilla_object_accessible_get_character_at_offset;
	iface->get_character_count = scintilla_object_accessible_get_character_count;
	iface->get_caret_offset = scintilla_object_accessible_get_caret_offset;
	iface->set_caret_offset = scintilla_object_accessible_set_caret_offset;
	iface->get_offset_at_point = scintilla_object_accessible_get_offset_at_point;
	iface->get_character_extents = scintilla_object_accessible_get_character_extents;
	iface->get_n_selections = scintilla_object_accessible_get_n_selections;
	iface->get_selection = scintilla_object_accessible_get_selection;
	iface->add_selection = scintilla_object_accessible_add_selection;
	iface->remove_selection = scintilla_object_accessible_remove_selection;
	iface->set_selection = scintilla_object_accessible_set_selection;
	iface->get_run_attributes = scintilla_object_accessible_get_run_attributes;
	iface->get_default_attributes = scintilla_object_accessible_get_default_attributes;
}

/* atkeditabletext.h */

static void scintilla_object_accessible_set_text_contents(AtkEditableText *text, const gchar *contents)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return;

	ScintillaObject *sci = SCINTILLA_OBJECT(widget);
	// FIXME: it's probably useless to check for READONLY here, SETTEXT probably does it just fine?
	if (! scintilla_send_message(sci, SCI_GETREADONLY, 0, 0)) {
		scintilla_send_message(sci, SCI_SETTEXT, 0, (sptr_t) contents);
	}
}

static void insert_text(ScintillaObject *sci, const gchar *text, gint length, gint *position)
{
	if (! scintilla_send_message(sci, SCI_GETREADONLY, 0, 0)) {
		gint tgt_start = scintilla_send_message(sci, SCI_GETTARGETSTART, 0, 0);
		gint tgt_end = scintilla_send_message(sci, SCI_GETTARGETEND, 0, 0);

		scintilla_send_message(sci, SCI_SETTARGETRANGE, *position, *position);
		scintilla_send_message(sci, SCI_REPLACETARGET, length, (sptr_t) text);
		(*position) += length;

		// restore the old target
		if (tgt_start > *position)
			tgt_start += length;
		if (tgt_end > *position)
			tgt_end += length;
		scintilla_send_message(sci, SCI_SETTARGETRANGE, tgt_start, tgt_end);
	}
}

static void scintilla_object_accessible_insert_text(AtkEditableText *text, const gchar *contents, gint length, gint *position)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return;

	insert_text(SCINTILLA_OBJECT(widget), contents, length, position);
}

static void scintilla_object_accessible_copy_text(AtkEditableText *text, gint start, gint end)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return;

	scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_COPYRANGE, start, end);
}

static void scintilla_object_accessible_cut_text(AtkEditableText *text, gint start, gint end)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return;

	g_return_if_fail(end >= start);

	ScintillaObject *sci = SCINTILLA_OBJECT(widget);
	if (! scintilla_send_message(sci, SCI_GETREADONLY, 0, 0)) {
		gint tgt_start = scintilla_send_message(sci, SCI_GETTARGETSTART, 0, 0);
		gint tgt_end = scintilla_send_message(sci, SCI_GETTARGETEND, 0, 0);

		struct Sci_TextRange range;
		range.chrg.cpMin = start;
		range.chrg.cpMax = end;
		range.lpstrText = (char *) g_malloc(range.chrg.cpMax - range.chrg.cpMin + 1);
		scintilla_send_message(sci, SCI_GETTEXTRANGE, 0, (sptr_t) &range);
		scintilla_send_message(sci, SCI_COPYTEXT, range.chrg.cpMax - range.chrg.cpMin + 1, (sptr_t) range.lpstrText);
		scintilla_send_message(sci, SCI_SETTARGETRANGE, start, end);
		scintilla_send_message(sci, SCI_REPLACETARGET, 0, (sptr_t) "");

		// restore the old target
		scintilla_send_message(sci, SCI_SETTARGETRANGE, tgt_start, tgt_end);
	}
}

static void scintilla_object_accessible_delete_text(AtkEditableText *text, gint start, gint end)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (! widget)
		return;

	g_return_if_fail(end >= start);

	ScintillaObject *sci = SCINTILLA_OBJECT(widget);
	if (! scintilla_send_message(sci, SCI_GETREADONLY, 0, 0)) {
		gint tgt_start = scintilla_send_message(sci, SCI_GETTARGETSTART, 0, 0);
		gint tgt_end = scintilla_send_message(sci, SCI_GETTARGETEND, 0, 0);

		scintilla_send_message(sci, SCI_SETTARGETRANGE, start, end);
		scintilla_send_message(sci, SCI_REPLACETARGET, 0, (sptr_t) "");

		// restore the old target
		scintilla_send_message(sci, SCI_SETTARGETRANGE, tgt_start, tgt_end);
	}
}

typedef struct
{
	Document *doc;
	gint position;
} PasteData;

static void paste_received(GtkClipboard *clipboard, const gchar *text, gpointer data)
{
	PasteData *paste = (PasteData *) data;

	if (text) {
		paste->doc->InsertString(paste->position, text, (int) strlen(text));
	}

	paste->doc->Release();
}

static void scintilla_object_accessible_paste_text(AtkEditableText *text, gint position)
{
	PasteData paste;

	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (widget == NULL)
		return;

	if (scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_GETREADONLY, 0, 0))
		return;

	paste.doc = ScintillaBaseFromWidget(widget)->pdoc;
	paste.doc->AddRef();
	paste.position = position;

	GtkClipboard *clipboard = gtk_widget_get_clipboard(widget, GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_request_text(clipboard, paste_received, &paste);
}

static void atk_editable_text_interface_init (AtkEditableTextIface *iface)
{
	iface->set_text_contents = scintilla_object_accessible_set_text_contents;
	iface->insert_text = scintilla_object_accessible_insert_text;
	iface->copy_text = scintilla_object_accessible_copy_text;
	iface->cut_text = scintilla_object_accessible_cut_text;
	iface->delete_text = scintilla_object_accessible_delete_text;
	iface->paste_text = scintilla_object_accessible_paste_text;
	//~ iface->set_run_attributes = scintilla_object_accessible_set_run_attributes;
}

/* Callbacks */

static void scintilla_object_accessible_update_cursor(ScintillaObjectAccessible *accessible, ScintillaObject *sci)
{
	ScintillaObjectAccessiblePrivate *priv = SCINTILLA_OBJECT_ACCESSIBLE_GET_PRIVATE(accessible);

	int pos = scintilla_send_message(sci, SCI_GETCURRENTPOS, 0, 0);
	if (priv->pos != pos) {
		g_signal_emit_by_name(accessible, "text-caret-moved", pos);
		priv->pos = pos;
	}

	int n_selections = scintilla_send_message(sci, SCI_GETSELECTIONS, 0, 0);
	int prev_n_selections = priv->carets->len;
	gboolean selection_changed = n_selections != prev_n_selections;

	g_array_set_size(priv->carets, n_selections);
	g_array_set_size(priv->anchors, n_selections);
	for (int i = 0; i < n_selections; i++) {
		int caret = scintilla_send_message(sci, SCI_GETSELECTIONNSTART, i, 0);
		int anchor = scintilla_send_message(sci, SCI_GETSELECTIONNEND, i, 0);

		if (i < prev_n_selections && ! selection_changed) {
			int prev_caret = g_array_index(priv->carets, int, i);
			int prev_anchor = g_array_index(priv->anchors, int, i);
			selection_changed = (prev_caret != caret || prev_anchor != anchor);
		}

		g_array_index(priv->carets, int, i) = caret;
		g_array_index(priv->anchors, int, i) = anchor;
	}

	if (selection_changed)
		g_signal_emit_by_name(accessible, "text-selection-changed");
}

static void sci_notify_handler(GtkWidget *widget, gint code, SCNotification *nt, gpointer data)
{
	ScintillaObjectAccessible *accessible = (ScintillaObjectAccessible *) data;

	switch (nt->nmhdr.code) {
		case SCN_MODIFIED: {
			if (nt->modificationType & SC_MOD_INSERTTEXT) {
				// FIXME: check that
				g_signal_emit_by_name(accessible, "text-changed::insert",
				                      (gint) (nt->position - nt->length), (gint) nt->length);
				scintilla_object_accessible_update_cursor(accessible, SCINTILLA_OBJECT(widget));
			}
			if (nt->modificationType & SC_MOD_DELETETEXT) {
				// FIXME: check that
				g_signal_emit_by_name(accessible, "text-changed::delete", (gint) nt->position, (gint) nt->length);
				scintilla_object_accessible_update_cursor(accessible, SCINTILLA_OBJECT(widget));
			}
			if (nt->modificationType & SC_MOD_CHANGESTYLE) {
				g_signal_emit_by_name(accessible, "text-attributes-changed");
			}
		} break;
		case SCN_UPDATEUI: {
			ScintillaObjectAccessiblePrivate *priv = SCINTILLA_OBJECT_ACCESSIBLE_GET_PRIVATE(accessible);
			ScintillaObject *sci = SCINTILLA_OBJECT(widget);

			if (nt->updated & SC_UPDATE_SELECTION) {
				scintilla_object_accessible_update_cursor(accessible, sci);

				// SC_UPDATE_SELECTION is the only signal we get when DOCPOINTER changes, so check here
				Document *doc = ScintillaBaseFromWidget(widget)->pdoc;
				if (doc != priv->doc) {
					scintilla_object_accessible_change_document(accessible, sci, doc);
				}
			}
			int readonly = scintilla_send_message(sci, SCI_GETREADONLY, 0, 0);
			if (priv->readonly != readonly) {
				atk_object_notify_state_change(gtk_widget_get_accessible(widget), ATK_STATE_EDITABLE, ! readonly);
				priv->readonly = readonly;
			}
		} break;
	}
}

#if 0
void
_gtk_text_view_accessible_set_buffer (GtkTextView   *textview,
                                      GtkTextBuffer *old_buffer)
{
  GtkTextViewAccessible *accessible;

  g_return_if_fail (GTK_IS_TEXT_VIEW (textview));
  g_return_if_fail (old_buffer == NULL || GTK_IS_TEXT_BUFFER (old_buffer));

  accessible = GTK_TEXT_VIEW_ACCESSIBLE (_gtk_widget_peek_accessible (GTK_WIDGET (textview)));
  if (accessible == NULL)
    return;

  gtk_text_view_accessible_change_buffer (accessible,
                                          old_buffer,
                                          gtk_text_view_get_buffer (textview));
}
#endif

#if HAVE_GTK_FACTORY
// Object factory
typedef AtkObjectFactory ScintillaObjectAccessibleFactory;
typedef AtkObjectFactoryClass ScintillaObjectAccessibleFactoryClass;

G_DEFINE_TYPE(ScintillaObjectAccessibleFactory, scintilla_object_accessible_factory, ATK_TYPE_OBJECT_FACTORY)

static void scintilla_object_accessible_factory_init(ScintillaObjectAccessibleFactory *factory)
{
}

static GType scintilla_object_accessible_factory_get_accessible_type(void)
{
	return SCINTILLA_TYPE_OBJECT_ACCESSIBLE;
}

static AtkObject *scintilla_object_accessible_factory_create_accessible(GObject *obj)
{
	return scintilla_object_accessible_new(0, obj);
}

static void scintilla_object_accessible_factory_class_init(AtkObjectFactoryClass * klass)
{
	klass->create_accessible = scintilla_object_accessible_factory_create_accessible;
	klass->get_accessible_type = scintilla_object_accessible_factory_get_accessible_type;
}
#endif
