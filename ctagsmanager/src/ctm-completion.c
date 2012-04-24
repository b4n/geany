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

#include "ctm-completion.h"

#include <string.h>
#include <glib.h>

#include "ctm-tag.h"
#include "ctm-workspace.h"


static gint
lang_cmp (langType a,
          langType b)
{
  /* 0 is C, 1 is C++, allow one another to match */
  if (a <= 1 && b <= 1) {
    return 0;
  } else {
    return a - b;
  }
}


typedef struct {
  langType      lang;
  const gchar  *type_name;
} ResolveTypedefData;

static gint
resolve_typedef_tag_cmp_sort (const CtmTag *a,
                              const CtmTag *b)
{
  gint cmp;
  
  if ((cmp = lang_cmp (a->file->lang, b->file->lang)) == 0) {
    if ((cmp = a->type - b->type) == 0) {
      if (! a->var_type || ! b->var_type) {
        cmp = a->var_type - b->var_type;
      } else {
        if ((cmp = strcmp (a->name, b->name)) == 0) {
          cmp = strcmp (a->var_type, b->var_type);
        }
      }
    }
  }
  
  return cmp;
}

static gint
resolve_typedef_tag_cmp_match (const CtmTag  *tag,
                               gpointer       data_)
{
  ResolveTypedefData *data = data_;
  gint                cmp;
  
  if ((cmp = lang_cmp (data->lang, tag->file->lang)) == 0) {
    if ((cmp = CTM_TAG_TYPE_TYPEDEF - tag->type) == 0) {
      if (! tag->var_type) {
        cmp = 1;
      } else {
        cmp = strcmp (data->type_name, tag->name);
      }
    }
  }
  
  return cmp;
  
  /*return (tag->type & CTM_TAG_TYPE_TYPEDEF &&
          lang_cmp (tag->file->lang, data->lang) == 0 &&
          strcmp (tag->name, data->type_name) == 0 &&
          tag->var_type != NULL &&
          strcmp (tag->var_type, data->type_name) != 0);*/
}

/* gets the real type of @name_orig by resolving the typedefs
 * @file: (in/out) the preferred CtmSourceFile, will be filled with the
 *        CtmSourceFile in which the returned name was found.  This may point
 *        to @NULL, but cannot be a NULL pointer */
static const gchar *
resolve_typedef (CtmSourceFile  **file,
                 const gchar     *type_name,
                 langType         lang)
{
  CtmWorkspace       *ws;
  guint               pass = 0;
  ResolveTypedefData  data;
  
  g_return_val_if_fail (file != NULL, NULL);
  g_return_val_if_fail (type_name != NULL, NULL);
  
  /*g_debug ("resolving type %s", type_name);*/
  
  ws = ctm_workspace_get_default ();
  
  data.lang = lang;
  data.type_name = type_name;
  
  /* 8 is an arbitrary limit not to loop infinitely on recursive
   * self-referencing typedefs */
  for (pass = 0; pass < 8; pass++) {
    CtmTag *tag;
    
    tag = ctm_workspace_find_first (ws, (*file && (*file)->lang == lang) ? *file : NULL,
                                    resolve_typedef_tag_cmp_sort,
                                    resolve_typedef_tag_cmp_match, &data);
    if (! tag) {
      break;
    } else {
      /*g_debug ("got a proxy or result: %s %s", tag->var_type, tag->name);*/
      data.type_name = tag->var_type;
      *file = tag->file;
      ctm_tag_unref (tag);
      /* we need to resolve the new name in case it is typedefed again,
       * trying the file containing the typedef first */
    }
  }
  
  /*g_debug ("resolved to %s", data.type_name);*/
  
  return data.type_name;
}



#if 0
/* FIXME: check this func */
static gint
scope_start_cmp_match (const CtmTag *tag,
                       gpointer      data)
{
  register const gchar *p1 = tag->scope;
  register const gchar *p2 = data;
  gint                  cmp = 0;
  
  if (! p1) {
    return 1;
  }
  
  /* compare like strncmp(p1, p2, strlen(p2)) */
  while (*p2 && (cmp = *p2 - *p1) == 0) {
    p1++, p2++;
  }
  /* if we got there and it matched, make sure the tag's scope is either exactly
   * the same (also ended) or has deeper scope (has a separator just after) to
   * make sure we don't match "Foo" against "Foobar" */
  if (cmp == 0 && *p1 != 0 && *p1 != '.' && *p1 != ':') {
    cmp = -1;
  }
  
  return cmp;
}
#endif

static gint
scope_rcmp (const gchar *a,
            const gchar *b)
{
  register const gchar *p1;
  register const gchar *p2;
  gint cmp;
  
  /* find the end of the scopes */
  for (p1 = a; *p1; p1++);
  for (p2 = b; *p2; p2++);
  
  while (p1 > a && p2 > b && (cmp = *--p1 - *--p2) == 0);
  
  if (cmp == 0 && p2 > b && *p2 != ':' && *p2 != '.') {
    cmp = -1;
  }
  
  return cmp;
}

