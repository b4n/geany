/* Scintilla source code edit control */
/* ScintillaGTKAccessible.cxx - GTK+ accessibility for ScintillaGTK */
/* Copyright 2016 by Colomban Wendling <colomban@geany.org>
 * The License.txt file describes the conditions under which this software may be distributed. */

/* based on GtkTextViewAccessible
 * Copyright 2001, 2002, 2003 Sun Microsystems Inc.
 * GPL2
 * FIXME */

#include <sys/types.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#define GTK
#include "ScintillaGTKAccessible.h"
#include "Scintilla.h"
#include "ScintillaWidget.h"

struct _ScintillaObjectAccessiblePrivate
{
	gboolean readonly;

	gint pos;
	GArray *carets;
	GArray *anchors;
};

static void sci_notify_handler(GtkWidget *widget, gint code, SCNotification *nt, gpointer data);

static void atk_editable_text_interface_init(AtkEditableTextIface *iface);
static void atk_text_interface_init(AtkTextIface *iface);

G_DEFINE_TYPE_WITH_CODE(ScintillaObjectAccessible, scintilla_object_accessible, GTK_TYPE_CONTAINER_ACCESSIBLE,
                        G_ADD_PRIVATE (ScintillaObjectAccessible)
                        G_IMPLEMENT_INTERFACE (ATK_TYPE_EDITABLE_TEXT, atk_editable_text_interface_init)
                        G_IMPLEMENT_INTERFACE (ATK_TYPE_TEXT, atk_text_interface_init))


static void scintilla_object_accessible_initialize(AtkObject *obj, gpointer data)
{
	ATK_OBJECT_CLASS (scintilla_object_accessible_parent_class)->initialize(obj, data);

	obj->role = ATK_ROLE_TEXT;
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

/* FIXME: how to get notified about document changes? */
static void scintilla_object_accessible_change_document(ScintillaObjectAccessible *accessible, void *old_doc, void *new_doc)
{
	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(accessible));
	if (widget == NULL)
		return;

	if (old_doc) {
		g_signal_emit_by_name(accessible, "text-changed::delete", 0,
		                      scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_GETLENGTH, 0, 0));
    }

	if (new_doc)
	{
		g_signal_emit_by_name(accessible, "text-changed::inserted", 0,
		                      scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_GETLENGTH, 0, 0));

		accessible->priv->readonly = scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_GETREADONLY, 0, 0);
	}
}

static void scintilla_object_accessible_widget_set(GtkAccessible *accessible)
{
	GtkWidget *widget = gtk_accessible_get_widget(accessible);
	if (widget == NULL)
		return;

	scintilla_object_accessible_change_document(SCINTILLA_OBJECT_ACCESSIBLE(accessible),
			NULL, (void*) scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_GETDOCPOINTER, 0, 0));

	g_signal_connect(widget, "sci-notify", G_CALLBACK(sci_notify_handler), accessible);
}

static void scintilla_object_accessible_widget_unset(GtkAccessible *accessible)
{
	GtkWidget *widget = gtk_accessible_get_widget(accessible);
	if (widget == NULL)
		return;

	scintilla_object_accessible_change_document(SCINTILLA_OBJECT_ACCESSIBLE(accessible),
			(void*) scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_GETDOCPOINTER, 0, 0), NULL);
}

static void scintilla_object_accessible_finalize(GObject *object)
{
	ScintillaObjectAccessible *accessible = SCINTILLA_OBJECT_ACCESSIBLE(object);

	g_array_free(accessible->priv->carets, TRUE);
	g_array_free(accessible->priv->anchors, TRUE);
}

static void scintilla_object_accessible_class_init(ScintillaObjectAccessibleClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	AtkObjectClass *object_class = ATK_OBJECT_CLASS(klass);
	GtkAccessibleClass *accessible_class = GTK_ACCESSIBLE_CLASS(klass);

	accessible_class->widget_set = scintilla_object_accessible_widget_set;
	accessible_class->widget_unset = scintilla_object_accessible_widget_unset;

	object_class->ref_state_set = scintilla_object_accessible_ref_state_set;
	object_class->initialize = scintilla_object_accessible_initialize;

	gobject_class->finalize = scintilla_object_accessible_finalize;
}

static void scintilla_object_accessible_init(ScintillaObjectAccessible *accessible)
{
	accessible->priv = scintilla_object_accessible_get_instance_private(accessible);

	accessible->priv->pos = 0;
	accessible->priv->readonly = FALSE;

	accessible->priv->carets = g_array_new(FALSE, FALSE, sizeof(int));
	accessible->priv->anchors = g_array_new(FALSE, FALSE, sizeof(int));
}

