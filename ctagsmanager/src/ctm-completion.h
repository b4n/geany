/*
 *  Copyright (C) 2011  Colomban Wendling <ban@herbesfolles.org>
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 */

#ifndef H_CTM_COMPLETION
#define H_CTM_COMPLETION

#include <glib.h>

#include "ctm-source-file.h"
#include "ctm-tag.h"

G_BEGIN_DECLS


GList      *ctm_completion_get_scoped_name        (CtmSourceFile *file,
                                                   langType       lang,
                                                   const gchar   *scope,
                                                   const gchar   *name,
                                                   gboolean       partial);
GList      *ctm_completion_get_scoped_methods     (CtmSourceFile *file,
                                                   langType       lang,
                                                   const gchar   *scope,
                                                   const gchar   *name,
                                                   gboolean       partial);
GList      *ctm_completion_get_methods            (CtmSourceFile *file,
                                                   langType       lang,
                                                   const gchar   *name,
                                                   gboolean       partial);
GList      *ctm_completion_get_scope_members      (CtmSourceFile  *file,
                                                   const gchar    *scope);
GList      *ctm_completion_get_scope_completions  (CtmSourceFile  *file,
                                                   const gchar    *name);
GList      *ctm_completion_get_completions        (CtmSourceFile *file,
                                                   const gchar   *start);
CtmTag     *ctm_completion_get_function_at_line   (CtmSourceFile *file,
                                                   gulong         line);


G_END_DECLS

#endif /* guard */
