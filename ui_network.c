#include "archwarden.h"

typedef struct {
    char proto[16];
    char remote_addr[128];
    int alert_level;
    char warning_msg[128];
} SubConnection;

typedef struct {
    char process_name[128];
    GList *sub_conns;
    int highest_alert;
} NetGroup;

gboolean on_draw_network_chart(GtkWidget *widget, cairo_t *cr, gpointer data) {
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    
    g_mutex_lock(&net_mutex);
    
    cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 1.0);
    cairo_paint(cr);
    
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 0.5);
    cairo_set_line_width(cr, 1.0);
    
    for (int i = 1; i < 4; i++) {
        cairo_move_to(cr, 0, i * h / 4.0);
        cairo_line_to(cr, w, i * h / 4.0);
        cairo_stroke(cr);
    }
    
    double max_val = 1.0;
    for (int i = 0; i < 60; i++) {
        if (rx_history[i] > max_val) max_val = rx_history[i];
        if (tx_history[i] > max_val) max_val = tx_history[i];
    }
    
    double step_x = (double)w / 59.0;

    cairo_set_source_rgba(cr, 0.1, 0.6, 1.0, 0.8);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, 0, h - (tx_history[59] / max_val * h));
    for (int i = 58; i >= 0; i--) {
        cairo_line_to(cr, w - (i * step_x), h - (tx_history[i] / max_val * h));
    }
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, 0.2, 0.8, 0.2, 0.8);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr, 0, h - (rx_history[59] / max_val * h));
    for (int i = 58; i >= 0; i--) {
        cairo_line_to(cr, w - (i * step_x), h - (rx_history[i] / max_val * h));
    }
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.7);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 11);
    
    char max_lbl[64];
    snprintf(max_lbl, sizeof(max_lbl), "Top: %.1f KB/s", max_val);
    cairo_move_to(cr, 5, 15);
    cairo_show_text(cr, max_lbl);
    
    cairo_move_to(cr, w - 150, 15);
    cairo_set_source_rgba(cr, 0.2, 0.8, 0.2, 1.0);
    cairo_show_text(cr, "● Download");
    
    cairo_move_to(cr, w - 70, 15);
    cairo_set_source_rgba(cr, 0.1, 0.6, 1.0, 1.0);
    cairo_show_text(cr, "● Upload");

    g_mutex_unlock(&net_mutex);
    return FALSE;
}

void build_network_card(GtkWidget *grid, int x, int y, const char *title, GtkWidget **lbl_val) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "net-card");
    
    GtkWidget *l_title = gtk_label_new(NULL);
    char markup[128];
    snprintf(markup, sizeof(markup), "<span foreground='gray' size='small'><b>%s</b></span>", title);
    gtk_label_set_markup(GTK_LABEL(l_title), markup);
    gtk_widget_set_halign(l_title, GTK_ALIGN_START);
    
    *lbl_val = gtk_label_new("Loading...");
    gtk_widget_set_halign(*lbl_val, GTK_ALIGN_START);
    
    gtk_box_pack_start(GTK_BOX(card), l_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), *lbl_val, TRUE, TRUE, 0);
    
    gtk_grid_attach(GTK_GRID(grid), card, x, y, 1, 1);
}

void kill_connection_btn_clicked(GtkWidget *btn, gpointer data) {
    char *process_name = (char *)data;
    char kill_cmd[256];
    snprintf(kill_cmd, sizeof(kill_cmd), "killall -9 %s 2>/dev/null", process_name);
    run_task_cmd("Killing Process", kill_cmd);
}

