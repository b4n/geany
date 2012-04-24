/*
 *  Copyright (C) 2012  Colomban Wendling <ban@herbesfolles.org>
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

/*
 * Simple and naive backend implementation.
 */

#include <string.h>
#include <glib.h>

#include "ctm-data-backend.h"
#include "ctm-tag.h"


typedef struct _CtmDataBackendSimple
{
  CtmDataBackend  parent;
  
  GPtrArray      *array;
} CtmDataBackendSimple;


static void
ctm_data_backend_simple_insert (CtmDataBackend  *backend,
                                CtmTag          *tag)
{
  CtmDataBackendSimple *self = (CtmDataBackendSimple *) backend;
  
  ctm_data_backend_write_lock (backend);
  g_ptr_array_add (self->array, ctm_tag_ref (tag));
  ctm_data_backend_write_unlock (backend);
}

static void
ctm_data_backend_simple_remove (CtmDataBackend  *backend,
                                CtmTag          *tag)
{
  CtmDataBackendSimple *self = (CtmDataBackendSimple *) backend;
  
  ctm_data_backend_write_lock (backend);
  /* what criteria should be used to find the tag to remove?  like find() or
   * by pointer?  if like find(), what fields to match?  all? */
  g_ptr_array_remove (self->array, tag);
  ctm_data_backend_write_unlock (backend);
}

static GList *
ctm_data_backend_simple_find (CtmDataBackend             *backend,
                              guint                       limit,
                              CtmDataBackendSortDirection sort_dir,
                              CtmTagCompareFunc           cmp_func,
                              CtmTagMatchFunc             match_func,
                              gpointer                    data)
{
  CtmDataBackendSimple *self = (CtmDataBackendSimple *) backend;
  guint                 i;
  guint                 n = 0;
  GList                *tags = NULL;
  
  ctm_data_backend_read_lock (backend);
  for (i = 0; i < self->array->len; i++) {
    CtmTag *tag;
    
    tag = g_ptr_array_index (self->array, i);
    if (match_func (tag, data) == 0) {
      tags = g_list_prepend (tags, ctm_tag_ref (tag));
      n ++;
      if (limit && n >= limit) {
        break;
      }
    }
  }
  ctm_data_backend_read_unlock (backend);
  switch (sort_dir) {
    case CTM_DATA_BACKEND_SORT_DIR_ASC:
      tags = g_list_sort (tags, (GCompareFunc) cmp_func);
      break;
    
    case CTM_DATA_BACKEND_SORT_DIR_DESC:
      tags = g_list_sort (tags, (GCompareFunc) cmp_func);
      /* fallthough */
    
    case CTM_DATA_BACKEND_SORT_DIR_NONE:
      tags = g_list_reverse (tags);
      break;
  }
  
  return tags;
}

static void
ctm_data_backend_simple_merge (CtmDataBackend  *backend,
                               CtmDataBackend  *dest)
{
  const CtmDataBackendSimple *self = (const CtmDataBackendSimple *) backend;
  guint                       i;
  
  ctm_data_backend_read_lock (backend);
  for (i = 0; i < self->array->len; i++) {
    CtmTag *tag;
    
    tag = g_ptr_array_index (self->array, i);
    ctm_data_backend_insert (dest, tag);
  }
  ctm_data_backend_read_unlock (backend);
}

static void
ctm_data_backend_simple_clear (CtmDataBackend  *backend)
{
  const CtmDataBackendSimple *self = (const CtmDataBackendSimple *) backend;
  
  ctm_data_backend_write_lock (backend);
  g_ptr_array_set_size (self->array, 0);
  ctm_data_backend_write_unlock (backend);
}


static void
ctm_data_backend_simple_free (CtmDataBackend *backend)
{
  CtmDataBackendSimple *self = (CtmDataBackendSimple *) backend;
  
  g_ptr_array_unref (self->array);
}

CtmDataBackend *
ctm_data_backend_simple_new (void)
{
  CtmDataBackend       *backend;
  CtmDataBackendSimple *self;
  
  backend = _ctm_data_backend_alloc (sizeof (CtmDataBackendSimple), 1/*FIXME*/);
  self = (CtmDataBackendSimple *) backend;
  
  backend->free         = ctm_data_backend_simple_free;
  backend->insert       = ctm_data_backend_simple_insert;
  backend->remove       = ctm_data_backend_simple_remove;
  backend->find         = ctm_data_backend_simple_find;
  backend->merge        = ctm_data_backend_simple_merge;
  backend->clear        = ctm_data_backend_simple_clear;
  
  self->array = g_ptr_array_new_with_free_func ((GDestroyNotify) ctm_tag_unref);
  
  return backend;
}
