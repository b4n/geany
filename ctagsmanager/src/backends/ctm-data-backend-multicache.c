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
 * A backend that caches sorts in multiple arrays.
 * 
 * It is optimized for searching elements, not for inserting them.
 * 
 * TODO: use a BTree for better insertion performances?  Actually now insertion
 *       is done directly sorted, the only real overhead is the memory
 *       manipulation involved in reordering the arrays.
 */

#include <string.h>
#include <glib.h>

#include "ctm-data-backend.h"
#include "ctm-tag.h"


typedef struct _CacheItem {
  GPtrArray        *array;
  CtmTagCompareFunc sort_func;
} CacheItem;

#define cache_item_alloc() (g_slice_alloc (sizeof (CacheItem)))
static void
cache_item_free (CacheItem *item)
{
  g_ptr_array_free (item->array, TRUE);
  g_slice_free1 (sizeof *item, item);
}


typedef struct _CtmDataBackendMulticache
{
  CtmDataBackend  parent;
  
  GPtrArray  *array;          /* base array, unsorted */
  GHashTable *caches_by_func; /* table of CacheItems indexed on the sort func */
  GList      *caches;         /* list of CacheItem in the hash table for fast
                               * traversal */
  
  GSource    *stats_source;
} CtmDataBackendMulticache;



static void
ptr_array_insert (GPtrArray  *array,
                  gpointer    data,
                  gsize       pos)
{
  gsize len;
  
  len = array->len;
  g_ptr_array_add (array, data);
  /* if not inserting at the end, reorder items */
  if (pos < len) {
    g_memmove (&(array->pdata)[pos + 1], &(array->pdata)[pos],
               sizeof *(array->pdata) * (len - pos));
    array->pdata[pos] = data;
  }
}

/* inserts an element in a GPtrArray at the position found by @compare_func
 * the a must be already sorted according to @compare_func for this function
 * to work properly */
static void
ptr_array_insert_sorted (GPtrArray   *array,
                         gpointer     data,
                         GCompareFunc compare_func)
{
  gsize lower = 0;
  gsize upper = array->len;
  gsize idx   = 0;
  gint  cmp   = 0;
  
  /* from eglibc's bsearch() impl, find the insertion pos */
  while (lower < upper) {
    idx = (lower + upper) / 2;
    cmp = compare_func (data, (array->pdata)[idx]);
    if (cmp < 0) {
      upper = idx;
    } else if (cmp > 0) {
      lower = idx + 1;
    } else {
      break;
    }
  }
  
  /* in the (likely) case we didn't find any match, insert at the lowest
   * non-positive pos */
  if (cmp != 0) {
    idx = lower;
  }
  
  ptr_array_insert (array, data, idx);
}

static void
ctm_data_backend_multicache_insert (CtmDataBackend  *backend,
                                    CtmTag          *tag)
{
  CtmDataBackendMulticache *self = (CtmDataBackendMulticache *) backend;
  GList                    *item;
  
  ctm_data_backend_write_lock (backend);
  g_ptr_array_add (self->array, ctm_tag_ref (tag));
  for (item = self->caches; item; item = item->next) {
    CacheItem *ci = item->data;
    
    /* we only get one ref to the tag, in the main array */
    ptr_array_insert_sorted (ci->array, tag, (GCompareFunc) ci->sort_func);
  }
  ctm_data_backend_write_unlock (backend);
}

static void
ctm_data_backend_multicache_remove (CtmDataBackend  *backend,
                                    CtmTag          *tag)
{
  CtmDataBackendMulticache *self = (CtmDataBackendMulticache *) backend;
  GList                    *item;
  
  ctm_data_backend_write_lock (backend);
  /* FIXME: this is boringly slow */
  g_ptr_array_remove (self->array, tag);
  for (item = self->caches; item; item = item->next) {
    CacheItem *ci = item->data;
    
    g_ptr_array_remove (ci->array, tag);
  }
  ctm_data_backend_write_unlock (backend);
}


/*#define BDEBUG*/

#ifdef BDEBUG
# define BDEBUG_STMT(s) s
#else
# define BDEBUG_STMT(s) /* nothing */
#endif

