#include "archwarden.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <time.h>

// Global Variables
GList *local_cache = NULL;
GList *store_cache = NULL;
GList *filtered_local = NULL;
GList *filtered_store = NULL;
GHashTable *installed_hash = NULL;

GMutex sync_mutex;
GMutex error_mutex;
GMutex config_mutex;

char global_error_log[8192] = {0};
char store_cache_path[512] = {0};
char local_cache_path[512] = {0};
char config_path[512] = {0};
char debug_log_path[512] = "/tmp/archwarden_debug.log";

gboolean is_processing_local = FALSE;
gboolean is_processing_store = FALSE;
AppConfig config = { TRUE, TRUE, TRUE, FALSE };

int local_render_idx = 0;
int store_render_idx = 0;

void log_debug(const char *format, ...) {
    if (!DEBUG_MODE) return;
    FILE *f = fopen(debug_log_path, "a");
    if (!f) return;
    time_t now; time(&now);
    char *date = ctime(&now); date[strlen(date) - 1] = '\0';
    fprintf(f, "[%s] ", date);
    va_list args; va_start(args, format); vfprintf(f, format, args); va_end(args);
    fprintf(f, "\n"); fclose(f);
}

void safe_strcpy(char *dest, const char *src, size_t max_len) {
    if (!src) { dest[0] = '\0'; return; }
    char *valid = g_utf8_make_valid(src, -1);
    strncpy(dest, valid, max_len - 1);
    dest[max_len - 1] = '\0';
    g_free(valid);
}

void init_config() {
    g_mutex_init(&error_mutex);
    g_mutex_init(&sync_mutex);
    g_mutex_init(&config_mutex);
    
    const char *home = g_get_home_dir();
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.config/archwarden", home);
    mkdir(dir, 0755);
    snprintf(config_path, sizeof(config_path), "%s/settings.ini", dir);
    snprintf(store_cache_path, sizeof(store_cache_path), "%s/store_cache.txt", dir);
    snprintf(local_cache_path, sizeof(local_cache_path), "%s/local_cache.txt", dir);
    
    FILE *f = fopen(config_path, "r");
    if (f) {
        int ep = 1, ea = 1, ef = 1, au = 0;
        if (fscanf(f, "pacman=%d\naur=%d\nflatpak=%d\nauto_upd=%d\n", &ep, &ea, &ef, &au) == 4) {
            config.en_pacman = ep; config.en_aur = ea; 
            config.en_flatpak = ef; config.auto_update_cache = au;
        }
        fclose(f);
    }
}

void save_config() {
    g_mutex_lock(&config_mutex);
    FILE *f = fopen(config_path, "w");
    if (f) {
        fprintf(f, "pacman=%d\naur=%d\nflatpak=%d\nauto_upd=%d\n", 
                config.en_pacman, config.en_aur, config.en_flatpak, config.auto_update_cache);
        fclose(f);
    }
    g_mutex_unlock(&config_mutex);
}

void append_error(const char *err) {
    log_debug("ERROR: %s", err);
    g_mutex_lock(&error_mutex);
    strncat(global_error_log, err, sizeof(global_error_log) - strlen(global_error_log) - 1);
    strncat(global_error_log, "\n", sizeof(global_error_log) - strlen(global_error_log) - 1);
    g_mutex_unlock(&error_mutex);
}

char* exec_cmd_sync(const char* cmd, int *exit_code) {
    gchar *std_out = NULL;
    gchar *std_err = NULL;
    gint status = 0;
    
    gchar *argv[] = { "sh", "-c", (gchar*)cmd, NULL };
    gboolean res = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &std_out, &std_err, &status, NULL);
    
    if (exit_code) *exit_code = res ? WEXITSTATUS(status) : -1;
    g_free(std_err); 
    
    if (res && std_out) {
        char *valid = g_utf8_make_valid(std_out, -1);
        g_free(std_out);
        return valid;
    }
    return g_strdup("");
}

