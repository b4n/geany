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

#include "ctm-workspace.h"

#include <glib.h>

#include "ctm-data-backend.h"
#include "ctm-source-file.h"


static CtmWorkspace  *default_ws = NULL;

CtmWorkspace *
ctm_workspace_get_default (void)
{
  if (! default_ws) {
    default_ws = g_malloc (sizeof *default_ws);
    default_ws->ref_count   = 1;
    default_ws->files       = NULL;
    default_ws->tags        = ctm_data_backend_new ();
    default_ws->global_tags = ctm_data_backend_new ();
  }
  
  return default_ws;
}

CtmWorkspace *
ctm_workspace_ref (CtmWorkspace *ws)
{
  g_atomic_int_inc (&ws->ref_count);
  
  return ws;
}

void
ctm_workspace_unref (CtmWorkspace *ws)
{
  if (g_atomic_int_dec_and_test (&ws->ref_count)) {
    g_critical ("Destroying a CtmWorkspace. "
                "It's unlikely to be a wanted thing.");
    
    ctm_data_backend_unref (ws->global_tags);
    ctm_data_backend_unref (ws->tags);
    g_free (ws);
  }
}

void
ctm_workspace_add (CtmWorkspace  *ws,
                   CtmSourceFile *file)
{
  ws->files = g_list_prepend (ws->files, ctm_source_file_ref (file));
}

void
ctm_workspace_remove (CtmWorkspace  *ws,
                      CtmSourceFile *file)
{
  GList *item;
  
  item = g_list_find (ws->files, file);
  if (item) {
    ctm_source_file_unref (item->data);
    ws->files = g_list_delete_link (ws->files, item);
    
    /* FIXME: use
     * ctm_data_backend_remove_matched (ws->tags, 0,
     *                                  ctm_tag_cmp_file,
     *                                  ctm_tag_match_file, file); */
    ctm_workspace_update (ws);
  }
}

void
ctm_workspace_update (CtmWorkspace *ws)
{
  GList *item;
  
  ctm_data_backend_clear (ws->tags);
  for (item = ws->files; item; item = item->next) {
    CtmSourceFile *file = item->data;
    
    ctm_data_backend_merge (file->backend, ws->tags);
  }
}

void
ctm_workspace_update_file (CtmWorkspace  *ws,
                           CtmSourceFile *file)
{
  GList *tags;
  GList *item;
  
  /* FIXME: implement this in the backend side */
  tags = ctm_data_backend_find (ws->tags, 0, CTM_DATA_BACKEND_SORT_DIR_NONE,
                                ctm_tag_cmp_file, ctm_tag_match_file, file);
  for (item = tags; item; item = item->next) {
    ctm_data_backend_remove (ws->tags, item->data);
    ctm_tag_unref (item->data);
  }
  g_list_free (tags);
  
  /* and now add new tags */
  ctm_data_backend_merge (file->backend, ws->tags);
}

/**
 * ctm_workspace_find_with_file:
 * @ws: A #CtmWorkspace
 * @file: A #CtmSourceFile where search in priority, or %NULL
 * @all: Whether to find all tags or only until first match
 * @limit: Limit
 * @sort_dir: Sort direction
 * @cmp_func: Comparison function
 * @match_func: Match function
 * @user_data: User data for @match_func
 * 
 * Wrapper for ctm_data_backend_find() that searches in @file and and in the
 * workspace tags.
 * 
 * Returns: The list of tags found
 */
GList *
ctm_workspace_find (CtmWorkspace               *ws,
                    CtmSourceFile              *file,
                    gboolean                    all,
                    guint                       limit,
                    CtmDataBackendSortDirection sort_dir,
                    CtmTagCompareFunc           cmp_func,
                    CtmTagMatchFunc             match_func,
                    gpointer                    user_data)
{
  guint           i;
  CtmDataBackend *backend[3];
  GList          *tags = NULL;
  
  g_return_val_if_fail (ws != NULL, NULL);
  
  backend[0] = file ? file->backend : NULL;
  backend[1] = ws->tags;
  backend[2] = ws->global_tags;
  
  for (i = 0; (all || ! tags) && i < G_N_ELEMENTS (backend); i++) {
    if (backend[i]) {
      GList *result;
      
      result = ctm_data_backend_find (backend[i], limit, sort_dir, cmp_func,
                                      match_func, user_data);
      tags = g_list_concat (tags, result);
    }
  }
  
  return tags;
}

CtmTag *
ctm_workspace_find_first (CtmWorkspace     *ws,
                          CtmSourceFile    *file,
                          CtmTagCompareFunc cmp_func,
                          CtmTagMatchFunc   match_func,
                          gpointer          user_data)
{
  guint           i;
  CtmDataBackend *backend[3];
  CtmTag         *tag = NULL;
  
  g_return_val_if_fail (ws != NULL, NULL);
  
  backend[0] = file ? file->backend : NULL;
  backend[1] = ws->tags;
  backend[2] = ws->global_tags;
  
  for (i = 0; ! tag && i < G_N_ELEMENTS (backend); i++) {
    if (backend[i]) {
      tag = ctm_data_backend_find_first (backend[i], cmp_func,
                                         match_func, user_data);
    }
  }
  
  return tag;
}
