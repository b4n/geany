
lib_LIBRARIES   = libctagsmanager.a

AM_CPPFLAGS  = -DG_LOG_DOMAIN=\"CTagsManager\" -DCTM_BUILD
AM_CFLAGS    = @GTK_CFLAGS@ @GTHREAD_CFLAGS@ -I$(srcdir)/.. -I$(srcdir)/../ctags

libctagsmanager_a_SOURCES   = ctm-completion.c \
                              ctm-completion.h \
                              ctm-data-backend.c \
                              ctm-data-backend.h \
                              ctm-parse.c \
                              ctm-parse.h \
                              ctm-source-file.c \
                              ctm-source-file.h \
                              ctm-tag.c \
                              ctm-tag.h \
                              ctm-workspace.c \
                              ctm-workspace.h \
                              backends/ctm-data-backend-simple.c \
                              backends/ctm-data-backend-multicache.c
