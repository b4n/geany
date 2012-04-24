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

#include "ctm-data-backend.h"

#include <stdarg.h>
#include <glib.h>

#include "ctm-tag.h"


/**
 * Common & wrapper backend code.
 * 
 * Data backends stores the #CtmTag objects and allows find them by various
 * criteria.
 * 
 * The goal of data backends is to provide optimized data representation for
 * the various possible cases.  For example, a particular backend might be
 * optimized for low memory consumption, while another might focus on speed at
 * the memory consumption expense.
 * 
 * Currently only the #CtmDataBackendSimple implementation exists and it is not
 * focused on any particular aspect apart being actually functional.
 */



/* default and naive implementation of ctm_data_backend_merge() */
static void
ctm_data_backend_merge_impl (CtmDataBackend  *src,
                             CtmDataBackend  *dest)
{
  GList  *tags;
  GList  *item;
  
  tags = ctm_data_backend_find (src, 0, CTM_DATA_BACKEND_SORT_DIR_NONE,
                                ctm_tag_cmp_name,
                                ctm_tag_match_all, NULL); /* finds all */
  for (item = tags; item; item = item->next) {
    CtmTag *tag = item->data;
    
    ctm_data_backend_insert (dest, tag);
    ctm_tag_unref (tag);
  }
  g_list_free (tags);
}

/* default and naive implementation of ctm_data_backend_clear() */
static void
ctm_data_backend_clear_impl (CtmDataBackend *backend)
{
  GList *tags;
  GList *item;
  
  tags = ctm_data_backend_find (backend, 0, CTM_DATA_BACKEND_SORT_DIR_NONE,
                                ctm_tag_cmp_name, ctm_tag_match_all, NULL);
  for (item = tags; item; item = item->next) {
    ctm_data_backend_remove (backend, item->data);
    ctm_tag_unref (item->data);
  }
  g_list_free (tags);
}


/*
 * _ctm_data_backend_alloc:
 * @struct_size: size of the backend struct to allocate
 * @backend_type: type of the backend
 * 
 * Allocates a backend structure and fills the common fields with their
 * default values.
 * 
 * Returns: A newly allocated backend structure
 */
CtmDataBackend *
_ctm_data_backend_alloc (gsize  struct_size,
                         guint  backend_type)
{
  CtmDataBackend *backend;
  
  g_assert (struct_size >= sizeof *backend);
  
  backend = g_slice_alloc (struct_size);
  backend->ref_count    = 1;
  backend->size         = struct_size;
  backend->backend_type = backend_type;
#ifdef CTM_DATA_BACKEND_THREADSAFE
# if GLIB_CHECK_VERSION (2, 32, 0)
  g_rw_lock_init (&backend->lock);
# else
  backend->lock = g_mutex_new ();
# endif
#endif
  
  backend->free         = NULL;
  
  backend->insert       = NULL;
  backend->remove       = NULL;
  backend->find         = NULL;
  backend->merge        = ctm_data_backend_merge_impl;
  backend->clear        = ctm_data_backend_clear_impl;
  
  return backend;
}

CtmDataBackend *
ctm_data_backend_ref (CtmDataBackend *backend)
{
  g_atomic_int_inc (&backend->ref_count);
  
  return backend;
}

void
ctm_data_backend_unref (CtmDataBackend *backend)
{
  if (g_atomic_int_dec_and_test (&backend->ref_count)) {
    if (backend->free) {
      backend->free (backend);
    }
#if defined (CTM_DATA_BACKEND_THREADSAFE) && ! GLIB_CHECK_VERSION (2, 32, 0)
    g_mutex_free (backend->lock);
#endif
    g_slice_free1 (backend->size, backend);
  }
}


#ifdef CTM_DATA_BACKEND_THREADSAFE
void
ctm_data_backend_read_lock (CtmDataBackend  *backend)
{
# if GLIB_CHECK_VERSION (2, 32, 0)
  g_rw_lock_reader_lock (&backend->lock);
# else
  g_mutex_lock (backend->lock);
# endif
}

