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

#include "ctm-tag.h"

#include <string.h>
#include <glib.h>
#include <glib-object.h>

#include "ctags/entry.h"


static CtmTagType
get_entry_type (const tagEntryInfo *entry)
{
  static const struct {
    const gchar  *name;
    CtmTagType    type;
  } match_table[] = {
    /* keep this sorted by .name */
    { "class",      CTM_TAG_TYPE_CLASS },
    { "enum",       CTM_TAG_TYPE_ENUM },
    { "enumerator", CTM_TAG_TYPE_ENUMERATOR },
    { "externvar",  CTM_TAG_TYPE_EXTERNVAR },
    { "field",      CTM_TAG_TYPE_FIELD },
    { "function",   CTM_TAG_TYPE_FUNCTION },
    { "interface",  CTM_TAG_TYPE_INTERFACE },
    { "macro",      CTM_TAG_TYPE_MACRO },
    { "member",     CTM_TAG_TYPE_MEMBER },
    { "method",     CTM_TAG_TYPE_METHOD },
    { "namespace",  CTM_TAG_TYPE_NAMESPACE },
    { "other",      CTM_TAG_TYPE_OTHER },
    { "package",    CTM_TAG_TYPE_PACKAGE },
    { "prototype",  CTM_TAG_TYPE_PROTOTYPE },
    { "struct",     CTM_TAG_TYPE_STRUCT },
    { "typedef",    CTM_TAG_TYPE_TYPEDEF },
    { "union",      CTM_TAG_TYPE_UNION },
    { "variable",   CTM_TAG_TYPE_VARIABLE }
  };
  guint       i;
  CtmTagType  type = CTM_TAG_TYPE_UNDEF;
  
  g_return_val_if_fail (entry->kindName != NULL, CTM_TAG_TYPE_UNDEF);
  
  for (i = 0; i < G_N_ELEMENTS (match_table); i++) {
    gint cmp;
    
    cmp = strcmp (entry->kindName, match_table[i].name);
    if (cmp == 0) {
      type = match_table[i].type;
      break;
    } else if (cmp < 0) {
      /* we're sorted alphabetically, no need to search further then */
      break;
    }
  }
  
  /* if we have args and it is macro type, it's actually macro with args */
  if (type & CTM_TAG_TYPE_MACRO && entry->extensionFields.arglist) {
    type = CTM_TAG_TYPE_MACRO_WITH_ARG;
  }
  
  return type;
}

static gchar *
get_entry_scope (const tagEntryInfo *entry)
{
  /* FIXME: taken from tm_tag_init(), but... ?? */
  if (entry->extensionFields.scope[1] &&
      (g_ascii_isalpha (entry->extensionFields.scope[1][0]) ||
       entry->extensionFields.scope[1][0] == '_')) {
    return g_strdup (entry->extensionFields.scope[1]);
  } else {
    return NULL;
  }
}

static CtmTagAccess
get_entry_access (const tagEntryInfo *entry)
{
  static const struct {
    const gchar  *name;
    CtmTagAccess  access;
  } match_table[] = {
    /* keep this sorted by .name */
    { "default",    CTM_TAG_ACCESS_DEFAULT },
    { "friend",     CTM_TAG_ACCESS_FRIEND },
    { "private",    CTM_TAG_ACCESS_PRIVATE },
    { "protected",  CTM_TAG_ACCESS_PROTECTED },
    { "public",     CTM_TAG_ACCESS_PUBLIC }
  };
  
  if (entry->extensionFields.access) {
    guint i;
    
    for (i = 0; i < G_N_ELEMENTS (match_table); i++) {
      gint cmp;
      
      cmp = strcmp (entry->extensionFields.access, match_table[i].name);
      if (cmp == 0) {
        return match_table[i].access;
      } else if (cmp < 0) {
        /* we're sorted alphabetically, no need to search further then */
        break;
      }
    }
    
    g_debug ("unknown access type \"%s\"", entry->extensionFields.access);
  }
  
  return CTM_TAG_ACCESS_UNKNOWN;
}

static CtmTagImpl
get_entry_impl (const tagEntryInfo *entry)
{
  if (entry->extensionFields.implementation) {
    if (strcmp (entry->extensionFields.implementation, "virtual") == 0 ||
        strcmp (entry->extensionFields.implementation, "pure virtual") == 0) {
      return CTM_TAG_IMPL_VIRTUAL;
    } else {
      g_debug ("unknown implementation type \"%s\"",
               entry->extensionFields.implementation);
    }
  }
  
  return CTM_TAG_IMPL_UNKNOWN;
}

CtmTag *
ctm_tag_new (CtmSourceFile       *file,
             const tagEntryInfo  *entry)
{
  CtmTag *tag;
  
  g_return_val_if_fail (file != NULL, NULL);
  g_return_val_if_fail (entry != NULL, NULL);
  g_return_val_if_fail (entry->name != NULL, NULL);
  
  tag = g_slice_alloc (sizeof *tag);
  tag->ref_count  = 1;
  
  /* The tag doesn't hold a ref to the source file because of circular
   * refs */
  tag->file         = /*ctm_source_file_ref*/ (file);
  tag->name         = g_strdup (entry->name);
  tag->type         = get_entry_type (entry);
  tag->line         = entry->lineNumber;
  tag->local        = entry->isFileScope;
  tag->arglist      = g_strdup (entry->extensionFields.arglist);
  tag->scope        = get_entry_scope (entry);
  tag->inheritance  = g_strdup (entry->extensionFields.inheritance);
  tag->var_type     = g_strdup (entry->extensionFields.varType);
  tag->access       = get_entry_access (entry);
  tag->impl         = get_entry_impl (entry);
  
  return tag;
}

