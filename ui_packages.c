#include "archwarden.h"

typedef struct {
    GtkTextBuffer *buffer;
    char *cmd;
} InfoTask;

int match_category(gboolean is_gui, const char *desc) {
    if (!desc) return is_gui ? 1 : 2;
    gchar *d = g_ascii_strdown(desc, -1);
    int cat = is_gui ? 1 : 2;
    
    if (strstr(d, "game") || strstr(d, "play")) {
        cat = 3; 
    } else if (strstr(d, "video") || strstr(d, "photo") || strstr(d, "image") || strstr(d, "music") || strstr(d, "audio")) {
        cat = 4;
    } else if (strstr(d, "dev") || strstr(d, "compiler") || strstr(d, "code") || strstr(d, "git")) {
        cat = 5; 
    } else if (strstr(d, "net") || strstr(d, "web") || strstr(d, "browser") || strstr(d, "mail")) {
        cat = 6;
    }
    g_free(d); 
    return cat;
}

gint compare_local(gconstpointer a, gconstpointer b) {
    const PackageInfo *pa = a; 
    const PackageInfo *pb = b;
    if (pa->status != pb->status) return pa->status - pb->status; 
    if (pa->is_gui != pb->is_gui) return pb->is_gui - pa->is_gui; 
    return g_strcmp0(pa->name, pb->name);
}

gint compare_store(gconstpointer a, gconstpointer b) {
    const StorePackageInfo *pa = a; 
    const StorePackageInfo *pb = b;
    if (pa->is_gui != pb->is_gui) return pb->is_gui - pa->is_gui; 
    if (pa->popularity != pb->popularity) return pb->popularity - pa->popularity; 
    return g_strcmp0(pa->name, pb->name);
}

void update_batch_action_bars(void) {
    int count_local = 0;
    if (listbox_local) {
        GList *children_local = gtk_container_get_children(GTK_CONTAINER(listbox_local));
        for (GList *l = children_local; l != NULL; l = l->next) {
            if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(l->data), "is_selected"))) {
                count_local++;
            }
        }
        g_list_free(children_local);
    }

    int count_store = 0;
    if (listbox_store) {
        GList *children_store = gtk_container_get_children(GTK_CONTAINER(listbox_store));
        for (GList *l = children_store; l != NULL; l = l->next) {
            if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(l->data), "is_selected"))) {
                count_store++;
            }
        }
        g_list_free(children_store);
    }

    char markup[128];
    if (count_local > 0 && action_bar_local && lbl_selected_local) {
        g_snprintf(markup, sizeof(markup), "<b>%d</b> Selected", count_local);
        gtk_label_set_markup(GTK_LABEL(lbl_selected_local), markup);
        gtk_widget_show(action_bar_local);
    } else if (action_bar_local) {
        gtk_widget_hide(action_bar_local);
    }

    if (count_store > 0 && action_bar_store && lbl_selected_store) {
        g_snprintf(markup, sizeof(markup), "<b>%d</b> Selected", count_store);
        gtk_label_set_markup(GTK_LABEL(lbl_selected_store), markup);
        gtk_widget_show(action_bar_store);
    } else if (action_bar_store) {
        gtk_widget_hide(action_bar_store);
    }
}

void toggle_row_selection(GtkWidget *row) {
    gboolean is_sel = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "is_selected"));
    is_sel = !is_sel;
    g_object_set_data(G_OBJECT(row), "is_selected", GINT_TO_POINTER(is_sel));

    GtkWidget *box = gtk_bin_get_child(GTK_BIN(row));
    if (is_sel) {
        gtk_style_context_add_class(gtk_widget_get_style_context(box), "row-selected");
    } else {
        gtk_style_context_remove_class(gtk_widget_get_style_context(box), "row-selected");
    }
    update_batch_action_bars();
}

static void on_row_long_pressed(GtkGestureLongPress *gesture, gdouble x, gdouble y, gpointer row) {
    toggle_row_selection(GTK_WIDGET(row));
}

static gboolean on_row_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) { 
        toggle_row_selection(widget);
        return TRUE;
    }
    return FALSE;
}

void select_batch_items(GtkWidget *btn, gpointer data) {
    int mode = GPOINTER_TO_INT(data);
    GtkListBox *listbox = (mode >= 10) ? GTK_LIST_BOX(listbox_store) : GTK_LIST_BOX(listbox_local);
    
    GList *children = gtk_container_get_children(GTK_CONTAINER(listbox));
    for (GList *l = children; l != NULL; l = l->next) {
        GtkWidget *row = GTK_WIDGET(l->data);
        PackageInfo *pkg = g_object_get_data(G_OBJECT(row), "pkg_data");
        if (!pkg) continue;

        gboolean should_select = FALSE;
        if (mode == 1 || mode == 11) {
            should_select = TRUE; 
        } else if (mode == 2 && pkg->status == PKG_UPDATE) {
            should_select = TRUE; 
        } else if (mode == 3 && pkg->status == PKG_ORPHAN) {
            should_select = TRUE; 
        }

        if (mode == 0 || mode == 10) {
            should_select = FALSE; 
        }

        g_object_set_data(G_OBJECT(row), "is_selected", GINT_TO_POINTER(should_select));
        GtkWidget *box = gtk_bin_get_child(GTK_BIN(row));
        if (should_select) {
            gtk_style_context_add_class(gtk_widget_get_style_context(box), "row-selected");
        } else {
            gtk_style_context_remove_class(gtk_widget_get_style_context(box), "row-selected");
        }
    }
    g_list_free(children);
    update_batch_action_bars();
}

