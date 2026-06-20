#include "archwarden.h"

// --- UI Globals ---
GtkWidget *listbox_local;
GtkWidget *listbox_store;
GtkWidget *search_entry_local;
GtkWidget *search_entry_store;
GtkWidget *progress_local;
GtkWidget *progress_store;
GtkWidget *btn_global_update;
GtkWidget *btn_global_orphans;
GtkWidget *main_stack;
GtkWidget *settings_revealer;
GtkWidget *sidebar_stack;
GtkWidget *box_tasks;

gboolean f_loc_pacman = TRUE, f_loc_aur = TRUE, f_loc_flatpak = TRUE;
gboolean f_loc_updates = FALSE, f_loc_orphans = FALSE;
int f_loc_cat = 0; 

gboolean f_sto_pacman = TRUE, f_sto_aur = TRUE, f_sto_flatpak = TRUE;
int f_sto_cat = 0;

const char *categories[] = {
    "All Categories", "Desktop Apps / GUI", "CLI & Core Libraries", 
    "Games & Entertainment", "Photo, Video & Audio", 
    "Development & Tools", "Internet & Network"
};

typedef struct {
    GtkTextBuffer *buffer;
    char *cmd;
} InfoTask;

typedef struct {
    GtkWidget *row;
    GtkWidget *progress;
    GtkWidget *lbl_status;
    GtkWidget *btn_dismiss;
} TaskUI;

// --- Forward Declarations ---
void apply_local_filters(GtkEntry *entry, gpointer data);
void apply_store_filters(GtkEntry *entry, gpointer data);
void trigger_sync(GtkWidget *btn, gpointer data);

// --- UI Helpers ---
void check_and_show_errors(void) {
    g_mutex_lock(&error_mutex);
    if (strlen(global_error_log) > 0) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "System Report");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", global_error_log);
        gtk_dialog_run(GTK_DIALOG(dialog)); 
        gtk_widget_destroy(dialog);
        global_error_log[0] = '\0';
    }
    g_mutex_unlock(&error_mutex);
}

void get_icon_for_pkg(const char *name, char *out_icon, PkgSource src) {
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    if (gtk_icon_theme_has_icon(theme, name)) {
        safe_strcpy(out_icon, name, 256);
    } else {
        if (src == SRC_FLATPAK && gtk_icon_theme_has_icon(theme, "application-x-executable")) {
            safe_strcpy(out_icon, "application-x-executable", 256);
        } else if (gtk_icon_theme_has_icon(theme, "package-x-generic")) {
            safe_strcpy(out_icon, "package-x-generic", 256);
        } else {
            safe_strcpy(out_icon, "system-run", 256); 
        }
    }
}

int match_category(gboolean is_gui, const char *desc) {
    if (!desc) return is_gui ? 1 : 2;
    gchar *d = g_ascii_strdown(desc, -1);
    int cat = is_gui ? 1 : 2;
    if (strstr(d, "game") || strstr(d, "play")) cat = 3; 
    else if (strstr(d, "video") || strstr(d, "photo") || strstr(d, "image") || strstr(d, "music") || strstr(d, "audio")) cat = 4;
    else if (strstr(d, "dev") || strstr(d, "compiler") || strstr(d, "code") || strstr(d, "git")) cat = 5;
    else if (strstr(d, "net") || strstr(d, "web") || strstr(d, "browser") || strstr(d, "mail")) cat = 6;
    g_free(d); 
    return cat;
}

gint compare_local(gconstpointer a, gconstpointer b) {
    const PackageInfo *pa = a; const PackageInfo *pb = b;
    if (pa->status != pb->status) return pa->status - pb->status; 
    if (pa->is_gui != pb->is_gui) return pb->is_gui - pa->is_gui; 
    return g_strcmp0(pa->name, pb->name);
}

gint compare_store(gconstpointer a, gconstpointer b) {
    const StorePackageInfo *pa = a; const StorePackageInfo *pb = b;
    if (pa->is_gui != pb->is_gui) return pb->is_gui - pa->is_gui;
    if (pa->popularity != pb->popularity) return pb->popularity - pa->popularity;
    return g_strcmp0(pa->name, pb->name);
}

gboolean progress_pulse_local(gpointer data) {
    if (is_processing_local) { 
        gtk_widget_show(progress_local); 
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_local)); 
        return G_SOURCE_CONTINUE; 
    }
    gtk_widget_hide(progress_local); 
    return G_SOURCE_REMOVE;
}