gboolean is_gui_app(const char *name, const char *desc, PkgSource src) {
    if (src == SRC_FLATPAK) return TRUE;
    if (desc) {
        gchar *d = g_ascii_strdown(desc, -1);
        gboolean res = (strstr(d, "gui") || strstr(d, "desktop") || strstr(d, "gtk") || strstr(d, "qt") || 
            strstr(d, "browser") || strstr(d, "player") || strstr(d, "editor") || strstr(d, "game") || strstr(d, "video"));
        g_free(d);
        return res;
    }
    return FALSE;
}

// Caching functions
void save_local_cache() {
    g_mutex_lock(&sync_mutex);
    FILE *f = fopen(local_cache_path, "w");
    if (f) {
        for (GList *it = local_cache; it != NULL; it = it->next) {
            PackageInfo *p = (PackageInfo *)it->data;
            fprintf(f, "%s|%s|%s|%s|%d|%d|%d|%d|%s|%s\n", 
                p->name, p->version, p->new_version, p->size_str, 
                p->source, p->status, p->is_gui, p->is_unstable, 
                p->warning_msg[0] ? p->warning_msg : "none", p->desc);
        }
        fclose(f);
    }
    g_mutex_unlock(&sync_mutex);
}

void load_local_cache() {
    g_mutex_lock(&sync_mutex);
    FILE *f = fopen(local_cache_path, "r");
    if (f) {
        char line[4096];
        while (fgets(line, sizeof(line), f)) {
            g_strstrip(line);
            if (strlen(line) < 3) continue;
            char **parts = g_strsplit(line, "|", 10);
            if (g_strv_length(parts) == 10) {
                PackageInfo *p = g_new0(PackageInfo, 1);
                g_strlcpy(p->name, parts[0], 256);
                g_strlcpy(p->version, parts[1], 256);
                g_strlcpy(p->new_version, parts[2], 256);
                g_strlcpy(p->size_str, parts[3], 64);
                p->source = atoi(parts[4]);
                p->status = atoi(parts[5]);
                p->is_gui = atoi(parts[6]);
                p->is_unstable = atoi(parts[7]);
                if (strcmp(parts[8], "none") != 0) g_strlcpy(p->warning_msg, parts[8], 128);
                g_strlcpy(p->desc, parts[9], 512);
                get_icon_for_pkg(p->name, p->icon_name, p->source);
                local_cache = g_list_prepend(local_cache, p);
            }
            g_strfreev(parts);
        }
        fclose(f);
        local_cache = g_list_reverse(local_cache);
    }
    g_mutex_unlock(&sync_mutex);
}

void save_store_cache() {
    g_mutex_lock(&sync_mutex);
    FILE *f = fopen(store_cache_path, "w");
    if (f) {
        for (GList *it = store_cache; it != NULL; it = it->next) {
            StorePackageInfo *p = (StorePackageInfo *)it->data;
            fprintf(f, "%s|%s|%s|%s|%d|%d|%d|%d|%s|%s\n", 
                p->repo, p->name, p->version, p->size_str, 
                p->source, p->is_gui, p->popularity, p->is_unstable,
                p->warning_msg[0] ? p->warning_msg : "none", p->desc);
        }
        fclose(f);
    }
    g_mutex_unlock(&sync_mutex);
}

