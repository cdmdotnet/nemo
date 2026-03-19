/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

/*
 *  Nemo — Preview Pane
 *
 *  Copyright (C) 2025 Linux Mint
 *
 *  Nemo is free software; you can redistribute it and/or modify it under the
 *  terms of the GNU General Public License as published by the Free Software
 *  Foundation; either version 2 of the License, or (at your option) any
 *  later version.
 */

#ifndef NEMO_PREVIEW_PANE_H
#define NEMO_PREVIEW_PANE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NEMO_TYPE_PREVIEW_PANE         (nemo_preview_pane_get_type ())
#define NEMO_PREVIEW_PANE(o)           (G_TYPE_CHECK_INSTANCE_CAST  ((o), NEMO_TYPE_PREVIEW_PANE, NemoPreviewPane))
#define NEMO_PREVIEW_PANE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST     ((k), NEMO_TYPE_PREVIEW_PANE, NemoPreviewPaneClass))
#define NEMO_IS_PREVIEW_PANE(o)        (G_TYPE_CHECK_INSTANCE_TYPE  ((o), NEMO_TYPE_PREVIEW_PANE))
#define NEMO_IS_PREVIEW_PANE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE     ((k), NEMO_TYPE_PREVIEW_PANE))
#define NEMO_PREVIEW_PANE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS   ((o), NEMO_TYPE_PREVIEW_PANE, NemoPreviewPaneClass))

typedef struct _NemoPreviewPane        NemoPreviewPane;
typedef struct _NemoPreviewPaneClass   NemoPreviewPaneClass;
typedef struct _NemoPreviewPanePrivate NemoPreviewPanePrivate;

struct _NemoPreviewPane {
    GtkBox                  parent;
    NemoPreviewPanePrivate *priv;
};

struct _NemoPreviewPaneClass {
    GtkBoxClass parent_class;
};

GType      nemo_preview_pane_get_type (void) G_GNUC_CONST;
GtkWidget *nemo_preview_pane_new      (void);
void       nemo_preview_pane_load_uri (NemoPreviewPane *pane,
                                       const char      *uri,
                                       const char      *mime_type);
void       nemo_preview_pane_clear    (NemoPreviewPane *pane);

G_END_DECLS

#endif /* NEMO_PREVIEW_PANE_H */
