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

/* inspired from TMTag from the original TagManager */

#ifndef H_CTM_TAG
#define H_CTM_TAG

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS
/* types are declared before local includes for circular deps */

typedef enum _CtmTagType
{
  CTM_TAG_TYPE_UNDEF          = 0, /*!< Unknown type */
  CTM_TAG_TYPE_CLASS          = 1 << 0, /*!< Class declaration */
  CTM_TAG_TYPE_ENUM           = 1 << 1, /*!< Enum declaration */
  CTM_TAG_TYPE_ENUMERATOR     = 1 << 2, /*!< Enumerator value */
  CTM_TAG_TYPE_FIELD          = 1 << 3, /*!< Field (Java only) */
  CTM_TAG_TYPE_FUNCTION       = 1 << 4, /*!< Function definition */
  CTM_TAG_TYPE_INTERFACE      = 1 << 5, /*!< Interface (Java only) */
  CTM_TAG_TYPE_MEMBER         = 1 << 6, /*!< Member variable of class/struct */
  CTM_TAG_TYPE_METHOD         = 1 << 7, /*!< Class method (Java only) */
  CTM_TAG_TYPE_NAMESPACE      = 1 << 8, /*!< Namespace declaration */
  CTM_TAG_TYPE_PACKAGE        = 1 << 9, /*!< Package (Java only) */
  CTM_TAG_TYPE_PROTOTYPE      = 1 << 10, /*!< Function prototype */
  CTM_TAG_TYPE_STRUCT         = 1 << 11, /*!< Struct declaration */
  CTM_TAG_TYPE_TYPEDEF        = 1 << 12, /*!< Typedef */
  CTM_TAG_TYPE_UNION          = 1 << 13, /*!< Union */
  CTM_TAG_TYPE_VARIABLE       = 1 << 14, /*!< Variable */
  CTM_TAG_TYPE_EXTERNVAR      = 1 << 15, /*!< Extern or forward declaration */
  CTM_TAG_TYPE_MACRO          = 1 << 16, /*!<  Macro (without arguments) */
  CTM_TAG_TYPE_MACRO_WITH_ARG = 1 << 17, /*!< Parameterized macro */
  CTM_TAG_TYPE_OTHER          = 1 << 18, /*!< Other (non C/C++/Java tag) */
  CTM_TAG_TYPE_ANY            = 0x07ffff /*!< Maximum value of TMTagType */
} CtmTagType;

/* tag access type for C++/Java member functions and variables */
typedef gchar             CtmTagAccess;
#define CTM_TAG_ACCESS_PUBLIC     'p' /* public member */
#define CTM_TAG_ACCESS_PROTECTED  'r' /* protected member */
#define CTM_TAG_ACCESS_PRIVATE    'v' /* private member */
#define CTM_TAG_ACCESS_FRIEND     'f' /* friend members/functions */
#define CTM_TAG_ACCESS_DEFAULT    'd' /* default access (Java) */
#define CTM_TAG_ACCESS_UNKNOWN    'x' /* unknown access type */

/* tag implementation type for functions */
typedef gchar             CtmTagImpl;
#define CTM_TAG_IMPL_VIRTUAL      'v' /* virtual implementation */
#define CTM_TAG_IMPL_UNKNOWN      'x' /* unknown implementation */

typedef struct _CtmTag CtmTag;

typedef gint      (*CtmTagCompareFunc)    (const CtmTag *a,
                                           const CtmTag *b);
typedef gint      (*CtmTagMatchFunc)      (const CtmTag  *tag,
                                           gpointer       data);
G_END_DECLS

#include "ctm-source-file.h"
#ifdef CTM_BUILD
# include "ctags/entry.h"
#else
typedef void tagEntryInfo;
#endif

G_BEGIN_DECLS


#define CTM_TYPE_TAG      (ctm_tag_get_type ())
#define CTM_TAG(T)        ((CtmTag *) (T))


struct _CtmTag {
  gint            ref_count;
  
  CtmSourceFile  *file;
  gchar          *name;
  CtmTagType      type;
  /* atts */
  gulong          line;         /* line at which the tag occurred */
  gboolean        local;        /* whether the tag is of local scope */
  gchar          *arglist;      /* argument list (functions/prototypes/macros) */
  gchar          *scope;        /* scope of the tag */
  gchar          *inheritance;  /* parent classes */
  gchar          *var_type;     /* variable type (maps to struct for typedefs) */
  CtmTagAccess    access;       /* access type (public/protected/private/etc.) */
  CtmTagImpl      impl;         /* implementation (virtual, etc.) */
};


CtmTag       *ctm_tag_new             (CtmSourceFile       *file,
                                       const tagEntryInfo  *entry);
CtmTag       *ctm_tag_ref             (CtmTag *tag);
void          ctm_tag_unref           (CtmTag *tag);
GType         ctm_tag_get_type        (void) G_GNUC_CONST;

gint          ctm_tag_match_all         (const CtmTag  *tag,
                                         gpointer       dummy);
gint          ctm_tag_cmp_name          (const CtmTag *a,
                                         const CtmTag *b);
gint          ctm_tag_match_name        (const CtmTag  *tag,
                                         gpointer       name);
gint          ctm_tag_match_name_start  (const CtmTag  *tag,
                                         gpointer       start);
gint          ctm_tag_cmp_type          (const CtmTag *a,
                                         const CtmTag *b);
gint          ctm_tag_match_type        (const CtmTag  *tag,
                                         gpointer       type);
gint          ctm_tag_cmp_line          (const CtmTag *a,
                                         const CtmTag *b);
gint          ctm_tag_match_line        (const CtmTag  *tag,
                                         gpointer       line_as_ptr);
gint          ctm_tag_cmp_scope         (const CtmTag *a,
                                         const CtmTag *b);
gint          ctm_tag_match_scope       (const CtmTag  *tag,
                                         gpointer       scope);
gint          ctm_tag_cmp_file          (const CtmTag *a,
                                         const CtmTag *b);
gint          ctm_tag_match_file        (const CtmTag  *tag,
                                         gpointer       file);
gint          ctm_tag_cmp_filename      (const CtmTag *a,
                                         const CtmTag *b);
gint          ctm_tag_match_filename    (const CtmTag  *tag,
                                         gpointer       filename);


G_END_DECLS

#endif /* guard */
