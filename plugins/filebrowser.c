/*
 *      filebrowser.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2007-2012 Enrico Tröger <enrico(dot)troeger(at)uvena(dot)de>
 *      Copyright 2007-2012 Nick Treleaven <nick(dot)treleaven(at)btinternet(dot)com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License along
 *      with this program; if not, write to the Free Software Foundation, Inc.,
 *      51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* Sidebar file browser plugin. */

#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif

#include "geanyplugin.h"
#include "gtkcompat.h"
#include <string.h>

#include <gdk/gdkkeysyms.h>

#ifdef G_OS_WIN32
# include <windows.h>

# define OPEN_CMD "explorer \"%d\""
#elif defined(__APPLE__)
# define OPEN_CMD "open \"%d\""
#else
# define OPEN_CMD "nautilus \"%d\""
#endif


/* Keybinding(s) */
enum
{
	KB_FOCUS_FILE_LIST,
	KB_FOCUS_PATH_ENTRY,
	KB_COUNT
};


enum
{
	FILEVIEW_COLUMN_ICON = 0,
	FILEVIEW_COLUMN_NAME,
	FILEVIEW_COLUMN_FILENAME, /* the full filename, including path for display as tooltip */
	FILEVIEW_N_COLUMNS
};


typedef struct
{
	GeanyPlugin *plugin;

	gboolean fb_set_project_base_path;
	gboolean fb_follow_path;
	gboolean show_hidden_files;
	gboolean hide_object_files;

	GtkWidget *file_view_vbox;
	GtkWidget *file_view;
	GtkListStore *file_store;
	GtkTreeIter *last_dir_iter;
	GtkEntryCompletion *entry_completion;

	GtkWidget *filter_combo;
	GtkWidget *filter_entry;
	GtkWidget *path_combo;
	GtkWidget *path_entry;
	gchar *current_dir; /* in locale-encoding */
	gconstpointer last_activate_path;
	gchar *open_cmd; /* in locale-encoding */
	gchar *config_file;
	gchar **filter;
	gchar *hidden_file_extensions;

	gint page_number;

	struct
	{
		GtkWidget *open;
		GtkWidget *open_external;
		GtkWidget *find_in_files;
		GtkWidget *show_hidden_files;
	} popup_items;
}
Filebrowser;

static Filebrowser *G_self; /* FIXME; required for keybindings */


#ifdef G_OS_WIN32
static gboolean win32_check_hidden(const gchar *filename)
{
	DWORD attrs;
	static wchar_t w_filename[MAX_PATH];
	MultiByteToWideChar(CP_UTF8, 0, filename, -1, w_filename, sizeof(w_filename));
	attrs = GetFileAttributesW(w_filename);
	if (attrs != INVALID_FILE_ATTRIBUTES && attrs & FILE_ATTRIBUTE_HIDDEN)
		return TRUE;
	return FALSE;
}
#endif


/* Returns: whether name should be hidden. */
static gboolean check_hidden(const gchar *filename, const gchar *base_name)
{
	gsize len;

#ifdef G_OS_WIN32
	if (win32_check_hidden(filename))
		return TRUE;
#else
	if (base_name[0] == '.')
		return TRUE;
#endif

	len = strlen(base_name);
	return base_name[len - 1] == '~';
}


static gboolean check_object(Filebrowser *self, const gchar *base_name)
{
	gboolean ret = FALSE;
	gchar **ptr;
	gchar **exts = g_strsplit(self->hidden_file_extensions, " ", -1);

	foreach_strv(ptr, exts)
	{
		if (g_str_has_suffix(base_name, *ptr))
		{
			ret = TRUE;
			break;
		}
	}
	g_strfreev(exts);
	return ret;
}


/* Returns: whether filename should be removed. */
static gboolean check_filtered(Filebrowser *self, const gchar *base_name)
{
	gchar **filter_item;

	if (self->filter == NULL)
		return FALSE;

	foreach_strv(filter_item, self->filter)
	{
		if (utils_str_equal(*filter_item, "*") || g_pattern_match_simple(*filter_item, base_name))
		{
			return FALSE;
		}
	}
	return TRUE;
}


/* name is in locale encoding */
static void add_item(Filebrowser *self, const gchar *name)
{
	GtkTreeIter iter;
	gchar *fname, *utf8_name, *utf8_fullname;
	const gchar *sep;
	gboolean dir;

	if (G_UNLIKELY(EMPTY(name)))
		return;

	/* root directory doesn't need separator */
	sep = (utils_str_equal(self->current_dir, "/")) ? "" : G_DIR_SEPARATOR_S;
	fname = g_strconcat(self->current_dir, sep, name, NULL);
	dir = g_file_test(fname, G_FILE_TEST_IS_DIR);
	utf8_fullname = utils_get_locale_from_utf8(fname);
	utf8_name = utils_get_utf8_from_locale(name);
	g_free(fname);

	if (! self->show_hidden_files && check_hidden(utf8_fullname, utf8_name))
		goto done;

	if (dir)
	{
		if (self->last_dir_iter == NULL)
			gtk_list_store_prepend(self->file_store, &iter);
		else
		{
			gtk_list_store_insert_after(self->file_store, &iter, self->last_dir_iter);
			gtk_tree_iter_free(self->last_dir_iter);
		}
		self->last_dir_iter = gtk_tree_iter_copy(&iter);
	}
	else
	{
		if (! self->show_hidden_files && self->hide_object_files && check_object(self, utf8_name))
			goto done;
		if (check_filtered(self, utf8_name))
			goto done;

		gtk_list_store_append(self->file_store, &iter);
	}
	gtk_list_store_set(self->file_store, &iter,
		FILEVIEW_COLUMN_ICON, (dir) ? GTK_STOCK_DIRECTORY : GTK_STOCK_FILE,
		FILEVIEW_COLUMN_NAME, utf8_name,
		FILEVIEW_COLUMN_FILENAME, utf8_fullname,
		-1);
done:
	g_free(utf8_name);
	g_free(utf8_fullname);
}