gboolean progress_pulse_store(gpointer data) {
    if (is_processing_store) { 
        gtk_widget_show(progress_store); 
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_store)); 
        return G_SOURCE_CONTINUE; 
    }
    gtk_widget_hide(progress_store); 
    return G_SOURCE_REMOVE;
}

// --- Task Manager UI ---
void dismiss_task_cb(GtkWidget *btn, gpointer data) { 
    gtk_widget_destroy(GTK_WIDGET(data)); 
}

void task_row_destroy_cb(GtkWidget *widget, gpointer data) { 
    g_free(data); 
}

void task_done_cb(GPid pid, gint status, gpointer data) {
    TaskUI *ui = (TaskUI *)data;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress), 1.0);
    if (status == 0) {
        gtk_label_set_markup(GTK_LABEL(ui->lbl_status), "<span foreground='#4CAF50'><b>Completed</b></span>");
    } else {
        char err[128]; snprintf(err, sizeof(err), "<span foreground='#F44336'><b>Failed (%d)</b></span>", WEXITSTATUS(status));
        gtk_label_set_markup(GTK_LABEL(ui->lbl_status), err);
    }
    gtk_widget_show(ui->btn_dismiss); 
    g_spawn_close_pid(pid);
}

gboolean pulse_task_progress(gpointer data) {
    TaskUI *ui = (TaskUI *)data;
    if (!gtk_widget_get_visible(ui->btn_dismiss)) { 
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(ui->progress)); 
        return G_SOURCE_CONTINUE; 
    }
    return G_SOURCE_REMOVE;
}

void run_task_cmd(const char *title, const char *cmd) {
    char full_cmd[2048]; 
    snprintf(full_cmd, sizeof(full_cmd), "pkexec sh -c '%s'", cmd);
    
    TaskUI *ui = g_new0(TaskUI, 1);
    ui->row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); 
    gtk_style_context_add_class(gtk_widget_get_style_context(ui->row), "task-row");
    
    GtkWidget *lbl_title = gtk_label_new(title); 
    gtk_widget_set_size_request(lbl_title, 200, -1); 
    gtk_label_set_xalign(GTK_LABEL(lbl_title), 0.0);
    
    ui->progress = gtk_progress_bar_new(); 
    gtk_widget_set_hexpand(ui->progress, TRUE); 
    gtk_widget_set_valign(ui->progress, GTK_ALIGN_CENTER);
    
    ui->lbl_status = gtk_label_new("Running..."); 
    gtk_widget_set_size_request(ui->lbl_status, 120, -1);
    
    ui->btn_dismiss = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON); 
    gtk_widget_set_no_show_all(ui->btn_dismiss, TRUE);

    gtk_box_pack_start(GTK_BOX(ui->row), lbl_title, FALSE, FALSE, 5); 
    gtk_box_pack_start(GTK_BOX(ui->row), ui->progress, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(ui->row), ui->lbl_status, FALSE, FALSE, 5); 
    gtk_box_pack_start(GTK_BOX(ui->row), ui->btn_dismiss, FALSE, FALSE, 5);

    g_signal_connect(ui->btn_dismiss, "clicked", G_CALLBACK(dismiss_task_cb), ui->row); 
    g_signal_connect(ui->row, "destroy", G_CALLBACK(task_row_destroy_cb), ui);
    gtk_box_pack_start(GTK_BOX(box_tasks), ui->row, FALSE, FALSE, 0); 
    gtk_widget_show_all(ui->row); 
    gtk_widget_hide(ui->btn_dismiss); 

    g_timeout_add(100, pulse_task_progress, ui);

    gint argc; 
    gchar **argv;
    if (g_shell_parse_argv(full_cmd, &argc, &argv, NULL)) {
        GPid pid;
        gboolean res = g_spawn_async(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH, NULL, NULL, &pid, NULL);
        g_strfreev(argv);
        if (res) {
            g_child_watch_add(pid, task_done_cb, ui);
        } else { 
            gtk_label_set_markup(GTK_LABEL(ui->lbl_status), "<span foreground='#F44336'>Start Failed</span>"); 
            gtk_widget_show(ui->btn_dismiss); 
        }
    } else {
        gtk_label_set_markup(GTK_LABEL(ui->lbl_status), "<span foreground='#F44336'>Parse Error</span>"); 
        gtk_widget_show(ui->btn_dismiss);
    }
}

void trigger_task_from_args(GtkWidget *btn, gpointer data) {
    char **args = (char**)data; 
    run_task_cmd(args[0], args[1]);
}