static gchar *get_text_range(ScintillaObject *sci, gint start_offset, gint end_offset)
{
	struct Sci_TextRange range;

	g_return_val_if_fail(start_offset >= 0, NULL);
	g_return_val_if_fail(end_offset >= start_offset, NULL);

	range.chrg.cpMin = start_offset;
	range.chrg.cpMax = end_offset;
	range.lpstrText = g_malloc(end_offset - start_offset + 1);
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

#if 0 // FIXME: implement those, because Orca still use them
static gchar *
gtk_text_view_accessible_get_text_after_offset (AtkText         *text,
                                                gint             offset,
                                                AtkTextBoundary  boundary_type,
                                                gint            *start_offset,
                                                gint            *end_offset)
{
  GtkWidget *widget;
  GtkTextView *view;
  GtkTextBuffer *buffer;
  GtkTextIter pos;
  GtkTextIter start, end;

  widget = gtk_accessible_get_widget (GTK_ACCESSIBLE (text));
  if (widget == NULL)
    return NULL;

  view = GTK_TEXT_VIEW (widget);
  buffer = gtk_text_view_get_buffer (view);
  gtk_text_buffer_get_iter_at_offset (buffer, &pos, offset);
  start = end = pos;
  if (boundary_type == ATK_TEXT_BOUNDARY_LINE_START)
    {
      gtk_text_view_forward_display_line (view, &end);
      start = end;
      gtk_text_view_forward_display_line (view, &end);
    }
  else if (boundary_type == ATK_TEXT_BOUNDARY_LINE_END)
    {
      gtk_text_view_forward_display_line_end (view, &end);
      start = end;
      gtk_text_view_forward_display_line (view, &end);
      gtk_text_view_forward_display_line_end (view, &end);
    }
  else
    _gtk_text_buffer_get_text_after (buffer, boundary_type, &pos, &start, &end);

  *start_offset = gtk_text_iter_get_offset (&start);
  *end_offset = gtk_text_iter_get_offset (&end);

  return gtk_text_buffer_get_slice (buffer, &start, &end, FALSE);
}

static gchar *
gtk_text_view_accessible_get_text_before_offset (AtkText         *text,
                                                 gint             offset,
                                                 AtkTextBoundary  boundary_type,
                                                 gint            *start_offset,
                                                 gint            *end_offset)
{
  GtkWidget *widget;
  GtkTextView *view;
  GtkTextBuffer *buffer;
  GtkTextIter pos;
  GtkTextIter start, end;

  widget = gtk_accessible_get_widget (GTK_ACCESSIBLE (text));
  if (widget == NULL)
    return NULL;

  view = GTK_TEXT_VIEW (widget);
  buffer = gtk_text_view_get_buffer (view);
  gtk_text_buffer_get_iter_at_offset (buffer, &pos, offset);
  start = end = pos;

  if (boundary_type == ATK_TEXT_BOUNDARY_LINE_START)
    {
      gtk_text_view_backward_display_line_start (view, &start);
      end = start;
      gtk_text_view_backward_display_line (view, &start);
      gtk_text_view_backward_display_line_start (view, &start);
    }
  else if (boundary_type == ATK_TEXT_BOUNDARY_LINE_END)
    {
      gtk_text_view_backward_display_line_start (view, &start);
      if (!gtk_text_iter_is_start (&start))
        {
          gtk_text_view_backward_display_line (view, &start);
          end = start;
          gtk_text_view_forward_display_line_end (view, &end);
          if (!gtk_text_iter_is_start (&start))
            {
              if (gtk_text_view_backward_display_line (view, &start))
                gtk_text_view_forward_display_line_end (view, &start);
              else
                gtk_text_iter_set_offset (&start, 0);
            }
        }
      else
        end = start;
    }
  else
    _gtk_text_buffer_get_text_before (buffer, boundary_type, &pos, &start, &end);

  *start_offset = gtk_text_iter_get_offset (&start);
  *end_offset = gtk_text_iter_get_offset (&end);

  return gtk_text_buffer_get_slice (buffer, &start, &end, FALSE);
}
#endif

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
			*start_offset = scintilla_send_message(sci, SCI_WORDSTARTPOSITION, offset, 0);
			*end_offset = scintilla_send_message(sci, SCI_WORDENDPOSITION, offset, 0);
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
	gchar *font = g_malloc(font_len + 1);
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

	range.lpstrText = g_malloc(range.chrg.cpMax - range.chrg.cpMin + 1);
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
	//~ iface->get_text_after_offset = scintilla_object_accessible_get_text_after_offset;
	iface->get_text_at_offset = scintilla_object_accessible_get_text_at_offset;
	//~ iface->get_text_before_offset = scintilla_object_accessible_get_text_before_offset;
	iface->get_string_at_offset = scintilla_object_accessible_get_string_at_offset;
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
		range.lpstrText = g_malloc(range.chrg.cpMax - range.chrg.cpMin + 1);
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
	ScintillaObject *sci;
	void *doc;
	gint position;
} PasteData;