void execute_batch_action(GtkWidget *btn, gpointer data) {
    int mode = GPOINTER_TO_INT(data);
    GtkListBox *listbox = (mode == 0) ? GTK_LIST_BOX(listbox_store) : GTK_LIST_BOX(listbox_local);
    
    GString *cmd_pacman = g_string_new("");
    GString *cmd_flatpak = g_string_new("");
    int count = 0;

    GList *children = gtk_container_get_children(GTK_CONTAINER(listbox));
    for (GList *l = children; l != NULL; l = l->next) {
        GtkListBoxRow *row = GTK_LIST_BOX_ROW(l->data);
        if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "is_selected"))) {
            PackageInfo *pkg = g_object_get_data(G_OBJECT(row), "pkg_data");
            if (!pkg) continue;
            
            if (pkg->source == SRC_FLATPAK) {
                g_string_append_printf(cmd_flatpak, "%s ", pkg->name);
            } else {
                g_string_append_printf(cmd_pacman, "%s ", pkg->name);
            }
            
            g_object_set_data(G_OBJECT(row), "is_selected", GINT_TO_POINTER(FALSE));
            gtk_style_context_remove_class(gtk_widget_get_style_context(gtk_bin_get_child(GTK_BIN(row))), "row-selected");
            count++;
        }
    }
    g_list_free(children);
    update_batch_action_bars();

    if (count == 0) {
        g_string_free(cmd_pacman, TRUE);
        g_string_free(cmd_flatpak, TRUE);
        return;
    }

    GString *final_cmd = g_string_new("");
    if (mode == 0 || mode == 1 || mode == 3) {
        if (cmd_pacman->len > 0) {
            g_string_append_printf(final_cmd, "yay -Sy --noconfirm %s ; ", cmd_pacman->str);
        }
        if (cmd_flatpak->len > 0) {
            g_string_append_printf(final_cmd, "flatpak install -y flathub %s ; ", cmd_flatpak->str);
        }
    } else {
        if (cmd_pacman->len > 0) {
            g_string_append_printf(final_cmd, "pacman -Rns --noconfirm %s ; ", cmd_pacman->str);
        }
        if (cmd_flatpak->len > 0) {
            g_string_append_printf(final_cmd, "flatpak uninstall -y %s ; ", cmd_flatpak->str);
        }
    }

    char title[256];
    snprintf(title, sizeof(title), "Processing %d Packages", count);
    run_task_cmd(title, final_cmd->str);

    g_string_free(cmd_pacman, TRUE);
    g_string_free(cmd_flatpak, TRUE);
    g_string_free(final_cmd, TRUE);
}

void popover_closed_cb(GtkWidget *popover, gpointer data) {
    gtk_widget_destroy(popover);
}

gboolean info_task_done(gpointer data) {
    char **res = (char**)data;
    if (res[1] != NULL) {
        GtkTextBuffer *buf = GTK_TEXT_BUFFER(res[1]);
        gtk_text_buffer_set_text(buf, res[0], -1);
    }
    g_free(res[0]);
    g_free(res);
    return G_SOURCE_REMOVE;
}

gpointer fetch_info_async(gpointer data) {
    InfoTask *task = (InfoTask*)data;
    char *info = exec_cmd_sync(task->cmd, NULL);
    char **res = g_new(char*, 2);
    res[0] = info;
    res[1] = (char*)task->buffer;
    g_idle_add(info_task_done, res);
    g_free(task->cmd);
    g_free(task);
    return NULL;
}