// --- Enhanced Popover Info ---
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
    GtkWidget *img_icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_DIALOG); 
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
        g_snprintf(warn_markup, sizeof(warn_markup), "<span foreground='#FF9800'><b>⚠️ %s</b></span>", warning);
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

void on_local_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    PackageInfo *pkg = g_object_get_data(G_OBJECT(row), "pkg_data");
    if (pkg) show_enhanced_info_popover(GTK_WIDGET(row), pkg->name, pkg->source, pkg->version, pkg->icon_name, pkg->is_unstable, pkg->warning_msg);
}

void on_store_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    StorePackageInfo *pkg = g_object_get_data(G_OBJECT(row), "pkg_data");
    if (pkg) show_enhanced_info_popover(GTK_WIDGET(row), pkg->name, pkg->source, pkg->version, pkg->icon_name, pkg->is_unstable, pkg->warning_msg);
}

gboolean apply_local_cache_ui(gpointer data) {
    GList *new_list = (GList *)data;
    if (local_cache) g_list_free_full(local_cache, g_free);
    local_cache = new_list;
    save_local_cache();

    if (installed_hash) g_hash_table_destroy(installed_hash);
    installed_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (GList *it = local_cache; it != NULL; it = it->next) {
        PackageInfo *p = it->data;
        g_hash_table_add(installed_hash, g_strdup(p->name));
    }

    is_processing_local = FALSE;
    apply_local_filters(NULL, NULL);
    check_and_show_errors();
    return G_SOURCE_REMOVE;
}

gboolean apply_store_cache_ui(gpointer data) {
    GList *new_list = (GList *)data;
    if (store_cache) g_list_free_full(store_cache, g_free);
    store_cache = new_list;
    save_store_cache();
    is_processing_store = FALSE;
    apply_store_filters(NULL, NULL);
    check_and_show_errors();
    return G_SOURCE_REMOVE;
}

void render_local_chunk() {
    int end_idx = local_render_idx + CHUNK_SIZE;
    GList *iter = g_list_nth(filtered_local, local_render_idx);
    
    while (iter != NULL && local_render_idx < end_idx) {
        PackageInfo *pkg = (PackageInfo *)iter->data;
        GtkWidget *row = gtk_list_box_row_new(); 
        g_object_set_data(G_OBJECT(row), "pkg_data", pkg);

        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); 
        gtk_style_context_add_class(gtk_widget_get_style_context(box), "pkg-row");
        
        if (pkg->status == PKG_UPDATE) gtk_style_context_add_class(gtk_widget_get_style_context(box), "pkg-update");
        if (pkg->status == PKG_ORPHAN) gtk_style_context_add_class(gtk_widget_get_style_context(box), "pkg-orphan");

        char icon_name[256];
        get_icon_for_pkg(pkg->name, icon_name, pkg->source);
        GtkWidget *img = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_DIALOG); 
        gtk_image_set_pixel_size(GTK_IMAGE(img), 48); 
        gtk_box_pack_start(GTK_BOX(box), img, FALSE, FALSE, 5);

        GtkWidget *vbox_info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        char markup[1024]; 
        const char *src = (pkg->source == SRC_FLATPAK) ? "[Flatpak]" : (pkg->source == SRC_AUR) ? "[AUR]" : "[Pacman]";
        
        if (pkg->status == PKG_UPDATE) {
            g_snprintf(markup, sizeof(markup), "<b>%s</b> <span foreground='gray'>%s</span>%s\n<span size='small' foreground='#4CAF50'><b>%s ➔ %s</b></span>", 
                       pkg->name, src, pkg->is_unstable ? " <span foreground='#FF9800'>[⚠️]</span>" : "", pkg->version, pkg->new_version);
        } else {
            g_snprintf(markup, sizeof(markup), "<b>%s</b> <span foreground='gray'>%s</span>%s\n<span size='small'>%s</span>", 
                       pkg->name, src, pkg->is_unstable ? " <span foreground='#FF9800'>[⚠️]</span>" : "", pkg->version);
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

        if (pkg->status == PKG_UPDATE) {
            GtkWidget *btn_upd = gtk_button_new_with_label("Update");
            char title[256]; g_snprintf(title, sizeof(title), "Updating %s", pkg->name);
            char cmd[512];
            if (pkg->source == SRC_FLATPAK) snprintf(cmd, sizeof(cmd), "flatpak update -y %s", pkg->name);
            else snprintf(cmd, sizeof(cmd), "pacman -S --noconfirm %s || yay -S --noconfirm %s", pkg->name, pkg->name);
            char **args = g_new(char*, 3); args[0] = g_strdup(title); args[1] = g_strdup(cmd); args[2] = NULL;
            g_signal_connect_data(btn_upd, "clicked", G_CALLBACK(trigger_task_from_args), args, (GClosureNotify)g_strfreev, 0);
            gtk_box_pack_start(GTK_BOX(hbox_actions), btn_upd, FALSE, FALSE, 0);
        }

        GtkWidget *btn_del = gtk_button_new_with_label("Remove");
        char title_del[256]; g_snprintf(title_del, sizeof(title_del), "Removing %s", pkg->name);
        char cmd_del[512];
        if (pkg->source == SRC_FLATPAK) snprintf(cmd_del, sizeof(cmd_del), "flatpak uninstall -y %s", pkg->name);
        else snprintf(cmd_del, sizeof(cmd_del), "pacman -Rns --noconfirm %s", pkg->name);
        char **args_del = g_new(char*, 3); args_del[0] = g_strdup(title_del); args_del[1] = g_strdup(cmd_del); args_del[2] = NULL;
        g_signal_connect_data(btn_del, "clicked", G_CALLBACK(trigger_task_from_args), args_del, (GClosureNotify)g_strfreev, 0);
        gtk_box_pack_start(GTK_BOX(hbox_actions), btn_del, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(box), hbox_actions, FALSE, FALSE, 5); 
        gtk_container_add(GTK_CONTAINER(row), box);
        gtk_widget_show_all(row); 
        gtk_list_box_insert(GTK_LIST_BOX(listbox_local), row, -1);
        
        local_render_idx++;
        iter = iter->next;
    }
}