void load_store_cache() {
    g_mutex_lock(&sync_mutex);
    FILE *f = fopen(store_cache_path, "r");
    if (f) {
        char line[4096];
        while (fgets(line, sizeof(line), f)) {
            g_strstrip(line);
            if (strlen(line) < 3) continue;
            char **parts = g_strsplit(line, "|", 10);
            if (g_strv_length(parts) == 10) {
                StorePackageInfo *pkg = g_new0(StorePackageInfo, 1);
                g_strlcpy(pkg->repo, parts[0], 128);
                g_strlcpy(pkg->name, parts[1], 256);
                g_strlcpy(pkg->version, parts[2], 256);
                g_strlcpy(pkg->size_str, parts[3], 64);
                pkg->source = atoi(parts[4]);
                pkg->is_gui = atoi(parts[5]);
                pkg->popularity = atoi(parts[6]);
                pkg->is_unstable = atoi(parts[7]);
                if (strcmp(parts[8], "none") != 0) g_strlcpy(pkg->warning_msg, parts[8], 128);
                g_strlcpy(pkg->desc, parts[9], 512);
                get_icon_for_pkg(pkg->name, pkg->icon_name, pkg->source);
                store_cache = g_list_prepend(store_cache, pkg);
            }
            g_strfreev(parts);
        }
        fclose(f);
        store_cache = g_list_reverse(store_cache);
    }
    g_mutex_unlock(&sync_mutex);
}