/* adds ".." to the start of the file list */
static void add_top_level_entry(Filebrowser *self)
{
	GtkTreeIter iter;
	gchar *utf8_dir;

	if (EMPTY(g_path_skip_root(self->current_dir)))
		return;	/* ignore 'C:\' or '/' */

	utf8_dir = g_path_get_dirname(self->current_dir);
	SETPTR(utf8_dir, utils_get_utf8_from_locale(utf8_dir));

	gtk_list_store_prepend(self->file_store, &iter);
	self->last_dir_iter = gtk_tree_iter_copy(&iter);

	gtk_list_store_set(self->file_store, &iter,
		FILEVIEW_COLUMN_ICON, GTK_STOCK_DIRECTORY,
		FILEVIEW_COLUMN_NAME, "..",
		FILEVIEW_COLUMN_FILENAME, utf8_dir,
		-1);
	g_free(utf8_dir);
}


static void clear(Filebrowser *self)
{
	gtk_list_store_clear(self->file_store);

	/* reset the directory item pointer */
	if (self->last_dir_iter != NULL)
		gtk_tree_iter_free(self->last_dir_iter);
	self->last_dir_iter = NULL;
}


/* recreate the tree model from current_dir. */
static void refresh(Filebrowser *self)
{
	gchar *utf8_dir;
	GSList *list, *node;

	/* don't clear when the new path doesn't exist */
	if (! g_file_test(self->current_dir, G_FILE_TEST_EXISTS))
		return;

	clear(self);

	utf8_dir = utils_get_utf8_from_locale(self->current_dir);
	gtk_entry_set_text(GTK_ENTRY(self->path_entry), utf8_dir);
	gtk_widget_set_tooltip_text(self->path_entry, utf8_dir);
	ui_combo_box_add_to_history(GTK_COMBO_BOX_TEXT(self->path_combo), utf8_dir, 0);
	g_free(utf8_dir);

	add_top_level_entry(self);	/* ".." item */

	list = utils_get_file_list(self->current_dir, NULL, NULL);
	if (list != NULL)
	{
		/* free filenames as we go through the list */
		foreach_slist(node, list)
		{
			gchar *fname = node->data;

			add_item(self, fname);
			g_free(fname);
		}
		g_slist_free(list);
	}
	gtk_entry_completion_set_model(self->entry_completion, GTK_TREE_MODEL(self->file_store));
}


static void on_go_home(Filebrowser *self)
{
	SETPTR(self->current_dir, g_strdup(g_get_home_dir()));
	refresh(self);
}


/* TODO: use utils_get_default_dir_utf8() */
static gchar *get_default_dir(Filebrowser *self)
{
	const gchar *dir = NULL;
	GeanyProject *project = self->plugin->geany_data->app->project;

	if (project)
		dir = project->base_path;
	else
		dir = self->plugin->geany_data->prefs->default_open_path;

	if (!EMPTY(dir))
		return utils_get_locale_from_utf8(dir);

	return g_get_current_dir();
}


static void on_current_path(Filebrowser *self)
{
	gchar *fname;
	gchar *dir;
	GeanyDocument *doc = document_get_current();

	if (doc == NULL || doc->file_name == NULL || ! g_path_is_absolute(doc->file_name))
	{
		SETPTR(self->current_dir, get_default_dir(self));
		refresh(self);
		return;
	}
	fname = doc->file_name;
	fname = utils_get_locale_from_utf8(fname);
	dir = g_path_get_dirname(fname);
	g_free(fname);

	SETPTR(self->current_dir, dir);
	refresh(self);
}


static void on_go_up(Filebrowser *self)
{
	gsize len = strlen(self->current_dir);
	if (self->current_dir[len-1] == G_DIR_SEPARATOR)
		self->current_dir[len-1] = '\0';
	/* remove the highest directory part (which becomes the basename of current_dir) */
	SETPTR(self->current_dir, g_path_get_dirname(self->current_dir));
	refresh(self);
}


static gboolean check_single_selection(GtkTreeSelection *treesel)
{
	if (gtk_tree_selection_count_selected_rows(treesel) == 1)
		return TRUE;

	ui_set_statusbar(FALSE, _("Too many items selected!"));
	return FALSE;
}


/* Returns: TRUE if at least one of selected_items is a folder. */
static gboolean is_folder_selected(Filebrowser *self, GList *selected_items)
{
	GList *item;
	GtkTreeModel *model = GTK_TREE_MODEL(self->file_store);
	gboolean dir_found = FALSE;

	for (item = selected_items; item != NULL; item = g_list_next(item))
	{
		gchar *icon;
		GtkTreeIter iter;
		GtkTreePath *treepath;

		treepath = (GtkTreePath*) item->data;
		gtk_tree_model_get_iter(model, &iter, treepath);
		gtk_tree_model_get(model, &iter, FILEVIEW_COLUMN_ICON, &icon, -1);

		if (utils_str_equal(icon, GTK_STOCK_DIRECTORY))
		{
			dir_found = TRUE;
			g_free(icon);
			break;
		}
		g_free(icon);
	}
	return dir_found;
}