void apply_local_filters(GtkEntry *entry, gpointer data) {
    if (filtered_local) { g_list_free(filtered_local); filtered_local = NULL; }
    GList *children = gtk_container_get_children(GTK_CONTAINER(listbox_local));
    for(GList *it = children; it != NULL; it = it->next) gtk_widget_destroy(GTK_WIDGET(it->data));
    g_list_free(children);

    const char *term = gtk_entry_get_text(GTK_ENTRY(search_entry_local));
    int total_updates = 0, total_orphans = 0;
    
    for (GList *it = local_cache; it != NULL; it = it->next) {
        PackageInfo *pkg = (PackageInfo *)it->data;
        if (pkg->status == PKG_UPDATE) total_updates++;
        if (pkg->status == PKG_ORPHAN) total_orphans++;

        gboolean match_src = (pkg->source == SRC_PACMAN && f_loc_pacman) || 
                             (pkg->source == SRC_AUR && f_loc_aur) || 
                             (pkg->source == SRC_FLATPAK && f_loc_flatpak);
                             
        gboolean match_stat = (!f_loc_updates || pkg->status == PKG_UPDATE) && 
                              (!f_loc_orphans || pkg->status == PKG_ORPHAN);
                              
        int p_cat = match_category(pkg->is_gui, pkg->desc);
        gboolean match_cat = (f_loc_cat == 0) || (p_cat == f_loc_cat);
                              
        if (match_src && match_stat && match_cat && (strlen(term) == 0 || strcasestr(pkg->name, term))) {
            filtered_local = g_list_append(filtered_local, pkg);
        }
    }
    
    gtk_widget_set_visible(btn_global_update, total_updates > 0); 
    gtk_widget_set_visible(btn_global_orphans, total_orphans > 0);

    filtered_local = g_list_sort(filtered_local, compare_local); 
    local_render_idx = 0; 
    render_local_chunk();
}