void show_enhanced_info_popover(GtkWidget *parent_row, const char *name, PkgSource src, const char *version, const char *icon_name, gboolean is_unstable, const char *warning) {
    GtkWidget *popover = gtk_popover_new(parent_row);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    g_signal_connect(popover, "closed", G_CALLBACK(popover_closed_cb), NULL);

    GtkWidget *vbox_main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox_main), 15);
    gtk_widget_set_size_request(vbox_main, 500, 350);

    GtkWidget *hbox_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    char icon[256];
    get_icon_for_pkg(name, icon, src);
    GtkWidget *img_icon = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size(GTK_IMAGE(img_icon), 64);
    gtk_box_pack_start(GTK_BOX(hbox_top), img_icon, FALSE, FALSE, 0);

    GtkWidget *vbox_title = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    char title_markup[512];
    g_snprintf(title_markup, sizeof(title_markup), "<span size='x-large' weight='bold'>%s</span>", name);
    GtkWidget *lbl_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_title), title_markup);
    gtk_widget_set_halign(lbl_title, GTK_ALIGN_START);
    
    GtkWidget *lbl_ver = gtk_label_new(version);
    gtk_widget_set_halign(lbl_ver, GTK_ALIGN_START);
    
    gtk_box_pack_start(GTK_BOX(vbox_title), lbl_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_title), lbl_ver, FALSE, FALSE, 0);

    if (is_unstable) {
        char warn_markup[256];
        g_snprintf(warn_markup, sizeof(warn_markup), "<span foreground='#FF9800'>⚠️ %s</span>", warning);
        GtkWidget *lbl_warn = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lbl_warn), warn_markup);
        gtk_widget_set_halign(lbl_warn, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(vbox_title), lbl_warn, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(hbox_top), vbox_title, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_main), hbox_top, FALSE, FALSE, 0);

    GtkWidget *lbl_det = gtk_label_new("<b>Technical Details</b>");
    gtk_label_set_use_markup(GTK_LABEL(lbl_det), TRUE);
    gtk_widget_set_halign(lbl_det, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox_main), lbl_det, FALSE, FALSE, 0);

    GtkWidget *tv_scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD);
    
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    gtk_text_buffer_set_text(buf, "Fetching information...", -1);
    
    gtk_container_add(GTK_CONTAINER(tv_scroll), tv);
    gtk_box_pack_start(GTK_BOX(vbox_main), tv_scroll, TRUE, TRUE, 0);

    InfoTask *task = g_new(InfoTask, 1);
    task->buffer = buf;
    g_object_add_weak_pointer(G_OBJECT(buf), (gpointer *)&task->buffer);

    if (src == SRC_FLATPAK) {
        task->cmd = g_strdup_printf("timeout 10 flatpak info %s 2>/dev/null || timeout 10 flatpak remote-info flathub %s 2>/dev/null", name, name);
    } else {
        task->cmd = g_strdup_printf("timeout 10 pacman -Qi %s 2>/dev/null || timeout 10 pacman -Si %s 2>/dev/null || timeout 10 yay -Si %s 2>/dev/null", name, name, name);
    }
    g_thread_new("info_fetch", fetch_info_async, task);

    gtk_container_add(GTK_CONTAINER(popover), vbox_main);
    gtk_widget_show_all(popover);
    gtk_popover_popup(GTK_POPOVER(popover));
}

void on_local_info_btn_clicked(GtkWidget *btn, gpointer data) {
    GtkListBoxRow *row = GTK_LIST_BOX_ROW(data);
    PackageInfo *pkg = g_object_get_data(G_OBJECT(row), "pkg_data");
    if (pkg) {
        show_enhanced_info_popover(GTK_WIDGET(row), pkg->name, pkg->source, pkg->version, pkg->icon_name, pkg->is_unstable, pkg->warning_msg);
    }
}

void on_store_info_btn_clicked(GtkWidget *btn, gpointer data) {
    GtkListBoxRow *row = GTK_LIST_BOX_ROW(data);
    StorePackageInfo *pkg = g_object_get_data(G_OBJECT(row), "pkg_data");
    if (pkg) {
        show_enhanced_info_popover(GTK_WIDGET(row), pkg->name, pkg->source, pkg->version, pkg->icon_name, pkg->is_unstable, pkg->warning_msg);
    }
}

void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    PackageInfo *pkg = g_object_get_data(G_OBJECT(row), "pkg_data");
    if (pkg) {
        show_enhanced_info_popover(GTK_WIDGET(row), pkg->name, pkg->source, pkg->version, pkg->icon_name, pkg->is_unstable, pkg->warning_msg);
    }
}

void setup_row_events(GtkWidget *row) {
    gtk_widget_add_events(row, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(row, "button-press-event", G_CALLBACK(on_row_button_press), NULL);

    GtkGesture *long_press = gtk_gesture_long_press_new(row);
    gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(long_press), FALSE);
    g_signal_connect(long_press, "pressed", G_CALLBACK(on_row_long_pressed), row);
    g_object_set_data_full(G_OBJECT(row), "long-press-gesture", long_press, g_object_unref);
}