/* Returns: the full filename in locale encoding. */
static gchar *get_tree_path_filename(Filebrowser *self, GtkTreePath *treepath)
{
	GtkTreeModel *model = GTK_TREE_MODEL(self->file_store);
	GtkTreeIter iter;
	gchar *name, *fname;

	gtk_tree_model_get_iter(model, &iter, treepath);
	gtk_tree_model_get(model, &iter, FILEVIEW_COLUMN_FILENAME, &name, -1);

	fname = utils_get_locale_from_utf8(name);
	g_free(name);

	return fname;
}


static void open_external(Filebrowser *self, const gchar *fname, gboolean dir_found)
{
	gchar *cmd;
	gchar *locale_cmd;
	gchar *dir;
	GString *cmd_str = g_string_new(self->open_cmd);
	GError *error = NULL;

	if (! dir_found)
		dir = g_path_get_dirname(fname);
	else
		dir = g_strdup(fname);

	utils_string_replace_all(cmd_str, "%f", fname);
	utils_string_replace_all(cmd_str, "%d", dir);

	cmd = g_string_free(cmd_str, FALSE);
	locale_cmd = utils_get_locale_from_utf8(cmd);
	if (! g_spawn_command_line_async(locale_cmd, &error))
	{
		gchar *c = strchr(cmd, ' ');

		if (c != NULL)
			*c = '\0';
		ui_set_statusbar(TRUE,
			_("Could not execute configured external command '%s' (%s)."),
			cmd, error->message);
		g_error_free(error);
	}
	g_free(locale_cmd);
	g_free(cmd);
	g_free(dir);
}


static void on_external_open(Filebrowser *self)
{
	GtkTreeSelection *treesel;
	GtkTreeModel *model;
	GList *list;
	gboolean dir_found;

	treesel = gtk_tree_view_get_selection(GTK_TREE_VIEW(self->file_view));

	list = gtk_tree_selection_get_selected_rows(treesel, &model);
	dir_found = is_folder_selected(self, list);

	if (! dir_found || check_single_selection(treesel))
	{
		GList *item;

		for (item = list; item != NULL; item = g_list_next(item))
		{
			GtkTreePath *treepath = item->data;
			gchar *fname = get_tree_path_filename(self, treepath);

			open_external(self, fname, dir_found);
			g_free(fname);
		}
	}

	g_list_foreach(list, (GFunc) gtk_tree_path_free, NULL);
	g_list_free(list);
}


/* We use document_open_files() as it's more efficient. */
static void open_selected_files(Filebrowser *self, GList *list, gboolean do_not_focus)
{
	GSList *files = NULL;
	GList *item;
	GeanyDocument *doc;

	for (item = list; item != NULL; item = g_list_next(item))
	{
		GtkTreePath *treepath = item->data;
		gchar *fname = get_tree_path_filename(self, treepath);

		files = g_slist_prepend(files, fname);
	}
	files = g_slist_reverse(files);
	document_open_files(files, FALSE, NULL, NULL);
	doc = document_get_current();
	if (doc != NULL && ! do_not_focus)
		keybindings_send_command(GEANY_KEY_GROUP_FOCUS, GEANY_KEYS_FOCUS_EDITOR);

	g_slist_foreach(files, (GFunc) g_free, NULL);	/* free filenames */
	g_slist_free(files);
}


static void open_folder(Filebrowser *self, GtkTreePath *treepath)
{
	gchar *fname = get_tree_path_filename(self, treepath);

	SETPTR(self->current_dir, fname);
	refresh(self);
}


static void open_selected(Filebrowser *self, gboolean do_not_focus)
{
	GtkTreeSelection *treesel;
	GtkTreeModel *model;
	GList *list;
	gboolean dir_found;

	treesel = gtk_tree_view_get_selection(GTK_TREE_VIEW(self->file_view));

	list = gtk_tree_selection_get_selected_rows(treesel, &model);
	dir_found = is_folder_selected(self, list);

	if (dir_found)
	{
		if (check_single_selection(treesel))
		{
			GtkTreePath *treepath = list->data;	/* first selected item */

			open_folder(self, treepath);
		}
	}
	else
		open_selected_files(self, list, do_not_focus);

	g_list_foreach(list, (GFunc) gtk_tree_path_free, NULL);
	g_list_free(list);
}


static void on_open_clicked(Filebrowser *self)
{
	open_selected(self, FALSE);
}


static void on_find_in_files(Filebrowser *self)
{
	GtkTreeSelection *treesel;
	GtkTreeModel *model;
	GList *list;
	gchar *dir;
	gboolean is_dir = FALSE;

	treesel = gtk_tree_view_get_selection(GTK_TREE_VIEW(self->file_view));
	/* allow 0 or 1 selections */
	if (gtk_tree_selection_count_selected_rows(treesel) > 0 &&
		! check_single_selection(treesel))
		return;

	list = gtk_tree_selection_get_selected_rows(treesel, &model);
	is_dir = is_folder_selected(self, list);

	if (is_dir)
	{
		GtkTreePath *treepath = list->data;	/* first selected item */

		dir = get_tree_path_filename(self, treepath);
	}
	else
		dir = g_strdup(self->current_dir);

	g_list_foreach(list, (GFunc) gtk_tree_path_free, NULL);
	g_list_free(list);

	SETPTR(dir, utils_get_utf8_from_locale(dir));
	search_show_find_in_files_dialog(dir);
	g_free(dir);
}


static void on_hidden_files_clicked(GtkCheckMenuItem *item, Filebrowser *self)
{
	self->show_hidden_files = gtk_check_menu_item_get_active(item);
	refresh(self);
}


static void on_hide_sidebar(void)
{
	keybindings_send_command(GEANY_KEY_GROUP_VIEW, GEANY_KEYS_VIEW_SIDEBAR);
}


static void on_show_preferences(Filebrowser *self)
{
	plugin_show_configure(self->plugin);
}