void render_store_chunk() {
    int end_idx = store_render_idx + CHUNK_SIZE;
    GList *iter = g_list_nth(filtered_store, store_render_idx);
    
    while (iter != NULL && store_render_idx < end_idx) {
        StorePackageInfo *pkg = (StorePackageInfo *)iter->data;
        GtkWidget *row = gtk_list_box_row_new(); 
        g_object_set_data(G_OBJECT(row), "pkg_data", pkg);

        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); 
        gtk_style_context_add_class(gtk_widget_get_style_context(box), "pkg-row");

        char icon_name[256];
        get_icon_for_pkg(pkg->name, icon_name, pkg->source);
        GtkWidget *img = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_DIALOG); 
        gtk_image_set_pixel_size(GTK_IMAGE(img), 48);
        gtk_box_pack_start(GTK_BOX(box), img, FALSE, FALSE, 5);

        GtkWidget *vbox_info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        char markup[512]; 
        g_snprintf(markup, sizeof(markup), "<b>%s</b> <span foreground='gray'>[%s]</span>%s\n<span size='small'>%s</span>", 
                   pkg->name, pkg->repo, pkg->is_unstable ? " <span foreground='#FF9800'>[⚠️]</span>" : "", pkg->version);
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

        GtkWidget *btn_inst = gtk_button_new_with_label("Install");
        char title[256]; g_snprintf(title, sizeof(title), "Installing %s", pkg->name);
        char cmd[512];
        if (pkg->source == SRC_FLATPAK) snprintf(cmd, sizeof(cmd), "flatpak install -y flathub %s", pkg->name);
        else snprintf(cmd, sizeof(cmd), "pacman -S --noconfirm %s || yay -S --noconfirm %s", pkg->name, pkg->name);
        char **args = g_new(char*, 3); 
        args[0] = g_strdup(title); args[1] = g_strdup(cmd); args[2] = NULL;
        
        g_signal_connect_data(btn_inst, "clicked", G_CALLBACK(trigger_task_from_args), args, (GClosureNotify)g_strfreev, 0);
        gtk_box_pack_start(GTK_BOX(hbox_actions), btn_inst, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(box), hbox_actions, FALSE, FALSE, 5); 
        gtk_container_add(GTK_CONTAINER(row), box);
        gtk_widget_show_all(row); 
        gtk_list_box_insert(GTK_LIST_BOX(listbox_store), row, -1);
        
        store_render_idx++;
        iter = iter->next;
    }
}