void render_local_chunk() {
    int end_idx = local_render_idx + CHUNK_SIZE;
    GList *iter = g_list_nth(filtered_local, local_render_idx);

    while (iter != NULL && local_render_idx < end_idx) {
        PackageInfo *pkg = (PackageInfo *)iter->data;
        GtkWidget *row = gtk_list_box_row_new();
        g_object_set_data(G_OBJECT(row), "pkg_data", pkg);
        g_object_set_data(G_OBJECT(row), "is_selected", GINT_TO_POINTER(FALSE));

        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_style_context_add_class(gtk_widget_get_style_context(box), "pkg-row");

        if (pkg->status == PKG_UPDATE) {
            gtk_style_context_add_class(gtk_widget_get_style_context(box), "pkg-update");
        } else if (pkg->status == PKG_ORPHAN) {
            gtk_style_context_add_class(gtk_widget_get_style_context(box), "pkg-orphan");
        } else if (pkg->status == PKG_ORPHAN_USEFUL) {
            gtk_style_context_add_class(gtk_widget_get_style_context(box), "pkg-orphan-useful");
        }

        char icon_name[256];
        get_icon_for_pkg(pkg->name, icon_name, pkg->source);
        GtkWidget *img = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_DIALOG);
        gtk_image_set_pixel_size(GTK_IMAGE(img), 48);
        gtk_box_pack_start(GTK_BOX(box), img, FALSE, FALSE, 5);

        GtkWidget *vbox_info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        char markup[1024];
        const char *src = (pkg->source == SRC_FLATPAK) ? "[Flatpak]" : (pkg->source == SRC_AUR) ? "[AUR]" : "[Pacman]";

        if (pkg->status == PKG_UPDATE) {
            g_snprintf(markup, sizeof(markup), "<b>%s</b> <span foreground='gray'>%s</span>%s\n<span size='small' foreground='#4CAF50'><b>%s ➔ %s</b></span>", pkg->name, src, pkg->is_unstable ? " <span foreground='#FF9800'>⚠️</span>" : "", pkg->version, pkg->new_version);
        } else {
            g_snprintf(markup, sizeof(markup), "<b>%s</b> <span foreground='gray'>%s</span>%s\n<span size='small'>%s</span>", pkg->name, src, pkg->is_unstable ? " <span foreground='#FF9800'>⚠️</span>" : "", pkg->version);
        }

        GtkWidget *lbl = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lbl), markup);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(vbox_info), lbl, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), vbox_info, TRUE, TRUE, 5);

        char size_markup[128];
        g_snprintf(size_markup, sizeof(size_markup), "<span foreground='white'>[%s]</span>", pkg->size_str);
        GtkWidget *lbl_size = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lbl_size), size_markup);
        gtk_widget_set_halign(lbl_size, GTK_ALIGN_END);
        
        GtkWidget *hbox_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_box_pack_start(GTK_BOX(hbox_actions), lbl_size, FALSE, FALSE, 10);

        GtkWidget *btn_info = gtk_button_new_from_icon_name("dialog-information-symbolic", GTK_ICON_SIZE_BUTTON);
        gtk_widget_set_tooltip_text(btn_info, "Details");
        g_signal_connect(btn_info, "clicked", G_CALLBACK(on_local_info_btn_clicked), row);
        gtk_box_pack_start(GTK_BOX(hbox_actions), btn_info, FALSE, FALSE, 5);

        if (pkg->status == PKG_UPDATE) {
            GtkWidget *btn_upd = gtk_button_new_with_label("Update");
            char title[256];
            g_snprintf(title, sizeof(title), "Updating %s", pkg->name);
            GString *cmd = g_string_new("");
            if (pkg->source == SRC_FLATPAK) {
                g_string_printf(cmd, "flatpak update -y %s", pkg->name);
            } else {
                g_string_printf(cmd, "pacman -S --noconfirm %s || yay -S --noconfirm %s", pkg->name, pkg->name);
            }
            char **args = g_new(char*, 3);
            args[0] = g_strdup(title);
            args[1] = g_string_free(cmd, FALSE);
            args[2] = NULL;
            g_signal_connect_data(btn_upd, "clicked", G_CALLBACK(trigger_task_from_args), args, (GClosureNotify)g_strfreev, 0);
            gtk_box_pack_start(GTK_BOX(hbox_actions), btn_upd, FALSE, FALSE, 0);
        }

        GtkWidget *btn_del = gtk_button_new_with_label("Remove");
        char title_del[256];
        g_snprintf(title_del, sizeof(title_del), "Removing %s", pkg->name);
        GString *cmd_del = g_string_new("");
        if (pkg->source == SRC_FLATPAK) {
            g_string_printf(cmd_del, "flatpak uninstall -y %s", pkg->name);
        } else {
            g_string_printf(cmd_del, "pacman -Rns --noconfirm %s", pkg->name);
        }
        char **args_del = g_new(char*, 3);
        args_del[0] = g_strdup(title_del);
        args_del[1] = g_string_free(cmd_del, FALSE);
        args_del[2] = NULL;
        g_signal_connect_data(btn_del, "clicked", G_CALLBACK(trigger_task_from_args), args_del, (GClosureNotify)g_strfreev, 0);
        gtk_box_pack_start(GTK_BOX(hbox_actions), btn_del, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(box), hbox_actions, FALSE, FALSE, 5);
        gtk_container_add(GTK_CONTAINER(row), box);
        
        setup_row_events(row);
        gtk_widget_show_all(row);
        gtk_list_box_insert(GTK_LIST_BOX(listbox_local), row, -1);

        local_render_idx++;
        iter = iter->next;
    }
}