static GtkWidget *create_popup_menu(Filebrowser *self)
{
	GtkWidget *item, *menu;

	menu = gtk_menu_new();

	item = ui_image_menu_item_new(GTK_STOCK_OPEN, _("Open in _Geany"));
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect_swapped(item, "activate", G_CALLBACK(on_open_clicked), self);
	self->popup_items.open = item;

	item = ui_image_menu_item_new(GTK_STOCK_OPEN, _("Open _Externally"));
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect_swapped(item, "activate", G_CALLBACK(on_external_open), self);
	self->popup_items.open_external = item;

	item = gtk_separator_menu_item_new();
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);

	item = gtk_image_menu_item_new_from_stock(GTK_STOCK_REFRESH, NULL);
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect_swapped(item, "activate", G_CALLBACK(refresh), self);

	item = ui_image_menu_item_new(GTK_STOCK_FIND, _("_Find in Files..."));
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect_swapped(item, "activate", G_CALLBACK(on_find_in_files), self);
	self->popup_items.find_in_files = item;

	item = gtk_separator_menu_item_new();
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);

	item = gtk_check_menu_item_new_with_mnemonic(_("Show _Hidden Files"));
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_hidden_files_clicked), self);
	self->popup_items.show_hidden_files = item;

	item = gtk_separator_menu_item_new();
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);

	item = gtk_image_menu_item_new_from_stock(GTK_STOCK_PREFERENCES, NULL);
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_show_preferences), self);

	item = gtk_separator_menu_item_new();
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);

	item = ui_image_menu_item_new(GTK_STOCK_CLOSE, _("H_ide Sidebar"));
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_hide_sidebar), NULL);

	return menu;
}


static void on_tree_selection_changed(GtkTreeSelection *selection, Filebrowser *self)
{
	gboolean have_sel = (gtk_tree_selection_count_selected_rows(selection) > 0);
	gboolean multi_sel = (gtk_tree_selection_count_selected_rows(selection) > 1);

	if (self->popup_items.open != NULL)
		gtk_widget_set_sensitive(self->popup_items.open, have_sel);
	if (self->popup_items.open_external != NULL)
		gtk_widget_set_sensitive(self->popup_items.open_external, have_sel);
	if (self->popup_items.find_in_files != NULL)
		gtk_widget_set_sensitive(self->popup_items.find_in_files, have_sel && ! multi_sel);
}


static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, Filebrowser *self)
{
	if (event->button == 1 && event->type == GDK_2BUTTON_PRESS)
	{
		open_selected(self, FALSE);
		return TRUE;
	}
	else if (event->button == 3)
	{
		static GtkWidget *popup_menu = NULL;

		if (popup_menu == NULL)
			popup_menu = create_popup_menu(self);

		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(self->popup_items.show_hidden_files),
			self->show_hidden_files);
		gtk_menu_popup(GTK_MENU(popup_menu), NULL, NULL, NULL, NULL, event->button, event->time);
		/* don't return TRUE here, unless the selection won't be changed */
	}
	return FALSE;
}


static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, Filebrowser *self)
{
	if (ui_is_keyval_enter_or_return(event->keyval))
	{
		open_selected(self, FALSE);
		return TRUE;
	}

	if (event->keyval == GDK_space)
	{
		open_selected(self, TRUE);
		return TRUE;
	}

	if (( (event->keyval == GDK_Up || event->keyval == GDK_KP_Up) && (event->state & GDK_MOD1_MASK)) || /* FIXME: Alt-Up doesn't seem to work! */
		(event->keyval == GDK_BackSpace) )
	{
		on_go_up(self);
		return TRUE;
	}

	if ((event->keyval == GDK_F10 && event->state & GDK_SHIFT_MASK) || event->keyval == GDK_Menu)
	{
		GdkEventButton button_event;

		button_event.time = event->time;
		button_event.button = 3;

		on_button_press(widget, &button_event, self);
		return TRUE;
	}

	return FALSE;
}


static void clear_filter(Filebrowser *self)
{
	if (self->filter != NULL)
	{
		g_strfreev(self->filter);
		self->filter = NULL;
	}
}


static void on_path_entry_activate(GtkEntry *entry, Filebrowser *self)
{
	gchar *new_dir = (gchar*) gtk_entry_get_text(entry);

	if (!EMPTY(new_dir))
	{
		if (g_str_has_suffix(new_dir, ".."))
		{
			on_go_up(self);
			return;
		}
		else if (new_dir[0] == '~')
		{
			GString *str = g_string_new(new_dir);
			utils_string_replace_first(str, "~", g_get_home_dir());
			new_dir = g_string_free(str, FALSE);
		}
		else
			new_dir = utils_get_locale_from_utf8(new_dir);
	}
	else
		new_dir = g_strdup(g_get_home_dir());

	SETPTR(self->current_dir, new_dir);

	clear_filter(self);
	gtk_entry_set_text(GTK_ENTRY(self->filter_entry), "");
	refresh(self);
}


static void ui_combo_box_changed(GtkComboBox *combo, gpointer user_data)
{
	/* we get this callback on typing as well as choosing an item */
	if (gtk_combo_box_get_active(combo) >= 0)
		gtk_widget_activate(gtk_bin_get_child(GTK_BIN(combo)));
}


static void on_filter_activate(GtkEntry *entry, Filebrowser *self)
{
	/* We use spaces for consistency with Find in Files file patterns
	 * ';' also supported like original patch. */
	self->filter = g_strsplit_set(gtk_entry_get_text(entry), "; ", -1);
	if (self->filter == NULL || g_strv_length(self->filter) == 0)
	{
		clear_filter(self);
	}
	ui_combo_box_add_to_history(GTK_COMBO_BOX_TEXT(self->filter_combo), NULL, 0);
	refresh(self);
}