// Fetch Threads
gpointer fetch_local_thread(gpointer data) {
    GList *new_list = NULL;
    int err = 0;
    
    char *aur_list = exec_cmd_sync("pacman -Qqm 2>/dev/null", NULL);

    if (config.en_pacman || config.en_aur) {
        char *pac = exec_cmd_sync("LANG=C pacman -Qi 2>/dev/null | awk -F':' '/^Name/ {n=$2; gsub(/^[ \\t]+/, \"\", n)} /^Version/ {v=$2; gsub(/^[ \\t]+/, \"\", v)} /^Description/ {d=$2; gsub(/^[ \\t]+/, \"\", d)} /^Installed Size/ {s=$2; gsub(/^[ \\t]+/, \"\", s); print n\"|\"v\"|\"s\"|\"d}'", &err);
        if (err != 0) append_error("- Installed: Pacman status sync failed.");
        
        char *line = strtok(pac, "\n");
        while(line) {
            char **parts = g_strsplit(line, "|", 4);
            if (g_strv_length(parts) == 4) {
                PackageInfo *p = g_new0(PackageInfo, 1);
                safe_strcpy(p->name, parts[0], 256);
                safe_strcpy(p->version, parts[1], 256);
                safe_strcpy(p->size_str, parts[2], 64);
                safe_strcpy(p->desc, parts[3], 512);
                p->status = PKG_NORMAL;
                
                char srch[260]; g_snprintf(srch, sizeof(srch), "%s\n", p->name);
                if (aur_list && (strstr(aur_list, srch) || strstr(aur_list, p->name) == aur_list)) p->source = SRC_AUR;
                else p->source = SRC_PACMAN;
                
                if ((p->source == SRC_PACMAN && config.en_pacman) || (p->source == SRC_AUR && config.en_aur)) {
                    p->is_gui = is_gui_app(p->name, p->desc, p->source);
                    get_icon_for_pkg(p->name, p->icon_name, p->source);
                    warden_check_stability(p->name, p->version, p->desc, &p->is_unstable, p->warning_msg);
                    new_list = g_list_prepend(new_list, p);
                } else {
                    g_free(p);
                }
            }
            g_strfreev(parts);
            line = strtok(NULL, "\n");
        }
        free(pac);
    }

    if (config.en_flatpak) {
        char *flp = exec_cmd_sync("timeout 15 flatpak list --app --columns=application,version,description 2>/dev/null", &err);
        if (err != 0) append_error("- Installed: Flatpak command failed.");
        char *line = strtok(flp, "\n");
        while(line) {
            char **parts = g_strsplit(line, "\t", 3);
            if (g_strv_length(parts) >= 2) {
                PackageInfo *p = g_new0(PackageInfo, 1);
                safe_strcpy(p->name, g_strstrip(parts[0]), 256); 
                safe_strcpy(p->version, g_strstrip(parts[1]), 256);
                if (g_strv_length(parts) == 3) safe_strcpy(p->desc, g_strstrip(parts[2]), 512);
                strcpy(p->size_str, "Flatpak");
                p->source = SRC_FLATPAK; p->status = PKG_NORMAL; p->is_gui = TRUE;
                get_icon_for_pkg(p->name, p->icon_name, p->source);
                warden_check_stability(p->name, p->version, p->desc, &p->is_unstable, p->warning_msg);
                new_list = g_list_prepend(new_list, p);
            }
            g_strfreev(parts);
            line = strtok(NULL, "\n");
        }
        free(flp);
    }

    char *upd = exec_cmd_sync("timeout 15 checkupdates 2>/dev/null; timeout 15 yay -Qu 2>/dev/null; timeout 15 flatpak remote-ls --updates --columns=application,version 2>/dev/null", NULL);
    GHashTable *upd_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (strlen(upd) > 0) {
        char *u_line = strtok(upd, "\n");
        while (u_line) {
            char un[256], uo[256], unew[256];
            if (sscanf(u_line, "%255s %255s -> %255s", un, uo, unew) == 3) g_hash_table_insert(upd_hash, g_strdup(un), g_strdup(unew));
            else if (sscanf(u_line, "%255s %255s", un, unew) == 2) g_hash_table_insert(upd_hash, g_strdup(un), g_strdup(unew));
            u_line = strtok(NULL, "\n");
        }
    }

    char *orp = exec_cmd_sync("timeout 10 pacman -Qdtq 2>/dev/null", NULL);
    GHashTable *orp_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (strlen(orp) > 0) {
        char *o_line = strtok(orp, "\n");
        while (o_line) {
            g_hash_table_insert(orp_hash, g_strdup(o_line), GINT_TO_POINTER(1));
            o_line = strtok(NULL, "\n");
        }
    }

    g_mutex_lock(&sync_mutex);
    for (GList *it = new_list; it != NULL; it = it->next) {
        PackageInfo *pkg = (PackageInfo *)it->data;
        if (g_hash_table_contains(orp_hash, pkg->name)) pkg->status = PKG_ORPHAN;
        
        char *new_ver = g_hash_table_lookup(upd_hash, pkg->name);
        if (new_ver) {
            pkg->status = PKG_UPDATE;
            safe_strcpy(pkg->new_version, new_ver, 256);
            warden_check_stability(pkg->name, pkg->new_version, pkg->desc, &pkg->is_unstable, pkg->warning_msg);
        } else if (strlen(upd) == 0 && local_cache != NULL) {
            for (GList *old_it = local_cache; old_it != NULL; old_it = old_it->next) {
                PackageInfo *old_pkg = (PackageInfo *)old_it->data;
                if (strcmp(old_pkg->name, pkg->name) == 0 && old_pkg->status == PKG_UPDATE) {
                    pkg->status = PKG_UPDATE;
                    safe_strcpy(pkg->new_version, old_pkg->new_version, 256);
                    pkg->is_unstable = old_pkg->is_unstable;
                    strcpy(pkg->warning_msg, old_pkg->warning_msg);
                    break;
                }
            }
        }
    }
    g_mutex_unlock(&sync_mutex);

    g_hash_table_destroy(upd_hash); g_hash_table_destroy(orp_hash);
    free(upd); free(orp); free(aur_list);
    
    g_idle_add(apply_local_cache_ui, new_list);
    return NULL;
}