void
ctm_data_backend_read_unlock (CtmDataBackend  *backend)
{
# if GLIB_CHECK_VERSION (2, 32, 0)
  g_rw_lock_reader_unlock (&backend->lock);
# else
  g_mutex_unlock (backend->lock);
# endif
}

void
ctm_data_backend_write_lock (CtmDataBackend  *backend)
{
# if GLIB_CHECK_VERSION (2, 32, 0)
  g_rw_lock_writer_lock (&backend->lock);
# else
  g_mutex_lock (backend->lock);
# endif
}

void
ctm_data_backend_write_unlock (CtmDataBackend  *backend)
{
# if GLIB_CHECK_VERSION (2, 32, 0)
  g_rw_lock_writer_unlock (&backend->lock);
# else
  g_mutex_unlock (backend->lock);
# endif
}
#endif /* CTM_DATA_BACKEND_THREADSAFE */

/**
 * ctm_data_backend_insert:
 * @backend: The data backend
 * @tag: The tag to insert
 * 
 * Inserts @tag in the backend.
 */
void
ctm_data_backend_insert (CtmDataBackend  *backend,
                         CtmTag          *tag)
{
  g_assert (backend->insert != NULL);
  
  backend->insert (backend, tag);
}

void
ctm_data_backend_remove (CtmDataBackend  *backend,
                         CtmTag          *tag)
{
  g_assert (backend->remove != NULL);
  
  backend->remove (backend, tag);
}

void
ctm_data_backend_clear (CtmDataBackend *backend)
{
  g_assert (backend->clear != NULL);
  
  backend->clear (backend);
}

GList *
ctm_data_backend_find (CtmDataBackend              *backend,
                       guint                        limit,
                       CtmDataBackendSortDirection  sort_dir,
                       CtmTagCompareFunc            cmp_func,
                       CtmTagMatchFunc              match_func,
                       gpointer                     data)
{
  g_assert (backend->find != NULL);
  
  return backend->find (backend, limit, sort_dir, cmp_func, match_func, data);
}

CtmTag *
ctm_data_backend_find_first (CtmDataBackend    *backend,
                             CtmTagCompareFunc  cmp_func,
                             CtmTagMatchFunc    match_func,
                             gpointer           data)
{
  CtmTag *tag = NULL;
  GList  *tags;
  
  tags = ctm_data_backend_find (backend, 1, CTM_DATA_BACKEND_SORT_DIR_ASC,
                                cmp_func, match_func, data);
  if (tags) {
    tag = tags->data;
    g_list_free (tags);
  }
  
  return tag;
}

/**
 * ctm_data_backend_merge:
 * @src: Source backend
 * @dest: Destination backend
 * 
 * Inserts all symbols of @src in @dest.
 * 
 * The arguments order might be a little confusing, but it is the source
 * backend that inserts on the destination one rather than the destination one
 * that reads the source one.  This is because a backend is expected to
 * optimize traversal internally, but the insertion is expected to be already
 * quite efficient.
 * 
 * A backend implementation is not required to provide this operation, and if
 * it's missing an unoptimized default one will be used instead.
 */
void
ctm_data_backend_merge (CtmDataBackend  *src,
                        CtmDataBackend  *dest)
{
  g_assert (src->merge != NULL);
  g_assert (src != dest);
  
  src->merge (src, dest);
}



/* FIXME: */
CtmDataBackend *ctm_data_backend_simple_new     (void);
CtmDataBackend *ctm_data_backend_multicache_new (void);

CtmDataBackend *
ctm_data_backend_new (void)
{
  return ctm_data_backend_multicache_new ();
  return ctm_data_backend_simple_new ();
}

/* creates a new backend trying to be of the same type of @b */
CtmDataBackend *
ctm_data_backend_new_similar (const CtmDataBackend *b)
{
  /*FIXME:*/
  return ctm_data_backend_new ();
}