static void on_filter_clear(Filebrowser *self)
{
	clear_filter(self);
	refresh(self);
}


static void prepare_file_view(Filebrowser *self)
{
	GtkCellRenderer *text_renderer, *icon_renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;

	self->file_store = gtk_list_store_new(FILEVIEW_N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	gtk_tree_view_set_model(GTK_TREE_VIEW(self->file_view), GTK_TREE_MODEL(self->file_store));
	g_object_unref(self->file_store);

	icon_renderer = gtk_cell_renderer_pixbuf_new();
	text_renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new();
	gtk_tree_view_column_pack_start(column, icon_renderer, FALSE);
	gtk_tree_view_column_set_attributes(column, icon_renderer, "stock-id", FILEVIEW_COLUMN_ICON, NULL);
	gtk_tree_view_column_pack_start(column, text_renderer, TRUE);
	gtk_tree_view_column_set_attributes(column, text_renderer, "text", FILEVIEW_COLUMN_NAME, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(self->file_view), column);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(self->file_view), FALSE);

	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(self->file_view), TRUE);
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(self->file_view), FILEVIEW_COLUMN_NAME);

	ui_widget_modify_font_from_string(self->file_view, self->plugin->geany_data->interface_prefs->tagbar_font);

	/* tooltips */
	ui_tree_view_set_tooltip_text_column(GTK_TREE_VIEW(self->file_view), FILEVIEW_COLUMN_FILENAME);

	/* selection handling */
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(self->file_view));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

	/* Show the current path when the FB is first needed */
	g_signal_connect_swapped(self->file_view, "realize", G_CALLBACK(on_current_path), self);
	g_signal_connect(selection, "changed", G_CALLBACK(on_tree_selection_changed), self);
	g_signal_connect(self->file_view, "button-press-event", G_CALLBACK(on_button_press), self);
	g_signal_connect(self->file_view, "key-press-event", G_CALLBACK(on_key_press), self);
}


static GtkWidget *make_toolbar(Filebrowser *self)
{
	GtkWidget *wid, *toolbar;

	toolbar = gtk_toolbar_new();
	gtk_toolbar_set_icon_size(GTK_TOOLBAR(toolbar), GTK_ICON_SIZE_MENU);
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);

	wid = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_GO_UP));
	gtk_widget_set_tooltip_text(wid, _("Up"));
	g_signal_connect_swapped(wid, "clicked", G_CALLBACK(on_go_up), self);
	gtk_container_add(GTK_CONTAINER(toolbar), wid);

	wid = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_REFRESH));
	gtk_widget_set_tooltip_text(wid, _("Refresh"));
	g_signal_connect_swapped(wid, "clicked", G_CALLBACK(refresh), self);
	gtk_container_add(GTK_CONTAINER(toolbar), wid);

	wid = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_HOME));
	gtk_widget_set_tooltip_text(wid, _("Home"));
	g_signal_connect_swapped(wid, "clicked", G_CALLBACK(on_go_home), self);
	gtk_container_add(GTK_CONTAINER(toolbar), wid);

	wid = GTK_WIDGET(gtk_tool_button_new_from_stock(GTK_STOCK_JUMP_TO));
	gtk_widget_set_tooltip_text(wid, _("Set path from document"));
	g_signal_connect_swapped(wid, "clicked", G_CALLBACK(on_current_path), self);
	gtk_container_add(GTK_CONTAINER(toolbar), wid);

	return toolbar;
}


static GtkWidget *make_filterbar(Filebrowser *self)
{
	GtkWidget *label, *filterbar;

	filterbar = gtk_hbox_new(FALSE, 1);

	label = gtk_label_new(_("Filter:"));

	self->filter_combo = gtk_combo_box_text_new_with_entry();
	self->filter_entry = gtk_bin_get_child(GTK_BIN(self->filter_combo));

	ui_entry_add_clear_icon(GTK_ENTRY(self->filter_entry));
	g_signal_connect_swapped(self->filter_entry, "icon-release", G_CALLBACK(on_filter_clear), self);

	gtk_widget_set_tooltip_text(self->filter_entry,
		_("Filter your files with the usual wildcards. Separate multiple patterns with a space."));
	g_signal_connect(self->filter_entry, "activate", G_CALLBACK(on_filter_activate), self);
	g_signal_connect(self->filter_combo, "changed", G_CALLBACK(ui_combo_box_changed), NULL);

	gtk_box_pack_start(GTK_BOX(filterbar), label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(filterbar), self->filter_combo, TRUE, TRUE, 0);

	return filterbar;
}


static gboolean completion_match_func(GtkEntryCompletion *completion, const gchar *key,
									  GtkTreeIter *iter, gpointer user_data)
{
	gchar *str, *icon;
	gboolean result = FALSE;
	Filebrowser *self = user_data;

	gtk_tree_model_get(GTK_TREE_MODEL(self->file_store), iter,
		FILEVIEW_COLUMN_ICON, &icon, FILEVIEW_COLUMN_NAME, &str, -1);

	if (str != NULL && icon != NULL && utils_str_equal(icon, GTK_STOCK_DIRECTORY) &&
		! g_str_has_suffix(key, G_DIR_SEPARATOR_S))
	{
		/* key is something like "/tmp/te" and str is a filename like "test",
		 * so strip the path from key to make them comparable */
		gchar *base_name = g_path_get_basename(key);
		gchar *str_lowered = g_utf8_strdown(str, -1);
		result = g_str_has_prefix(str_lowered, base_name);
		g_free(base_name);
		g_free(str_lowered);
	}
	g_free(str);
	g_free(icon);

	return result;
}


