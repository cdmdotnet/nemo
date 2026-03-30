/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * nemo-tab-state.c: Persist and restore open tabs across Nemo sessions.
 *
 * File format  (~/.config/nemo/tab-state)
 * ----------------------------------------
 * Lines beginning with '#' are comments / version markers and are ignored
 * when reading.
 *
 *   # version=2
 *   dual_pane=<0|1>
 *   pane<N>_tabs=<uri>|<uri>|...     (N = 1 or 2)
 *   pane<N>_active=<index>
 *
 * Blank lines and unknown keys are silently ignored so that older Nemo
 * builds that write a subset of keys remain compatible.
 *
 * Copyright (C) 2025 Nemo contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "config.h"

#include "nemo-tab-state.h"

#include "nemo-window-pane.h"
#include "nemo-window-slot.h"
#include "nemo-window-private.h"

#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

#define TAB_STATE_FILE_NAME "tab-state"
#define TAB_STATE_VERSION   "2"
/* Separator between URIs within a single pane's tab list */
#define URI_SEPARATOR       "|"

/* Counts the number of location loads started by nemo_tab_state_restore()
 * that have not yet delivered their update_for_new_location() callback.
 * save is suppressed (via nemo_tab_state_is_restoring) while this is > 0.
 * Using a counter rather than a bool lets us handle multiple simultaneous
 * async loads correctly — restore starts N loads and each completion
 * decrements the counter; only when it hits zero is saving re-enabled. */
static gint restore_pending_loads = 0;

gboolean
nemo_tab_state_is_restoring (void)
{
        return restore_pending_loads > 0;
}

void
nemo_tab_state_restore_load_complete (NemoWindow *window)
{
        if (restore_pending_loads <= 0) {
                /* Should not happen, but be defensive. */
                return;
        }

        restore_pending_loads--;

        if (restore_pending_loads == 0) {
                /* All restore loads have completed — slot->location values are
                 * now fully current.  Write the definitive state file. */
                nemo_tab_state_save (window);
        }
}

static gchar *
get_tab_state_path (void)
{
        gchar *user_dir = nemo_get_user_directory ();
        gchar *path = g_build_filename (user_dir, TAB_STATE_FILE_NAME, NULL);
        g_free (user_dir);
        return path;
}

/*
 * Returns TRUE if @uri points to a location that Nemo can navigate to right
 * now.  For local paths this is a simple existence check.  For remote URIs
 * (smb://, sftp://, etc.) we check whether the GMount backing the root of
 * the URI is already in the system's mount table.  We intentionally do NOT
 * attempt to mount on-demand here because that would block the startup path
 * and potentially prompt the user for credentials before the UI is ready.
 */
static gboolean
uri_is_accessible (const gchar *uri)
{
        GFile *file;
        gboolean result = FALSE;

        if (uri == NULL || *uri == '\0') {
                return FALSE;
        }

        file = g_file_new_for_uri (uri);

        if (g_file_is_native (file)) {
                /* Local path – just check existence */
                result = g_file_query_exists (file, NULL);
        } else {
                /* Remote – accept if there is an existing mount for the URI.
                 * This covers network shares that were mounted in a previous
                 * session and are still active, while silently falling back to
                 * home for shares that have since been unmounted. */
                GMount *mount = g_file_find_enclosing_mount (file, NULL, NULL);
                if (mount != NULL) {
                        result = TRUE;
                        g_object_unref (mount);
                }
        }

        g_object_unref (file);
        return result;
}

/*
 * Sanitise a single URI: return a g_strdup'd copy of @uri when it is
 * accessible, otherwise return a g_strdup'd URI for the home directory.
 */
static gchar *
sanitise_uri (const gchar *uri)
{
        if (uri_is_accessible (uri)) {
                return g_strdup (uri);
        }
        /* Fall back to home */
        GFile *home = g_file_new_for_path (g_get_home_dir ());
        gchar *home_uri = g_file_get_uri (home);
        g_object_unref (home);
        return home_uri;
}