void render_network_connections() {
    GList *children = gtk_container_get_children(GTK_CONTAINER(listbox_network));
    for(GList *it = children; it != NULL; it = it->next) {
        gtk_widget_destroy(GTK_WIDGET(it->data));
    }
    g_list_free(children);

    g_mutex_lock(&net_mutex);
    GHashTable *groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    
    for (GList *it = network_connections; it != NULL; it = it->next) {
        NetConnection *conn = (NetConnection *)it->data;
        
        char proc_name[128];
        safe_strcpy(proc_name, conn->process_name, 128);
        
        NetGroup *group = g_hash_table_lookup(groups, proc_name);
        if (!group) {
            group = g_new0(NetGroup, 1);
            safe_strcpy(group->process_name, proc_name, 128);
            g_hash_table_insert(groups, g_strdup(proc_name), group);
        }
        
        SubConnection *sub = g_new0(SubConnection, 1);
        safe_strcpy(sub->proto, conn->proto, 16);
        safe_strcpy(sub->remote_addr, conn->remote_addr, 128);
        sub->alert_level = conn->is_suspicious ? 2 : (strcmp(conn->process_name, "unknown") == 0 ? 1 : 0);
        safe_strcpy(sub->warning_msg, conn->warning_msg, 128);
        
        group->sub_conns = g_list_prepend(group->sub_conns, sub);
        if (sub->alert_level > group->highest_alert) {
            group->highest_alert = sub->alert_level;
        }
    }

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, groups);
    
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        NetGroup *group = (NetGroup *)value;
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);

        GtkWidget *group_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_style_context_add_class(gtk_widget_get_style_context(group_box), "net-group");
        
        if (group->highest_alert == 2) {
            gtk_style_context_add_class(gtk_widget_get_style_context(group_box), "net-group-alert");
        }

        GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        
        char icon_name[256];
        get_icon_for_pkg(group->process_name, icon_name, SRC_PACMAN);
        GtkWidget *img = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_DND);
        gtk_image_set_pixel_size(GTK_IMAGE(img), 32);
        gtk_box_pack_start(GTK_BOX(header_box), img, FALSE, FALSE, 5);

        GtkWidget *expander = gtk_expander_new(NULL);
        char exp_title[256];
        if (group->highest_alert == 2) {
            snprintf(exp_title, sizeof(exp_title), "<span foreground='#F44336'><b>%s</b></span> (Alert)", group->process_name);
        } else if (group->highest_alert == 1) {
            snprintf(exp_title, sizeof(exp_title), "<span foreground='#FFC107'><b>%s</b></span> (Hidden)", group->process_name);
        } else {
            snprintf(exp_title, sizeof(exp_title), "<b>%s</b>", group->process_name);
        }
        
        GtkWidget *lbl_exp = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lbl_exp), exp_title);
        gtk_expander_set_label_widget(GTK_EXPANDER(expander), lbl_exp);
        gtk_box_pack_start(GTK_BOX(header_box), expander, TRUE, TRUE, 0);

        if (group->highest_alert > 0 && strcmp(group->process_name, "unknown") != 0) {
            GtkWidget *btn_kill = gtk_button_new_with_label("Kill App Network");
            gtk_widget_set_tooltip_text(btn_kill, "Kill all processes with this name");
            g_signal_connect_data(btn_kill, "clicked", G_CALLBACK(kill_connection_btn_clicked), g_strdup(group->process_name), (GClosureNotify)g_free, 0);
            gtk_box_pack_end(GTK_BOX(header_box), btn_kill, FALSE, FALSE, 5);
        }

        gtk_box_pack_start(GTK_BOX(group_box), header_box, FALSE, FALSE, 5);

        GtkWidget *vbox_subs = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_margin_start(vbox_subs, 40);
        gtk_widget_set_margin_top(vbox_subs, 5);
        gtk_widget_set_margin_bottom(vbox_subs, 5);

        for (GList *sl = group->sub_conns; sl != NULL; sl = sl->next) {
            SubConnection *sub = (SubConnection *)sl->data;
            GtkWidget *s_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            
            char s_markup[512];
            if (sub->alert_level == 2) {
                snprintf(s_markup, sizeof(s_markup), "<b>%s</b> <span foreground='gray'>[%s]</span> <span foreground='#F44336'>%s</span>", sub->remote_addr, sub->proto, sub->warning_msg);
            } else {
                snprintf(s_markup, sizeof(s_markup), "<b>%s</b> <span foreground='gray'>[%s]</span>", sub->remote_addr, sub->proto);
            }
            
            GtkWidget *s_lbl = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(s_lbl), s_markup);
            gtk_widget_set_halign(s_lbl, GTK_ALIGN_START);
            gtk_box_pack_start(GTK_BOX(s_box), s_lbl, TRUE, TRUE, 0);

            gtk_box_pack_start(GTK_BOX(vbox_subs), s_box, FALSE, FALSE, 2);
            g_free(sub);
        }
        g_list_free(group->sub_conns);
        g_free(group);
        
        gtk_container_add(GTK_CONTAINER(expander), vbox_subs);
        gtk_container_add(GTK_CONTAINER(row), group_box);
        gtk_widget_show_all(row);
        gtk_list_box_insert(GTK_LIST_BOX(listbox_network), row, -1);
    }
    
    g_hash_table_destroy(groups);
    g_mutex_unlock(&net_mutex);
}