static gboolean completion_match_selected(GtkEntryCompletion *widget, GtkTreeModel *model,
										  GtkTreeIter *iter, Filebrowser *self)
{
	gchar *str;
	gtk_tree_model_get(model, iter, FILEVIEW_COLUMN_NAME, &str, -1);
	if (str != NULL)
	{
		gchar *text = g_strconcat(self->current_dir, G_DIR_SEPARATOR_S, str, NULL);
		gtk_entry_set_text(GTK_ENTRY(self->path_entry), text);
		gtk_editable_set_position(GTK_EDITABLE(self->path_entry), -1);
		/* force change of directory when completion is done */
		on_path_entry_activate(GTK_ENTRY(self->path_entry), self);
		g_free(text);
	}
	g_free(str);

	return TRUE;
}


static void completion_create(Filebrowser *self)
{
	self->entry_completion = gtk_entry_completion_new();

	gtk_entry_completion_set_inline_completion(self->entry_completion, FALSE);
	gtk_entry_completion_set_popup_completion(self->entry_completion, TRUE);
	gtk_entry_completion_set_text_column(self->entry_completion, FILEVIEW_COLUMN_NAME);
	gtk_entry_completion_set_match_func(self->entry_completion, completion_match_func, self, NULL);

	g_signal_connect(self->entry_completion, "match-selected",
		G_CALLBACK(completion_match_selected), self);

	gtk_entry_set_completion(GTK_ENTRY(self->path_entry), self->entry_completion);
}


static void load_settings(Filebrowser *self)
{
	GKeyFile *config = g_key_file_new();

	self->config_file = g_strconcat(self->plugin->geany_data->app->configdir, G_DIR_SEPARATOR_S,
			"plugins", G_DIR_SEPARATOR_S, "filebrowser", G_DIR_SEPARATOR_S, "filebrowser.conf", NULL);
	g_key_file_load_from_file(config, self->config_file, G_KEY_FILE_NONE, NULL);

	self->open_cmd = utils_get_setting_string(config, "filebrowser", "open_command", OPEN_CMD);
	/* g_key_file_get_boolean defaults to FALSE */
	self->show_hidden_files = g_key_file_get_boolean(config, "filebrowser", "show_hidden_files", NULL);
	self->hide_object_files = utils_get_setting_boolean(config, "filebrowser", "hide_object_files", TRUE);
	self->hidden_file_extensions = utils_get_setting_string(config, "filebrowser", "hidden_file_extensions",
		".o .obj .so .dll .a .lib .pyc");
	self->fb_follow_path = g_key_file_get_boolean(config, "filebrowser", "fb_follow_path", NULL);
	self->fb_set_project_base_path = g_key_file_get_boolean(config, "filebrowser", "fb_set_project_base_path", NULL);

	g_key_file_free(config);
}


static void project_change_cb(G_GNUC_UNUSED GObject *obj, G_GNUC_UNUSED GKeyFile *config,
							  Filebrowser *self)
{
	gchar *new_dir;
	GeanyProject *project = self->plugin->geany_data->app->project;

	if (! self->fb_set_project_base_path || project == NULL || EMPTY(project->base_path))
		return;

	/* TODO this is a copy of project_get_base_path(), add it to the plugin API */
	if (g_path_is_absolute(project->base_path))
		new_dir = g_strdup(project->base_path);
	else
	{	/* build base_path out of project file name's dir and base_path */
		gchar *dir = g_path_get_dirname(project->file_name);

		new_dir = g_strconcat(dir, G_DIR_SEPARATOR_S, project->base_path, NULL);
		g_free(dir);
	}
	/* get it into locale encoding */
	SETPTR(new_dir, utils_get_locale_from_utf8(new_dir));

	if (! utils_str_equal(self->current_dir, new_dir))
	{
		SETPTR(self->current_dir, new_dir);
		refresh(self);
	}
	else
		g_free(new_dir);
}


static void document_activate_cb(G_GNUC_UNUSED GObject *obj, GeanyDocument *doc,
								 Filebrowser *self)
{
	gchar *new_dir;

	self->last_activate_path = doc->real_path;

	if (! self->fb_follow_path || doc->file_name == NULL || ! g_path_is_absolute(doc->file_name))
		return;

	new_dir = g_path_get_dirname(doc->file_name);
	SETPTR(new_dir, utils_get_locale_from_utf8(new_dir));

	if (! utils_str_equal(self->current_dir, new_dir))
	{
		SETPTR(self->current_dir, new_dir);
		refresh(self);
	}
	else
		g_free(new_dir);
}


static void document_save_cb(GObject *obj, GeanyDocument *doc, Filebrowser *self)
{
	if (!self->last_activate_path)
		document_activate_cb(obj, doc, self);
}


static void kb_activate(guint key_id)
{
	Filebrowser *self = G_self;
	gtk_notebook_set_current_page(GTK_NOTEBOOK(self->plugin->geany_data->main_widgets->sidebar_notebook),
			self->page_number);
	switch (key_id)
	{
		case KB_FOCUS_FILE_LIST:
			gtk_widget_grab_focus(self->file_view);
			break;
		case KB_FOCUS_PATH_ENTRY:
			gtk_widget_grab_focus(self->path_entry);
			break;
	}
}


