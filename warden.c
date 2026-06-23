#include "archwarden.h"
#include <string.h>

gboolean contains_keyword(const gchar *text, const char *keyword) {
    if (!text) return FALSE;
    return strstr(text, keyword) != NULL;
}

void warden_check_stability(const char *name, const char *version, const char *desc, gboolean *is_unstable, char *reason) {
    *is_unstable = FALSE; reason[0] = '\0';
    if (!name && !version) return;

    gchar *n_lower = name ? g_ascii_strdown(name, -1) : g_strdup("");
    gchar *v_lower = version ? g_ascii_strdown(version, -1) : g_strdup("");
    gchar *d_lower = desc ? g_ascii_strdown(desc, -1) : g_strdup("");

    if (g_str_has_suffix(n_lower, "-git") || g_str_has_suffix(n_lower, "-svn") || g_str_has_suffix(n_lower, "-nightly") || g_str_has_suffix(n_lower, "-dev")) {
        *is_unstable = TRUE; safe_strcpy(reason, "Dev Build (Unstable / Source)", 128);
    } else if (contains_keyword(v_lower, "alpha") || contains_keyword(v_lower, "pre-alpha")) {
        *is_unstable = TRUE; safe_strcpy(reason, "Alpha Release (Bugs Expected)", 128);
    } else if (contains_keyword(v_lower, "beta")) {
        *is_unstable = TRUE; safe_strcpy(reason, "Beta Release (Testing Phase)", 128);
    } else if (contains_keyword(v_lower, "rc") || contains_keyword(v_lower, "pre")) {
        *is_unstable = TRUE; safe_strcpy(reason, "Release Candidate (Pre-release)", 128);
    } else if (contains_keyword(d_lower, "deprecated") || contains_keyword(d_lower, "obsolete")) {
        *is_unstable = TRUE; safe_strcpy(reason, "Deprecated Package", 128);
    } else if (contains_keyword(d_lower, "experimental") || contains_keyword(d_lower, "unstable")) {
        *is_unstable = TRUE; safe_strcpy(reason, "Marked as Experimental", 128);
    }

    g_free(n_lower); g_free(v_lower); g_free(d_lower);
}

void warden_check_network_anomaly(const char *remote_addr, const char *process, gboolean *is_suspicious, char *reason) {
    *is_suspicious = FALSE; reason[0] = '\0';
    if (strstr(remote_addr, ":4444") || strstr(remote_addr, ":6667") || strstr(remote_addr, ":3333")) {
        *is_suspicious = TRUE; safe_strcpy(reason, "Suspicious Port Detected", 128);
    } else if (strlen(process) == 0 || strcmp(process, "unknown") == 0) {
        *is_suspicious = TRUE; safe_strcpy(reason, "Hidden Process Transmission", 128);
    } else if (strstr(process, "bash") || strstr(process, "sh") || strstr(process, "python")) {
        if (!strstr(remote_addr, "127.0.0.1") && !strstr(remote_addr, "::1")) {
            *is_suspicious = TRUE; safe_strcpy(reason, "Script making external connection", 128);
        }
    }
}