void apply_store_filters(GtkEntry *entry, gpointer data) {
    if (filtered_store) { g_list_free(filtered_store); filtered_store = NULL; }
    GList *children = gtk_container_get_children(GTK_CONTAINER(listbox_store));
    for(GList *it = children; it != NULL; it = it->next) gtk_widget_destroy(GTK_WIDGET(it->data));
    g_list_free(children);

    const char *term = gtk_entry_get_text(GTK_ENTRY(search_entry_store));
    
    for (GList *it = store_cache; it != NULL; it = it->next) {
        StorePackageInfo *pkg = (StorePackageInfo *)it->data;
        if (installed_hash && g_hash_table_contains(installed_hash, pkg->name)) continue;

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
}

// --- Menu & Sync logic ---
void trigger_sync(GtkWidget *btn, gpointer data) {
    int mode = GPOINTER_TO_INT(data);
    if (mode == 0 && !is_processing_local) {
        is_processing_local = TRUE; 
        g_timeout_add(100, progress_pulse_local, NULL); 
        g_thread_new("loc", fetch_local_thread, NULL);
    } else if (mode == 1 && !is_processing_store) {
        is_processing_store = TRUE; 
        g_timeout_add(100, progress_pulse_store, NULL); 
        g_thread_new("sto", fetch_store_thread, NULL);
    }
}

gboolean delayed_store_sync(gpointer data) {
    if (!is_processing_store && config.auto_update_cache) {
        trigger_sync(NULL, GINT_TO_POINTER(1)); 
    }
    return G_SOURCE_REMOVE;
}

void on_local_scroll(GtkScrolledWindow *sw, GtkPositionType pos, gpointer data) { 
    if (pos == GTK_POS_BOTTOM) render_local_chunk(); 
}

void on_store_scroll(GtkScrolledWindow *sw, GtkPositionType pos, gpointer data) { 
    if (pos == GTK_POS_BOTTOM) render_store_chunk(); 
}

void on_setting_toggled(GtkSwitch *widget, gpointer data) {
    gboolean *setting_val = (gboolean*)data; 
    *setting_val = gtk_switch_get_active(widget); 
    save_config();
}

void show_sidebar_page(GtkWidget *btn, gpointer data) { 
    gtk_stack_set_visible_child_name(GTK_STACK(sidebar_stack), (const char*)data); 
}

void toggle_settings_sidebar(GtkWidget *btn, gpointer data) {
    gboolean active = gtk_revealer_get_reveal_child(GTK_REVEALER(settings_revealer)); 
    gtk_revealer_set_reveal_child(GTK_REVEALER(settings_revealer), !active);
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
    if (mode == 0) f_loc_cat = active; 
    else f_sto_cat = active;
    
    if (mode == 0) apply_local_filters(NULL, NULL); 
    else apply_store_filters(NULL, NULL);
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
    for (int i=0; i < sizeof(categories)/sizeof(categories[0]); i++) {
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
        
        GtkWidget *c_orp = gtk_check_button_new_with_label("Orphans"); 
        g_signal_connect(c_orp, "toggled", G_CALLBACK(on_filter_changed), &f_loc_orphans); 
        gtk_box_pack_start(GTK_BOX(vbox), c_orp, FALSE, FALSE, 0);
    }

    GList *labels = gtk_container_get_children(GTK_CONTAINER(vbox));
    for(GList *l = labels; l != NULL; l = l->next) {
        if(GTK_IS_LABEL(l->data)) gtk_label_set_use_markup(GTK_LABEL(l->data), TRUE);
    }
    g_list_free(labels); 
    gtk_container_add(GTK_CONTAINER(pop), vbox); 
    gtk_widget_show_all(vbox); 
    return pop;
}

void on_tab_switched(GtkToggleButton *btn, gpointer data) {
    if (gtk_toggle_button_get_active(btn)) {
        gtk_stack_set_visible_child_name(GTK_STACK(main_stack), (const char*)data);
    }
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    init_config();

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        ".pkg-update { border-left: 4px solid #4CAF50; background: rgba(76, 175, 80, 0.08); }\n"
        ".pkg-orphan { border-left: 4px solid #F44336; background: rgba(244, 67, 54, 0.08); }\n"
        ".pkg-row { padding: 2px; margin: 1px; border-radius: 6px; background: rgba(128,128,128,0.05); }\n"
        ".task-row { background: #333; padding: 4px 8px; border-radius: 6px; margin: 2px 10px; }\n"
        ".tab-box { background: #2D2D2D; border-radius: 16px; padding: 2px; }\n"
        ".tab-btn { border-radius: 14px; background: transparent; border: none; font-weight: bold; padding: 3px 12px; }\n"
        ".tab-btn:checked { background: #1E88E5; color: white; }\n"
        "button { min-height: 20px; padding: 2px 6px; }\n"
        "entry { min-height: 20px; padding: 2px 4px; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "ArchWarden"); 
    gtk_window_set_default_size(GTK_WINDOW(window), 1050, 700);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); 
    gtk_container_add(GTK_CONTAINER(window), main_vbox);

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
    
    GtkWidget *btn_settings = gtk_button_new_from_icon_name("view-more-symbolic", GTK_ICON_SIZE_BUTTON); 
    g_signal_connect(btn_settings, "clicked", G_CALLBACK(toggle_settings_sidebar), NULL);
    
    gtk_box_pack_start(GTK_BOX(top_bar), tab_box, TRUE, FALSE, 0); 
    gtk_box_pack_end(GTK_BOX(top_bar), btn_settings, FALSE, FALSE, 0); 
    gtk_box_pack_start(GTK_BOX(main_vbox), top_bar, FALSE, FALSE, 0);

    box_tasks = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2); 
    gtk_box_pack_start(GTK_BOX(main_vbox), box_tasks, FALSE, FALSE, 0);

    GtkWidget *content_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    main_stack = gtk_stack_new(); 
    gtk_stack_set_transition_type(GTK_STACK(main_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    
    settings_revealer = gtk_revealer_new(); 
    gtk_revealer_set_transition_type(GTK_REVEALER(settings_revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
    sidebar_stack = gtk_stack_new(); 
    gtk_stack_set_transition_type(GTK_STACK(sidebar_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    
    GtkWidget *vbox_menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5); 
    gtk_widget_set_size_request(vbox_menu, 230, -1); 
    gtk_container_set_border_width(GTK_CONTAINER(vbox_menu), 10);
    
    GtkWidget *btn_go_settings = gtk_button_new_with_label("Settings"); 
    GtkWidget *btn_go_about = gtk_button_new_with_label("About");
    g_signal_connect(btn_go_settings, "clicked", G_CALLBACK(show_sidebar_page), "settings"); 
    g_signal_connect(btn_go_about, "clicked", G_CALLBACK(show_sidebar_page), "about");
    
    gtk_box_pack_start(GTK_BOX(vbox_menu), btn_go_settings, FALSE, FALSE, 2); 
    gtk_box_pack_start(GTK_BOX(vbox_menu), btn_go_about, FALSE, FALSE, 2); 
    gtk_stack_add_named(GTK_STACK(sidebar_stack), vbox_menu, "menu");
    
    GtkWidget *vbox_settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8); 
    gtk_container_set_border_width(GTK_CONTAINER(vbox_settings), 15);
    GtkWidget *btn_back_settings = gtk_button_new_with_label("← Back"); 
    g_signal_connect(btn_back_settings, "clicked", G_CALLBACK(show_sidebar_page), "menu"); 
    gtk_box_pack_start(GTK_BOX(vbox_settings), btn_back_settings, FALSE, FALSE, 5);

    GtkWidget *lbl = gtk_label_new("<span size='large' weight='bold'>Manager Settings</span>"); 
    gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE); 
    gtk_widget_set_halign(lbl, GTK_ALIGN_START); 
    gtk_box_pack_start(GTK_BOX(vbox_settings), lbl, FALSE, FALSE, 5);

    struct { char *name; gboolean *ptr; } toggles[] = { 
        {"Pacman Repos", &config.en_pacman}, 
        {"AUR (yay)", &config.en_aur}, 
        {"Flatpak", &config.en_flatpak}, 
        {"Auto Update Browse", &config.auto_update_cache} 
    };
    
    for (int i=0; i<4; i++) {
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5); 
        GtkWidget *sw = gtk_switch_new(); 
        gtk_switch_set_active(GTK_SWITCH(sw), *(toggles[i].ptr));
        g_signal_connect(sw, "notify::active", G_CALLBACK(on_setting_toggled), toggles[i].ptr); 
        gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(toggles[i].name), FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(hbox), sw, FALSE, FALSE, 0); 
        gtk_box_pack_start(GTK_BOX(vbox_settings), hbox, FALSE, FALSE, 2);
    }
    gtk_stack_add_named(GTK_STACK(sidebar_stack), vbox_settings, "settings");

    GtkWidget *vbox_about = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10); 
    gtk_container_set_border_width(GTK_CONTAINER(vbox_about), 15);
    GtkWidget *btn_back_about = gtk_button_new_with_label("← Back"); 
    g_signal_connect(btn_back_about, "clicked", G_CALLBACK(show_sidebar_page), "menu"); 
    gtk_box_pack_start(GTK_BOX(vbox_about), btn_back_about, FALSE, FALSE, 5);
    
    GtkWidget *lbl_about = gtk_label_new("<span size='x-large' weight='bold'>ArchWarden</span>\nVersion 0.0.1\nSecure Arch Linux Manager");
    gtk_label_set_use_markup(GTK_LABEL(lbl_about), TRUE); 
    gtk_label_set_justify(GTK_LABEL(lbl_about), GTK_JUSTIFY_CENTER); 
    gtk_box_pack_start(GTK_BOX(vbox_about), lbl_about, FALSE, FALSE, 10);
    gtk_stack_add_named(GTK_STACK(sidebar_stack), vbox_about, "about");

    gtk_container_add(GTK_CONTAINER(settings_revealer), sidebar_stack);
    
    gtk_box_pack_start(GTK_BOX(content_hbox), main_stack, TRUE, TRUE, 0); 
    gtk_box_pack_start(GTK_BOX(content_hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content_hbox), settings_revealer, FALSE, FALSE, 0); 
    gtk_box_pack_start(GTK_BOX(main_vbox), content_hbox, TRUE, TRUE, 0);

    // --- Installed View ---
    GtkWidget *vbox_local = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); 
    GtkWidget *header_loc = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5); 
    gtk_container_set_border_width(GTK_CONTAINER(header_loc), 5);
    
    search_entry_local = gtk_search_entry_new(); 
    gtk_widget_set_hexpand(search_entry_local, TRUE); 
    g_signal_connect(search_entry_local, "search-changed", G_CALLBACK(apply_local_filters), NULL);
    
    btn_global_update = gtk_button_new_with_label("Update All"); 
    char **args_upd = g_new(char*, 3); 
    args_upd[0] = g_strdup("System Update"); args_upd[1] = g_strdup("pacman -Syu --noconfirm && flatpak update -y"); args_upd[2] = NULL;
    g_signal_connect_data(btn_global_update, "clicked", G_CALLBACK(trigger_task_from_args), args_upd, (GClosureNotify)g_strfreev, 0); 
    gtk_widget_set_no_show_all(btn_global_update, TRUE);

    btn_global_orphans = gtk_button_new_with_label("Remove Orphans"); 
    char **args_orp = g_new(char*, 3); 
    args_orp[0] = g_strdup("Removing Orphans"); args_orp[1] = g_strdup("pacman -Rns $(pacman -Qdtq) --noconfirm"); args_orp[2] = NULL;
    g_signal_connect_data(btn_global_orphans, "clicked", G_CALLBACK(trigger_task_from_args), args_orp, (GClosureNotify)g_strfreev, 0); 
    gtk_widget_set_no_show_all(btn_global_orphans, TRUE);

    GtkWidget *btn_filt_loc = gtk_button_new_with_label("Filter"); 
    GtkWidget *pop_loc = create_filter_popover(btn_filt_loc, 0); 
    g_signal_connect_swapped(btn_filt_loc, "clicked", G_CALLBACK(gtk_popover_popup), pop_loc);

    gtk_box_pack_start(GTK_BOX(header_loc), search_entry_local, TRUE, TRUE, 0); 
    gtk_box_pack_start(GTK_BOX(header_loc), btn_global_update, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_loc), btn_global_orphans, FALSE, FALSE, 0); 
    gtk_box_pack_start(GTK_BOX(header_loc), btn_filt_loc, FALSE, FALSE, 0);
    
    progress_local = gtk_progress_bar_new(); 
    gtk_widget_set_no_show_all(progress_local, TRUE);

    GtkWidget *scroll_loc = gtk_scrolled_window_new(NULL, NULL); 
    g_signal_connect(scroll_loc, "edge-reached", G_CALLBACK(on_local_scroll), NULL);
    listbox_local = gtk_list_box_new(); 
    g_signal_connect(listbox_local, "row-activated", G_CALLBACK(on_local_row_activated), NULL); 
    gtk_container_add(GTK_CONTAINER(scroll_loc), listbox_local);

    gtk_box_pack_start(GTK_BOX(vbox_local), header_loc, FALSE, FALSE, 0); 
    gtk_box_pack_start(GTK_BOX(vbox_local), progress_local, FALSE, FALSE, 0); 
    gtk_box_pack_start(GTK_BOX(vbox_local), scroll_loc, TRUE, TRUE, 0);
    gtk_stack_add_named(GTK_STACK(main_stack), vbox_local, "installed");

    // --- Browse View ---
    GtkWidget *vbox_store = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); 
    GtkWidget *header_sto = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5); 
    gtk_container_set_border_width(GTK_CONTAINER(header_sto), 5);
    
    search_entry_store = gtk_search_entry_new(); 
    gtk_widget_set_hexpand(search_entry_store, TRUE); 
    g_signal_connect(search_entry_store, "search-changed", G_CALLBACK(apply_store_filters), NULL);
    
    GtkWidget *btn_filt_sto = gtk_button_new_with_label("Filter"); 
    GtkWidget *pop_sto = create_filter_popover(btn_filt_sto, 1); 
    g_signal_connect_swapped(btn_filt_sto, "clicked", G_CALLBACK(gtk_popover_popup), pop_sto);
    
    GtkWidget *btn_ref_sto = gtk_button_new_with_label("Sync Browse"); 
    g_signal_connect(btn_ref_sto, "clicked", G_CALLBACK(trigger_sync), GINT_TO_POINTER(1));

    gtk_box_pack_start(GTK_BOX(header_sto), search_entry_store, TRUE, TRUE, 0); 
    gtk_box_pack_start(GTK_BOX(header_sto), btn_filt_sto, FALSE, FALSE, 0); 
    gtk_box_pack_start(GTK_BOX(header_sto), btn_ref_sto, FALSE, FALSE, 0);

    progress_store = gtk_progress_bar_new(); 
    gtk_widget_set_no_show_all(progress_store, TRUE);

    GtkWidget *scroll_sto = gtk_scrolled_window_new(NULL, NULL); 
    g_signal_connect(scroll_sto, "edge-reached", G_CALLBACK(on_store_scroll), NULL);
    listbox_store = gtk_list_box_new(); 
    g_signal_connect(listbox_store, "row-activated", G_CALLBACK(on_store_row_activated), NULL); 
    gtk_container_add(GTK_CONTAINER(scroll_sto), listbox_store);

    gtk_box_pack_start(GTK_BOX(vbox_store), header_sto, FALSE, FALSE, 0); 
    gtk_box_pack_start(GTK_BOX(vbox_store), progress_store, FALSE, FALSE, 0); 
    gtk_box_pack_start(GTK_BOX(vbox_store), scroll_sto, TRUE, TRUE, 0);
    gtk_stack_add_named(GTK_STACK(main_stack), vbox_store, "browse");

    gtk_widget_show_all(window);
    
    load_local_cache(); 
    if (local_cache != NULL) apply_local_filters(NULL, NULL); 
    trigger_sync(NULL, GINT_TO_POINTER(0)); 
    
    load_store_cache(); 
    if (store_cache != NULL) apply_store_filters(NULL, NULL); 
    g_timeout_add(1500, delayed_store_sync, NULL);

    gtk_main();
    return 0;
}