static gboolean filebrowser_init(GeanyPlugin *plugin, gpointer user_data)
{
	GtkWidget *scrollwin, *toolbar, *filterbar;
	Filebrowser *self = g_malloc0(sizeof *self);
	GeanyKeyGroup *group;

	G_self = self; /* FIXME: needed for keybindings */

	geany_plugin_set_data(plugin, self, g_free);

	self->fb_set_project_base_path = FALSE;
	self->fb_follow_path = FALSE;
	self->show_hidden_files = FALSE;
	self->hide_object_files = TRUE;

	self->last_dir_iter = NULL;
	self->entry_completion = NULL;

	self->current_dir = NULL;
	self->last_activate_path = NULL;
	self->filter = NULL;
	self->hidden_file_extensions = NULL;

	self->page_number = 0;

	self->file_view_vbox = gtk_vbox_new(FALSE, 0);
	toolbar = make_toolbar(self);
	gtk_box_pack_start(GTK_BOX(self->file_view_vbox), toolbar, FALSE, FALSE, 0);

	filterbar = make_filterbar(self);
	gtk_box_pack_start(GTK_BOX(self->file_view_vbox), filterbar, FALSE, FALSE, 0);

	self->path_combo = gtk_combo_box_text_new_with_entry();
	gtk_box_pack_start(GTK_BOX(self->file_view_vbox), self->path_combo, FALSE, FALSE, 2);
	g_signal_connect(self->path_combo, "changed", G_CALLBACK(ui_combo_box_changed), self);
	self->path_entry = gtk_bin_get_child(GTK_BIN(self->path_combo));
	g_signal_connect(self->path_entry, "activate", G_CALLBACK(on_path_entry_activate), self);

	self->file_view = gtk_tree_view_new();
	prepare_file_view(self);
	completion_create(self);

	self->popup_items.open = self->popup_items.open_external = self->popup_items.find_in_files = NULL;

	scrollwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(
		GTK_SCROLLED_WINDOW(scrollwin),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scrollwin), self->file_view);
	gtk_box_pack_start(GTK_BOX(self->file_view_vbox), scrollwin, TRUE, TRUE, 0);

	/* load settings before file_view "realize" callback */
	load_settings(self);

	gtk_widget_show_all(self->file_view_vbox);
	self->page_number = gtk_notebook_append_page(GTK_NOTEBOOK(plugin->geany_data->main_widgets->sidebar_notebook),
		self->file_view_vbox, gtk_label_new(_("Files")));

	/* setup keybindings */
	group = plugin_set_key_group(plugin, "file_browser", KB_COUNT, NULL);
	keybindings_set_item(group, KB_FOCUS_FILE_LIST, kb_activate,
		0, 0, "focus_file_list", _("Focus File List"), NULL);
	keybindings_set_item(group, KB_FOCUS_PATH_ENTRY, kb_activate,
		0, 0, "focus_path_entry", _("Focus Path Entry"), NULL);

	plugin_signal_connect(plugin, NULL, "document-activate", TRUE,
		(GCallback) &document_activate_cb, self);
	plugin_signal_connect(plugin, NULL, "document-save", TRUE,
		(GCallback) &document_save_cb, self);
	plugin_signal_connect(plugin, NULL, "project-open", TRUE,
		(GCallback) &project_change_cb, self);
	plugin_signal_connect(plugin, NULL, "project-save", TRUE,
		(GCallback) &project_change_cb, self);

	return TRUE;
}


static void save_settings(Filebrowser *self)
{
	GKeyFile *config = g_key_file_new();
	gchar *data;
	gchar *config_dir = g_path_get_dirname(self->config_file);

	g_key_file_load_from_file(config, self->config_file, G_KEY_FILE_NONE, NULL);

	g_key_file_set_string(config, "filebrowser", "open_command", self->open_cmd);
	g_key_file_set_boolean(config, "filebrowser", "show_hidden_files", self->show_hidden_files);
	g_key_file_set_boolean(config, "filebrowser", "hide_object_files", self->hide_object_files);
	g_key_file_set_string(config, "filebrowser", "hidden_file_extensions", self->hidden_file_extensions);
	g_key_file_set_boolean(config, "filebrowser", "fb_follow_path", self->fb_follow_path);
	g_key_file_set_boolean(config, "filebrowser", "fb_set_project_base_path",
		self->fb_set_project_base_path);

	if (! g_file_test(config_dir, G_FILE_TEST_IS_DIR) && utils_mkdir(config_dir, TRUE) != 0)
	{
		dialogs_show_msgbox(GTK_MESSAGE_ERROR,
			_("Plugin configuration directory could not be created."));
	}
	else
	{
		/* write config to file */
		data = g_key_file_to_data(config, NULL, NULL);
		utils_write_file(self->config_file, data);
		g_free(data);
	}
	g_free(config_dir);
	g_key_file_free(config);
}


typedef struct
{
	Filebrowser *self;
	GtkWidget *open_cmd_entry;
	GtkWidget *show_hidden_checkbox;
	GtkWidget *hide_objects_checkbox;
	GtkWidget *hidden_files_entry;
	GtkWidget *follow_path_checkbox;
	GtkWidget *set_project_base_path_checkbox;
}
FilebrowserPrefData;

static void
on_configure_response(GtkDialog *dialog, gint response, FilebrowserPrefData *pref_data)
{
	Filebrowser *self = pref_data->self;
	if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_APPLY)
	{
		g_free(self->open_cmd);
		self->open_cmd = g_strdup(gtk_entry_get_text(GTK_ENTRY(pref_data->open_cmd_entry)));
		self->show_hidden_files = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pref_data->show_hidden_checkbox));
		self->hide_object_files = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pref_data->hide_objects_checkbox));
		g_free(self->hidden_file_extensions);
		self->hidden_file_extensions = g_strdup(gtk_entry_get_text(GTK_ENTRY(pref_data->hidden_files_entry)));
		self->fb_follow_path = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pref_data->follow_path_checkbox));
		self->fb_set_project_base_path = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
			pref_data->set_project_base_path_checkbox));

		/* apply the changes */
		refresh(self);
	}
}


