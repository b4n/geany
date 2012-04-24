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

#ifndef H_CTM_SOURCE_FILE
#define H_CTM_SOURCE_FILE

#include <glib.h>

G_BEGIN_DECLS
/* types are declared before local includes for circular deps */
typedef struct _CtmSourceFile CtmSourceFile;
G_END_DECLS

#include "ctm-data-backend.h"

#ifdef CTM_BUILD
# include "ctags/parse.h"
#else
typedef int langType;
#endif

G_BEGIN_DECLS


struct _CtmSourceFile
{
  gint            ref_count;
  
  gchar          *name;
  langType        lang;
  CtmDataBackend *backend;
};


CtmSourceFile    *ctm_source_file_new           (const gchar *name,
                                                 langType     lang);
CtmSourceFile    *ctm_source_file_ref           (CtmSourceFile *sf);
void              ctm_source_file_unref         (CtmSourceFile *sf);
void              ctm_source_file_set_name      (CtmSourceFile   *sf,
                                                 const gchar     *name);
void              ctm_source_file_set_lang      (CtmSourceFile   *sf,
                                                 langType         lang);
void              ctm_source_file_set_backend   (CtmSourceFile   *sf,
                                                 CtmDataBackend  *backend);


G_END_DECLS

#endif /* guard */