static gboolean
binary_sreach_range (GPtrArray       *array,
                     CtmTagMatchFunc  match_func,
                     gpointer         data,
                     guint            limit,
                     gsize           *l_,
                     gsize           *u_)
{
  gsize l = 0;
  gsize u = array->len;
  BDEBUG_STMT (gsize n = 0;)
  
  /* from eglibc's bsearch() impl */
  while (l < u) {
    gsize     idx;
    gint      cmp;
    
    BDEBUG_STMT (n++);
    
    idx = (l + u) / 2;
    cmp = match_func (array->pdata[idx], data);
    if (cmp < 0) {
      u = idx;
    } else if (cmp > 0) {
      l = idx + 1;
    } else {
      gsize r_u = idx;
      gsize r_l = idx;
      
      BDEBUG_STMT (g_debug ("found element @%zu in %zu iterations", idx, n);)
      BDEBUG_STMT (n = 0;)
      BDEBUG_STMT (g_debug ("now getting range...");)
      
      /* find the lower end of the range */
      while (l < r_l) {
        BDEBUG_STMT (n++;)
        
        idx = (l + r_l) / 2;
        cmp = match_func (array->pdata[idx], data);
        if (cmp > 0) {
          l = idx + 1;
        } else {
          r_l = idx;
        }
      }
      BDEBUG_STMT (g_debug ("found lower end in %zu iterations: %zu", n, r_l);)
      BDEBUG_STMT (n = 0;)
      
      /* only find the upper end of the range if the limit isn't already
       * reached */
      if (! limit || (r_l + limit) > r_u) {
        /* find the upper end of the range */
        while (r_u < u) {
          BDEBUG_STMT (n++;)
          
          idx = (r_u + u) / 2;
          cmp = match_func (array->pdata[idx], data);
          if (cmp < 0) {
            u = idx;
          } else {
            r_u = idx + 1;
          }
        }
        BDEBUG_STMT (g_debug ("found upper end in %zu iterations: %zu", n, r_u);)
        BDEBUG_STMT (n = 0;)
      }
      
      /* clamp the range to the limit */
      if (limit && (r_l + limit) < r_u) {
        r_u = r_l + limit;
      }
      
      *l_ = r_l;
      *u_ = r_u;
      
      return TRUE;
    }
  }
  
  return FALSE;
}

static GList *
do_find (GPtrArray                   *array,
         guint                        limit,
         CtmDataBackendSortDirection  sort_dir,
         CtmTagMatchFunc              match_func,
         gpointer                     data)
{
  gsize   lower;
  gsize   upper;
  GList  *tags = NULL;
  
  if (binary_sreach_range (array, match_func, data, limit, &lower, &upper)) {
    gsize i;
    
    if (sort_dir == CTM_DATA_BACKEND_SORT_DIR_DESC) {
      for (i = lower; i < upper; i++) {
        tags = g_list_prepend (tags, ctm_tag_ref (array->pdata[i]));
      }
    } else {
      for (i = upper; i > lower; i--) {
        tags = g_list_prepend (tags, ctm_tag_ref (array->pdata[i - 1]));
      }
    }
  }
  
  return tags;
}

static gint
sort_array (gconstpointer a,
            gconstpointer b,
            gpointer      data)
{
  CacheItem *ci = data;
  
  return ci->sort_func (* (CtmTag **) a, * (CtmTag **) b);
}

