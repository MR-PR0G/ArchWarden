#include "archwarden.h"

// Define Global TaskUI internal to main.c
typedef struct {
    GtkWidget *row;
    GtkWidget *progress;
    GtkWidget *lbl_status;
    GtkWidget *btn_dismiss;
} TaskUI;

// Define Global UI Variables
GtkWidget *listbox_local = NULL;
GtkWidget *listbox_store = NULL;
GtkWidget *listbox_network = NULL;
GtkWidget *search_entry_local = NULL;
GtkWidget *search_entry_store = NULL;
GtkWidget *progress_local = NULL;
GtkWidget *progress_store = NULL;
GtkWidget *action_bar_local = NULL;
GtkWidget *action_bar_store = NULL;
GtkWidget *lbl_selected_local = NULL;
GtkWidget *lbl_selected_store = NULL;
GtkWidget *box_tasks = NULL;
GtkWidget *main_content_stack = NULL;
GtkWidget *packages_stack = NULL;
GtkWidget *settings_revealer = NULL;
GtkWidget *sidebar_stack = NULL;
GtkWidget *popover_activity = NULL;

GtkWidget *lbl_rx_total = NULL;
GtkWidget *lbl_tx_total = NULL;
GtkWidget *lbl_ping = NULL;
GtkWidget *lbl_pub_ip = NULL;
GtkWidget *lbl_loc_ip = NULL;
GtkWidget *lbl_isp = NULL;
GtkWidget *network_chart = NULL;

gboolean f_loc_pacman = TRUE;
gboolean f_loc_aur = TRUE;
gboolean f_loc_flatpak = TRUE;
gboolean f_loc_updates = FALSE;
gboolean f_loc_orphans = FALSE;
int f_loc_cat = 0;

gboolean f_sto_pacman = TRUE;
gboolean f_sto_aur = TRUE;
gboolean f_sto_flatpak = TRUE;
int f_sto_cat = 0;

const char *categories[7] = {
    "All Categories", "Desktop Apps / GUI", "CLI & Core Libraries", 
    "Games & Entertainment", "Photo, Video & Audio", 
    "Development & Tools", "Internet & Network"
};

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

void trigger_sync(GtkWidget *btn, gpointer data) {
    int mode = GPOINTER_TO_INT(data);
    if (mode == 0 && !is_processing_local) {
        is_processing_local = TRUE;
        if (progress_local) gtk_widget_show(progress_local);
        g_thread_new("loc", fetch_local_thread, NULL);
    } else if (mode == 1 && !is_processing_store) {
        is_processing_store = TRUE;
        if (progress_store) gtk_widget_show(progress_store);
        g_thread_new("sto", fetch_store_thread, NULL);
    }
}

void switch_main_module(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    int index = gtk_list_box_row_get_index(row);
    if (index == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(main_content_stack), "package_manager");
    } else if (index == 1) {
        gtk_stack_set_visible_child_name(GTK_STACK(main_content_stack), "network_monitor");
    }
}

void show_sidebar_page(GtkWidget *btn, gpointer data) {
    gtk_stack_set_visible_child_name(GTK_STACK(sidebar_stack), (const char*)data);
}

void toggle_settings_sidebar(GtkWidget *btn, gpointer data) {
    gboolean active = gtk_revealer_get_reveal_child(GTK_REVEALER(settings_revealer));
    gtk_revealer_set_reveal_child(GTK_REVEALER(settings_revealer), !active);
}

void on_setting_toggled(GtkSwitch *widget, gpointer data) {
    gboolean *setting_val = (gboolean*)data;
    *setting_val = gtk_switch_get_active(widget);
    save_config();
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
        char err[128];
        snprintf(err, sizeof(err), "<span foreground='#F44336'><b>Failed (%d)</b></span>", WEXITSTATUS(status));
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
    GString *full_cmd = g_string_new("pkexec sh -c '");
    g_string_append(full_cmd, cmd);
    g_string_append(full_cmd, "'");

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

    if (box_tasks) {
        gtk_box_pack_start(GTK_BOX(box_tasks), ui->row, FALSE, FALSE, 0);
        gtk_widget_show_all(ui->row);
        gtk_widget_hide(ui->btn_dismiss);
    }

    if (popover_activity) {
        gtk_popover_popup(GTK_POPOVER(popover_activity));
    }

    g_timeout_add(100, pulse_task_progress, ui);

    gint argc;
    gchar **argv;
    if (g_shell_parse_argv(full_cmd->str, &argc, &argv, NULL)) {
        GPid pid;
        gboolean res = g_spawn_async(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH, NULL, NULL, &pid, NULL);
        g_strfreev(argv);
        if (res) {
            g_child_watch_add(pid, task_done_cb, ui);
        } else {
            gtk_label_set_markup(GTK_LABEL(ui->lbl_status), "<span foreground='#F44336'>Failed</span>");
            gtk_widget_show(ui->btn_dismiss);
        }
    } else {
        gtk_label_set_markup(GTK_LABEL(ui->lbl_status), "<span foreground='#F44336'>Error</span>");
        gtk_widget_show(ui->btn_dismiss);
    }
    g_string_free(full_cmd, TRUE);
}

