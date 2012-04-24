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

#include "ctm-source-file.h"

#include <glib.h>

#include "ctm-data-backend.h"


CtmSourceFile *
ctm_source_file_new (const gchar *name,
                     langType     lang)
{
  CtmSourceFile *sf;
  
  sf = g_slice_alloc (sizeof *sf);
  sf->ref_count = 1;
  sf->name      = g_strdup (name);
  sf->lang      = lang;
  sf->backend   = ctm_data_backend_new ();
  
  return sf;
}

CtmSourceFile *
ctm_source_file_ref (CtmSourceFile *sf)
{
  g_atomic_int_inc (&sf->ref_count);
  
  return sf;
}

void
ctm_source_file_unref (CtmSourceFile *sf)
{
  if (g_atomic_int_dec_and_test (&sf->ref_count)) {
    ctm_data_backend_unref (sf->backend);
    g_free (sf->name);
    g_slice_free1 (sizeof *sf, sf);
  }
}

void
ctm_source_file_set_name (CtmSourceFile *sf,
                          const gchar   *name)
{
  g_free (sf->name);
  sf->name = g_strdup (name);
}

void
ctm_source_file_set_lang (CtmSourceFile   *sf,
                          langType         lang)
{
  sf->lang = lang;
}

void
ctm_source_file_set_backend (CtmSourceFile   *sf,
                             CtmDataBackend  *backend)
{
  if (sf->backend) {
    ctm_data_backend_unref (sf->backend);
  }
  sf->backend = ctm_data_backend_ref (backend);
}
