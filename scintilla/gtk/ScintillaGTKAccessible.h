/* Scintilla source code edit control */
/* ScintillaGTKAccessible.h - GTK+ accessibility for ScintillaGTK */
/* Copyright 2016 by Colomban Wendling <colomban@geany.org>
 * The License.txt file describes the conditions under which this software may be distributed. */

#ifndef SCINTILLAGTKACCESSIBLE_H
#define SCINTILLAGTKACCESSIBLE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS


AtkObject *scintilla_object_accessible_widget_get_accessible_impl(GtkWidget *widget, AtkObject **cache, gpointer widget_parent_class);


G_END_DECLS

#endif /* SCINTILLAGTKACCESSIBLE_H */
