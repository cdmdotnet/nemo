/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  Nemo — Preview Pane
 *
 *  A right-hand panel that renders a preview of the currently selected file.
 *
 *  All optional renderer libraries (GStreamer, WebKit2GTK, libevview) are
 *  loaded at *runtime* via GModule (dlopen).  The code compiles cleanly even
 *  when none of the dev headers are installed.  If a library is absent at
 *  runtime the pane shows a styled "missing dependencies" notice with the
 *  apt install command needed to enable that renderer.
 *
 *  Enable debug logging with:
 *    NEMO_DEBUG=PreviewPane nemo
 *
 *  Copyright (C) 2025 Linux Mint
 *  License: GNU GPL v2 or later.
 */

#include <config.h>
#include "nemo-preview-pane.h"

/* Nemo debug infrastructure */
#define DEBUG_FLAG NEMO_DEBUG_PREVIEW_PANE
#include <libnemo-private/nemo-debug.h>

#include <glib/gi18n.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gmodule.h>
#include <gio/gio.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* GDK_WINDOW_XID is only available on X11 backends */
#ifdef GDK_WINDOWING_X11
#  include <gdk/gdkx.h>
#endif


/* ═══════════════════════════════════════════════════════════════════════════
 *  Runtime library availability flags
 *  Checked once on first use via check_runtime_deps().
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    RUNTIME_UNKNOWN   = 0,
    RUNTIME_AVAILABLE = 1,
    RUNTIME_MISSING   = 2,
} RuntimeState;

static RuntimeState runtime_gstreamer = RUNTIME_UNKNOWN;
static RuntimeState runtime_webkit    = RUNTIME_UNKNOWN;
static RuntimeState runtime_evview    = RUNTIME_UNKNOWN;

/* Try to open a shared library by one of several candidate names.
 * Returns TRUE and sets *out_module if found, FALSE otherwise.
 * The caller should NOT close the module — we keep it open for the
 * lifetime of the process. */
static gboolean
try_load_library (const char * const *candidates, GModule **out_module)
{
    for (int i = 0; candidates[i] != NULL; i++) {
        GModule *m = g_module_open (candidates[i], G_MODULE_BIND_LAZY);
        if (m) {
            DEBUG ("runtime dep found: %s", candidates[i]);
            if (out_module) *out_module = m;
            return TRUE;
        }
        DEBUG ("runtime dep not found: %s", candidates[i]);
    }
    return FALSE;
}

static void
check_runtime_gstreamer (void)
{
    if (runtime_gstreamer != RUNTIME_UNKNOWN) return;

    const char *candidates[] = {
        "libgstreamer-1.0.so.0",
        "libgstreamer-1.0.so",
        NULL
    };
    runtime_gstreamer = try_load_library (candidates, NULL)
                        ? RUNTIME_AVAILABLE : RUNTIME_MISSING;

    if (runtime_gstreamer == RUNTIME_AVAILABLE) {
        DEBUG ("GStreamer: available at runtime");
    } else {
        DEBUG ("GStreamer: NOT available at runtime");
    }
}

static void
check_runtime_webkit (void)
{
    if (runtime_webkit != RUNTIME_UNKNOWN) return;

    const char *candidates[] = {
        "libwebkit2gtk-4.1.so.0",
        "libwebkit2gtk-4.0.so.37",
        "libwebkit2gtk-4.0.so",
        NULL
    };
    runtime_webkit = try_load_library (candidates, NULL)
                     ? RUNTIME_AVAILABLE : RUNTIME_MISSING;

    if (runtime_webkit == RUNTIME_AVAILABLE) {
        DEBUG ("WebKit2GTK: available at runtime");
    } else {
        DEBUG ("WebKit2GTK: NOT available at runtime");
    }
}