gboolean update_network_ui(gpointer data) {
    gtk_widget_queue_draw(network_chart);
    
    g_mutex_lock(&net_mutex);
    char markup[256];
    
    g_snprintf(markup, sizeof(markup), "<span size='large'>%.2f MB</span>", (double)last_rx_bytes / 1048576.0);
    gtk_label_set_markup(GTK_LABEL(lbl_rx_total), markup);
    
    g_snprintf(markup, sizeof(markup), "<span size='large'>%.2f MB</span>", (double)last_tx_bytes / 1048576.0);
    gtk_label_set_markup(GTK_LABEL(lbl_tx_total), markup);
    
    g_snprintf(markup, sizeof(markup), "<span size='large'>%s</span>", local_ip);
    gtk_label_set_markup(GTK_LABEL(lbl_loc_ip), markup);
    
    g_snprintf(markup, sizeof(markup), "<span size='large'>%s</span>", public_ip);
    gtk_label_set_markup(GTK_LABEL(lbl_pub_ip), markup);
    
    g_snprintf(markup, sizeof(markup), "<span size='large'>%s</span>", current_ping);
    gtk_label_set_markup(GTK_LABEL(lbl_ping), markup);
    
    g_snprintf(markup, sizeof(markup), "<span size='large'>%s</span>", isp_name);
    gtk_label_set_markup(GTK_LABEL(lbl_isp), markup);
    
    g_mutex_unlock(&net_mutex);
    
    render_network_connections();
    
    return G_SOURCE_CONTINUE;
}

GtkWidget* build_ui_network() {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);
    
    GtkWidget *chart_frame = gtk_frame_new(NULL);
    network_chart = gtk_drawing_area_new();
    gtk_widget_set_size_request(network_chart, -1, 150);
    g_signal_connect(G_OBJECT(network_chart), "draw", G_CALLBACK(on_draw_network_chart), NULL);
    gtk_container_add(GTK_CONTAINER(chart_frame), network_chart);
    gtk_box_pack_start(GTK_BOX(vbox), chart_frame, FALSE, FALSE, 5);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    
    build_network_card(grid, 0, 0, "⬇️ Total Downloaded", &lbl_rx_total);
    build_network_card(grid, 1, 0, "⬆️ Total Uploaded", &lbl_tx_total);
    build_network_card(grid, 2, 0, "⏱️ Google Ping", &lbl_ping);
    build_network_card(grid, 0, 1, "🏠 Local IP", &lbl_loc_ip);
    build_network_card(grid, 1, 1, "🌍 Public IP", &lbl_pub_ip);
    build_network_card(grid, 2, 1, "🏢 ISP", &lbl_isp);
    
    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 10);

    GtkWidget *lbl_conn = gtk_label_new("<b>Active Applications (Warden Analyzed)</b>");
    gtk_label_set_use_markup(GTK_LABEL(lbl_conn), TRUE);
    gtk_widget_set_halign(lbl_conn, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_conn, FALSE, FALSE, 5);
    
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    listbox_network = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox_network), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scroll), listbox_network);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    g_timeout_add(2000, update_network_ui, NULL);
    
    return vbox;
}