void render_store_chunk() {
    int end_idx = store_render_idx + CHUNK_SIZE;
    GList *iter = g_list_nth(filtered_store, store_render_idx);

    while (iter != NULL && store_render_idx < end_idx) {
        StorePackageInfo *pkg = (StorePackageInfo *)iter->data;
        GtkWidget *row = gtk_list_box_row_new();
        g_object_set_data(G_OBJECT(row), "pkg_data", pkg);
        g_object_set_data(G_OBJECT(row), "is_selected", GINT_TO_POINTER(FALSE));

        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_style_context_add_class(gtk_widget_get_style_context(box), "pkg-row");

        char icon_name[256];
        get_icon_for_pkg(pkg->name, icon_name, pkg->source);
        GtkWidget *img = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_DIALOG);
        gtk_image_set_pixel_size(GTK_IMAGE(img), 48);
        gtk_box_pack_start(GTK_BOX(box), img, FALSE, FALSE, 5);

        GtkWidget *vbox_info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        char markup[512];
        g_snprintf(markup, sizeof(markup), "<b>%s</b> <span foreground='gray'>[%s]</span>%s\n<span size='small'>%s</span>", pkg->name, pkg->repo, pkg->is_unstable ? " <span foreground='#FF9800'>⚠️</span>" : "", pkg->version);
        
        GtkWidget *lbl = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lbl), markup);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        
        gtk_box_pack_start(GTK_BOX(vbox_info), lbl, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), vbox_info, TRUE, TRUE, 5);

        char size_markup[128];
        g_snprintf(size_markup, sizeof(size_markup), "<span foreground='white'>[%s]</span>", pkg->size_str);
        GtkWidget *lbl_size = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lbl_size), size_markup);
        gtk_widget_set_halign(lbl_size, GTK_ALIGN_END);
        
        GtkWidget *hbox_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_box_pack_start(GTK_BOX(hbox_actions), lbl_size, FALSE, FALSE, 10);

        GtkWidget *btn_info = gtk_button_new_from_icon_name("dialog-information-symbolic", GTK_ICON_SIZE_BUTTON);
        gtk_widget_set_tooltip_text(btn_info, "Details");
        g_signal_connect(btn_info, "clicked", G_CALLBACK(on_store_info_btn_clicked), row);
        gtk_box_pack_start(GTK_BOX(hbox_actions), btn_info, FALSE, FALSE, 5);

        GtkWidget *btn_inst = gtk_button_new_with_label("Install");
        char title[256];
        g_snprintf(title, sizeof(title), "Installing %s", pkg->name);
        GString *cmd = g_string_new("");
        
        if (pkg->source == SRC_FLATPAK) {
            g_string_printf(cmd, "flatpak install -y flathub %s", pkg->name);
        } else {
            g_string_printf(cmd, "pacman -S --noconfirm %s || yay -S --noconfirm %s", pkg->name, pkg->name);
        }
        
        char **args = g_new(char*, 3);
        args[0] = g_strdup(title);
        args[1] = g_string_free(cmd, FALSE);
        args[2] = NULL;
        
        g_signal_connect_data(btn_inst, "clicked", G_CALLBACK(trigger_task_from_args), args, (GClosureNotify)g_strfreev, 0);
        gtk_box_pack_start(GTK_BOX(hbox_actions), btn_inst, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(box), hbox_actions, FALSE, FALSE, 5);
        gtk_container_add(GTK_CONTAINER(row), box);
        
        setup_row_events(row);
        gtk_widget_show_all(row);
        gtk_list_box_insert(GTK_LIST_BOX(listbox_store), row, -1);

        store_render_idx++;
        iter = iter->next;
    }
}

void apply_local_filters(GtkEntry *entry, gpointer data) {
    if (listbox_local) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(listbox_local));
        for(GList *it = children; it != NULL; it = it->next) {
            gtk_widget_destroy(GTK_WIDGET(it->data));
        }
        g_list_free(children);
    }
    
    if (filtered_local) {
        g_list_free(filtered_local);
        filtered_local = NULL;
    }

    const char *term = gtk_entry_get_text(GTK_ENTRY(search_entry_local));
    
    for (GList *it = local_cache; it != NULL; it = it->next) {
        PackageInfo *pkg = (PackageInfo *)it->data;
        gboolean match_src = (pkg->source == SRC_PACMAN && f_loc_pacman) || 
                             (pkg->source == SRC_AUR && f_loc_aur) || 
                             (pkg->source == SRC_FLATPAK && f_loc_flatpak);
                             
        gboolean match_stat = TRUE;
        if (f_loc_updates || f_loc_orphans) {
            match_stat = FALSE;
            if (f_loc_updates && pkg->status == PKG_UPDATE) match_stat = TRUE;
            if (f_loc_orphans && (pkg->status == PKG_ORPHAN || pkg->status == PKG_ORPHAN_USEFUL)) match_stat = TRUE;
        }

        int p_cat = match_category(pkg->is_gui, pkg->desc);
        gboolean match_cat = (f_loc_cat == 0) || (p_cat == f_loc_cat);
        
        if (match_src && match_stat && match_cat && (strlen(term) == 0 || strcasestr(pkg->name, term))) {
            filtered_local = g_list_append(filtered_local, pkg);
        }
    }
    
    filtered_local = g_list_sort(filtered_local, compare_local);
    local_render_idx = 0;
    render_local_chunk();
    update_batch_action_bars();
}

void apply_store_filters(GtkEntry *entry, gpointer data) {
    if (listbox_store) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(listbox_store));
        for(GList *it = children; it != NULL; it = it->next) {
            gtk_widget_destroy(GTK_WIDGET(it->data));
        }
        g_list_free(children);
    }

    if (filtered_store) {
        g_list_free(filtered_store);
        filtered_store = NULL;
    }
    
    const char *term = gtk_entry_get_text(GTK_ENTRY(search_entry_store));
    for (GList *it = store_cache; it != NULL; it = it->next) {
        StorePackageInfo *pkg = (StorePackageInfo *)it->data;
        
        if (installed_hash && g_hash_table_contains(installed_hash, pkg->name)) {
            continue;
        }

        gboolean match_src = (pkg->source == SRC_PACMAN && f_sto_pacman) || 
                             (pkg->source == SRC_FLATPAK && f_sto_flatpak) || 
                             (pkg->source == SRC_AUR && f_sto_aur);
                             
        int p_cat = match_category(pkg->is_gui, pkg->desc);
        gboolean match_cat = (f_sto_cat == 0) || (p_cat == f_sto_cat);
        
        if (match_src && match_cat && (strlen(term) == 0 || strcasestr(pkg->name, term))) {
            filtered_store = g_list_append(filtered_store, pkg);
        }
    }
    
    filtered_store = g_list_sort(filtered_store, compare_store);
    store_render_idx = 0;
    render_store_chunk();
    update_batch_action_bars();
}