static void on_toggle_hidden(FilebrowserPrefData *pref_data)
{
	gboolean enabled = !gtk_toggle_button_get_active(
		GTK_TOGGLE_BUTTON(pref_data->show_hidden_checkbox));

	gtk_widget_set_sensitive(pref_data->hide_objects_checkbox, enabled);
	enabled &= gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pref_data->hide_objects_checkbox));
	gtk_widget_set_sensitive(pref_data->hidden_files_entry, enabled);
}


static GtkWidget *filebrowser_configure(GeanyPlugin *plugin, GtkDialog *dialog, gpointer user_data)
{
	GtkWidget *label, *entry, *checkbox_of, *checkbox_hf, *checkbox_fp, *checkbox_pb, *vbox;
	GtkWidget *box, *align;
	Filebrowser *self = user_data;
	FilebrowserPrefData *pref_data = g_malloc(sizeof *pref_data);

	pref_data->self = self;

	vbox = gtk_vbox_new(FALSE, 6);
	box = gtk_vbox_new(FALSE, 3);

	label = gtk_label_new(_("External open command:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

	entry = gtk_entry_new();
	if (self->open_cmd != NULL)
		gtk_entry_set_text(GTK_ENTRY(entry), self->open_cmd);
	gtk_widget_set_tooltip_text(entry,
		_("The command to execute when using \"Open with\". You can use %f and %d wildcards.\n"
		  "%f will be replaced with the filename including full path\n"
		  "%d will be replaced with the path name of the selected file without the filename"));
	gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
	pref_data->open_cmd_entry = entry;

	gtk_box_pack_start(GTK_BOX(vbox), box, FALSE, FALSE, 3);

	checkbox_hf = gtk_check_button_new_with_label(_("Show hidden files"));
	gtk_button_set_focus_on_click(GTK_BUTTON(checkbox_hf), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox_hf), self->show_hidden_files);
	gtk_box_pack_start(GTK_BOX(vbox), checkbox_hf, FALSE, FALSE, 0);
	pref_data->show_hidden_checkbox = checkbox_hf;
	g_signal_connect_swapped(checkbox_hf, "toggled", G_CALLBACK(on_toggle_hidden), pref_data);

	box = gtk_vbox_new(FALSE, 3);
	checkbox_of = gtk_check_button_new_with_label(_("Hide file extensions:"));
	gtk_button_set_focus_on_click(GTK_BUTTON(checkbox_of), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox_of), self->hide_object_files);
	gtk_box_pack_start(GTK_BOX(box), checkbox_of, FALSE, FALSE, 0);
	pref_data->hide_objects_checkbox = checkbox_of;
	g_signal_connect_swapped(checkbox_of, "toggled", G_CALLBACK(on_toggle_hidden), pref_data);

	entry = gtk_entry_new();
	if (self->hidden_file_extensions != NULL)
		gtk_entry_set_text(GTK_ENTRY(entry), self->hidden_file_extensions);
	gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
	pref_data->hidden_files_entry = entry;

	align = gtk_alignment_new(1, 0.5, 1, 1);
	gtk_alignment_set_padding(GTK_ALIGNMENT(align), 0, 0, 12, 0);
	gtk_container_add(GTK_CONTAINER(align), box);
	gtk_box_pack_start(GTK_BOX(vbox), align, FALSE, FALSE, 0);
	on_toggle_hidden(pref_data);

	checkbox_fp = gtk_check_button_new_with_label(_("Follow the path of the current file"));
	gtk_button_set_focus_on_click(GTK_BUTTON(checkbox_fp), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox_fp), self->fb_follow_path);
	gtk_box_pack_start(GTK_BOX(vbox), checkbox_fp, FALSE, FALSE, 0);
	pref_data->follow_path_checkbox = checkbox_fp;

	checkbox_pb = gtk_check_button_new_with_label(_("Use the project's base directory"));
	gtk_button_set_focus_on_click(GTK_BUTTON(checkbox_pb), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox_pb), self->fb_set_project_base_path);
	gtk_widget_set_tooltip_text(checkbox_pb,
		_("Change the directory to the base directory of the currently opened project"));
	gtk_box_pack_start(GTK_BOX(vbox), checkbox_pb, FALSE, FALSE, 0);
	pref_data->set_project_base_path_checkbox = checkbox_pb;

	gtk_widget_show_all(vbox);

	g_signal_connect_data(dialog, "response", G_CALLBACK(on_configure_response),
			pref_data, (GClosureNotify) g_free, 0);
	return vbox;
}


static void filebrowser_cleanup(GeanyPlugin *plugin, gpointer user_data)
{
	Filebrowser *self = user_data;

	save_settings(self);

	g_free(self->config_file);
	g_free(self->open_cmd);
	g_free(self->hidden_file_extensions);
	clear_filter(self);
	gtk_widget_destroy(self->file_view_vbox);
	g_object_unref(G_OBJECT(self->entry_completion));
}


void geany_load_module(GeanyPlugin *plugin, GModule *module, gint geany_api_version)
{
	plugin->info->name = _("File Browser");
	plugin->info->description = _("Adds a file browser tab to the sidebar.");
	plugin->info->version = VERSION;
	plugin->info->author = _("The Geany developer team");

	plugin->hooks->init = filebrowser_init;
	plugin->hooks->cleanup = filebrowser_cleanup;
	plugin->hooks->configure = filebrowser_configure;

	GEANY_PLUGIN_REGISTER(plugin, GEANY_API_VERSION);
}