/* ------------------------------------------------------------------ */
/* Collecting state from the live window                               */
/* ------------------------------------------------------------------ */

/*
 * Build a '|'-separated string of URIs for every slot (tab) in @pane.
 * Returns a newly-allocated string.  The active-tab index is written to
 * *out_active_index.
 */
static gchar *
collect_pane_tabs (NemoWindowPane *pane, gint *out_active_index)
{
        GList *l;
        GPtrArray *uris;
        gint idx = 0;
        gint active_idx = 0;

        uris = g_ptr_array_new_with_free_func (g_free);

        for (l = pane->slots; l != NULL; l = l->next, idx++) {
                NemoWindowSlot *slot = NEMO_WINDOW_SLOT (l->data);
                GFile *location = nemo_window_slot_get_location (slot);

                if (location != NULL) {
                        gchar *uri = g_file_get_uri (location);

                        /* Skip search URIs – they are transient and meaningless
                         * to restore; use home instead. */
                        if (g_file_has_uri_scheme (location, "x-nemo-search")) {
                                g_free (uri);
                                GFile *home = g_file_new_for_path (g_get_home_dir ());
                                uri = g_file_get_uri (home);
                                g_object_unref (home);
                        }

                        g_ptr_array_add (uris, uri);
                        g_object_unref (location);
                } else {
                        /* Slot has no location yet (still loading) – use home */
                        GFile *home = g_file_new_for_path (g_get_home_dir ());
                        g_ptr_array_add (uris, g_file_get_uri (home));
                        g_object_unref (home);
                }

                if (slot == pane->active_slot) {
                        active_idx = idx;
                }
        }

        /* Guard: if the array is empty add a home entry so we always have at
         * least one tab to restore. */
        if (uris->len == 0) {
                GFile *home = g_file_new_for_path (g_get_home_dir ());
                g_ptr_array_add (uris, g_file_get_uri (home));
                g_object_unref (home);
                active_idx = 0;
        }

        *out_active_index = active_idx;

        /* NULL-terminate for g_strjoinv */
        g_ptr_array_add (uris, NULL);
        gchar *result = g_strjoinv (URI_SEPARATOR, (gchar **) uris->pdata);
        g_ptr_array_free (uris, TRUE);
        return result;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void
nemo_tab_state_save (NemoWindow *window)
{
        if (restore_pending_loads > 0) {
                return;
        }

        if (!g_settings_get_boolean (nemo_preferences,
                                     NEMO_PREFERENCES_REMEMBER_OPEN_TABS)) {
                return;
        }

        GList *panes = window->details->panes;
        if (panes == NULL) {
                return;
        }

        gboolean dual_pane = (g_list_length (panes) > 1);

        GString *content = g_string_new (NULL);
        g_string_append (content, "# version=" TAB_STATE_VERSION "\n");
        g_string_append_printf (content, "dual_pane=%d\n", dual_pane ? 1 : 0);

        gint pane_number = 1;
        GList *l;
        for (l = panes; l != NULL; l = l->next, pane_number++) {
                NemoWindowPane *pane = NEMO_WINDOW_PANE (l->data);
                gint active_idx = 0;
                gchar *tabs_str = collect_pane_tabs (pane, &active_idx);

                g_string_append_printf (content, "pane%d_tabs=%s\n",
                                        pane_number, tabs_str);
                g_string_append_printf (content, "pane%d_active=%d\n",
                                        pane_number, active_idx);
                g_free (tabs_str);
        }

        gchar *path = get_tab_state_path ();
        GError *error = NULL;

        if (!g_file_set_contents (path, content->str, -1, &error)) {
                g_warning ("nemo-tab-state: could not write %s: %s",
                           path, error->message);
                g_error_free (error);
        }

        g_free (path);
        g_string_free (content, TRUE);
}

gboolean
nemo_tab_state_has_saved_state (void)
{
        if (!g_settings_get_boolean (nemo_preferences,
                                     NEMO_PREFERENCES_REMEMBER_OPEN_TABS)) {
                return FALSE;
        }

        gchar *path = get_tab_state_path ();
        gboolean exists = g_file_test (path, G_FILE_TEST_EXISTS);
        g_free (path);
        return exists;
}

/*
 * Parse the key=value flat-file format.  Returns a new GHashTable
 * (string→string, both owned) or NULL on hard read error.
 * The caller must free the table with g_hash_table_destroy().
 */
static GHashTable *
parse_tab_state_file (const gchar *path)
{
        gchar *contents = NULL;
        GError *error = NULL;

        if (!g_file_get_contents (path, &contents, NULL, &error)) {
                g_debug ("nemo-tab-state: cannot read %s: %s",
                         path, error->message);
                g_error_free (error);
                return NULL;
        }

        GHashTable *kv = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, g_free);
        gchar **lines = g_strsplit (contents, "\n", -1);
        g_free (contents);

        for (gint i = 0; lines[i] != NULL; i++) {
                gchar *line = g_strstrip (lines[i]);

                /* Skip blank lines and comments */
                if (*line == '\0' || *line == '#') {
                        continue;
                }

                gchar *eq = strchr (line, '=');
                if (eq == NULL) {
                        continue;
                }

                gchar *key = g_strndup (line, (gsize)(eq - line));
                gchar *val = g_strdup (eq + 1);
                g_hash_table_insert (kv, key, val);
        }

        g_strfreev (lines);
        return kv;
}

