SUBDIRS = mio

noinst_LIBRARIES   = libctags.a

AM_CPPFLAGS = -DG_LOG_DOMAIN=\"CTags\"
AM_CFLAGS   = @GTK_CFLAGS@

parsers   = abc.c \
            actionscript.c \
            asm.c \
            basic.c \
            c.c \
            cobol.c \
            conf.c \
            css.c \
            diff.c \
            docbook.c \
            fortran.c \
            haskell.c \
            haxe.c \
            html.c \
            js.c \
            latex.c \
            lua.c \
            make.c \
            markdown.c \
            matlab.c \
            nsis.c \
            objc.c \
            pascal.c \
            perl.c \
            php.c \
            python.c \
            r.c \
            rest.c \
            ruby.c \
            sh.c \
            sql.c \
            tcl.c \
            txt2tags.c \
            verilog.c \
            vhdl.c

libctags_a_SOURCES  = args.c \
                      args.h \
                      ctags.c \
                      ctags.h \
                      entry.c \
                      entry.h \
                      general.h \
                      get.c \
                      get.h \
                      keyword.c \
                      keyword.h \
                      lregex.c \
                      main.h \
                      nestlevel.c \
                      nestlevel.h \
                      options.c \
                      options.h \
                      parse.c \
                      parse.h \
                      parsers.h \
                      read.c \
                      read.h \
                      sort.c \
                      sort.h \
                      strlist.c \
                      strlist.h \
                      vstring.c \
                      vstring.h \
                      $(parsers)
libctags_a_LIBADD   = mio/libmio.a
