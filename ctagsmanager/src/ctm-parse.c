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

#include "ctm-parse.h"

#include <glib.h>

#include "ctm-tag.h"
#include "ctm-source-file.h"
#include "ctm-data-backend.h"
#include "ctm-workspace.h"

#include "ctags/read.h"
#include "ctags/parse.h"
#include "ctags/entry.h"

/* 
 * there is support for both asynchronous and synchronous parsing, but *only
 * one of the flavor must be used at a time*.  you *cannot* mix both, otherwise
 * you'll experience random errors.
 * 
 * we run asynchronous parsers in a separate thread, using a thread pool to
 * limit one parsing at a time, because they can't run concurrently.
 * so it's asynchronous parsing, not concurrent.
 */


typedef struct _ParseJob {
  CtmSourceFile          *file;
  CtmDataBackend         *backend;
  gpointer                buffer;
  gsize                   size;
  gboolean                success;
  
  CtmBufferParseCallback  callback;
  gpointer                user_data;
} ParseJob;


static void   parse_pool_func             (gpointer data1,
                                           gpointer data2);
static int    tag_entry_func              (const tagEntryInfo *const entry);
static void   tag_entry_set_arglist_func  (const char *tag_name,
                                           const char *arglist);


static GThreadPool     *parse_pool      = NULL;
static CtmSourceFile   *current_file    = NULL; /* the current file */
static CtmDataBackend  *current_backend = NULL; /* the current backend */


gboolean
ctm_parser_init (void)
{
  if (G_UNLIKELY (parse_pool == NULL)) {
    GError *err = NULL;
    
    parse_pool = g_thread_pool_new (parse_pool_func, NULL, 1, TRUE, &err);
    if (err) {
      g_critical ("Failed to create parsing thread pool: %s", err->message);
      g_error_free (err);
    }
  }
  if (G_UNLIKELY (LanguageTable == NULL)) {
    initializeParsing ();
    installLanguageMapDefaults ();
    TagEntryFunction = tag_entry_func;
    TagEntrySetArglistFunction = tag_entry_set_arglist_func;
  }
  
  return LanguageTable != NULL && parse_pool != NULL;
}

void
ctm_parser_exit (void)
{
  if (parse_pool != NULL) {
    g_thread_pool_free (parse_pool, FALSE, TRUE);
    parse_pool = NULL;
  }
  if (LanguageTable != NULL) {
    freeParserResources ();
    TagEntryFunction = NULL;
    TagEntrySetArglistFunction = NULL;
  }
}

static int
tag_entry_func (const tagEntryInfo *const entry)
{
  CtmTag *tag;
  
  tag = ctm_tag_new (current_file, entry);
  /*g_debug ("%s: got a tag: %s%s%s",
           current_file->name,
           tag->scope ? tag->scope : "",
           tag->scope ? "." : "",
           tag->name);*/
  ctm_data_backend_insert (current_backend, tag);
  ctm_tag_unref (tag);
  
  return 0;
}

static void
tag_entry_set_arglist_func (const char *tag_name,
                            const char *arglist)
{
  g_debug ("%s: got arglist %s for tag %s",
           current_file->name, arglist, tag_name);
}

static gboolean
ctm_parser_parse_sync_to_backend (CtmSourceFile  *file,
                                  CtmDataBackend *backend,
                                  gconstpointer   buf,
                                  gsize           size)
{
  gboolean          success = FALSE;
  parserDefinition *parser;
  
  g_assert (ctm_parser_init ());
  g_return_val_if_fail (file != NULL, FALSE);
  
  /* bind source file & backend */
  current_file = ctm_source_file_ref (file);
  current_backend = ctm_data_backend_ref (backend);
  
  if (file->lang == LANG_AUTO) {
    file->lang = getFileLanguage (file->name);
  }
  if (file->lang != LANG_IGNORE) {
    parser = LanguageTable[file->lang];
    
    if (size < 1) {
      success = TRUE;
    } else if (parser->enabled) {
      if (parser->parser) { /* simple parsers */
        if (bufferOpen (buf, size, file->name, file->lang)) {
          parser->parser ();
          success = TRUE;
          bufferClose ();
        }
      } else if (parser->parser2) { /* retry parsers */
        guint pass;
        
        for (pass = 0; pass < 3 && ! success; pass ++) {
          if (bufferOpen (buf, size, file->name, file->lang)) {
            success = ! parser->parser2 (pass); /* returns whether to retry */
            bufferClose ();
          }
        }
      }
    }
  }
  
  /* unbind source file & backend */
  ctm_source_file_unref (current_file);
  current_file = NULL;
  ctm_data_backend_unref (current_backend);
  current_backend = NULL;
  
  return success;
}

gboolean
ctm_parser_parse_sync (CtmSourceFile *file,
                       gconstpointer  buf,
                       gsize          size)
{
  gboolean success;
  
  ctm_data_backend_clear (file->backend);
  success = ctm_parser_parse_sync_to_backend (file, file->backend, buf, size);
  ctm_workspace_update_file (ctm_workspace_get_default (), file);
  
  return success;
}

static gboolean
parse_pool_finish_in_idle (gpointer data)
{
  ParseJob *job = data;
  
  ctm_source_file_set_backend (job->file, job->backend);
  /* if backend were thread safe, we could do that in the thread...
   * although updating the whole workspcae in a thread seems legitimate,
   * it shouldn't be possible to have a worspace half-updated
   * anyway, boring stuff. */
  ctm_workspace_update_file (ctm_workspace_get_default (), job->file);
  if (job->callback) {
    job->callback (job->file, job->success, job->user_data);
  }
  g_free (job->buffer);
  ctm_data_backend_unref (job->backend);
  ctm_source_file_unref (job->file);
  g_slice_free1 (sizeof *job, job);
  
  return FALSE;
}

static void
parse_pool_func (gpointer data1,
                 gpointer data2)
{
  ParseJob *job = data1;
  
  job->success = ctm_parser_parse_sync_to_backend (job->file, job->backend,
                                                   job->buffer, job->size);
  g_idle_add (parse_pool_finish_in_idle, job);
}

void
ctm_parser_parse_async (CtmSourceFile          *file,
                        gconstpointer           buf,
                        gsize                   size,
                        CtmBufferParseCallback  callback,
                        gpointer                data)
{
  ParseJob *job;
  
  g_assert (ctm_parser_init ());
  g_return_if_fail (file != NULL);
  g_return_if_fail (buf != NULL || size == 0);
  
  job = g_slice_alloc (sizeof *job);
  job->file      = ctm_source_file_ref (file);
  job->backend   = ctm_data_backend_new_similar (file->backend);
  job->buffer    = g_memdup (buf, size);
  job->size      = size;
  job->callback  = callback;
  job->user_data = data;
  job->success   = FALSE;
  
  g_thread_pool_push (parse_pool, job, NULL);
}


const gchar *
ctm_parser_get_lang_name (langType lang)
{
  g_assert (ctm_parser_init ());
  
  return getLanguageName (lang);
}

langType
ctm_parser_get_named_lang (const gchar *name)
{
  g_assert (ctm_parser_init ());
  
  return getNamedLanguage (name);
}