void trigger_task_from_args(GtkWidget *btn, gpointer data) {
    char **args = (char**)data;
    run_task_cmd(args[0], args[1]);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    init_config();

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        ".pkg-update { border-left: 3px solid #4CAF50; }\n"
        ".pkg-orphan { border-left: 3px solid #F44336; }\n"
        ".pkg-orphan-useful { border-left: 3px solid #FF9800; }\n"
        ".pkg-row { padding: 8px; margin: 4px; border-radius: 10px; background: rgba(255,255,255,0.03); transition: all 0.2s; }\n"
        ".pkg-row:hover { background: rgba(255,255,255,0.06); }\n"
        ".row-selected { border: 1px solid #2196F3; background: rgba(33, 150, 243, 0.15); box-shadow: inset 4px 0px 0px #2196F3; }\n"
        ".net-card { background: #1E1E1E; border-radius: 16px; padding: 15px; border: 1px solid #333; }\n"
        ".net-group { background: #181818; border-radius: 12px; margin: 5px; border-left: 4px solid #1E88E5; padding: 5px; }\n"
        ".net-group-alert { border-left: 4px solid #F44336; }\n"
        ".task-row { background: #2A2A2A; padding: 8px; border-radius: 8px; margin: 4px; }\n"
        ".tab-box { background: #1E1E1E; border-radius: 20px; padding: 3px; }\n"
        ".tab-btn { border-radius: 16px; background: transparent; border: none; font-weight: bold; padding: 5px 15px; }\n"
        ".tab-btn:checked { background: #2196F3; color: white; }\n"
        ".main-sidebar { background: transparent; padding: 10px; }\n"
        ".dock-list { background: transparent; }\n"
        ".dock-list row { background: transparent; outline: none; padding: 8px 0; }\n"
        ".dock-icon-box { background: #2A2A2A; border-radius: 16px; padding: 12px; transition: all 0.3s; }\n"
        ".dock-list row:selected .dock-icon-box { background: #2196F3; box-shadow: 0 4px 15px rgba(33,150,243,0.4); }\n"
        ".sidebar-btn { padding: 12px; border-radius: 16px; background: #2A2A2A; }\n"
        ".sidebar-btn:hover { background: #333; }\n"
        ".glow-progress trough { background-color: transparent; min-height: 2px; }\n"
        ".glow-progress progress { background-color: #FFFFFF; box-shadow: 0 0 10px #FFFFFF; border-radius: 2px; min-height: 2px; }\n", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "ArchWarden");
    gtk_window_set_default_size(GTK_WINDOW(window), 1150, 750);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(window), main_hbox);

    GtkWidget *left_sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(left_sidebar, 70, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(left_sidebar), "main-sidebar");

    GtkWidget *nav_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(nav_list), GTK_SELECTION_SINGLE);
    gtk_style_context_add_class(gtk_widget_get_style_context(nav_list), "dock-list");
    gtk_widget_set_valign(nav_list, GTK_ALIGN_CENTER);
    g_signal_connect(nav_list, "row-activated", G_CALLBACK(switch_main_module), NULL);

    GtkWidget *row_pkg = gtk_list_box_row_new();
    GtkWidget *box_icon_pkg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(box_icon_pkg), "dock-icon-box");
    GtkWidget *icon_pkg = gtk_image_new_from_icon_name("system-software-install", GTK_ICON_SIZE_DND);
    gtk_container_add(GTK_CONTAINER(box_icon_pkg), icon_pkg);
    gtk_container_add(GTK_CONTAINER(row_pkg), box_icon_pkg);
    gtk_widget_set_tooltip_text(row_pkg, "Package Manager");
    gtk_list_box_insert(GTK_LIST_BOX(nav_list), row_pkg, -1);

    GtkWidget *row_net = gtk_list_box_row_new();
    GtkWidget *box_icon_net = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(box_icon_net), "dock-icon-box");
    GtkWidget *icon_net = gtk_image_new_from_icon_name("network-transmit-receive", GTK_ICON_SIZE_DND);
    gtk_container_add(GTK_CONTAINER(box_icon_net), icon_net);
    gtk_container_add(GTK_CONTAINER(row_net), box_icon_net);
    gtk_widget_set_tooltip_text(row_net, "Network Monitor");
    gtk_list_box_insert(GTK_LIST_BOX(nav_list), row_net, -1);

    gtk_box_pack_start(GTK_BOX(left_sidebar), nav_list, TRUE, TRUE, 0);

    GtkWidget *btn_settings = gtk_button_new_from_icon_name("emblem-system-symbolic", GTK_ICON_SIZE_DND);
    gtk_widget_set_tooltip_text(btn_settings, "Settings");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_settings), "sidebar-btn");
    g_signal_connect(btn_settings, "clicked", G_CALLBACK(toggle_settings_sidebar), NULL);
    gtk_box_pack_end(GTK_BOX(left_sidebar), btn_settings, FALSE, FALSE, 10);

    gtk_box_pack_start(GTK_BOX(main_hbox), left_sidebar, FALSE, FALSE, 0);

    main_content_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(main_content_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    settings_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(settings_revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
    sidebar_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(sidebar_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);

    GtkWidget *vbox_menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(vbox_menu, 230, -1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox_menu), 10);

    GtkWidget *btn_go_settings = gtk_button_new_with_label("Manager Settings");
    GtkWidget *btn_go_about = gtk_button_new_with_label("About ArchWarden");
    g_signal_connect(btn_go_settings, "clicked", G_CALLBACK(show_sidebar_page), "settings");
    g_signal_connect(btn_go_about, "clicked", G_CALLBACK(show_sidebar_page), "about");
    gtk_box_pack_start(GTK_BOX(vbox_menu), btn_go_settings, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(vbox_menu), btn_go_about, FALSE, FALSE, 2);
    gtk_stack_add_named(GTK_STACK(sidebar_stack), vbox_menu, "menu");

    GtkWidget *vbox_settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox_settings), 15);
    GtkWidget *btn_back_settings = gtk_button_new_with_label("Back");
    g_signal_connect(btn_back_settings, "clicked", G_CALLBACK(show_sidebar_page), "menu");
    gtk_box_pack_start(GTK_BOX(vbox_settings), btn_back_settings, FALSE, FALSE, 5);

    GtkWidget *lbl = gtk_label_new("<span size='large' weight='bold'>Settings</span>");
    gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox_settings), lbl, FALSE, FALSE, 5);

    struct { char *name; gboolean *ptr; } toggles[] = {
        {"Pacman Repos", &config.en_pacman},
        {"AUR (yay)", &config.en_aur},
        {"Flatpak", &config.en_flatpak}
    };

    for (int i = 0; i < 3; i++) {
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
    GtkWidget *btn_back_about = gtk_button_new_with_label("Back");
    g_signal_connect(btn_back_about, "clicked", G_CALLBACK(show_sidebar_page), "menu");
    gtk_box_pack_start(GTK_BOX(vbox_about), btn_back_about, FALSE, FALSE, 5);

    GtkWidget *lbl_about = gtk_label_new("<span size='x-large' weight='bold'>ArchWarden</span>\nVersion 0.5.0\nSecure System Dashboard");
    gtk_label_set_use_markup(GTK_LABEL(lbl_about), TRUE);
    gtk_label_set_justify(GTK_LABEL(lbl_about), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox_about), lbl_about, FALSE, FALSE, 10);
    gtk_stack_add_named(GTK_STACK(sidebar_stack), vbox_about, "about");

    gtk_container_add(GTK_CONTAINER(settings_revealer), sidebar_stack);

    GtkWidget *pkg_ui = build_ui_packages();
    GtkWidget *net_ui = build_ui_network();

    GtkWidget *content_wrapper_pkg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(content_wrapper_pkg), pkg_ui, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content_wrapper_pkg), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content_wrapper_pkg), settings_revealer, FALSE, FALSE, 0);

    gtk_stack_add_named(GTK_STACK(main_content_stack), content_wrapper_pkg, "package_manager");
    gtk_stack_add_named(GTK_STACK(main_content_stack), net_ui, "network_monitor");
    gtk_box_pack_start(GTK_BOX(main_hbox), main_content_stack, TRUE, TRUE, 0);

    gtk_widget_show_all(window);

    GtkListBoxRow *first_row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(nav_list), 0);
    gtk_list_box_select_row(GTK_LIST_BOX(nav_list), first_row);

    load_local_cache();
    if (local_cache != NULL) apply_local_filters(NULL, NULL);
    
    load_store_cache();
    if (store_cache != NULL) apply_store_filters(NULL, NULL);

    trigger_sync(NULL, GINT_TO_POINTER(0));
    trigger_sync(NULL, GINT_TO_POINTER(1));

    g_thread_new("net_mon", network_monitor_thread, NULL);

    gtk_main();
    return 0;
}