CtmTag *
ctm_tag_ref (CtmTag *tag)
{
  g_atomic_int_inc (&tag->ref_count);
  
  return tag;
}

void
ctm_tag_unref (CtmTag *tag)
{
  /* be NULL-proof because we replace tm_tag_unref() that is NULL-proof and
   * we don't want to debug its usage yet, but otherwise we could drop the check */
  if (tag && g_atomic_int_dec_and_test (&tag->ref_count)) {
    
    /*ctm_source_file_unref (tag->file);*/
    g_free (tag->name);
    g_free (tag->arglist);
    g_free (tag->scope);
    g_free (tag->inheritance);
    g_free (tag->var_type);
    
    g_slice_free1 (sizeof *tag, tag);
  }
}

GType
ctm_tag_get_type (void)
{
  static gsize type = 0;
  
  if (g_once_init_enter (&type)) {
    GType t;
    
    t = g_boxed_type_register_static ("CtmTag",
                                      (GBoxedCopyFunc) ctm_tag_ref,
                                      (GBoxedFreeFunc) ctm_tag_unref);
    g_once_init_leave (&type, t);
  }
  
  return type;
}



/* default compare funcs */
/* FIXME: check these funcs */

gint
ctm_tag_match_all (const CtmTag *tag,
                   gpointer      dummy)
{
  return 0;
}

/**
 * ctm_tag_cmp_name:
 * @a: A #CtmTag
 * @b: Another #CtmTag
 * 
 * Compares by name and by line if name is equal.
 * 
 * Returns: The difference between the two tags
 */
gint
ctm_tag_cmp_name (const CtmTag *a,
                  const CtmTag *b)
{
  gint cmp;
  
  cmp = strcmp (a->name, b->name);
  if (cmp == 0) {
    cmp = (gint) (a->line - b->line);
  }
  
  return cmp;
}

gint
ctm_tag_match_name (const CtmTag *tag,
                    gpointer      name)
{
  return g_strcmp0 (name, tag->name);
}

/* FIXME: check this func */
gint
ctm_tag_match_name_start (const CtmTag *tag,
                          gpointer      start)
{
  register const gchar *p1 = tag->name;
  register const gchar *p2 = start;
  gint                  cmp = 0;
  
  /* compare like strncmp(p2, p1, strlen(p2)) */
  while (*p2 && (cmp = *p2 - *p1) == 0) {
    p1++, p2++;
  }
  
  return cmp;
}

gint
ctm_tag_cmp_type (const CtmTag *a,
                  const CtmTag *b)
{
  gint cmp;
  
  if ((cmp = a->type - b->type) == 0) {
    cmp = ctm_tag_cmp_name (a, b);
  }
  
  return cmp;
}

/* FIXME: @type can only be a single type because the sort function can't sort by
 * multiple types since it doesn't know them... */
gint
ctm_tag_match_type (const CtmTag  *tag,
                    gpointer       type)
{
  return GPOINTER_TO_UINT (type) - tag->type;
}

/**
 * ctm_tag_cmp_line:
 * @a: 
 * @b: 
 * 
 * Compares by line, and then by name if lines are equal.
 * 
 * Returns: 
 */
gint
ctm_tag_cmp_line (const CtmTag *a,
                  const CtmTag *b)
{
  gint cmp;
  
  cmp = (gint) (a->line - b->line);
  if (cmp == 0) {
    /* if lines are equal, sort by name */
    cmp = strcmp (a->name, b->name);
  }
  
  return cmp;
}

gint
ctm_tag_match_line (const CtmTag *tag,
                    gpointer      line_as_ptr)
{
  return GPOINTER_TO_UINT (line_as_ptr) - tag->line;
}

gint
ctm_tag_cmp_scope (const CtmTag *a,
                   const CtmTag *b)
{
  gint cmp;
  
  cmp = g_strcmp0 (a->scope, b->scope);
  if (cmp == 0) {
    cmp = ctm_tag_cmp_name (a, b);
  }
  
  return cmp;
}

gint
ctm_tag_match_scope (const CtmTag *tag,
                     gpointer      scope)
{
  return g_strcmp0 (scope, tag->scope);
}

gint
ctm_tag_cmp_file (const CtmTag *a,
                  const CtmTag *b)
{
  return (gint) (a->file - b->file);
}

gint
ctm_tag_match_file (const CtmTag *tag,
                    gpointer      file)
{
  return (gint) ((CtmSourceFile *) file - tag->file);
}

gint
ctm_tag_cmp_filename (const CtmTag *a,
                      const CtmTag *b)
{
  gint cmp;
  
  cmp = strcmp (a->file->name, b->file->name);
  if (cmp == 0) {
    cmp = ctm_tag_cmp_name (a, b);
  }
  
  return cmp;
}

gint
ctm_tag_match_filename (const CtmTag *tag,
                        gpointer      filename)
{
  return g_strcmp0 (filename, tag->file->name);
}