gboolean apply_local_cache_ui(gpointer data) {
    GList *new_list = (GList *)data;
    
    if (listbox_local) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(listbox_local));
        for(GList *it = children; it != NULL; it = it->next) {
            gtk_widget_destroy(GTK_WIDGET(it->data));
        }
        g_list_free(children);
    }
    
    if (filtered_local) {
        g_list_free(filtered_local);
        filtered_local = NULL;
    }

    if (local_cache) {
        g_list_free_full(local_cache, g_free);
    }
    
    local_cache = new_list;
    save_local_cache();

    if (installed_hash) {
        g_hash_table_destroy(installed_hash);
    }
    installed_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (GList *it = local_cache; it != NULL; it = it->next) {
        PackageInfo *p = it->data;
        g_hash_table_add(installed_hash, g_strdup(p->name));
    }

    is_processing_local = FALSE;
    if (progress_local) gtk_widget_hide(progress_local);
    apply_local_filters(NULL, NULL);
    check_and_show_errors();
    return G_SOURCE_REMOVE;
}

gboolean apply_store_cache_ui(gpointer data) {
    GList *new_list = (GList *)data;
    
    if (listbox_store) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(listbox_store));
        for(GList *it = children; it != NULL; it = it->next) {
            gtk_widget_destroy(GTK_WIDGET(it->data));
        }
        g_list_free(children);
    }

    if (filtered_store) {
        g_list_free(filtered_store);
        filtered_store = NULL;
    }

    if (store_cache) {
        g_list_free_full(store_cache, g_free);
    }
    
    store_cache = new_list;
    save_store_cache();
    
    is_processing_store = FALSE;
    if (progress_store) gtk_widget_hide(progress_store);
    apply_store_filters(NULL, NULL);
    check_and_show_errors();
    return G_SOURCE_REMOVE;
}

void on_local_scroll(GtkScrolledWindow *sw, GtkPositionType pos, gpointer data) {
    if (pos == GTK_POS_BOTTOM) {
        render_local_chunk();
    }
}

void on_store_scroll(GtkScrolledWindow *sw, GtkPositionType pos, gpointer data) {
    if (pos == GTK_POS_BOTTOM) {
        render_store_chunk();
    }
}

void on_filter_changed(GtkToggleButton *btn, gpointer data) {
    gboolean *flag = (gboolean*)data;
    *flag = gtk_toggle_button_get_active(btn);
    apply_local_filters(NULL, NULL);
    apply_store_filters(NULL, NULL);
}

void on_category_changed(GtkComboBox *combo, gpointer data) {
    int mode = GPOINTER_TO_INT(data);
    int active = gtk_combo_box_get_active(combo);
    if (mode == 0) f_loc_cat = active; else f_sto_cat = active;
    if (mode == 0) apply_local_filters(NULL, NULL); else apply_store_filters(NULL, NULL);
}

GtkWidget* create_filter_popover(GtkWidget *parent, int tab_mode) {
    GtkWidget *pop = gtk_popover_new(parent);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
    
    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("<b>Sources</b>"), FALSE, FALSE, 1);
    
    GtkWidget *c_pac = gtk_check_button_new_with_label("Pacman");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c_pac), TRUE);
    g_signal_connect(c_pac, "toggled", G_CALLBACK(on_filter_changed), tab_mode == 0 ? &f_loc_pacman : &f_sto_pacman);
    gtk_box_pack_start(GTK_BOX(vbox), c_pac, FALSE, FALSE, 0);

    GtkWidget *c_aur = gtk_check_button_new_with_label("AUR");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c_aur), TRUE);
    g_signal_connect(c_aur, "toggled", G_CALLBACK(on_filter_changed), tab_mode == 0 ? &f_loc_aur : &f_sto_aur);
    gtk_box_pack_start(GTK_BOX(vbox), c_aur, FALSE, FALSE, 0);

    GtkWidget *c_flp = gtk_check_button_new_with_label("Flatpak");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c_flp), TRUE);
    g_signal_connect(c_flp, "toggled", G_CALLBACK(on_filter_changed), tab_mode == 0 ? &f_loc_flatpak : &f_sto_flatpak);
    gtk_box_pack_start(GTK_BOX(vbox), c_flp, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("<b>Categories</b>"), FALSE, FALSE, 1);
    
    GtkWidget *combo_cat = gtk_combo_box_text_new();
    for (int i=0; i < 7; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_cat), categories[i]);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_cat), 0);
    g_signal_connect(combo_cat, "changed", G_CALLBACK(on_category_changed), GINT_TO_POINTER(tab_mode));
    gtk_box_pack_start(GTK_BOX(vbox), combo_cat, FALSE, FALSE, 0);

    if (tab_mode == 0) {
        gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 3);
        gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("<b>Status</b>"), FALSE, FALSE, 1);
        
        GtkWidget *c_upd = gtk_check_button_new_with_label("Has Updates");
        g_signal_connect(c_upd, "toggled", G_CALLBACK(on_filter_changed), &f_loc_updates);
        gtk_box_pack_start(GTK_BOX(vbox), c_upd, FALSE, FALSE, 0);
        
        GtkWidget *c_orp = gtk_check_button_new_with_label("Show Orphans");
        g_signal_connect(c_orp, "toggled", G_CALLBACK(on_filter_changed), &f_loc_orphans);
        gtk_box_pack_start(GTK_BOX(vbox), c_orp, FALSE, FALSE, 0);
    }

    GList *labels = gtk_container_get_children(GTK_CONTAINER(vbox));
    for(GList *l = labels; l != NULL; l = l->next) {
        if(GTK_IS_LABEL(l->data)) {
            gtk_label_set_use_markup(GTK_LABEL(l->data), TRUE);
        }
    }
    g_list_free(labels);
    
    gtk_container_add(GTK_CONTAINER(pop), vbox);
    gtk_widget_show_all(vbox);
    
    return pop;
}

