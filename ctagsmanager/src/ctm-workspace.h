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

#ifndef H_CTM_WORKSPACE
#define H_CTM_WORKSPACE

#include <glib.h>

#include "ctm-data-backend.h"
#include "ctm-source-file.h"

G_BEGIN_DECLS


typedef struct _CtmWorkspace {
  gint            ref_count;
  
  GList          *files;
  CtmDataBackend *tags;
  CtmDataBackend *global_tags;
} CtmWorkspace;


CtmWorkspace   *ctm_workspace_get_default   (void);
CtmWorkspace   *ctm_workspace_ref           (CtmWorkspace *ws);
void            ctm_workspace_unref         (CtmWorkspace *ws);
void            ctm_workspace_add           (CtmWorkspace  *ws,
                                             CtmSourceFile *file);
void            ctm_workspace_remove        (CtmWorkspace  *ws,
                                             CtmSourceFile *file);
void            ctm_workspace_update        (CtmWorkspace  *ws);
void            ctm_workspace_update_file   (CtmWorkspace  *ws,
                                             CtmSourceFile *file);

GList          *ctm_workspace_find          (CtmWorkspace                *ws,
                                             CtmSourceFile               *file,
                                             gboolean                     all,
                                             guint                        limit,
                                             CtmDataBackendSortDirection  sort_dir,
                                             CtmTagCompareFunc            cmp_func,
                                             CtmTagMatchFunc              match_func,
                                             gpointer                     user_data);
CtmTag       *ctm_workspace_find_first      (CtmWorkspace               *ws,
                                             CtmSourceFile              *file,
                                             CtmTagCompareFunc           cmp_func,
                                             CtmTagMatchFunc             match_func,
                                             gpointer                    user_data);


G_END_DECLS

#endif /* guard */
