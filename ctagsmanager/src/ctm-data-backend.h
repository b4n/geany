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

#ifndef H_CTM_DATA_BACKEND
#define H_CTM_DATA_BACKEND

/* #define CTM_DATA_BACKEND_THREADSAFE */

#include <stdarg.h>
#include <glib.h>

G_BEGIN_DECLS
/* types are declared before local includes for circular deps */
typedef enum _CtmDataBackendSortDirection {
  CTM_DATA_BACKEND_SORT_DIR_NONE,
  CTM_DATA_BACKEND_SORT_DIR_ASC,
  CTM_DATA_BACKEND_SORT_DIR_DESC
} CtmDataBackendSortDirection;

typedef struct _CtmDataBackend CtmDataBackend;
G_END_DECLS

#include "ctm-tag.h"

G_BEGIN_DECLS


struct _CtmDataBackend
{
  gint    ref_count;
  gsize   size; /* the actual struct size, needed for GSlice */
  guint   backend_type;
#ifdef CTM_DATA_BACKEND_THREADSAFE
# if GLIB_CHECK_VERSION (2, 32, 0)
  GRWLock lock;
# else
  GMutex *lock;
# endif
#endif
  
  /* VFuncs */
  /* private */
  void      (*free)           (CtmDataBackend  *backend); /* optional */
  /* required */
  void      (*insert)         (CtmDataBackend  *backend,
                               CtmTag          *tag);
  void      (*remove)         (CtmDataBackend  *backend,
                               CtmTag          *tag);
  GList    *(*find)           (CtmDataBackend              *backend,
                               CtmDataBackendSortDirection  sort_dir,
                               guint                        limit,
                               CtmTagCompareFunc            cmp_func,
                               CtmTagMatchFunc              match_func,
                               gpointer                     data);
  /* optional */
  void      (*merge)          (CtmDataBackend  *src,
                               CtmDataBackend  *dest);
  void      (*clear)          (CtmDataBackend  *backend);
};


/* private for backend implementations */
CtmDataBackend *_ctm_data_backend_alloc       (gsize  struct_size,
                                               guint  backend_type);
               
CtmDataBackend *ctm_data_backend_new          (void);
CtmDataBackend *ctm_data_backend_new_similar  (const CtmDataBackend *b);
CtmDataBackend *ctm_data_backend_ref          (CtmDataBackend  *backend);
void            ctm_data_backend_unref        (CtmDataBackend  *backend);
#ifdef CTM_DATA_BACKEND_THREADSAFE
void            ctm_data_backend_read_lock    (CtmDataBackend  *backend);
void            ctm_data_backend_read_unlock  (CtmDataBackend  *backend);
void            ctm_data_backend_write_lock   (CtmDataBackend  *backend);
void            ctm_data_backend_write_unlock (CtmDataBackend  *backend);
#else
# define        ctm_data_backend_read_lock(backend)     /* nothing */
# define        ctm_data_backend_read_unlock(backend)   /* nothing */
# define        ctm_data_backend_write_lock(backend)    /* nothing */
# define        ctm_data_backend_write_unlock(backend)  /* nothing */
#endif
void            ctm_data_backend_insert       (CtmDataBackend  *backend,
                                               CtmTag          *tag);
void            ctm_data_backend_remove       (CtmDataBackend  *backend,
                                               CtmTag          *tag);
void            ctm_data_backend_clear        (CtmDataBackend  *backend);


GList          *ctm_data_backend_find         (CtmDataBackend              *backend,
                                               guint                        limit,
                                               CtmDataBackendSortDirection  sort_dir,
                                               CtmTagCompareFunc            cmp_func,
                                               CtmTagMatchFunc              match_func,
                                               gpointer                     data);
CtmTag         *ctm_data_backend_find_first   (CtmDataBackend    *backend,
                                               CtmTagCompareFunc  cmp_func,
                                               CtmTagMatchFunc    match_func,
                                               gpointer           data);
void            ctm_data_backend_merge        (CtmDataBackend  *src,
                                               CtmDataBackend  *dest);


G_END_DECLS

#endif /* guard */