static gint
cmp_scope_reversed (const CtmTag *a,
                    const CtmTag *b)
{
  gint cmp;
  
  if ((cmp = lang_cmp (a->file->lang, b->file->lang)) == 0) {
    if (a->scope && b->scope) {
      if ((cmp = scope_rcmp (a->scope, b->scope)) == 0) {
        cmp = ctm_tag_cmp_name (a, b);
      }
    } else {
      cmp = a->scope - b->scope;
    }
  }
  
  return cmp;
}

typedef struct {
  langType      lang;
  const gchar  *scope;
} FindScopeMembersData;

static gint
match_scope_end (const CtmTag  *tag,
                 gpointer       data_)
{
  FindScopeMembersData *data = data_;
  gint                  cmp;
  
  if ((cmp = lang_cmp (data->lang, tag->file->lang)) == 0) {
    if (! tag->scope) {
      cmp = 1;
    } else {
      cmp = scope_rcmp (data->scope, tag->scope);
    }
  }
  
  return cmp;
}

/* finds members of @scope
 * @file: the preferred CtmSourceFile (e.g. the one containing the type definitions) */
GList *
ctm_completion_get_scope_members (CtmSourceFile  *file,
                                  const gchar    *scope)
{
  FindScopeMembersData  data;
  
  g_return_val_if_fail (scope != NULL, NULL);
  
  /*g_debug ("searching children for %s...", scope);*/
  
  data.lang   = file->lang;
  data.scope  = scope;
  
  return ctm_workspace_find (ctm_workspace_get_default (), file,
                             FALSE, 0, CTM_DATA_BACKEND_SORT_DIR_ASC,
                             cmp_scope_reversed, match_scope_end, &data);
}



typedef struct {
  langType      lang;
  const gchar  *name;
} FindScopeCompletionsData;

static gint
name_with_vartype_tag_cmp_sort (const CtmTag *a,
                                const CtmTag *b)
{
  gint cmp;
  
  if ((cmp = lang_cmp (a->file->lang, b->file->lang)) == 0) {
    if ((cmp = strcmp (a->name, b->name)) == 0) {
      cmp = g_strcmp0 (a->var_type, b->var_type);
    }
  }
  
  return cmp;
}

static gint
name_with_vartype_tag_cmp_match (const CtmTag *tag,
                                 gpointer      data_)
{
  FindScopeCompletionsData *data = data_;
  gint                      cmp;
  
  if ((cmp = lang_cmp (data->lang, tag->file->lang)) == 0) {
    cmp = strcmp (data->name, tag->name);
    if (! tag->var_type && cmp == 0) {
      cmp = 1;
    }
  }
  
  return cmp;
}

/* finds scope completions for @name
 * @file: the preferred TMSourceFile (e.g. the one containing the name) */
GList *
ctm_completion_get_scope_completions (CtmSourceFile  *file,
                                      const gchar    *name)
{
  const CtmWorkspace       *ws;
  guint                     i;
  CtmDataBackend           *backend[3];
  GList                    *children = NULL;
  FindScopeCompletionsData  data;
  
  g_return_val_if_fail (file != NULL, NULL);
  
  ws = ctm_workspace_get_default ();
  
  backend[0] = file->backend;
  backend[1] = ws->tags;
  backend[2] = ws->global_tags;
  
  data.lang = file->lang;
  data.name = name;
  
  /*g_debug ("finding scope member for %s", name);*/
  
  /* FIXME: using ctm_workspace_find() would be better than redo it, but we
   * want to break in the middle depending on a computed value */
  for (i = 0; ! children && i < G_N_ELEMENTS (backend); i++) {
    if (backend[i]) {
      GList *candidates;
      GList *item;
      
      /*g_debug ("searching candidates...");*/
      candidates = ctm_data_backend_find (backend[i], 0,
                                          CTM_DATA_BACKEND_SORT_DIR_NONE,
                                          name_with_vartype_tag_cmp_sort,
                                          name_with_vartype_tag_cmp_match,
                                          &data);
      for (item = candidates; ! children && item; item = item->next) {
        const CtmTag *tag = item->data;
        const gchar  *type;
        
        /*g_debug ("got %s as a candidate", tag->name);*/
        file = tag->file;
        type = resolve_typedef (&file, tag->var_type, file->lang);
        /* this doesn't work properly for e.g. C functions because their return
         * type includes type modifiers such as "const" or "*". */
        children = ctm_completion_get_scope_members (file, type);
      }
    }
  }
  
  return children;
}



typedef struct {
  langType      lang;
  const gchar  *start;
} FindCompletionsData;

static gint
tag_cmp_lang_name (const CtmTag *a,
                   const CtmTag *b)
{
  gint cmp;
  
  if ((cmp = lang_cmp (a->file->lang, b->file->lang)) == 0) {
    cmp = ctm_tag_cmp_name (a, b);
  }
  
  return cmp;
}

static gint
tag_match_lang_name_start (const CtmTag *tag,
                           gpointer      data_)
{
  FindCompletionsData  *data = data_;
  gint                  cmp;
  
  if ((cmp = lang_cmp (data->lang, tag->file->lang)) == 0) {
    cmp = ctm_tag_match_name_start (tag, (gpointer) data->start);
  }
  
  return cmp;
}

