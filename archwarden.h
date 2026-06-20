#ifndef ARCHWARDEN_H
#define ARCHWARDEN_H

#include <gtk/gtk.h>
#include <glib.h>

#define CHUNK_SIZE 100
#define DEBUG_MODE 1

typedef enum { PKG_UPDATE = 0, PKG_NORMAL = 1, PKG_ORPHAN = 2 } PkgStatus;
typedef enum { SRC_PACMAN, SRC_AUR, SRC_FLATPAK } PkgSource;

typedef struct {
    char name[256]; char version[256]; char new_version[256];
    char size_str[64]; char icon_name[256]; char desc[512];
    PkgStatus status; PkgSource source; gboolean is_gui;
    gboolean is_unstable; char warning_msg[128];
} PackageInfo;

typedef struct {
    char name[256]; char version[256]; char repo[128];
    char size_str[64]; char icon_name[256]; char desc[512];
    PkgSource source; gboolean is_gui; int popularity;
    gboolean is_unstable; char warning_msg[128];
} StorePackageInfo;

typedef struct {
    gboolean en_pacman; gboolean en_aur; gboolean en_flatpak;
    gboolean auto_update_cache;
} AppConfig;

// Global Variables
extern GList *local_cache;
extern GList *store_cache;
extern GList *filtered_local;
extern GList *filtered_store;
extern GHashTable *installed_hash;

extern GMutex sync_mutex;
extern GMutex error_mutex;
extern GMutex config_mutex;

extern char global_error_log[8192];
extern char store_cache_path[512];
extern char local_cache_path[512];
extern char config_path[512];
extern char debug_log_path[512];

extern gboolean is_processing_local;
extern gboolean is_processing_store;
extern AppConfig config;

extern int local_render_idx;
extern int store_render_idx;

// Backend Functions
void log_debug(const char *format, ...);
void safe_strcpy(char *dest, const char *src, size_t max_len);
void init_config(void);
void save_config(void);
void append_error(const char *err);
char* exec_cmd_sync(const char* cmd, int *exit_code);
gboolean is_gui_app(const char *name, const char *desc, PkgSource src);

// Cache Management Functions
void save_local_cache(void);
void load_local_cache(void);
void save_store_cache(void);
void load_store_cache(void);
gpointer fetch_local_thread(gpointer data);
gpointer fetch_store_thread(gpointer data);

// Warden AI Functions
void warden_check_stability(const char *name, const char *version, const char *desc, gboolean *is_unstable, char *reason);

// UI Functions called by Backend
void get_icon_for_pkg(const char *name, char *out_icon, PkgSource src);
gboolean apply_local_cache_ui(gpointer data);
gboolean apply_store_cache_ui(gpointer data);
void check_and_show_errors(void);

#endif