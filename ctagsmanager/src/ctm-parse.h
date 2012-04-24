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

#ifndef H_CTM_PARSE
#define H_CTM_PARSE

#include <glib.h>

#include "ctm-source-file.h"

G_BEGIN_DECLS


typedef void (*CtmBufferParseCallback) (CtmSourceFile  *file,
                                        gboolean        success,
                                        gpointer        data);


gboolean    ctm_parser_init         (void);
void        ctm_parser_exit         (void);
gboolean    ctm_parser_parse_sync   (CtmSourceFile *file,
                                     gconstpointer  buf,
                                     gsize          size);
void        ctm_parser_parse_async  (CtmSourceFile          *file,
                                     gconstpointer           buf,
                                     gsize                   size,
                                     CtmBufferParseCallback  callback,
                                     gpointer                data);

const gchar  *ctm_parser_get_lang_name    (langType lang);
langType      ctm_parser_get_named_lang   (const gchar *name);


G_END_DECLS

#endif /* guard */
