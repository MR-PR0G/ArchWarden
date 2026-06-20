#include "archwarden.h"
#include <string.h>
#include <ctype.h>

gboolean contains_keyword(const gchar *text, const char *keyword) {
    if (!text) return FALSE;
    return strstr(text, keyword) != NULL;
}

void warden_check_stability(const char *name, const char *version, const char *desc, gboolean *is_unstable, char *reason) {
    *is_unstable = FALSE;
    reason[0] = '\0';

    if (!name && !version) return;

    gchar *n_lower = name ? g_ascii_strdown(name, -1) : g_strdup("");
    gchar *v_lower = version ? g_ascii_strdown(version, -1) : g_strdup("");
    gchar *d_lower = desc ? g_ascii_strdown(desc, -1) : g_strdup("");

    // 1. Name Heuristics
    if (g_str_has_suffix(n_lower, "-git") || g_str_has_suffix(n_lower, "-svn") || 
        g_str_has_suffix(n_lower, "-hg") || g_str_has_suffix(n_lower, "-nightly") || 
        g_str_has_suffix(n_lower, "-dev")) {
        *is_unstable = TRUE;
        safe_strcpy(reason, "Dev Build (Unstable / Source)", 128);
    } 
    // 2. Semantic Versioning
    else if (contains_keyword(v_lower, "alpha") || contains_keyword(v_lower, "pre-alpha")) {
        *is_unstable = TRUE;
        safe_strcpy(reason, "Alpha Release (Bugs Expected)", 128);
    }
    else if (contains_keyword(v_lower, "beta")) {
        *is_unstable = TRUE;
        safe_strcpy(reason, "Beta Release (Testing Phase)", 128);
    }
    else if (contains_keyword(v_lower, "rc") || contains_keyword(v_lower, "pre")) {
        *is_unstable = TRUE;
        safe_strcpy(reason, "Release Candidate (Pre-release)", 128);
    }
    else if (g_str_has_prefix(v_lower, "0.0.") || g_str_has_prefix(v_lower, "0.1.")) {
        *is_unstable = TRUE;
        safe_strcpy(reason, "Early Stage (v0.x)", 128);
    }
    // 3. Description NLP
    else if (contains_keyword(d_lower, "deprecated") || contains_keyword(d_lower, "obsolete")) {
        *is_unstable = TRUE;
        safe_strcpy(reason, "Deprecated Package", 128);
    }
    else if (contains_keyword(d_lower, "experimental") || contains_keyword(d_lower, "unstable")) {
        *is_unstable = TRUE;
        safe_strcpy(reason, "Marked as Experimental", 128);
    }

    g_free(n_lower);
    g_free(v_lower);
    g_free(d_lower);
}