static void paste_received(GtkClipboard *clipboard, const gchar *text, gpointer data)
{
	PasteData *paste = (PasteData *) data;

	if (text) {
		if (paste->doc != (void*) scintilla_send_message(paste->sci, SCI_GETDOCPOINTER, 0, 0)) {
			// dammit, doc pointer changed (unlikely, but who knows)
			g_object_unref(paste->sci);
			paste->sci = g_object_ref_sink(scintilla_object_new());
			scintilla_send_message(paste->sci, SCI_SETDOCPOINTER, 0, (sptr_t) paste->doc);
		}

		insert_text(paste->sci, text, strlen(text), &paste->position);
	}

	scintilla_send_message(paste->sci, SCI_RELEASEDOCUMENT, 0, (sptr_t) paste->doc);
	g_object_unref(paste->sci);
}

static void scintilla_object_accessible_paste_text(AtkEditableText *text, gint position)
{
	PasteData paste;

	GtkWidget *widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(text));
	if (widget == NULL)
		return;

	paste.sci = SCINTILLA_OBJECT(widget);
	if (scintilla_send_message(paste.sci, SCI_GETREADONLY, 0, 0))
		return;

	g_object_ref(paste.sci);
	paste.doc = (void*) scintilla_send_message(paste.sci, SCI_GETDOCPOINTER, 0, 0);
	scintilla_send_message(paste.sci, SCI_ADDREFDOCUMENT, 0, (sptr_t) paste.doc);
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
	int pos = scintilla_send_message(sci, SCI_GETCURRENTPOS, 0, 0);
	if (accessible->priv->pos != pos) {
		g_signal_emit_by_name(accessible, "text-caret-moved", pos);
		accessible->priv->pos = pos;
	}

	int n_selections = scintilla_send_message(sci, SCI_GETSELECTIONS, 0, 0);
	int prev_n_selections = accessible->priv->carets->len;
	gboolean selection_changed = n_selections != prev_n_selections;

	g_array_set_size(accessible->priv->carets, n_selections);
	g_array_set_size(accessible->priv->anchors, n_selections);
	for (int i = 0; i < n_selections; i++) {
		int caret = scintilla_send_message(sci, SCI_GETSELECTIONNSTART, i, 0);
		int anchor = scintilla_send_message(sci, SCI_GETSELECTIONNEND, i, 0);

		if (i < prev_n_selections && ! selection_changed) {
			int prev_caret = g_array_index(accessible->priv->carets, int, i);
			int prev_anchor = g_array_index(accessible->priv->anchors, int, i);
			selection_changed = (prev_caret != caret || prev_anchor != anchor);
		}

		g_array_index(accessible->priv->carets, int, i) = caret;
		g_array_index(accessible->priv->anchors, int, i) = anchor;
	}

	if (selection_changed)
		g_signal_emit_by_name(accessible, "text-selection-changed");
}

static void sci_notify_handler(GtkWidget *widget, gint code, SCNotification *nt, gpointer data)
{
	ScintillaObjectAccessible *accessible = data;

	switch (nt->nmhdr.code) {
		case SCN_MODIFIED: {
			switch (nt->modificationType) {
				case SC_MOD_INSERTTEXT: {
					// FIXME: check that
					g_signal_emit_by_name(accessible, "text-changed::insert",
					                      nt->position - nt->length, nt->length);
					scintilla_object_accessible_update_cursor(accessible, SCINTILLA_OBJECT(widget));
				} break;
				case SC_MOD_DELETETEXT: {
					// FIXME: check that
					g_signal_emit_by_name(accessible, "text-changed::delete", nt->position, nt->length);
					scintilla_object_accessible_update_cursor(accessible, SCINTILLA_OBJECT(widget));
				} break;
			}
		} break;
		case SCN_UPDATEUI: {
			if (nt->updated & SC_UPDATE_SELECTION) {
				scintilla_object_accessible_update_cursor(accessible, SCINTILLA_OBJECT(widget));
			}
			int readonly = scintilla_send_message(SCINTILLA_OBJECT(widget), SCI_GETREADONLY, 0, 0);
			if (accessible->priv->readonly != readonly) {
				atk_object_notify_state_change(gtk_widget_get_accessible(widget), ATK_STATE_EDITABLE, ! readonly);
				accessible->priv->readonly = readonly;
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
