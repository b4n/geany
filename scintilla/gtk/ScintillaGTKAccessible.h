/* GTK+ - accessibility implementations
 * Copyright 2001 Sun Microsystems Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SCINTILLAGTKACCESSIBLE_H
#define SCINTILLAGTKACCESSIBLE_H

#include <gtk/gtk-a11y.h>

G_BEGIN_DECLS

#define SCINTILLA_TYPE_OBJECT_ACCESSIBLE                  (scintilla_object_accessible_get_type())
#define SCINTILLA_OBJECT_ACCESSIBLE(obj)                  (G_TYPE_CHECK_INSTANCE_CAST((obj), SCINTILLA_TYPE_OBJECT_ACCESSIBLE, ScintillaObjectAccessible))
#define SCINTILLA_OBJECT_ACCESSIBLE_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST((klass), SCINTILLA_TYPE_OBJECT_ACCESSIBLE, ScintillaObjectAccessibleClass))
#define SCINTILLA_IS_OBJECT_ACCESSIBLE(obj)               (G_TYPE_CHECK_INSTANCE_TYPE((obj), SCINTILLA_TYPE_OBJECT_ACCESSIBLE))
#define SCINTILLA_IS_OBJECT_ACCESSIBLE_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE((klass), SCINTILLA_TYPE_OBJECT_ACCESSIBLE))
#define SCINTILLA_OBJECT_ACCESSIBLE_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS((obj), SCINTILLA_TYPE_OBJECT_ACCESSIBLE, ScintillaObjectAccessibleClass))

typedef struct _ScintillaObjectAccessible			ScintillaObjectAccessible;
typedef struct _ScintillaObjectAccessibleClass		ScintillaObjectAccessibleClass;
typedef struct _ScintillaObjectAccessiblePrivate	ScintillaObjectAccessiblePrivate;

struct _ScintillaObjectAccessible
{
  GtkContainerAccessible parent;

  ScintillaObjectAccessiblePrivate *priv;
};

struct _ScintillaObjectAccessibleClass
{
  GtkContainerAccessibleClass parent_class;
};


GType scintilla_object_accessible_get_type(void);


G_END_DECLS

#endif /* SCINTILLAGTKACCESSIBLE_H */