gpointer fetch_store_thread(gpointer data) {
    GList *new_list = NULL;
    int err = 0;

    if (config.en_pacman) {
        char *pac = exec_cmd_sync("LANG=C pacman -Si 2>/dev/null | awk -F':' '/^Repository/ {r=$2; gsub(/^[ \\t]+/, \"\", r)} /^Name/ {n=$2; gsub(/^[ \\t]+/, \"\", n)} /^Version/ {v=$2; gsub(/^[ \\t]+/, \"\", v)} /^Description/ {d=$2; gsub(/^[ \\t]+/, \"\", d)} /^Download Size/ {s=$2; gsub(/^[ \\t]+/, \"\", s); print r\"|\"n\"|\"v\"|\"s\"|\"d}'", &err);
        if (err != 0) append_error("- Browse: Pacman remote sync failed.");
        char *line = strtok(pac, "\n");
        while(line) {
            char **parts = g_strsplit(line, "|", 5);
            if (g_strv_length(parts) == 5) {
                StorePackageInfo *p = g_new0(StorePackageInfo, 1);
                safe_strcpy(p->repo, parts[0], 128); safe_strcpy(p->name, parts[1], 256); 
                safe_strcpy(p->version, parts[2], 256); safe_strcpy(p->size_str, parts[3], 64);
                safe_strcpy(p->desc, parts[4], 512);
                p->source = SRC_PACMAN; p->popularity = rand() % 1000;
                p->is_gui = is_gui_app(p->name, p->desc, p->source);
                get_icon_for_pkg(p->name, p->icon_name, p->source);
                warden_check_stability(p->name, p->version, p->desc, &p->is_unstable, p->warning_msg);
                new_list = g_list_prepend(new_list, p);
            }
            g_strfreev(parts);
            line = strtok(NULL, "\n");
        }
        free(pac);
    }

    if (config.en_flatpak) {
        char *flp = exec_cmd_sync("timeout 15 flatpak remote-ls flathub --app --columns=application,version,description 2>/dev/null", &err);
        if (err != 0) append_error("- Browse: Flatpak remote sync failed. (Check flathub config).");
        char *line = strtok(flp, "\n");
        while(line) {
            char **parts = g_strsplit(line, "\t", 3);
            if (g_strv_length(parts) >= 2) {
                StorePackageInfo *p = g_new0(StorePackageInfo, 1);
                strcpy(p->repo, "flathub"); safe_strcpy(p->name, g_strstrip(parts[0]), 256); 
                safe_strcpy(p->version, g_strstrip(parts[1]), 256);
                if (g_strv_length(parts) == 3) safe_strcpy(p->desc, g_strstrip(parts[2]), 512);
                strcpy(p->size_str, "Flatpak");
                p->source = SRC_FLATPAK; p->popularity = rand() % 1000 + 500; p->is_gui = TRUE;
                get_icon_for_pkg(p->name, p->icon_name, p->source);
                warden_check_stability(p->name, p->version, p->desc, &p->is_unstable, p->warning_msg);
                new_list = g_list_prepend(new_list, p);
            }
            g_strfreev(parts);
            line = strtok(NULL, "\n");
        }
        free(flp);
    }

    if (config.en_aur) {
        char *aur = exec_cmd_sync("curl -sfL --connect-timeout 5 --max-time 15 https://aur.archlinux.org/packages.gz | gunzip -c 2>/dev/null", &err);
        if (err != 0 || strlen(aur) < 10) append_error("- Browse: AUR archive sync failed (curl error/network).");
        char *line = strtok(aur, "\n");
        while(line && strlen(line) > 1) {
            StorePackageInfo *p = g_new0(StorePackageInfo, 1);
            strcpy(p->repo, "aur"); safe_strcpy(p->name, line, 256); 
            strcpy(p->version, "AUR"); strcpy(p->size_str, "Unknown");
            strcpy(p->desc, "AUR User Package");
            p->source = SRC_AUR; p->popularity = 0; p->is_gui = FALSE; 
            get_icon_for_pkg(p->name, p->icon_name, p->source);
            warden_check_stability(p->name, p->version, p->desc, &p->is_unstable, p->warning_msg);
            new_list = g_list_prepend(new_list, p);
            line = strtok(NULL, "\n");
        }
        free(aur);
    }

    new_list = g_list_reverse(new_list);
    g_idle_add(apply_store_cache_ui, new_list);
    return NULL;
}