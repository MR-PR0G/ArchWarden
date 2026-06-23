#ifndef ARCHWARDEN_H
#define ARCHWARDEN_H

#include <gtk/gtk.h>
#include <glib.h>
#include <string.h>

#define CHUNK_SIZE 100
#define DEBUG_MODE 1

typedef enum { 
    PKG_UPDATE = 0, 
    PKG_NORMAL = 1, 
    PKG_ORPHAN = 2, 
    PKG_ORPHAN_USEFUL = 3 
} PkgStatus;

typedef enum { SRC_PACMAN, SRC_AUR, SRC_FLATPAK } PkgSource;

typedef struct {
    char name[256];
    char version[256];
    char new_version[256];
    char size_str[64];
    char icon_name[256];
    char desc[512];
    PkgStatus status;
    PkgSource source;
    gboolean is_gui;
    gboolean is_unstable;
    char warning_msg[128];
} PackageInfo;

typedef struct {
    char name[256];
    char version[256];
    char repo[128];
    char size_str[64];
    char icon_name[256];
    char desc[512];
    PkgSource source;
    gboolean is_gui;
    int popularity;
    gboolean is_unstable;
    char warning_msg[128];
} StorePackageInfo;

typedef struct {
    char proto[16];
    char local_addr[128];
    char remote_addr[128];
    char process_name[128];
    gboolean is_suspicious;
    char warning_msg[128];
} NetConnection;

typedef struct {
    gboolean en_pacman;
    gboolean en_aur;
    gboolean en_flatpak;
    gboolean auto_update_cache;
} AppConfig;

extern GList *local_cache;
extern GList *store_cache;
extern GList *filtered_local;
extern GList *filtered_store;
extern GList *network_connections;
extern GHashTable *installed_hash;

extern GMutex sync_mutex;
extern GMutex error_mutex;
extern GMutex config_mutex;
extern GMutex net_mutex;

extern char global_error_log[8192];
extern char store_cache_path[1024];
extern char local_cache_path[1024];
extern char config_path[1024];
extern char debug_log_path[1024];

extern gboolean is_processing_local;
extern gboolean is_processing_store;
extern AppConfig config;

extern int local_render_idx;
extern int store_render_idx;

extern double rx_history[60];
extern double tx_history[60];
extern double current_rx_speed;
extern double current_tx_speed;
extern char public_ip[64];
extern char local_ip[64];
extern char current_ping[64];
extern char isp_name[128];
extern unsigned long long last_rx_bytes;
extern unsigned long long last_tx_bytes;

extern const char *categories[7];

extern GtkWidget *listbox_local;
extern GtkWidget *listbox_store;
extern GtkWidget *listbox_network;
extern GtkWidget *search_entry_local;
extern GtkWidget *search_entry_store;
extern GtkWidget *progress_local;
extern GtkWidget *progress_store;
extern GtkWidget *action_bar_local;
extern GtkWidget *action_bar_store;
extern GtkWidget *lbl_selected_local;
extern GtkWidget *lbl_selected_store;
extern GtkWidget *box_tasks;
extern GtkWidget *main_content_stack;
extern GtkWidget *packages_stack;
extern GtkWidget *settings_revealer;
extern GtkWidget *sidebar_stack;
extern GtkWidget *popover_activity;

extern GtkWidget *lbl_rx_total;
extern GtkWidget *lbl_tx_total;
extern GtkWidget *lbl_ping;
extern GtkWidget *lbl_pub_ip;
extern GtkWidget *lbl_loc_ip;
extern GtkWidget *lbl_isp;
extern GtkWidget *network_chart;

extern gboolean f_loc_pacman;
extern gboolean f_loc_aur;
extern gboolean f_loc_flatpak;
extern gboolean f_loc_updates;
extern gboolean f_loc_orphans;
extern int f_loc_cat;

extern gboolean f_sto_pacman;
extern gboolean f_sto_aur;
extern gboolean f_sto_flatpak;
extern int f_sto_cat;

void log_debug(const char *format, ...);
void safe_strcpy(char *dest, const char *src, size_t max_len);
void init_config(void);
void save_config(void);
void append_error(const char *err);
char* exec_cmd_sync(const char* cmd, int *exit_code);
gboolean is_gui_app(const char *name, const char *desc, PkgSource src);
gboolean is_useful_orphan(const char *name);
void get_icon_for_pkg(const char *name, char *out_icon, PkgSource src);

void save_local_cache(void);
void load_local_cache(void);
void save_store_cache(void);
void load_store_cache(void);
gpointer fetch_local_thread(gpointer data);
gpointer fetch_store_thread(gpointer data);
gpointer network_monitor_thread(gpointer data);

void warden_check_stability(const char *name, const char *version, const char *desc, gboolean *is_unstable, char *reason);
void warden_check_network_anomaly(const char *remote_addr, const char *process, gboolean *is_suspicious, char *reason);

void run_task_cmd(const char *title, const char *cmd);
void trigger_task_from_args(GtkWidget *btn, gpointer data);
void check_and_show_errors(void);
void trigger_sync(GtkWidget *btn, gpointer data);

GtkWidget* build_ui_packages(void);
GtkWidget* build_ui_network(void);

gboolean apply_local_cache_ui(gpointer data);
gboolean apply_store_cache_ui(gpointer data);
void apply_local_filters(GtkEntry *entry, gpointer data);
void apply_store_filters(GtkEntry *entry, gpointer data);
void render_local_chunk(void);
void render_store_chunk(void);
void on_local_scroll(GtkScrolledWindow *sw, GtkPositionType pos, gpointer data);
void on_store_scroll(GtkScrolledWindow *sw, GtkPositionType pos, gpointer data);

#endif