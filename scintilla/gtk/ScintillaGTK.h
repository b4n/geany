// Scintilla source code edit control
// ScintillaGTK.h - GTK+ specific subclass of ScintillaBase
// Copyright 1998-2004 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef SCINTILLAGTK_H
#define SCINTILLAGTK_H

#ifdef SCI_NAMESPACE
using namespace Scintilla;
#endif

// It's a bit odd to expose only ScintillaBase from the ScintillaGTK header, but that's all we
// actually need, so avoids having to have the ScintillaGTK class exposed
ScintillaBase *ScintillaBaseFromWidget(GtkWidget *widget);

#endif