static void
check_runtime_evview (void)
{
    if (runtime_evview != RUNTIME_UNKNOWN) return;

    const char *candidates[] = {
        "libevview3.so.3",
        "libevview3.so",
        NULL
    };
    runtime_evview = try_load_library (candidates, NULL)
                     ? RUNTIME_AVAILABLE : RUNTIME_MISSING;

    if (runtime_evview == RUNTIME_AVAILABLE) {
        DEBUG ("libevview: available at runtime");
    } else {
        DEBUG ("libevview: NOT available at runtime");
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  Page identifiers for the GtkStack
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    PREVIEW_PAGE_EMPTY = 0,   /* plain grey — unsupported type / no selection */
    PREVIEW_PAGE_MISSING_DEP, /* styled notice — runtime dep absent            */
    PREVIEW_PAGE_IMAGE,
    PREVIEW_PAGE_AUDIO,
    PREVIEW_PAGE_VIDEO,
    PREVIEW_PAGE_PDF,
    PREVIEW_PAGE_WEB,
} PreviewPage;


/* ═══════════════════════════════════════════════════════════════════════════
 *  Private instance data
 * ═══════════════════════════════════════════════════════════════════════════ */

struct _NemoPreviewPanePrivate {

    /* ── Stack ─────────────────────────────────────────────────────────── */
    GtkWidget  *stack;

    /* ── Page: EMPTY ───────────────────────────────────────────────────── */
    GtkWidget  *empty_box;

    /* ── Page: MISSING_DEP ─────────────────────────────────────────────── */
    GtkWidget  *missing_box;
    GtkWidget  *missing_title;
    GtkWidget  *missing_body;
    GtkWidget  *missing_cmd_label;
    GtkWidget  *missing_copy_btn;
    gchar      *missing_cmd_text;

    /* ── Page: IMAGE ───────────────────────────────────────────────────── */
    GtkWidget  *image_scroll;
    GtkWidget  *image_drawing_area;
    GdkPixbuf  *image_pixbuf;

    /* ── Page: AUDIO ───────────────────────────────────────────────────── */
    GtkWidget  *audio_box;
    GtkWidget  *audio_label;
    GtkWidget  *audio_play_btn;
    GtkWidget  *audio_seek;
    GtkWidget  *audio_time_label;

    /* ── Page: VIDEO ───────────────────────────────────────────────────── */
    GtkWidget  *video_box;
    GtkWidget  *video_area;         /* current widget (may be gtksink) */
    GtkWidget  *video_area_orig;    /* original GtkDrawingArea         */
    GtkWidget  *video_play_btn;
    GtkWidget  *video_seek;
    GtkWidget  *video_time_label;

    /* ── Page: PDF ─────────────────────────────────────────────────────── */
    GtkWidget  *pdf_scroll;
    GtkWidget  *pdf_view;

    /* ── Page: WEB ─────────────────────────────────────────────────────── */
    GtkWidget  *web_view;

    /* ── GStreamer state (runtime-loaded) ───────────────────────────────── */
    gpointer    gst_pipeline;    /* GstElement* cast to gpointer             */
    guint       gst_bus_watch_id;
    gboolean    gst_playing;
    gdouble     gst_duration;
    guint       gst_tick_id;
    gboolean    gst_seeking;

    /* ── LibreOffice conversion ─────────────────────────────────────────── */
    gchar      *lo_tmp_dir;
    gchar      *lo_html_path;
    GPid        lo_pid;
    guint       lo_watch_id;
    gulong      lo_web_cleanup_handler_id; /* load-changed signal handler, or 0 */

    /* ── Misc ───────────────────────────────────────────────────────────── */
    gchar      *current_uri;
    PreviewPage current_page;
};

G_DEFINE_TYPE_WITH_PRIVATE (NemoPreviewPane, nemo_preview_pane, GTK_TYPE_BOX)


/* ═══════════════════════════════════════════════════════════════════════════
 *  Forward declarations
 * ═══════════════════════════════════════════════════════════════════════════ */

static void preview_show_page        (NemoPreviewPane *, PreviewPage);
static void preview_clear_media      (NemoPreviewPane *);
static void preview_load_image       (NemoPreviewPane *, const char *);
static void preview_load_audio       (NemoPreviewPane *, const char *);
static void preview_load_video       (NemoPreviewPane *, const char *);
static void preview_load_pdf         (NemoPreviewPane *, const char *);
static void preview_load_web         (NemoPreviewPane *, const char *);
static void preview_load_office      (NemoPreviewPane *, const char *);
static void lo_disconnect_web_cleanup (NemoPreviewPane *);
static void preview_show_missing_dep (NemoPreviewPane *,
                                      const char *title,
                                      const char *body,
                                      const char *install_cmd);


/* ═══════════════════════════════════════════════════════════════════════════
 *  MIME helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static gboolean mime_is_image  (const char *m) { return m && g_str_has_prefix (m, "image/"); }
static gboolean mime_is_audio  (const char *m) { return m && g_str_has_prefix (m, "audio/"); }
static gboolean mime_is_video  (const char *m) { return m && g_str_has_prefix (m, "video/"); }
static gboolean mime_is_pdf    (const char *m) { return m && g_strcmp0 (m, "application/pdf") == 0; }
static gboolean mime_is_html   (const char *m) {
    return m && (g_strcmp0 (m, "text/html") == 0 ||
                 g_strcmp0 (m, "application/xhtml+xml") == 0);
}
static gboolean mime_is_office (const char *m) {
    if (!m) return FALSE;
    if (g_str_has_prefix (m, "application/vnd.oasis.opendocument."))            return TRUE;
    if (g_str_has_prefix (m, "application/vnd.openxmlformats-officedocument.")) return TRUE;
    if (g_strcmp0 (m, "application/msword") == 0)            return TRUE;
    if (g_strcmp0 (m, "application/vnd.ms-excel") == 0)      return TRUE;
    if (g_strcmp0 (m, "application/vnd.ms-powerpoint") == 0) return TRUE;
    return FALSE;
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  Stack / page switching
 * ═══════════════════════════════════════════════════════════════════════════ */

static void
preview_show_page (NemoPreviewPane *self, PreviewPage page)
{
    NemoPreviewPanePrivate *p = self->priv;
    const char *name = "empty";

    switch (page) {
        case PREVIEW_PAGE_MISSING_DEP: name = "missing"; break;
        case PREVIEW_PAGE_IMAGE:       name = "image";   break;
        case PREVIEW_PAGE_AUDIO:       name = "audio";   break;
        case PREVIEW_PAGE_VIDEO:       name = "video";   break;
        case PREVIEW_PAGE_PDF:         name = "pdf";     break;
        case PREVIEW_PAGE_WEB:         name = "web";     break;
        default:                       name = "empty";   break;
    }

    DEBUG ("switching to page '%s'", name);
    gtk_stack_set_visible_child_name (GTK_STACK (p->stack), name);
    p->current_page = page;
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  "Missing dependency" notice  (SVG + selectable command + copy button)
 * ═══════════════════════════════════════════════════════════════════════════ */

static const char MISSING_SVG[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 120 120\" width=\"96\" height=\"96\">"
    "  <defs>"
    "    <linearGradient id=\"bg\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">"
    "      <stop offset=\"0\" stop-color=\"#7986CB\" stop-opacity=\"0.18\"/>"
    "      <stop offset=\"1\" stop-color=\"#5C6BC0\" stop-opacity=\"0.28\"/>"
    "    </linearGradient>"
    "    <linearGradient id=\"piece\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">"
    "      <stop offset=\"0\" stop-color=\"#9FA8DA\"/>"
    "      <stop offset=\"1\" stop-color=\"#7986CB\"/>"
    "    </linearGradient>"
    "  </defs>"
    "  <circle cx=\"60\" cy=\"60\" r=\"56\" fill=\"url(#bg)\""
    "          stroke=\"#7986CB\" stroke-width=\"1.5\" stroke-opacity=\"0.35\"/>"
    "  <path fill=\"url(#piece)\" opacity=\"0.85\""
    "    d=\"M30 44 L30 38 Q30 32 36 32 L52 32 Q52 24 60 24 Q68 24 68 32 L84 32"
    "       Q90 32 90 38 L90 54 Q98 54 98 62 Q98 70 90 70 L90 86"
    "       Q90 92 84 92 L68 92 Q68 100 60 100 Q52 100 52 92 L36 92"
    "       Q30 92 30 86 L30 70 Q22 70 22 62 Q22 54 30 54 Z\"/>"
    "  <circle cx=\"75\" cy=\"45\" r=\"14\" fill=\"none\""
    "          stroke=\"white\" stroke-width=\"4\" stroke-opacity=\"0.92\"/>"
    "  <line x1=\"85\" y1=\"55\" x2=\"96\" y2=\"67\""
    "        stroke=\"white\" stroke-width=\"4\" stroke-linecap=\"round\" stroke-opacity=\"0.92\"/>"
    "  <text x=\"75\" y=\"51\" text-anchor=\"middle\""
    "        font-family=\"sans-serif\" font-size=\"14\" font-weight=\"bold\""
    "        fill=\"white\" opacity=\"0.95\">?</text>"
    "</svg>";

static gboolean
reset_copy_button_label (gpointer data)
{
    if (GTK_IS_BUTTON (data))
        gtk_button_set_label (GTK_BUTTON (data), _("Copy to Clipboard"));
    return G_SOURCE_REMOVE;
}

static void
on_copy_clicked (GtkButton *btn, gpointer user_data)
{
    NemoPreviewPane *self = NEMO_PREVIEW_PANE (user_data);
    NemoPreviewPanePrivate *p = self->priv;

    if (!p->missing_cmd_text) return;

    DEBUG ("copying install command to clipboard: %s", p->missing_cmd_text);
    GtkClipboard *cb = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text (cb, p->missing_cmd_text, -1);

    gtk_button_set_label (btn, _("Copied!"));
    g_timeout_add (1500, reset_copy_button_label, btn);
}

static GtkWidget *
build_missing_svg_image (void)
{
    GError       *err    = NULL;
    GInputStream *stream = g_memory_input_stream_new_from_data (
                               MISSING_SVG, (gssize) strlen (MISSING_SVG), NULL);
    GdkPixbuf *pb = gdk_pixbuf_new_from_stream_at_scale (stream, 96, 96, TRUE, NULL, &err);
    g_object_unref (stream);

    GtkWidget *img;
    if (pb) {
        img = gtk_image_new_from_pixbuf (pb);
        g_object_unref (pb);
    } else {
        if (err) { g_warning ("preview SVG render: %s", err->message); g_error_free (err); }
        img = gtk_image_new_from_icon_name ("dialog-information-symbolic", GTK_ICON_SIZE_DIALOG);
    }
    gtk_widget_set_opacity (img, 0.80);
    return img;
}

static GtkWidget *
build_missing_dep_page (NemoPreviewPane *self)
{
    NemoPreviewPanePrivate *p = self->priv;

    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign (vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_halign (vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start  (vbox, 20);
    gtk_widget_set_margin_end    (vbox, 20);
    gtk_widget_set_margin_top    (vbox, 24);
    gtk_widget_set_margin_bottom (vbox, 24);
    gtk_container_add (GTK_CONTAINER (scroll), vbox);

    /* SVG illustration */
    GtkWidget *svg = build_missing_svg_image ();
    gtk_widget_set_halign (svg, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom (svg, 16);
    gtk_box_pack_start (GTK_BOX (vbox), svg, FALSE, FALSE, 0);

    /* Title */
    p->missing_title = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (p->missing_title),
                          "<span weight=\"bold\" font_size=\"large\">"
                          "Preview unavailable"
                          "</span>");
    gtk_widget_set_halign (p->missing_title, GTK_ALIGN_CENTER);
    gtk_label_set_justify (GTK_LABEL (p->missing_title), GTK_JUSTIFY_CENTER);
    gtk_widget_set_margin_bottom (p->missing_title, 10);
    gtk_box_pack_start (GTK_BOX (vbox), p->missing_title, FALSE, FALSE, 0);

    /* Body */
    p->missing_body = gtk_label_new ("");
    gtk_widget_set_halign (p->missing_body, GTK_ALIGN_CENTER);
    gtk_label_set_justify  (GTK_LABEL (p->missing_body), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap (GTK_LABEL (p->missing_body), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (p->missing_body), 32);
    gtk_widget_set_opacity (p->missing_body, 0.75);
    gtk_widget_set_margin_bottom (p->missing_body, 16);
    gtk_box_pack_start (GTK_BOX (vbox), p->missing_body, FALSE, FALSE, 0);

    /* Command frame with selectable monospace label */
    GtkWidget *frame = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
    gtk_widget_set_margin_bottom (frame, 14);

    p->missing_cmd_label = gtk_label_new ("");
    gtk_label_set_selectable (GTK_LABEL (p->missing_cmd_label), TRUE);
    gtk_label_set_line_wrap  (GTK_LABEL (p->missing_cmd_label), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (p->missing_cmd_label), 32);
    {
        PangoAttrList *attrs = pango_attr_list_new ();
        pango_attr_list_insert (attrs, pango_attr_family_new ("Monospace"));
        pango_attr_list_insert (attrs, pango_attr_scale_new (PANGO_SCALE_SMALL));
        gtk_label_set_attributes (GTK_LABEL (p->missing_cmd_label), attrs);
        pango_attr_list_unref (attrs);
    }
    gtk_widget_set_margin_start  (p->missing_cmd_label, 10);
    gtk_widget_set_margin_end    (p->missing_cmd_label, 10);
    gtk_widget_set_margin_top    (p->missing_cmd_label, 8);
    gtk_widget_set_margin_bottom (p->missing_cmd_label, 8);
    gtk_container_add (GTK_CONTAINER (frame), p->missing_cmd_label);
    gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);

    /* Copy button */
    p->missing_copy_btn = gtk_button_new_with_label (_("Copy to Clipboard"));
    gtk_widget_set_halign (p->missing_copy_btn, GTK_ALIGN_CENTER);
    gtk_style_context_add_class (gtk_widget_get_style_context (p->missing_copy_btn),
                                 "suggested-action");
    g_signal_connect (p->missing_copy_btn, "clicked",
                      G_CALLBACK (on_copy_clicked), self);
    gtk_box_pack_start (GTK_BOX (vbox), p->missing_copy_btn, FALSE, FALSE, 0);

    gtk_widget_show_all (scroll);
    return scroll;
}

static void
preview_show_missing_dep (NemoPreviewPane *self,
                          const char      *title,
                          const char      *body,
                          const char      *install_cmd)
{
    NemoPreviewPanePrivate *p = self->priv;

    DEBUG ("showing missing-dep notice: %s", title ? title : "(null)");

    if (title) {
        gchar *markup = g_markup_printf_escaped (
            "<span weight=\"bold\" font_size=\"large\">%s</span>", title);
        gtk_label_set_markup (GTK_LABEL (p->missing_title), markup);
        g_free (markup);
    }
    gtk_label_set_text (GTK_LABEL (p->missing_body), body ? body : "");

    g_free (p->missing_cmd_text);
    p->missing_cmd_text = g_strdup (install_cmd ? install_cmd : "");
    gtk_label_set_text (GTK_LABEL (p->missing_cmd_label),
                        install_cmd ? install_cmd : "");

    gtk_button_set_label (GTK_BUTTON (p->missing_copy_btn), _("Copy to Clipboard"));
    gtk_widget_set_sensitive (p->missing_copy_btn, install_cmd && *install_cmd);

    preview_show_page (self, PREVIEW_PAGE_MISSING_DEP);
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  GStreamer — loaded entirely via GModule at runtime
 *
 *  We only need a tiny slice of the GStreamer API:
 *    gst_init, gst_is_initialized, gst_element_factory_make,
 *    gst_element_set_state, gst_element_get_bus, gst_object_unref,
 *    gst_bus_add_watch, gst_element_query_{position,duration},
 *    gst_element_seek_simple, gst_message_parse_error
 *
 *  These are resolved via g_module_symbol() and stored in function pointers.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Minimal GStreamer type aliases — enough for our usage */
typedef gpointer GstElement;
typedef gpointer GstBus;
typedef gpointer GstMessage;
typedef guint32  GstMessageType;
typedef gint     GstState;
typedef guint64  GstClockTime;
typedef gint     GstFormat;
typedef guint32  GstSeekFlags;

#define MY_GST_FORMAT_TIME   3
#define MY_GST_STATE_NULL    1
#define MY_GST_STATE_PAUSED  3
#define MY_GST_STATE_PLAYING 4
#define MY_GST_MESSAGE_EOS   (1 << 0)
#define MY_GST_MESSAGE_ERROR (1 << 1)
#define MY_GST_SECOND        ((GstClockTime)1000000000)
#define MY_GST_SEEK_FLAG_FLUSH     (1 << 0)
#define MY_GST_SEEK_FLAG_KEY_UNIT  (1 << 2)

typedef gboolean (*GstBusFunc)(GstBus *, GstMessage *, gpointer);

/* Function pointer types */
typedef void        (*Fn_gst_init)                    (int *, char ***);
typedef gboolean    (*Fn_gst_is_initialized)          (void);
typedef GstElement *(*Fn_gst_element_factory_make)    (const gchar *, const gchar *);
typedef gint        (*Fn_gst_element_set_state)       (GstElement *, GstState);
typedef GstBus     *(*Fn_gst_element_get_bus)         (GstElement *);
typedef void        (*Fn_gst_object_unref)            (gpointer);
typedef guint       (*Fn_gst_bus_add_watch)           (GstBus *, GstBusFunc, gpointer);
typedef gboolean    (*Fn_gst_element_query_position)  (GstElement *, GstFormat, gint64 *);
typedef gboolean    (*Fn_gst_element_query_duration)  (GstElement *, GstFormat, gint64 *);
typedef gboolean    (*Fn_gst_element_seek_simple)     (GstElement *, GstFormat, GstSeekFlags, gint64);
typedef void        (*Fn_gst_message_parse_error)     (GstMessage *, GError **, gchar **);
typedef guint32     (*Fn_gst_message_get_type_fn)     (GstMessage *);

/* Resolved symbols (NULL until gst_runtime_load() succeeds) */
static Fn_gst_init                    _gst_init                    = NULL;
static Fn_gst_is_initialized          _gst_is_initialized          = NULL;
static Fn_gst_element_factory_make    _gst_element_factory_make    = NULL;
static Fn_gst_element_set_state       _gst_element_set_state       = NULL;
static Fn_gst_element_get_bus         _gst_element_get_bus         = NULL;
static Fn_gst_object_unref            _gst_object_unref            = NULL;
static Fn_gst_bus_add_watch           _gst_bus_add_watch           = NULL;
static Fn_gst_element_query_position  _gst_element_query_position  = NULL;
static Fn_gst_element_query_duration  _gst_element_query_duration  = NULL;
static Fn_gst_element_seek_simple     _gst_element_seek_simple     = NULL;
static Fn_gst_message_parse_error     _gst_message_parse_error     = NULL;
static Fn_gst_message_get_type_fn     _gst_message_get_type        = NULL;

/* GstVideoOverlay — for embedding video into a GTK widget */
typedef void (*Fn_gst_video_overlay_set_window_handle) (gpointer overlay, guintptr handle);
typedef void (*Fn_gst_video_overlay_expose)            (gpointer overlay);
static Fn_gst_video_overlay_set_window_handle _gst_video_overlay_set_window_handle = NULL;
static Fn_gst_video_overlay_expose            _gst_video_overlay_expose            = NULL;

/* GObject property setter needed to set "uri" and "video-sink" on playbin */
/* g_object_set is already available — we use it directly through GLib.    */

static gboolean gst_runtime_loaded = FALSE;

static void
gst_reset_fn_pointers (void)
{
    _gst_init = NULL; _gst_is_initialized = NULL;
    _gst_element_factory_make = NULL; _gst_element_set_state = NULL;
    _gst_element_get_bus = NULL; _gst_object_unref = NULL;
    _gst_bus_add_watch = NULL; _gst_element_query_position = NULL;
    _gst_element_query_duration = NULL; _gst_element_seek_simple = NULL;
    _gst_message_parse_error = NULL; _gst_message_get_type = NULL;
}

static gboolean
gst_runtime_load (void)
{
    if (gst_runtime_loaded) return TRUE;

    check_runtime_gstreamer ();
    if (runtime_gstreamer != RUNTIME_AVAILABLE) return FALSE;

    const char *core_candidates[] = {
        "libgstreamer-1.0.so.0", "libgstreamer-1.0.so", NULL
    };
    GModule *core = NULL;
    if (!try_load_library (core_candidates, &core)) return FALSE;

#define LOAD_SYM(mod, fn, name) \
    if (!g_module_symbol (mod, name, (gpointer *)&fn)) { \
        g_warning ("NemoPreviewPane: GStreamer symbol '%s' not found", name); \
        gst_reset_fn_pointers (); \
        return FALSE; \
    }

    LOAD_SYM (core, _gst_init,                    "gst_init")
    LOAD_SYM (core, _gst_is_initialized,          "gst_is_initialized")
    LOAD_SYM (core, _gst_element_factory_make,    "gst_element_factory_make")
    LOAD_SYM (core, _gst_element_set_state,       "gst_element_set_state")
    LOAD_SYM (core, _gst_element_get_bus,         "gst_element_get_bus")
    LOAD_SYM (core, _gst_object_unref,            "gst_object_unref")
    LOAD_SYM (core, _gst_bus_add_watch,           "gst_bus_add_watch")
    LOAD_SYM (core, _gst_element_query_position,  "gst_element_query_position")
    LOAD_SYM (core, _gst_element_query_duration,  "gst_element_query_duration")
    LOAD_SYM (core, _gst_element_seek_simple,     "gst_element_seek_simple")
    LOAD_SYM (core, _gst_message_parse_error,     "gst_message_parse_error")
    LOAD_SYM (core, _gst_message_get_type,        "gst_message_get_message_type")
#undef LOAD_SYM

    /* The "type" field of GstMessage is at a fixed offset we read directly */

    if (!_gst_is_initialized ()) {
        int ac = 0; char **av = NULL;
        _gst_init (&ac, &av);
        DEBUG ("GStreamer initialised via runtime load");
    }

    /* GstVideoOverlay symbols — in libgstvideo or libgstreamer-video */
    const char *vid_candidates[] = {
        "libgstvideo-1.0.so.0", "libgstvideo-1.0.so", NULL
    };
    GModule *vidmod = NULL;
    if (try_load_library (vid_candidates, &vidmod)) {
        g_module_symbol (vidmod, "gst_video_overlay_set_window_handle",
                         (gpointer *)&_gst_video_overlay_set_window_handle);
        g_module_symbol (vidmod, "gst_video_overlay_expose",
                         (gpointer *)&_gst_video_overlay_expose);
        DEBUG ("GStreamer video overlay symbols loaded");
    } else {
        DEBUG ("GStreamer video overlay library not found — video renders in own window");
    }

    gst_runtime_loaded = TRUE;
    DEBUG ("GStreamer runtime load: SUCCESS");
    return TRUE;
}

/* Read the message type from a GstMessage — the type is the first
 * guint32 field after the GstMiniObject header (3 pointers + 2 guint32s).
 * This is stable across GStreamer 1.x releases. */
static GstMessageType
gst_msg_get_type (GstMessage *msg)
{
    if (_gst_message_get_type)
        return _gst_message_get_type (msg);
    return 0;
}


/* ── GStreamer pipeline helpers ─────────────────────────────────────────── */

static void
gst_stop_tick (NemoPreviewPane *self)
{
    if (self->priv->gst_tick_id) {
        g_source_remove (self->priv->gst_tick_id);
        self->priv->gst_tick_id = 0;
    }
}

static gboolean
gst_update_seek_bar (gpointer user_data)
{
    NemoPreviewPane *self = NEMO_PREVIEW_PANE (user_data);
    NemoPreviewPanePrivate *p = self->priv;

    if (!p->gst_pipeline || p->gst_seeking || !gst_runtime_loaded)
        return G_SOURCE_CONTINUE;

    gint64 pos = 0;
    _gst_element_query_position (p->gst_pipeline, MY_GST_FORMAT_TIME, &pos);
    gdouble pos_s = (gdouble)pos / MY_GST_SECOND;

    if (p->gst_duration < 0) {
        gint64 dur = 0;
        if (_gst_element_query_duration (p->gst_pipeline, MY_GST_FORMAT_TIME, &dur) && dur > 0)
            p->gst_duration = (gdouble)dur / MY_GST_SECOND;
    }

    GtkWidget *seek = (p->current_page == PREVIEW_PAGE_VIDEO) ? p->video_seek : p->audio_seek;
    GtkWidget *tlbl = (p->current_page == PREVIEW_PAGE_VIDEO) ? p->video_time_label : p->audio_time_label;

    if (seek && p->gst_duration > 0) {
        gtk_range_set_range (GTK_RANGE (seek), 0.0, p->gst_duration);
        g_signal_handlers_block_by_func (seek, NULL, self);
        gtk_range_set_value (GTK_RANGE (seek), pos_s);
        g_signal_handlers_unblock_by_func (seek, NULL, self);
    }
    if (tlbl) {
        int pm = (int)pos_s/60, ps = (int)pos_s%60;
        gchar *txt = (p->gst_duration > 0)
            ? g_strdup_printf ("%02d:%02d / %02d:%02d",
                               pm, ps,
                               (int)p->gst_duration/60, (int)p->gst_duration%60)
            : g_strdup_printf ("%02d:%02d", pm, ps);
        gtk_label_set_text (GTK_LABEL (tlbl), txt);
        g_free (txt);
    }
    return G_SOURCE_CONTINUE;
}

static gboolean
gst_bus_call (gpointer bus, gpointer msg, gpointer data)
{
    NemoPreviewPane *self = NEMO_PREVIEW_PANE (data);
    NemoPreviewPanePrivate *p = self->priv;
    (void) bus;

    GstMessageType mtype = gst_msg_get_type ((GstMessage *)msg);

    if (mtype & MY_GST_MESSAGE_EOS) {
        DEBUG ("GStreamer: EOS — rewinding");
        _gst_element_seek_simple (p->gst_pipeline, MY_GST_FORMAT_TIME,
                                  MY_GST_SEEK_FLAG_FLUSH | MY_GST_SEEK_FLAG_KEY_UNIT, 0);
        _gst_element_set_state (p->gst_pipeline, MY_GST_STATE_PAUSED);
        p->gst_playing = FALSE;
    } else if (mtype & MY_GST_MESSAGE_ERROR) {
        GError *err = NULL;
        _gst_message_parse_error ((GstMessage *)msg, &err, NULL);
        g_warning ("NemoPreviewPane: GStreamer error: %s", err ? err->message : "unknown");
        DEBUG ("GStreamer error detail: %s", err ? err->message : "unknown");
        if (err) g_error_free (err);
        preview_show_page (self, PREVIEW_PAGE_EMPTY);
    }
    return TRUE;
}

static void
gst_sync_play_button (NemoPreviewPane *self)
{
    NemoPreviewPanePrivate *p = self->priv;
    const char *icon = p->gst_playing
                       ? "media-playback-pause-symbolic"
                       : "media-playback-start-symbolic";
    GtkWidget *btn = (p->current_page == PREVIEW_PAGE_VIDEO) ? p->video_play_btn
                                                              : p->audio_play_btn;
    if (btn)
        gtk_button_set_image (GTK_BUTTON (btn),
                              gtk_image_new_from_icon_name (icon, GTK_ICON_SIZE_BUTTON));
}

static void
gst_play_pause_toggle (NemoPreviewPane *self)
{
    NemoPreviewPanePrivate *p = self->priv;
    if (!p->gst_pipeline || !gst_runtime_loaded) return;

    if (p->gst_playing) {
        _gst_element_set_state (p->gst_pipeline, MY_GST_STATE_PAUSED);
        p->gst_playing = FALSE;
        gst_stop_tick (self);
        DEBUG ("GStreamer: paused");
    } else {
        _gst_element_set_state (p->gst_pipeline, MY_GST_STATE_PLAYING);
        p->gst_playing = TRUE;
        if (!p->gst_tick_id)
            p->gst_tick_id = g_timeout_add (250, gst_update_seek_bar, self);
        DEBUG ("GStreamer: playing");
    }
    gst_sync_play_button (self);
}

static void on_play_pause_clicked (GtkButton *b, gpointer u) {
    (void)b; gst_play_pause_toggle (NEMO_PREVIEW_PANE (u));
}
static void on_seek_value_changed (GtkRange *r, gpointer u) {
    NemoPreviewPane *self = NEMO_PREVIEW_PANE (u);
    NemoPreviewPanePrivate *p = self->priv;
    if (!p->gst_pipeline || !p->gst_seeking || !gst_runtime_loaded) return;
    gdouble val = gtk_range_get_value (r);
    DEBUG ("GStreamer: seeking to %.2fs", val);
    _gst_element_seek_simple (p->gst_pipeline, MY_GST_FORMAT_TIME,
                              MY_GST_SEEK_FLAG_FLUSH | MY_GST_SEEK_FLAG_KEY_UNIT,
                              (gint64)(val * MY_GST_SECOND));
}
static void on_seek_pressed  (GtkWidget *w, GdkEvent *e, gpointer u) {
    (void)w; (void)e; NEMO_PREVIEW_PANE (u)->priv->gst_seeking = TRUE;
}
static void on_seek_released (GtkWidget *w, GdkEvent *e, gpointer u) {
    (void)e; NEMO_PREVIEW_PANE (u)->priv->gst_seeking = FALSE;
    on_seek_value_changed (GTK_RANGE (w), u);
}

static GtkWidget *
build_media_controls (NemoPreviewPane *self,
                      GtkWidget **out_play, GtkWidget **out_seek, GtkWidget **out_tlbl)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start  (box, 6);
    gtk_widget_set_margin_end    (box, 6);
    gtk_widget_set_margin_top    (box, 4);
    gtk_widget_set_margin_bottom (box, 4);

    GtkWidget *play = gtk_button_new_from_icon_name (
        "media-playback-start-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text (play, _("Play / Pause"));
    g_signal_connect (play, "clicked", G_CALLBACK (on_play_pause_clicked), self);
    gtk_box_pack_start (GTK_BOX (box), play, FALSE, FALSE, 0);

    GtkWidget *seek = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 1, 0.5);
    gtk_scale_set_draw_value (GTK_SCALE (seek), FALSE);
    gtk_widget_set_tooltip_text (seek, _("Seek"));
    gtk_box_pack_start (GTK_BOX (box), seek, TRUE, TRUE, 4);
    g_signal_connect (seek, "value-changed",       G_CALLBACK (on_seek_value_changed), self);
    g_signal_connect (seek, "button-press-event",  G_CALLBACK (on_seek_pressed),       self);
    g_signal_connect (seek, "button-release-event",G_CALLBACK (on_seek_released),      self);

    GtkWidget *tlbl = gtk_label_new ("--:--");
    gtk_box_pack_start (GTK_BOX (box), tlbl, FALSE, FALSE, 4);

    gtk_widget_show_all (box);
    if (out_play) *out_play = play;
    if (out_seek) *out_seek = seek;
    if (out_tlbl) *out_tlbl = tlbl;
    return box;
}

static void
gst_destroy_pipeline (NemoPreviewPane *self)
{
    NemoPreviewPanePrivate *p = self->priv;
    if (!gst_runtime_loaded) return;

    gst_stop_tick (self);
    if (p->gst_bus_watch_id) { g_source_remove (p->gst_bus_watch_id); p->gst_bus_watch_id = 0; }
    if (p->gst_pipeline) {
        DEBUG ("destroying GStreamer pipeline");
        _gst_element_set_state (p->gst_pipeline, MY_GST_STATE_NULL);
        _gst_object_unref (p->gst_pipeline);
        p->gst_pipeline = NULL;
    }
    p->gst_playing = FALSE; p->gst_duration = -1.0; p->gst_seeking = FALSE;

    if (p->video_area_orig && p->video_area != p->video_area_orig) {
        GtkWidget *vparent = gtk_widget_get_parent (p->video_area);
        if (vparent) {
            gtk_container_remove (GTK_CONTAINER (vparent), p->video_area);
            gtk_box_pack_start (GTK_BOX (vparent), p->video_area_orig, TRUE, TRUE, 0);
            gtk_box_reorder_child (GTK_BOX (vparent), p->video_area_orig, 0);
            gtk_widget_show (p->video_area_orig);
        }
        p->video_area = p->video_area_orig;
    }
}

static gboolean
gst_launch (NemoPreviewPane *self, const char *uri, gboolean is_video)
{
    NemoPreviewPanePrivate *p = self->priv;

    if (!gst_runtime_load ()) {
        DEBUG ("gst_launch: runtime not available");
        return FALSE;
    }

    gst_destroy_pipeline (self);

    DEBUG ("gst_launch: creating playbin for %s (video=%d)", uri, is_video);

    p->gst_pipeline = _gst_element_factory_make ("playbin", "preview");
    if (!p->gst_pipeline) {
        g_warning ("NemoPreviewPane: gst_element_factory_make(playbin) failed — "
                   "is gstreamer1.0-plugins-base installed?");
        return FALSE;
    }

    g_object_set (G_OBJECT (p->gst_pipeline), "uri", uri, NULL);

    if (!is_video) {
        /* Audio-only: suppress any video output */
        GstElement *fakesink = _gst_element_factory_make ("fakesink", "fakevideo");
        if (fakesink) {
            g_object_set (G_OBJECT (p->gst_pipeline), "video-sink", fakesink, NULL);
            DEBUG ("gst_launch: audio mode — fakesink suppresses video");
        } else {
            DEBUG ("gst_launch: fakesink not available");
        }
    } else {
        /* Video: try gtksink first (renders inside a GtkWidget we embed),
         * then xvimagesink (renders into an XWindow by handle),
         * then fall back to whatever autovideosink picks — but redirect
         * it via GstVideoOverlay so it targets our video_area widget. */
        GstElement *vsink = NULL;

        vsink = _gst_element_factory_make ("gtksink", "gtksink");
        if (vsink) {
            /* gtksink exposes a "widget" property — a GtkWidget we can embed */
            GtkWidget *gst_widget = NULL;
            g_object_get (G_OBJECT (vsink), "widget", &gst_widget, NULL);
            if (gst_widget) {
                gtk_widget_set_hexpand (gst_widget, TRUE);
                gtk_widget_set_vexpand (gst_widget, TRUE);
                /* Replace the black drawing area with the gtksink widget */
                GtkWidget *parent = gtk_widget_get_parent (p->video_area);
                if (parent) {
                    if (!p->video_area_orig)
                        p->video_area_orig = p->video_area;
                    gtk_container_remove (GTK_CONTAINER (parent), p->video_area);
                    gtk_box_pack_start (GTK_BOX (parent), gst_widget, TRUE, TRUE, 0);
                    gtk_box_reorder_child (GTK_BOX (parent), gst_widget, 0);
                    gtk_widget_show (gst_widget);
                    p->video_area = gst_widget;
                }
                /* gtksink owns the ref; we took one via get, release it */
                g_object_unref (gst_widget);
                DEBUG ("gst_launch: using gtksink (embedded GtkWidget)");
            }
            g_object_set (G_OBJECT (p->gst_pipeline), "video-sink", vsink, NULL);
        } else {
            /* No gtksink — use xvimagesink / ximagesink with XID overlay */
            vsink = _gst_element_factory_make ("xvimagesink", "xvimagesink");
            if (!vsink)
                vsink = _gst_element_factory_make ("ximagesink", "ximagesink");

            if (vsink) {
                g_object_set (G_OBJECT (p->gst_pipeline), "video-sink", vsink, NULL);
                /* Realise the drawing area so it has an X window handle.
                 * GDK_WINDOW_XID is only available on X11 — guard at both
                 * compile time and runtime. */
                gtk_widget_realize (p->video_area);
#ifdef GDK_WINDOWING_X11
                {
                    GdkDisplay *disp = gtk_widget_get_display (p->video_area);
                    if (GDK_IS_X11_DISPLAY (disp)) {
                        GdkWindow *gdk_win = gtk_widget_get_window (p->video_area);
                        if (gdk_win && _gst_video_overlay_set_window_handle) {
                            guintptr xid = (guintptr) GDK_WINDOW_XID (gdk_win);
                            _gst_video_overlay_set_window_handle (vsink, xid);
                            DEBUG ("gst_launch: using xv/ximagesink with XID overlay 0x%lx",
                                   (unsigned long)xid);
                        } else {
                            DEBUG ("gst_launch: overlay set-window-handle unavailable");
                        }
                    } else {
                        DEBUG ("gst_launch: non-X11 display, cannot embed xv/ximagesink");
                    }
                }
#else
                DEBUG ("gst_launch: X11 windowing not compiled in, cannot embed xv/ximagesink");
#endif
            } else {
                DEBUG ("gst_launch: no suitable video sink found, autovideosink will be used");
            }
        }
    }

    GstBus *bus = _gst_element_get_bus (p->gst_pipeline);
    p->gst_bus_watch_id = _gst_bus_add_watch (bus, (GstBusFunc)gst_bus_call, self);
    _gst_object_unref (bus);

    DEBUG ("gst_launch: set_state(PLAYING) returned %d",
           _gst_element_set_state (p->gst_pipeline, MY_GST_STATE_PLAYING));

    p->gst_playing  = TRUE;
    p->gst_duration = -1.0;
    p->gst_tick_id  = g_timeout_add (250, gst_update_seek_bar, self);
    gst_sync_play_button (self);
    return TRUE;
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  preview_clear_media
 * ═══════════════════════════════════════════════════════════════════════════ */

static void
preview_clear_media (NemoPreviewPane *self)
{
    NemoPreviewPanePrivate *p = self->priv;

    gst_destroy_pipeline (self);

    if (p->image_pixbuf) { g_object_unref (p->image_pixbuf); p->image_pixbuf = NULL; }

    if (p->lo_pid)       { g_spawn_close_pid (p->lo_pid); p->lo_pid = 0; }
    if (p->lo_watch_id)  { g_source_remove (p->lo_watch_id); p->lo_watch_id = 0; }
    lo_disconnect_web_cleanup (self);
    if (p->lo_html_path) { remove (p->lo_html_path); g_free (p->lo_html_path); p->lo_html_path = NULL; }
    if (p->lo_tmp_dir)   { rmdir  (p->lo_tmp_dir);  g_free (p->lo_tmp_dir);   p->lo_tmp_dir   = NULL; }

    g_free (p->current_uri); p->current_uri = NULL;
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  Image renderer — always available (GdkPixbuf)
 * ═══════════════════════════════════════════════════════════════════════════ */

static gboolean
on_image_draw (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    NemoPreviewPane *self = NEMO_PREVIEW_PANE (user_data);
    GdkPixbuf       *pb   = self->priv->image_pixbuf;
    if (!pb) return FALSE;

    int ww = gtk_widget_get_allocated_width  (widget);
    int wh = gtk_widget_get_allocated_height (widget);
    int pw = gdk_pixbuf_get_width  (pb);
    int ph = gdk_pixbuf_get_height (pb);
    if (ww <= 0 || wh <= 0 || pw <= 0 || ph <= 0) return FALSE;

    /* Scale to fill the available space on the constraining axis.
     * - landscape image (pw >= ph): scale so width == ww
     * - portrait image  (ph >  pw): scale so height == wh
     * Then clamp so neither dimension exceeds the widget.  Never upscale. */
    double scale_w = (double)ww / pw;
    double scale_h = (double)wh / ph;
    double scale   = MIN (scale_w, scale_h);   /* fit-inside, preserves aspect */
    if (scale > 1.0) scale = 1.0;             /* never upscale                */

    int sw = MAX (1, (int)(pw * scale));
    int sh = MAX (1, (int)(ph * scale));
    int ox = (ww - sw) / 2;
    int oy = (wh - sh) / 2;

    DEBUG ("image draw: widget=%dx%d image=%dx%d scale=%.3f rendered=%dx%d offset=%d,%d",
           ww, wh, pw, ph, scale, sw, sh, ox, oy);

    /* Fill background */
    GtkStyleContext *sc = gtk_widget_get_style_context (widget);
    GdkRGBA bg;
    gtk_style_context_get_background_color (sc, GTK_STATE_FLAG_NORMAL, &bg);
    cairo_set_source_rgba (cr, bg.red, bg.green, bg.blue, bg.alpha);
    cairo_paint (cr);

    GdkPixbuf *scaled = gdk_pixbuf_scale_simple (pb, sw, sh, GDK_INTERP_BILINEAR);
    if (scaled) {
        gdk_cairo_set_source_pixbuf (cr, scaled, ox, oy);
        cairo_paint (cr);
        g_object_unref (scaled);
    }
    return TRUE;
}

static void
preview_load_image (NemoPreviewPane *self, const char *uri)
{
    NemoPreviewPanePrivate *p = self->priv;

    DEBUG ("loading image: %s", uri);

    GFile *file = g_file_new_for_uri (uri);
    GError *err = NULL;
    GFileInputStream *stream = g_file_read (file, NULL, &err);
    g_object_unref (file);

    if (!stream) {
        g_warning ("NemoPreviewPane: cannot open image %s: %s", uri, err ? err->message : "?");
        DEBUG ("image open failed: %s", err ? err->message : "?");
        if (err) g_error_free (err);
        preview_show_page (self, PREVIEW_PAGE_EMPTY);
        return;
    }

    GdkPixbuf *pb = gdk_pixbuf_new_from_stream (G_INPUT_STREAM (stream), NULL, &err);
    g_object_unref (stream);

    if (!pb) {
        g_warning ("NemoPreviewPane: cannot decode image %s: %s", uri, err ? err->message : "?");
        DEBUG ("image decode failed: %s", err ? err->message : "?");
        if (err) g_error_free (err);
        preview_show_page (self, PREVIEW_PAGE_EMPTY);
        return;
    }

    DEBUG ("image loaded: %dx%d", gdk_pixbuf_get_width (pb), gdk_pixbuf_get_height (pb));

    if (p->image_pixbuf) g_object_unref (p->image_pixbuf);
    p->image_pixbuf = pb;
    gtk_widget_queue_draw (p->image_drawing_area);
    preview_show_page (self, PREVIEW_PAGE_IMAGE);
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  Audio renderer
 * ═══════════════════════════════════════════════════════════════════════════ */

static void
preview_load_audio (NemoPreviewPane *self, const char *uri)
{
    DEBUG ("loading audio: %s", uri);
    check_runtime_gstreamer ();

    if (runtime_gstreamer == RUNTIME_MISSING) {
        DEBUG ("audio: GStreamer runtime not available");
        preview_show_missing_dep (self,
            _("Audio preview unavailable"),
            _("To preview audio files, install GStreamer and its plugins, then restart Nemo:"),
            "sudo apt install gstreamer1.0-tools gstreamer1.0-plugins-good "
            "gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav");
        return;
    }

    if (gst_launch (self, uri, FALSE)) {
        preview_show_page (self, PREVIEW_PAGE_AUDIO);
    } else {
        g_warning ("NemoPreviewPane: GStreamer launch failed for audio: %s", uri);
        DEBUG ("audio: gst_launch failed");
        preview_show_page (self, PREVIEW_PAGE_EMPTY);
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  Video renderer
 * ═══════════════════════════════════════════════════════════════════════════ */

static void
preview_load_video (NemoPreviewPane *self, const char *uri)
{
    DEBUG ("loading video: %s", uri);
    check_runtime_gstreamer ();

    if (runtime_gstreamer == RUNTIME_MISSING) {
        DEBUG ("video: GStreamer runtime not available");
        preview_show_missing_dep (self,
            _("Video preview unavailable"),
            _("To preview video files, install GStreamer and its plugins, then restart Nemo:"),
            "sudo apt install gstreamer1.0-tools gstreamer1.0-plugins-good "
            "gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav");
        return;
    }

    if (gst_launch (self, uri, TRUE)) {
        preview_show_page (self, PREVIEW_PAGE_VIDEO);
    } else {
        g_warning ("NemoPreviewPane: GStreamer launch failed for video: %s", uri);
        DEBUG ("video: gst_launch failed");
        preview_show_page (self, PREVIEW_PAGE_EMPTY);
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  PDF renderer — libevview via GModule
 * ═══════════════════════════════════════════════════════════════════════════ */

static void
preview_load_pdf (NemoPreviewPane *self, const char *uri)
{
    DEBUG ("loading PDF: %s", uri);
    check_runtime_evview ();

    if (runtime_evview == RUNTIME_MISSING) {
        DEBUG ("PDF: libevview not available");
        preview_show_missing_dep (self,
            _("PDF preview unavailable"),
            _("To preview PDF files, install the Evince document viewer library, then restart Nemo:"),
            "sudo apt install libevview3-3 evince");
        return;
    }

    /* libevview is present — use it via dlopen/GModule.
     * We need: ev_init, ev_document_factory_get_document,
     *           ev_document_model_new_with_document, ev_view_new,
     *           ev_view_set_model */
    NemoPreviewPanePrivate *p = self->priv;

    const char *ev_candidates[] = { "libevview3.so.3", "libevview3.so", NULL };
    GModule *evmod = NULL;
    if (!try_load_library (ev_candidates, &evmod)) {
        DEBUG ("PDF: could not dlopen libevview3");
        preview_show_page (self, PREVIEW_PAGE_EMPTY);
        return;
    }

    typedef gboolean (*Fn_ev_init)(void);
    typedef gpointer (*Fn_ev_doc_factory)(const char *, GError **);
    typedef gpointer (*Fn_ev_doc_model_new)(gpointer);
    typedef GtkWidget *(*Fn_ev_view_new)(void);
    typedef void (*Fn_ev_view_set_model)(GtkWidget *, gpointer);

    Fn_ev_init          ev_init_fn    = NULL;
    Fn_ev_doc_factory   ev_factory_fn = NULL;
    Fn_ev_doc_model_new ev_model_fn   = NULL;
    Fn_ev_view_new      ev_view_fn    = NULL;
    Fn_ev_view_set_model ev_setmodel  = NULL;

#define LOAD_EV(fn, name) \
    if (!g_module_symbol (evmod, name, (gpointer *)&fn)) { \
        g_warning ("NemoPreviewPane: evview symbol '%s' missing", name); \
        preview_show_page (self, PREVIEW_PAGE_EMPTY); return; \
    }
    LOAD_EV (ev_init_fn,    "ev_init")
    LOAD_EV (ev_factory_fn, "ev_document_factory_get_document")
    LOAD_EV (ev_model_fn,   "ev_document_model_new_with_document")
    LOAD_EV (ev_view_fn,    "ev_view_new")
    LOAD_EV (ev_setmodel,   "ev_view_set_model")
#undef LOAD_EV

    ev_init_fn ();

    /* libevview only accepts file:// URIs.  For network paths (smb://, sftp://,
     * etc.) copy the file to a local temp file first using GIO. */
    const char *ev_uri = uri;
    gchar      *tmp_pdf_path = NULL;

    if (!g_str_has_prefix (uri, "file://")) {
        DEBUG ("PDF: non-local URI %s — copying to temp file", uri);
        GFile *src = g_file_new_for_uri (uri);
        GError *copy_err = NULL;
        gint tmp_fd = g_file_open_tmp ("nemo-preview-XXXXXX.pdf", &tmp_pdf_path, &copy_err);
        if (tmp_fd < 0 || !tmp_pdf_path) {
            g_warning ("NemoPreviewPane: cannot create temp PDF: %s",
                       copy_err ? copy_err->message : "?");
            if (copy_err) g_error_free (copy_err);
            g_object_unref (src);
            preview_show_page (self, PREVIEW_PAGE_EMPTY);
            return;
        }
        close (tmp_fd);
        GFile *dst = g_file_new_for_path (tmp_pdf_path);
        gboolean copied = g_file_copy (src, dst,
                                        G_FILE_COPY_OVERWRITE,
                                        NULL, NULL, NULL, &copy_err);
        g_object_unref (src);
        g_object_unref (dst);
        if (!copied) {
            g_warning ("NemoPreviewPane: GIO copy failed for PDF %s: %s",
                       uri, copy_err ? copy_err->message : "?");
            if (copy_err) g_error_free (copy_err);
            remove (tmp_pdf_path);
            g_free (tmp_pdf_path);
            tmp_pdf_path = NULL;
            preview_show_page (self, PREVIEW_PAGE_EMPTY);
            return;
        }
        gchar *file_uri = g_filename_to_uri (tmp_pdf_path, NULL, NULL);
        DEBUG ("PDF: temp copy at %s", file_uri ? file_uri : tmp_pdf_path);
        ev_uri = file_uri;  /* will be freed after doc is loaded */
    }

    GError *err = NULL;
    gpointer doc = ev_factory_fn (ev_uri, &err);

    /* Clean up temp file URI string if we created one */
    if (tmp_pdf_path) {
        g_free ((gchar *)ev_uri);
        ev_uri = uri; /* restore for any error messages */
    }

    if (!doc) {
        g_warning ("NemoPreviewPane: cannot load PDF %s: %s", uri, err ? err->message : "?");
        DEBUG ("PDF load failed: %s", err ? err->message : "?");
        if (err) g_error_free (err);
        if (tmp_pdf_path) { remove (tmp_pdf_path); g_free (tmp_pdf_path); }
        preview_show_page (self, PREVIEW_PAGE_EMPTY);
        return;
    }

    if (p->pdf_view) {
        gtk_container_remove (GTK_CONTAINER (p->pdf_scroll), p->pdf_view);
        p->pdf_view = NULL;
    }

    gpointer model = ev_model_fn (doc);
    GtkWidget *view = ev_view_fn ();
    ev_setmodel (view, model);
    g_object_unref (model);
    g_object_unref (doc);

    p->pdf_view = view;
    gtk_container_add (GTK_CONTAINER (p->pdf_scroll), view);
    gtk_widget_show (view);

    /* Remove temp file now that evview has loaded it */
    if (tmp_pdf_path) { remove (tmp_pdf_path); g_free (tmp_pdf_path); }

    DEBUG ("PDF loaded successfully");
    preview_show_page (self, PREVIEW_PAGE_PDF);
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  HTML / Web renderer — WebKit2GTK via GModule
 * ═══════════════════════════════════════════════════════════════════════════ */

static void
preview_load_web (NemoPreviewPane *self, const char *uri)
{
    /* Typedef hoisted to function scope so it is visible throughout */
    typedef GtkWidget *(*Fn_wk_new)(void);
    typedef gpointer   (*Fn_wk_settings_new)(void);
    typedef void       (*Fn_wk_settings_set_bool)(gpointer, gboolean);
    typedef void       (*Fn_wk_view_set_settings)(GtkWidget *, gpointer);
    typedef void       (*Fn_wk_load_uri)(GtkWidget *, const char *);

    DEBUG ("loading web/HTML: %s", uri);
    check_runtime_webkit ();

    if (runtime_webkit == RUNTIME_MISSING) {
        DEBUG ("web: WebKit2GTK not available");
        preview_show_missing_dep (self,
            _("HTML preview unavailable"),
            _("To preview HTML files, install the WebKitGTK library, then restart Nemo:"),
            "sudo apt install libwebkit2gtk-4.0-37");
        return;
    }

    NemoPreviewPanePrivate *p = self->priv;

    if (!p->web_view) {
        /* Lazy-create the WebKitWebView now that we know the library is present */
        const char *wk_candidates[] = {
            "libwebkit2gtk-4.1.so.0", "libwebkit2gtk-4.0.so.37",
            "libwebkit2gtk-4.0.so", NULL
        };
        GModule *wkmod = NULL;
        if (!try_load_library (wk_candidates, &wkmod)) {
            DEBUG ("web: could not dlopen webkit2gtk");
            preview_show_page (self, PREVIEW_PAGE_EMPTY);
            return;
        }

        Fn_wk_new               wk_new         = NULL;
        Fn_wk_settings_new      wk_settings_fn = NULL;
        Fn_wk_settings_set_bool wk_allow_file  = NULL;
        Fn_wk_settings_set_bool wk_allow_univ  = NULL;
        Fn_wk_view_set_settings wk_set_setts   = NULL;
        Fn_wk_load_uri          wk_load_init   = NULL;

#define LOAD_WK(fn, name) \
        if (!g_module_symbol (wkmod, name, (gpointer *)&fn)) { \
            g_warning ("NemoPreviewPane: webkit symbol '%s' missing", name); \
            preview_show_page (self, PREVIEW_PAGE_EMPTY); return; \
        }
        LOAD_WK (wk_new,          "webkit_web_view_new")
        LOAD_WK (wk_settings_fn,  "webkit_settings_new")
        LOAD_WK (wk_allow_file,   "webkit_settings_set_allow_file_access_from_file_urls")
        LOAD_WK (wk_allow_univ,   "webkit_settings_set_allow_universal_access_from_file_urls")
        LOAD_WK (wk_set_setts,    "webkit_web_view_set_settings")
        LOAD_WK (wk_load_init,    "webkit_web_view_load_uri")
#undef LOAD_WK

        GtkWidget *wv = wk_new ();
        gpointer   ws = wk_settings_fn ();
        wk_allow_file (ws, TRUE);
        wk_allow_univ (ws, TRUE);
        wk_set_setts (wv, ws);
        g_object_unref (ws);

        gtk_widget_set_hexpand (wv, TRUE);
        gtk_widget_set_vexpand (wv, TRUE);
        gtk_widget_show (wv);

        /* Replace the placeholder in the "web" stack page */
        GtkWidget *old = gtk_stack_get_child_by_name (GTK_STACK (p->stack), "web");
        if (old) gtk_container_remove (GTK_CONTAINER (p->stack), old);
        gtk_stack_add_named (GTK_STACK (p->stack), wv, "web");
        p->web_view = wv;

        /* Store load_uri function pointer on the widget for reuse across calls */
        g_object_set_data (G_OBJECT (wv), "wk-load-uri-fn", (gpointer)wk_load_init);

        DEBUG ("WebKitWebView created lazily");
    }

    /* Retrieve the stored load_uri function pointer and call it */
    Fn_wk_load_uri wk_load = (Fn_wk_load_uri)
        g_object_get_data (G_OBJECT (p->web_view), "wk-load-uri-fn");
    if (wk_load) {
        wk_load (p->web_view, uri);
        DEBUG ("web: loading URI %s", uri);
    }
    preview_show_page (self, PREVIEW_PAGE_WEB);
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  Office renderer
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Disconnect and clear the pending WebKit load-changed cleanup handler.
 * Safe to call at any time; does nothing if no handler is connected. */
static void
lo_disconnect_web_cleanup (NemoPreviewPane *self)
{
    NemoPreviewPanePrivate *p = self->priv;
    if (p->lo_web_cleanup_handler_id && p->web_view) {
        g_signal_handler_disconnect (p->web_view, p->lo_web_cleanup_handler_id);
        p->lo_web_cleanup_handler_id = 0;
    }
}

/* Called by WebKit on the main thread each time the load state advances.
 * We wait for WEBKIT_LOAD_COMMITTED — at that point WebKit has received the
 * initial data from the URI and the file on disk is no longer needed.
 * We do NOT wait for FINISHED because that fires after all sub-resources
 * load, which is much later and not necessary for a local HTML file. */
static void
lo_web_load_changed_cb (GtkWidget  *web_view,
                        gint        load_event,  /* WebKitLoadEvent enum value */
                        gpointer    user_data)
{
    NemoPreviewPane *self = NEMO_PREVIEW_PANE (user_data);
    NemoPreviewPanePrivate *p = self->priv;

    /* WEBKIT_LOAD_COMMITTED == 2 in the WebKitLoadEvent enum.
     * We use the raw integer to avoid a compile-time dependency on the
     * WebKit2GTK headers (the library is loaded at runtime via GModule). */
    if (load_event < 2)
        return;

    DEBUG ("lo_web_load_changed_cb: load event %d — cleaning up temp files", load_event);

    /* Disconnect ourselves first so we don't fire again for this load */
    lo_disconnect_web_cleanup (self);

    /* Delete the temp HTML file and its containing directory */
    if (p->lo_html_path) {
        remove (p->lo_html_path);
        g_free (p->lo_html_path);
        p->lo_html_path = NULL;
    }
    if (p->lo_tmp_dir) {
        rmdir (p->lo_tmp_dir);
        g_free (p->lo_tmp_dir);
        p->lo_tmp_dir = NULL;
    }
}

static void
lo_child_exited (GPid pid, gint status, gpointer user_data)
{
    NemoPreviewPane *self = NEMO_PREVIEW_PANE (user_data);
    NemoPreviewPanePrivate *p = self->priv;

    g_spawn_close_pid (pid);
    p->lo_pid = 0; p->lo_watch_id = 0;

    DEBUG ("LibreOffice conversion exited with status %d", status);

    if (WIFEXITED (status) && WEXITSTATUS (status) == 0
        && p->lo_html_path
        && g_file_test (p->lo_html_path, G_FILE_TEST_EXISTS)) {
        gchar *file_uri = g_filename_to_uri (p->lo_html_path, NULL, NULL);
        if (file_uri) {
            DEBUG ("LibreOffice: loading converted HTML %s", file_uri);
            preview_load_web (self, file_uri);
            g_free (file_uri);
            /* Temp files are deleted by lo_web_load_changed_cb once WebKit
             * has committed the load — do NOT delete them here. */
            if (p->web_view) {
                p->lo_web_cleanup_handler_id =
                    g_signal_connect (p->web_view, "load-changed",
                                      G_CALLBACK (lo_web_load_changed_cb), self);
            }
        } else {
            /* URI conversion failed — safe to clean up now */
            remove (p->lo_html_path);
            g_free (p->lo_html_path); p->lo_html_path = NULL;
            if (p->lo_tmp_dir) { rmdir (p->lo_tmp_dir); g_free (p->lo_tmp_dir); p->lo_tmp_dir = NULL; }
        }
    } else {
        g_warning ("NemoPreviewPane: LibreOffice conversion failed (status %d)", status);
        preview_show_page (self, PREVIEW_PAGE_EMPTY);
    }

    /* Release the reference taken in preview_load_office */
    g_object_unref (self);
}

static void
preview_load_office (NemoPreviewPane *self, const char *uri)
{
    DEBUG ("loading office doc: %s", uri);
    check_runtime_webkit ();

    /* Both WebKitGTK (to render the output) and LibreOffice (to convert)
     * are required.  Show a single consistent notice for either absence. */
    gboolean office_deps_ok = TRUE;
    if (runtime_webkit == RUNTIME_MISSING) {
        DEBUG ("office: WebKit2GTK not available");
        office_deps_ok = FALSE;
    }
    gchar *lo_bin = office_deps_ok ? g_find_program_in_path ("libreoffice") : NULL;
    if (office_deps_ok && !lo_bin) {
        DEBUG ("office: libreoffice binary not found in PATH");
        office_deps_ok = FALSE;
    }
    g_free (lo_bin);

    if (!office_deps_ok) {
        preview_show_missing_dep (self,
            _("Office document preview unavailable"),
            _("To preview office documents, install LibreOffice and WebKitGTK, then restart Nemo:"),
            "sudo apt install libreoffice libwebkit2gtk-4.0-37");
        return;
    }

    NemoPreviewPanePrivate *p = self->priv;

    GFile *file = g_file_new_for_uri (uri);
    gchar *local_path = g_file_get_path (file);
    gboolean used_tmp_copy = FALSE;

    /* For non-local URIs (smb://, sftp://, etc.) copy to a temp file first,
     * since LibreOffice can only open local paths. */
    if (!local_path) {
        DEBUG ("office: non-local URI, copying via GIO");
        /* Derive a temp filename preserving the extension for LO's type detection */
        const gchar *basename_uri = strrchr (uri, '/');
        const gchar *ext = basename_uri ? strrchr (basename_uri, '.') : NULL;
        gchar *tmpl = ext ? g_strdup_printf ("nemo-preview-XXXXXX%s", ext)
                          : g_strdup ("nemo-preview-XXXXXX");
        gint tmp_fd = -1;
        gchar *tmp_copy = NULL;
        GError *copy_err = NULL;
        tmp_fd = g_file_open_tmp (tmpl, &tmp_copy, &copy_err);
        g_free (tmpl);
        if (tmp_fd < 0) {
            g_warning ("NemoPreviewPane: cannot create temp for office copy: %s",
                       copy_err ? copy_err->message : "?");
            if (copy_err) g_error_free (copy_err);
            g_object_unref (file);
            preview_show_page (self, PREVIEW_PAGE_EMPTY);
            return;
        }
        close (tmp_fd);
        GFile *dst = g_file_new_for_path (tmp_copy);
        gboolean copied = g_file_copy (file, dst, G_FILE_COPY_OVERWRITE,
                                        NULL, NULL, NULL, &copy_err);
        g_object_unref (dst);
        if (!copied) {
            g_warning ("NemoPreviewPane: GIO copy failed for office doc %s: %s",
                       uri, copy_err ? copy_err->message : "?");
            if (copy_err) g_error_free (copy_err);
            remove (tmp_copy);
            g_free (tmp_copy);
            g_object_unref (file);
            preview_show_page (self, PREVIEW_PAGE_EMPTY);
            return;
        }
        local_path = tmp_copy;
        used_tmp_copy = TRUE;
        DEBUG ("office: temp copy at %s", local_path);
    }
    g_object_unref (file);

    gchar *tmp_dir = g_dir_make_tmp ("nemo-preview-XXXXXX", NULL);
    if (!tmp_dir) {
        if (used_tmp_copy) { remove (local_path); }
        g_free (local_path);
        preview_show_page (self, PREVIEW_PAGE_EMPTY);
        return;
    }
    p->lo_tmp_dir = tmp_dir;

    /* NOTE: --outdir must be a SEPARATE argument — LO does not accept --outdir=path */
    const gchar *argv[] = { "libreoffice", "--headless",
                             "--convert-to", "html",
                             "--outdir", tmp_dir,
                             local_path, NULL };

    GPid child_pid; GError *err = NULL;
    gboolean ok = g_spawn_async (NULL, (gchar **)argv, NULL,
                                 G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL, NULL, &child_pid, &err);

    if (!ok) {
        g_warning ("NemoPreviewPane: libreoffice spawn failed: %s", err ? err->message : "?");
        DEBUG ("office: spawn failed: %s", err ? err->message : "?");
        if (err) g_error_free (err);
        if (used_tmp_copy) { remove (local_path); }
        g_free (local_path);
        preview_show_page (self, PREVIEW_PAGE_EMPTY);
        return;
    }

    p->lo_pid = child_pid;

    gchar *basename = g_path_get_basename (local_path);
    gchar *dot = strrchr (basename, '.'); if (dot) *dot = '\0';
    gchar *html_name = g_strdup_printf ("%s.html", basename); g_free (basename);
    gchar *html_path = g_build_filename (tmp_dir, html_name, NULL); g_free (html_name);
    p->lo_html_path = html_path;

    /* Clean up temp input copy now — LO has already read the filename into argv */
    if (used_tmp_copy) { remove (local_path); }
    g_free (local_path);

    DEBUG ("office: spawned LO pid=%d, expecting output at %s", (int)child_pid, html_path);
    g_object_ref (self); /* released in lo_child_exited */
    p->lo_watch_id = g_child_watch_add (child_pid, lo_child_exited, self);
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

void
nemo_preview_pane_clear (NemoPreviewPane *pane)
{
    g_return_if_fail (NEMO_IS_PREVIEW_PANE (pane));
    DEBUG ("clear");
    preview_clear_media (pane);
    if (pane->priv->missing_copy_btn)
        gtk_button_set_label (GTK_BUTTON (pane->priv->missing_copy_btn),
                              _("Copy to Clipboard"));
    preview_show_page (pane, PREVIEW_PAGE_EMPTY);
}

void
nemo_preview_pane_load_uri (NemoPreviewPane *pane,
                             const char      *uri,
                             const char      *mime_type)
{
    g_return_if_fail (NEMO_IS_PREVIEW_PANE (pane));

    DEBUG ("load_uri: uri=%s mime=%s", uri ? uri : "(null)", mime_type ? mime_type : "(null)");

    preview_clear_media (pane);

    if (!uri || !mime_type) { preview_show_page (pane, PREVIEW_PAGE_EMPTY); return; }

    pane->priv->current_uri = g_strdup (uri);

    if      (mime_is_image  (mime_type)) preview_load_image  (pane, uri);
    else if (mime_is_audio  (mime_type)) preview_load_audio  (pane, uri);
    else if (mime_is_video  (mime_type)) preview_load_video  (pane, uri);
    else if (mime_is_pdf    (mime_type)) preview_load_pdf    (pane, uri);
    else if (mime_is_html   (mime_type)) preview_load_web    (pane, uri);
    else if (mime_is_office (mime_type)) preview_load_office (pane, uri);
    else {
        DEBUG ("unsupported MIME type: %s", mime_type);
        preview_show_page (pane, PREVIEW_PAGE_EMPTY);
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  Widget construction
 * ═══════════════════════════════════════════════════════════════════════════ */

static void
nemo_preview_pane_init (NemoPreviewPane *self)
{
    self->priv = nemo_preview_pane_get_instance_private (self);
    NemoPreviewPanePrivate *p = self->priv;

    gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
    p->gst_duration = -1.0;

    /* ── Stack ── */
    p->stack = gtk_stack_new ();
    gtk_stack_set_transition_type (GTK_STACK (p->stack), GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_box_pack_start (GTK_BOX (self), p->stack, TRUE, TRUE, 0);
    gtk_widget_show (p->stack);

    /* ── Page: EMPTY ── */
    p->empty_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_show (p->empty_box);
    gtk_stack_add_named (GTK_STACK (p->stack), p->empty_box, "empty");

    /* ── Page: MISSING_DEP ── */
    gtk_stack_add_named (GTK_STACK (p->stack), build_missing_dep_page (self), "missing");

    /* ── Page: IMAGE ── */
    {
        p->image_scroll = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (p->image_scroll),
                                        GTK_POLICY_NEVER, GTK_POLICY_NEVER);
        p->image_drawing_area = gtk_drawing_area_new ();
        gtk_widget_set_hexpand (p->image_drawing_area, TRUE);
        gtk_widget_set_vexpand (p->image_drawing_area, TRUE);
        g_signal_connect (p->image_drawing_area, "draw", G_CALLBACK (on_image_draw), self);
        /* Redraw when the pane is resized */
        g_signal_connect_swapped (p->image_drawing_area, "size-allocate",
                                  G_CALLBACK (gtk_widget_queue_draw), p->image_drawing_area);
        gtk_container_add (GTK_CONTAINER (p->image_scroll), p->image_drawing_area);
        gtk_widget_show_all (p->image_scroll);
        gtk_stack_add_named (GTK_STACK (p->stack), p->image_scroll, "image");
    }

    /* ── Page: AUDIO ── */
    {
        p->audio_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_valign (p->audio_box, GTK_ALIGN_CENTER);

        GtkWidget *icon = gtk_image_new_from_icon_name (
            "audio-x-generic-symbolic", GTK_ICON_SIZE_DIALOG);
        gtk_widget_set_opacity (icon, 0.5);
        gtk_box_pack_start (GTK_BOX (p->audio_box), icon, FALSE, FALSE, 12);

        p->audio_label = gtk_label_new (_("Audio file"));
        gtk_label_set_ellipsize (GTK_LABEL (p->audio_label), PANGO_ELLIPSIZE_END);
        gtk_widget_set_margin_start (p->audio_label, 8);
        gtk_widget_set_margin_end   (p->audio_label, 8);
        gtk_widget_set_opacity (p->audio_label, 0.6);
        gtk_box_pack_start (GTK_BOX (p->audio_box), p->audio_label, FALSE, FALSE, 0);

        GtkWidget *ctrl = build_media_controls (self,
                                                &p->audio_play_btn,
                                                &p->audio_seek,
                                                &p->audio_time_label);
        gtk_box_pack_end (GTK_BOX (p->audio_box), ctrl, FALSE, FALSE, 0);
        gtk_widget_show_all (p->audio_box);
        gtk_stack_add_named (GTK_STACK (p->stack), p->audio_box, "audio");
    }

    /* ── Page: VIDEO ── */
    {
        p->video_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

        p->video_area = gtk_drawing_area_new ();
        gtk_widget_set_hexpand (p->video_area, TRUE);
        gtk_widget_set_vexpand (p->video_area, TRUE);

        GtkCssProvider *css = gtk_css_provider_new ();
        gtk_css_provider_load_from_data (css, "* { background-color: black; }", -1, NULL);
        gtk_style_context_add_provider (gtk_widget_get_style_context (p->video_area),
                                        GTK_STYLE_PROVIDER (css),
                                        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref (css);

        gtk_box_pack_start (GTK_BOX (p->video_box), p->video_area, TRUE, TRUE, 0);

        GtkWidget *ctrl = build_media_controls (self,
                                                &p->video_play_btn,
                                                &p->video_seek,
                                                &p->video_time_label);
        gtk_box_pack_end (GTK_BOX (p->video_box), ctrl, FALSE, FALSE, 0);
        gtk_widget_show_all (p->video_box);
        gtk_stack_add_named (GTK_STACK (p->stack), p->video_box, "video");
    }

    /* ── Page: PDF ── */
    {
        p->pdf_scroll = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (p->pdf_scroll),
                                        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_widget_show (p->pdf_scroll);
        gtk_stack_add_named (GTK_STACK (p->stack), p->pdf_scroll, "pdf");
    }

    /* ── Page: WEB — placeholder; real WebKitWebView created lazily ── */
    {
        GtkWidget *ph = gtk_label_new ("");
        gtk_widget_show (ph);
        gtk_stack_add_named (GTK_STACK (p->stack), ph, "web");
        p->web_view = NULL; /* created on first use */
    }

    gtk_stack_set_visible_child_name (GTK_STACK (p->stack), "empty");
    p->current_page = PREVIEW_PAGE_EMPTY;

    DEBUG ("NemoPreviewPane initialised");
}

static void
nemo_preview_pane_finalize (GObject *object)
{
    NemoPreviewPane *self = NEMO_PREVIEW_PANE (object);
    NemoPreviewPanePrivate *p = self->priv;

    preview_clear_media (self);
    if (p->image_pixbuf) { g_object_unref (p->image_pixbuf); p->image_pixbuf = NULL; }
    g_free (p->missing_cmd_text);

    G_OBJECT_CLASS (nemo_preview_pane_parent_class)->finalize (object);
}

static void
nemo_preview_pane_class_init (NemoPreviewPaneClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = nemo_preview_pane_finalize;
}

GtkWidget *
nemo_preview_pane_new (void)
{
    return g_object_new (NEMO_TYPE_PREVIEW_PANE,
                         "orientation", GTK_ORIENTATION_VERTICAL,
                         "spacing", 0,
                         NULL);
}