/* lang will be file->lang */
GList *
ctm_completion_get_completions (CtmSourceFile *file,
                                const gchar   *start)
{
  FindCompletionsData data;
  
  g_return_val_if_fail (file != NULL, NULL);
  g_return_val_if_fail (start != NULL, NULL);
  
  data.lang = file->lang;
  data.start = start;
  
  return ctm_workspace_find (ctm_workspace_get_default (), file, TRUE, 0,
                             CTM_DATA_BACKEND_SORT_DIR_ASC,
                             tag_cmp_lang_name,
                             tag_match_lang_name_start, &data);
}


static gint
cmp_function_line (const CtmTag *a,
                   const CtmTag *b)
{
  gint cmp;
  
  /* sort in reverse order */
  if ((cmp = b->type - a->type) == 0) {
    cmp = ctm_tag_cmp_line (b, a);
  }
  
  return cmp;
}

static gint
match_function_line (const CtmTag *tag,
                     gpointer      data)
{
  gulong  line = GPOINTER_TO_SIZE (data);
  gint    cmp;
  
  /* since we're sorted last..first and we only match the first */
  if ((cmp = tag->type - CTM_TAG_TYPE_FUNCTION) == 0) {
    cmp = tag->line - line;
    if (cmp < 0) {
      cmp = 0;
    }
  }
  
  return cmp;
}

/*
 * ctm_completion_get_function_at_line:
 * @file: The CtmSourceFile in which search for the function
 * @line: The line to search function for
 * 
 * Finds the function at line @line.
 * 
 * Returns: The function tags for line @line, or #NULL
 */
CtmTag *
ctm_completion_get_function_at_line (CtmSourceFile *file,
                                     gulong         line)
{
  return ctm_data_backend_find_first (file->backend,
                                      cmp_function_line,
                                      match_function_line, GSIZE_TO_POINTER (line));
}



typedef struct {
  FindScopeMembersData  parent;
  
  const gchar          *name;
  CtmTagMatchFunc       name_match_func;
} FindScopedNameData;

static gint
match_scoped_name (const CtmTag  *tag,
                   gpointer       data_)
{
  FindScopedNameData *data = data_;
  gint                cmp;
  
  if ((cmp = lang_cmp (data->parent.lang, tag->file->lang)) == 0) {
    if ((cmp = data->name_match_func (tag, (gpointer) data->name)) == 0) {
      if (! tag->scope) {
        cmp = data->parent.scope - tag->scope;
      } else {
        cmp = scope_rcmp (data->parent.scope, tag->scope);
      }
    }
  }
  
  return cmp;
}

/* FIXME: merge with _get_scope_members() ? */
GList *
ctm_completion_get_scoped_name (CtmSourceFile *file,
                                langType       lang,
                                const gchar   *scope,
                                const gchar   *name,
                                gboolean       partial)
{
  FindScopedNameData data;
  
  /*g_return_val_if_fail (scope != NULL, NULL);*/
  g_return_val_if_fail (name != NULL, NULL);
  
  data.parent.lang      = lang;
  data.parent.scope     = scope;
  data.name             = name;
  data.name_match_func  = partial ? ctm_tag_match_name_start
                                  : ctm_tag_match_name;
  
  return ctm_workspace_find (ctm_workspace_get_default (), file, FALSE, 0,
                             CTM_DATA_BACKEND_SORT_DIR_ASC,
                             cmp_scope_reversed, match_scoped_name, &data);
}


static GList *
results_filter_type (GList     *tags,
                     CtmTagType types)
{
  GList *iter = tags;
  
  while (iter) {
    CtmTag *tag = iter->data;
    
    /* only keep matching types */
    if (tag->type & types) {
      iter = iter->next;
    } else {
      GList *next = iter->next;
      
      ctm_tag_unref (tag);
      tags = g_list_delete_link (tags, iter);
      iter = next;
    }
  }
  
  return tags;
}

GList *
ctm_completion_get_scoped_methods (CtmSourceFile *file,
                                   langType       lang,
                                   const gchar   *parent,
                                   const gchar   *name,
                                   gboolean       partial)
{
  GList *tags;
  
  tags = ctm_completion_get_scoped_name (file, lang, parent, name, partial);
  /* filter by type */
  /* FIXME: this should perhaps be done straight in the search?
   *        even though we can't filter by type, we could check whether
   *        tag->arglist != NULL */
  return results_filter_type (tags, (CTM_TAG_TYPE_FUNCTION |
                                     CTM_TAG_TYPE_PROTOTYPE |
                                     CTM_TAG_TYPE_METHOD |
                                     CTM_TAG_TYPE_MACRO_WITH_ARG));
}

GList *
ctm_completion_get_methods (CtmSourceFile *file,
                            langType       lang,
                            const gchar   *name,
                            gboolean       partial)
{
  /* FIXME: not sure empty scope ("") will work */
  return ctm_completion_get_scoped_methods (file, lang, NULL, name, partial);
}