gboolean
nemo_tab_state_restore (NemoWindow *window)
{
        if (!g_settings_get_boolean (nemo_preferences,
                                     NEMO_PREFERENCES_REMEMBER_OPEN_TABS)) {
                return FALSE;
        }

        gchar *path = get_tab_state_path ();
        GHashTable *kv = parse_tab_state_file (path);
        g_free (path);

        if (kv == NULL) {
                return FALSE;
        }

        /* ---- Read top-level keys ---- */
        const gchar *dual_str    = g_hash_table_lookup (kv, "dual_pane");
        gboolean     want_dual   = (dual_str != NULL && strcmp (dual_str, "1") == 0);

        const gchar *p1_tabs_str   = g_hash_table_lookup (kv, "pane1_tabs");
        const gchar *p1_active_str = g_hash_table_lookup (kv, "pane1_active");
        const gchar *p2_tabs_str   = g_hash_table_lookup (kv, "pane2_tabs");
        const gchar *p2_active_str = g_hash_table_lookup (kv, "pane2_active");

        if (p1_tabs_str == NULL || *p1_tabs_str == '\0') {
                g_hash_table_destroy (kv);
                return FALSE;
        }

        gint p1_active = (p1_active_str != NULL) ? atoi (p1_active_str) : 0;
        gint p2_active = (p2_active_str != NULL) ? atoi (p2_active_str) : 0;

        gchar **p1_uris   = g_strsplit (p1_tabs_str, URI_SEPARATOR, -1);
        gint    p1_count  = g_strv_length (p1_uris);

        gchar **p2_uris  = NULL;
        gint    p2_count = 0;
        gboolean do_pane2 = want_dual && p2_tabs_str != NULL && *p2_tabs_str != '\0';

        if (do_pane2) {
                p2_uris  = g_strsplit (p2_tabs_str, URI_SEPARATOR, -1);
                p2_count = g_strv_length (p2_uris);
        }

        /* Count every nemo_window_slot_open_location call we are about to make
         * so the pending-loads counter can be set before the first call.
         * update_for_new_location (where the save lives) fires asynchronously
         * via the view-load pipeline — potentially after this function returns.
         * Each completion decrements the counter; saves are suppressed while it
         * is > 0 so no partial state is written mid-restore. */
        gint total_loads = p1_count;          /* one open_location per pane1 tab */
        if (do_pane2) {
                total_loads += p2_count;      /* one open_location per pane2 tab */
        }

        if (total_loads == 0) {
                g_strfreev (p1_uris);
                if (p2_uris) g_strfreev (p2_uris);
                g_hash_table_destroy (kv);
                return FALSE;
        }

        /* If "Always start in dual-pane view" is on, nemo_window_split_view_on
         * ran during window construction and added pane2 before restore() was
         * called.  We must reconcile that with what the state file says:
         *
         *  save says dual_pane=0 → close the extra pane now before we do anything
         *                          else, so the window is single-pane when we
         *                          restore pane1's tabs.
         *
         *  save says dual_pane=1 → we will reuse the existing pane2 below rather
         *                          than calling split_view_on_for_restore (which
         *                          would create a third pane).  The construction
         *                          load for pane2 will also fire
         *                          update_for_new_location, so we bump the counter
         *                          by 1 to account for it. */
        if (nemo_window_split_view_showing (window)) {
                if (!do_pane2) {
                        nemo_window_split_view_off (window);
                } else {
                        /* Account for the construction load that will also complete. */
                        total_loads++;
                }
        }

        restore_pending_loads = total_loads;

        /* Block the "switch-page" signal on both panes' notebooks for the
         * entire restore.  gtk_notebook_set_current_page fires switch-page,
         * which calls nemo_window_set_active_slot → real_set_active_pane,
         * making pane2 the window's active pane and emitting "loading_uri".
         * That causes the global sidebar (pane==NULL) to update its tracking
         * and sidebar2 (locked to pane2) to lose sync with pane2's content.
         * We unblock after manually correcting active_slot and active_pane. */
        NemoWindowPane *pane1 = NEMO_WINDOW_PANE (window->details->panes->data);

        g_signal_handlers_block_matched (pane1->notebook,
                                         G_SIGNAL_MATCH_ID,
                                         g_signal_lookup ("switch-page",
                                                 GTK_TYPE_NOTEBOOK),
                                         0, NULL, NULL, NULL);

        /* ---- Restore pane 1 ---- */
        /* The window already has one pane with one slot when constructed.
         * Use that existing slot for tab[0] and open new slots for the rest. */
        NemoWindowSlot *first_slot = pane1->active_slot;

        if (p1_active >= p1_count) {
                p1_active = MAX (0, p1_count - 1);
        }

        if (p1_count > 0) {
                gchar *safe_uri = sanitise_uri (p1_uris[0]);
                GFile *loc = g_file_new_for_uri (safe_uri);
                nemo_window_slot_open_location (first_slot, loc, 0);
                g_object_unref (loc);
                g_free (safe_uri);
        }

        for (gint i = 1; i < p1_count; i++) {
                gchar *safe_uri = sanitise_uri (p1_uris[i]);
                GFile *loc = g_file_new_for_uri (safe_uri);
                NemoWindowSlot *slot =
                        nemo_window_pane_open_slot (pane1, NEMO_WINDOW_OPEN_SLOT_APPEND);
                nemo_window_slot_open_location (slot, loc, 0);
                g_object_unref (loc);
                g_free (safe_uri);
        }

        /* Set active tab by index — switch-page is blocked so active_slot
         * won't change yet; we fix it up manually below. */
        gtk_notebook_set_current_page (GTK_NOTEBOOK (pane1->notebook), p1_active);

        {
                GtkWidget *page = gtk_notebook_get_nth_page (
                        GTK_NOTEBOOK (pane1->notebook), p1_active);
                if (page != NULL) {
                        pane1->active_slot = NEMO_WINDOW_SLOT (page);
                }
        }

        g_strfreev (p1_uris);

        /* ---- Restore pane 2 (if needed) ---- */
        if (do_pane2) {
                if (p2_active >= p2_count) {
                        p2_active = MAX (0, p2_count - 1);
                }

                NemoWindowPane *pane2 = NULL;
                NemoWindowSlot *p2_first_slot = NULL;

                if (nemo_window_split_view_showing (window)) {
                        /* start_with_dual_pane already created pane2 — reuse it. */
                        GList *last = g_list_last (window->details->panes);
                        pane2 = NEMO_WINDOW_PANE (last->data);
                        p2_first_slot = pane2->active_slot;
                } else {
                        /* Normal case: no pre-existing pane2. */
                        p2_first_slot = nemo_window_split_view_on_for_restore (window);
                        if (p2_first_slot != NULL) {
                                pane2 = p2_first_slot->pane;
                        }
                }

                if (p2_first_slot != NULL && pane2 != NULL) {
                        g_signal_handlers_block_matched (pane2->notebook,
                                                         G_SIGNAL_MATCH_ID,
                                                         g_signal_lookup ("switch-page",
                                                                 GTK_TYPE_NOTEBOOK),
                                                         0, NULL, NULL, NULL);

                        if (p2_count > 0) {
                                gchar *safe_uri = sanitise_uri (p2_uris[0]);
                                GFile *loc = g_file_new_for_uri (safe_uri);
                                nemo_window_slot_open_location (p2_first_slot, loc, 0);
                                g_object_unref (loc);
                                g_free (safe_uri);
                        }

                        for (gint i = 1; i < p2_count; i++) {
                                gchar *safe_uri = sanitise_uri (p2_uris[i]);
                                GFile *loc = g_file_new_for_uri (safe_uri);
                                NemoWindowSlot *slot =
                                        nemo_window_pane_open_slot (pane2,
                                                                    NEMO_WINDOW_OPEN_SLOT_APPEND);
                                nemo_window_slot_open_location (slot, loc, 0);
                                g_object_unref (loc);
                                g_free (safe_uri);
                        }

                        gtk_notebook_set_current_page (GTK_NOTEBOOK (pane2->notebook), p2_active);

                        {
                                GtkWidget *page = gtk_notebook_get_nth_page (
                                        GTK_NOTEBOOK (pane2->notebook), p2_active);
                                if (page != NULL) {
                                        pane2->active_slot = NEMO_WINDOW_SLOT (page);
                                }
                        }

                        g_signal_handlers_unblock_matched (pane2->notebook,
                                                           G_SIGNAL_MATCH_ID,
                                                           g_signal_lookup ("switch-page",
                                                                   GTK_TYPE_NOTEBOOK),
                                                           0, NULL, NULL, NULL);
                }
        }

        /* Unblock pane1 and ensure it is the active pane when restore finishes
         * so the global sidebar tracks pane1 as expected. */
        g_signal_handlers_unblock_matched (pane1->notebook,
                                           G_SIGNAL_MATCH_ID,
                                           g_signal_lookup ("switch-page",
                                                   GTK_TYPE_NOTEBOOK),
                                           0, NULL, NULL, NULL);

        /* Directly assign active_pane to pane1 without going through
         * nemo_window_set_active_slot (which emits signals). */
        window->details->active_pane = pane1;

        if (p2_uris) {
                g_strfreev (p2_uris);
        }

        g_hash_table_destroy (kv);

        /* Do NOT clear restore_pending_loads here — the async loads fired above
         * have not completed yet.  update_for_new_location() in
         * nemo-window-manage-views.c decrements the counter on each completion
         * and triggers the save automatically when it reaches zero. */
        return TRUE;
}

void
nemo_tab_state_delete (void)
{
        gchar *path = get_tab_state_path ();
        if (g_file_test (path, G_FILE_TEST_EXISTS)) {
                if (g_remove (path) != 0) {
                        g_warning ("nemo-tab-state: could not delete %s", path);
                }
        }
        g_free (path);
}