static GList *
ctm_data_backend_multicache_find (CtmDataBackend             *backend,
                                  guint                       limit,
                                  CtmDataBackendSortDirection sort_dir,
                                  CtmTagCompareFunc           cmp_func,
                                  CtmTagMatchFunc             match_func,
                                  gpointer                    data)
{
  CtmDataBackendMulticache *self = (CtmDataBackendMulticache *) backend;
  GList                    *tags = NULL;
  CacheItem                *ci;
  
  ctm_data_backend_write_lock (backend);
  ci = g_hash_table_lookup (self->caches_by_func, cmp_func);
  /* if we ain't got no cache for this function, build it */
  if (! ci) {
    guint i;
    
    ci = g_slice_alloc (sizeof *ci);
    ci->array = g_ptr_array_sized_new (self->array->len);
    ci->sort_func = cmp_func;
    
    /* build new cache array */
    for (i = 0; i < self->array->len; i++) {
      /* we don't use _add() to save a few checks & function calls (yes, this
       * is overkill) */
      ci->array->pdata[ci->array->len++] = self->array->pdata[i];
    }
    g_ptr_array_sort_with_data (ci->array, sort_array, ci);
    
    g_hash_table_insert (self->caches_by_func, ci->sort_func, ci);
    self->caches = g_list_prepend (self->caches, ci);
  }
  
  /* now we got the cache, search it */
  if (ci->array->len > 0) {
    tags = do_find (ci->array, limit, sort_dir, match_func, data);
  }
  ctm_data_backend_write_unlock (backend);
  
  return tags;
}

static void
ctm_data_backend_multicache_merge (CtmDataBackend  *backend,
                                   CtmDataBackend  *dest)
{
  CtmDataBackendMulticache *self = (CtmDataBackendMulticache *) backend;
  guint                     i;
  
  ctm_data_backend_read_lock (backend);
  for (i = 0; i < self->array->len; i++) {
    CtmTag *tag;
    
    tag = g_ptr_array_index (self->array, i);
    ctm_data_backend_insert (dest, tag);
  }
  ctm_data_backend_read_unlock (backend);
}

static void
ctm_data_backend_multicache_clear (CtmDataBackend  *backend)
{
  CtmDataBackendMulticache *self = (CtmDataBackendMulticache *) backend;
  
  ctm_data_backend_write_lock (backend);
  g_ptr_array_set_size (self->array, 0);
  g_hash_table_remove_all (self->caches_by_func);
  g_list_free (self->caches);
  self->caches = NULL;
  ctm_data_backend_write_unlock (backend);
}

static gboolean
print_backend_stats (CtmDataBackendMulticache *self)
{
#if 0
  gsize n_caches;
  gsize n_items;
  gsize item_size;
  
  n_caches = g_hash_table_size (self->caches_by_func);
  n_caches += 1; /* default cache */
  n_items = self->array->len;
  item_size = sizeof (CacheItem);
  
  g_debug ("Multicache backend %p:", self);
  g_debug ("        n items: %zu", n_items);
  g_debug ("     items size: %zu", item_size);
  g_debug ("       n caches: %zu", n_caches);
  g_debug ("          total: %zu", n_items * item_size * n_caches);
#endif
  
  return TRUE;
}

static void
ctm_data_backend_multicache_free (CtmDataBackend *backend)
{
  CtmDataBackendMulticache *self = (CtmDataBackendMulticache *) backend;
  
  print_backend_stats (self);
  g_source_destroy (self->stats_source);
  
  ctm_data_backend_multicache_clear (backend);
  g_ptr_array_free (self->array, TRUE);
  g_hash_table_destroy (self->caches_by_func);
}

CtmDataBackend *
ctm_data_backend_multicache_new (void)
{
  CtmDataBackend           *backend;
  CtmDataBackendMulticache *self;
  
  backend = _ctm_data_backend_alloc (sizeof (CtmDataBackendMulticache), 2/*FIXME*/);
  self = (CtmDataBackendMulticache *) backend;
  
  backend->free         = ctm_data_backend_multicache_free;
  backend->insert       = ctm_data_backend_multicache_insert;
  backend->remove       = ctm_data_backend_multicache_remove;
  backend->find         = ctm_data_backend_multicache_find;
  backend->merge        = ctm_data_backend_multicache_merge;
  backend->clear        = ctm_data_backend_multicache_clear;
  
  self->array = g_ptr_array_new_with_free_func ((GDestroyNotify) ctm_tag_unref);
  self->caches_by_func = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                NULL,
                                                (GDestroyNotify) cache_item_free);
  self->caches = NULL;
  
  self->stats_source = g_timeout_source_new_seconds (5);
  g_source_set_callback (self->stats_source, print_backend_stats, self, NULL);
  /*g_source_attach (self->stats_source, NULL);*/
  
  return backend;
}