void on_tab_switched(GtkToggleButton *btn, gpointer data) {
    if (gtk_toggle_button_get_active(btn)) {
        gtk_stack_set_visible_child_name(GTK_STACK(packages_stack), (const char*)data);
    }
}

GtkWidget* build_ui_packages(void) {
    GtkWidget *pkg_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    GtkWidget *top_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(top_bar), 5);
    
    GtkWidget *tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(tab_box), "tab-box");
    gtk_widget_set_halign(tab_box, GTK_ALIGN_CENTER);
    
    GtkWidget *btn_tab_loc = gtk_radio_button_new_with_label(NULL, "Installed");
    GtkWidget *btn_tab_sto = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(btn_tab_loc), "Browse");
    
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_tab_loc), "tab-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_tab_sto), "tab-btn");
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(btn_tab_loc), FALSE);
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(btn_tab_sto), FALSE);
    
    g_signal_connect(btn_tab_loc, "toggled", G_CALLBACK(on_tab_switched), "installed");
    g_signal_connect(btn_tab_sto, "toggled", G_CALLBACK(on_tab_switched), "browse");
    
    gtk_box_pack_start(GTK_BOX(tab_box), btn_tab_loc, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tab_box), btn_tab_sto, FALSE, FALSE, 0);
    
    GtkWidget *btn_sync_all = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(btn_sync_all, "Sync Repositories & Updates");
    g_signal_connect(btn_sync_all, "clicked", G_CALLBACK(trigger_sync), GINT_TO_POINTER(0));
    g_signal_connect(btn_sync_all, "clicked", G_CALLBACK(trigger_sync), GINT_TO_POINTER(1));
    
    GtkWidget *btn_activity = gtk_button_new_from_icon_name("preferences-system-notifications-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(btn_activity, "Active Tasks");
    
    popover_activity = gtk_popover_new(btn_activity);
    gtk_popover_set_position(GTK_POPOVER(popover_activity), GTK_POS_BOTTOM);
    box_tasks = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(box_tasks), 10);
    gtk_container_add(GTK_CONTAINER(popover_activity), box_tasks);
    
    g_signal_connect(btn_activity, "clicked", G_CALLBACK(gtk_popover_popup), popover_activity);
    
    gtk_box_pack_start(GTK_BOX(top_bar), tab_box, TRUE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(top_bar), btn_activity, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(top_bar), btn_sync_all, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(pkg_vbox), top_bar, FALSE, FALSE, 0);

    packages_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(packages_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    
    // Packages: Installed
    GtkWidget *vbox_local = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header_loc = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(header_loc), 5);
    
    search_entry_local = gtk_search_entry_new();
    gtk_widget_set_hexpand(search_entry_local, TRUE);
    g_signal_connect(search_entry_local, "search-changed", G_CALLBACK(apply_local_filters), NULL);
    
    GtkWidget *btn_filt_loc = gtk_button_new_with_label("Filter");
    GtkWidget *pop_loc = create_filter_popover(btn_filt_loc, 0);
    g_signal_connect_swapped(btn_filt_loc, "clicked", G_CALLBACK(gtk_popover_popup), pop_loc);
    
    gtk_box_pack_start(GTK_BOX(header_loc), search_entry_local, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(header_loc), btn_filt_loc, FALSE, FALSE, 0);
    
    progress_local = gtk_progress_bar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(progress_local), "glow-progress");
    gtk_widget_set_no_show_all(progress_local, TRUE);
    
    GtkWidget *scroll_loc = gtk_scrolled_window_new(NULL, NULL);
    g_signal_connect(scroll_loc, "edge-reached", G_CALLBACK(on_local_scroll), NULL);
    
    listbox_local = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox_local), GTK_SELECTION_NONE);
    g_signal_connect(listbox_local, "row-activated", G_CALLBACK(on_row_activated), NULL);
    gtk_container_add(GTK_CONTAINER(scroll_loc), listbox_local);
    
    gtk_box_pack_start(GTK_BOX(vbox_local), header_loc, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_local), progress_local, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_local), scroll_loc, TRUE, TRUE, 0);

    action_bar_local = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(action_bar_local), 10);
    
    GtkWidget *btn_sel_all = gtk_button_new_with_label("All");
    g_signal_connect(btn_sel_all, "clicked", G_CALLBACK(select_batch_items), GINT_TO_POINTER(1));
    gtk_box_pack_start(GTK_BOX(action_bar_local), btn_sel_all, FALSE, FALSE, 0);

    GtkWidget *btn_sel_upd = gtk_button_new_with_label("Updates");
    g_signal_connect(btn_sel_upd, "clicked", G_CALLBACK(select_batch_items), GINT_TO_POINTER(2));
    gtk_box_pack_start(GTK_BOX(action_bar_local), btn_sel_upd, FALSE, FALSE, 0);

    GtkWidget *btn_sel_orp = gtk_button_new_with_label("Orphans");
    g_signal_connect(btn_sel_orp, "clicked", G_CALLBACK(select_batch_items), GINT_TO_POINTER(3));
    gtk_box_pack_start(GTK_BOX(action_bar_local), btn_sel_orp, FALSE, FALSE, 0);

    GtkWidget *btn_sel_clr = gtk_button_new_with_label("Clear");
    g_signal_connect(btn_sel_clr, "clicked", G_CALLBACK(select_batch_items), GINT_TO_POINTER(0));
    gtk_box_pack_start(GTK_BOX(action_bar_local), btn_sel_clr, FALSE, FALSE, 5);

    lbl_selected_local = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(action_bar_local), lbl_selected_local, FALSE, FALSE, 10);
    
    GtkWidget *btn_batch_upd = gtk_button_new_with_label("Update Sel");
    g_signal_connect(btn_batch_upd, "clicked", G_CALLBACK(execute_batch_action), GINT_TO_POINTER(1));
    gtk_box_pack_end(GTK_BOX(action_bar_local), btn_batch_upd, FALSE, FALSE, 0);

    GtkWidget *btn_batch_rem = gtk_button_new_with_label("Remove Sel");
    g_signal_connect(btn_batch_rem, "clicked", G_CALLBACK(execute_batch_action), GINT_TO_POINTER(2));
    gtk_box_pack_end(GTK_BOX(action_bar_local), btn_batch_rem, FALSE, FALSE, 0);
    
    GtkWidget *btn_batch_reb = gtk_button_new_with_label("Reinstall Sel");
    g_signal_connect(btn_batch_reb, "clicked", G_CALLBACK(execute_batch_action), GINT_TO_POINTER(3));
    gtk_box_pack_end(GTK_BOX(action_bar_local), btn_batch_reb, FALSE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(vbox_local), action_bar_local, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(action_bar_local, TRUE);

    gtk_stack_add_named(GTK_STACK(packages_stack), vbox_local, "installed");

    // Packages: Browse
    GtkWidget *vbox_store = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header_sto = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(header_sto), 5);
    
    search_entry_store = gtk_search_entry_new();
    gtk_widget_set_hexpand(search_entry_store, TRUE);
    g_signal_connect(search_entry_store, "search-changed", G_CALLBACK(apply_store_filters), NULL);
    
    GtkWidget *btn_filt_sto = gtk_button_new_with_label("Filter");
    GtkWidget *pop_sto = create_filter_popover(btn_filt_sto, 1);
    g_signal_connect_swapped(btn_filt_sto, "clicked", G_CALLBACK(gtk_popover_popup), pop_sto);
    
    gtk_box_pack_start(GTK_BOX(header_sto), search_entry_store, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(header_sto), btn_filt_sto, FALSE, FALSE, 0);
    
    progress_store = gtk_progress_bar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(progress_store), "glow-progress");
    gtk_widget_set_no_show_all(progress_store, TRUE);
    
    GtkWidget *scroll_sto = gtk_scrolled_window_new(NULL, NULL);
    g_signal_connect(scroll_sto, "edge-reached", G_CALLBACK(on_store_scroll), NULL);
    
    listbox_store = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox_store), GTK_SELECTION_NONE);
    g_signal_connect(listbox_store, "row-activated", G_CALLBACK(on_row_activated), NULL);
    gtk_container_add(GTK_CONTAINER(scroll_sto), listbox_store);
    
    gtk_box_pack_start(GTK_BOX(vbox_store), header_sto, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_store), progress_store, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_store), scroll_sto, TRUE, TRUE, 0);

    action_bar_store = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(action_bar_store), 10);
    
    GtkWidget *btn_sto_all = gtk_button_new_with_label("Select All");
    g_signal_connect(btn_sto_all, "clicked", G_CALLBACK(select_batch_items), GINT_TO_POINTER(11));
    gtk_box_pack_start(GTK_BOX(action_bar_store), btn_sto_all, FALSE, FALSE, 0);

    GtkWidget *btn_sto_clr = gtk_button_new_with_label("Clear");
    g_signal_connect(btn_sto_clr, "clicked", G_CALLBACK(select_batch_items), GINT_TO_POINTER(10));
    gtk_box_pack_start(GTK_BOX(action_bar_store), btn_sto_clr, FALSE, FALSE, 5);
    
    lbl_selected_store = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(action_bar_store), lbl_selected_store, FALSE, FALSE, 10);
    
    GtkWidget *btn_batch_inst = gtk_button_new_with_label("Install Selected");
    g_signal_connect(btn_batch_inst, "clicked", G_CALLBACK(execute_batch_action), GINT_TO_POINTER(0));
    gtk_box_pack_end(GTK_BOX(action_bar_store), btn_batch_inst, FALSE, FALSE, 5);

    gtk_box_pack_end(GTK_BOX(vbox_store), action_bar_store, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(action_bar_store, TRUE);

    gtk_stack_add_named(GTK_STACK(packages_stack), vbox_store, "browse");
    gtk_box_pack_start(GTK_BOX(pkg_vbox), packages_stack, TRUE, TRUE, 0);
    
    return pkg_vbox;
}