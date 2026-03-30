/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * nemo-tab-state.h: Persist and restore open tabs across Nemo sessions.
 *
 * Each NemoWindow saves its tab layout (per-pane URIs, active-tab index,
 * dual-pane mode) to ~/.config/nemo/tab-state whenever something changes.
 * On the next cold start – when the user launches Nemo with no explicit
 * URI arguments – the saved state is restored instead of opening the
 * default home directory.
 *
 * The feature is gated behind the "remember-open-tabs" GSettings key.
 * When the key is off the save/restore functions are no-ops.
 *
 * Copyright (C) 2025 Nemo contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef NEMO_TAB_STATE_H
#define NEMO_TAB_STATE_H

#include <glib.h>
#include "nemo-window.h"

G_BEGIN_DECLS

/*
 * nemo_tab_state_save:
 * @window: the NemoWindow whose state should be serialised.
 *
 * Serialises the open tabs for every pane in @window and writes the result
 * to ~/.config/nemo/tab-state.  Silently does nothing when the
 * "remember-open-tabs" preference is disabled, or while a restore is in
 * progress (nemo_tab_state_is_restoring() returns TRUE).
 */
void nemo_tab_state_save (NemoWindow *window);

/*
 * nemo_tab_state_is_restoring:
 *
 * Returns TRUE while nemo_tab_state_restore() is running.  Callers that
 * would otherwise trigger a save (e.g. update_for_new_location) should
 * check this and skip the save to avoid writing partial state mid-restore.
 */
gboolean nemo_tab_state_is_restoring (void);

/*
 * nemo_tab_state_restore_load_complete:
 *
 * Called by update_for_new_location() for every location-load completion
 * that was started during nemo_tab_state_restore().  Decrements the
 * internal pending-load counter; when it reaches zero, performs the final
 * save so the state file reflects the fully-restored layout.  The @window
 * argument is used only for that final save call.
 */
void nemo_tab_state_restore_load_complete (NemoWindow *window);

/*
 * nemo_tab_state_has_saved_state:
 *
 * Returns TRUE if a non-empty tab-state file exists and the
 * "remember-open-tabs" preference is enabled.  Used by the startup path
 * to decide whether to restore state instead of opening home.
 */
gboolean nemo_tab_state_has_saved_state (void);

/*
 * nemo_tab_state_restore:
 * @window: a freshly-constructed NemoWindow with a single empty slot.
 *
 * Reads ~/.config/nemo/tab-state and opens the saved tabs in @window.
 * Folders that no longer exist are silently replaced with the home
 * directory.  Remote locations that are not currently mounted are also
 * replaced with home (the user sees a normal home view rather than an
 * error).  Returns TRUE if any tabs were restored.
 */
gboolean nemo_tab_state_restore (NemoWindow *window);

/*
 * nemo_tab_state_delete:
 *
 * Removes the tab-state file.  Called when the feature is toggled off
 * so stale state is not accidentally restored if the user re-enables it.
 */
void nemo_tab_state_delete (void);

G_END_DECLS

#endif /* NEMO_TAB_STATE_H */
