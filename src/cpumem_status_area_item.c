/*
 * Cpumem-applet - status area plugin with extensive debugging
 */

#include <gtk/gtk.h>
#include <hildon/hildon.h>
#include <glib/gerror.h>
#include <glib.h>
#include <string.h>
#include <inttypes.h>
#include <libosso.h>

#include "cpumem_status_area_item.h"

#define CPUMEM_ICON_WIDTH  16
#define CPUMEM_ICON_HEIGHT 16
#define CPUMEM_BOX_WIDTH   5
#define CPUMEM_BOX_HEIGHT  3
#define CPUMEM_CPU_MAX 5

#define CPUMEM_APPLET_STATUS_AREA_ITEM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE (obj, CPUMEM_APPLET_TYPE_STATUS_AREA_ITEM, CpumemAppletStatusAreaItemPrivate))

struct _CpumemAppletStatusAreaItemPrivate {
  guint timeout_id;
  gint lastU, lastN, lastIO, lastI;
  guchar last_mem_level;
  guchar last_cpu_level;
  GdkPixbuf *pixbuf;
  GdkPixbuf *pixbuf_on;
  GdkPixbuf *pixbuf_red;
  GdkPixbuf *pixbuf_off;
  osso_context_t *osso;
  gboolean red;
};

HD_DEFINE_PLUGIN_MODULE (CpumemAppletStatusAreaItem, cpumem_applet_status_area_item, HD_TYPE_STATUS_PLUGIN_ITEM);

/*
 * Read current MEM usage and return indicator between 5 and 1 - how many bars are "full"
 */
static guchar
la_check_mem (CpumemAppletStatusAreaItemPrivate *priv)
{
  #define MEMFILE "/proc/meminfo"
  #define MAX_READ_CHARS 128
  char read_buffer[MAX_READ_CHARS];
  FILE *fin;
  int mem_used = 0;
  int mem_total = 0;
  int mem_cached = 0;
  int mem_buffers = 0;
  int mem_free = 0;

  g_debug("la_check_mem: opening %s", MEMFILE);
  fin = fopen(MEMFILE, "r");
  if (fin == NULL) {
    g_warning("Can't open " MEMFILE);
    return 1;
  }

  while (fgets(read_buffer, MAX_READ_CHARS, fin) != NULL) {
    g_debug("la_check_mem: read line: %s", read_buffer);
    if (strncmp(read_buffer, "MemTotal", 8) == 0) {
      sscanf(read_buffer + 10, "%d", &mem_total);
      g_debug("MemTotal = %d", mem_total);
    } else if (strncmp(read_buffer, "MemFree", 6) == 0) {
      sscanf(read_buffer + 9, "%d", &mem_free);
      g_debug("MemFree = %d", mem_free);
    } else if (strncmp(read_buffer, "Buffers", 6) == 0) {
      sscanf(read_buffer + 9, "%d", &mem_buffers);
      g_debug("Buffers = %d", mem_buffers);
    } else if (strncmp(read_buffer, "Cached", 6) == 0) {
      sscanf(read_buffer + 8, "%d", &mem_cached);
      g_debug("Cached = %d", mem_cached);
      break;
    }
  }
  fclose(fin);

  mem_used = mem_total - mem_free - mem_buffers - mem_cached;
  g_debug("Memory used calculated: %d/%d", mem_used, mem_total);

  if (mem_used > 0.9*mem_total)
    return 5;
  else if (mem_used > 0.7*mem_total)
    return 4;
  else if (mem_used > 0.5*mem_total)
    return 3;
  else if (mem_used > 0.3*mem_total)
    return 2;
  else
    return 1;
}

/*
 * Read current CPU usage and return indicator between 5 and 1 - how many bars are "full"
 */
static guchar
la_check_cpu (CpumemAppletStatusAreaItemPrivate *priv)
{
#define CPUFILE "/proc/stat"
  guint64 user, nice, system, idle, iowait, irq, softirq, steal;
  guint64 cur_total, cur_idle;
  guint64 delta_total, delta_idle;
  gint load;
  FILE *f;

  f = fopen(CPUFILE, "r");
  if (!f) {
    g_warning("Can't open %s", CPUFILE);
    return 1;
  }

  if (fscanf(
        f,
        "cpu  %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
        " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
        &user, &nice, &system, &idle,
        &iowait, &irq, &softirq, &steal
      ) < 4) {
    fclose(f);
    g_warning("Failed to parse %s", CPUFILE);
    return 1;
      }

  fclose(f);

  cur_idle = idle + iowait;
  cur_total = user + nice + system + idle + iowait + irq + softirq + steal;

  if (priv->lastI == 0) {
    priv->lastI = cur_idle;
    priv->lastU = cur_total;
    return 1;
  }

  delta_idle = cur_idle - priv->lastI;
  delta_total = cur_total - priv->lastU;

  priv->lastI = cur_idle;
  priv->lastU = cur_total;

  if (delta_total == 0)
    return 1;

  load = (gint)(((delta_total - delta_idle) * 100) / delta_total);

  g_debug(
    "CPU load=%d%% (total=%" PRIu64 " idle=%" PRIu64 ")",
    load, delta_total, delta_idle
  );

  if (load > 90)
    return 5;
  else if (load > 70)
    return 4;
  else if (load > 45)
    return 3;
  else if (load > 19)
    return 2;
  else
    return 1;
}

/*
 * Compose and blit the current status of memory bars
 */
static void
la_blit_memory_bars (const guchar level, CpumemAppletStatusAreaItemPrivate *priv)
{
  guint x, y;
  g_debug("Blitting memory bars, level=%d", level);

  gdk_pixbuf_fill(priv->pixbuf, 0x00000000);

  x = 9;
  for (y = 1; y <= 13; y += 4) {
    if (level >= ((y + 3) / 4)) {
      g_debug("Blitting ON bar at y=%d", y);
      gdk_pixbuf_composite(priv->pixbuf_on, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
    } else {
      g_debug("Blitting OFF bar at y=%d", y);
      gdk_pixbuf_composite(priv->pixbuf_off, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
    }
  }
}

/*
 * Compose and blit current status of CPU bars
 */
static void
la_blit_cpu_bars (const guchar level, CpumemAppletStatusAreaItemPrivate *priv)
{
  guint x, y;
  g_debug("Blitting CPU bars, level=%d", level);

  x = 2;
  for (y = 1; y <= 13; y += 4) {
    if (level >= ((y + 3) / 4)) {
      if (priv->red == TRUE && y == 1) {
        g_debug("Blitting RED bar at y=%d", y);
        gdk_pixbuf_composite(priv->pixbuf_red, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
        priv->red = FALSE;
      } else {
        g_debug("Blitting ON bar at y=%d", y);
        gdk_pixbuf_composite(priv->pixbuf_on, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
        priv->red = TRUE;
      }
    } else {
      g_debug("Blitting OFF bar at y=%d", y);
      gdk_pixbuf_composite(priv->pixbuf_off, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
    }
  }
}

/*
 * Ran to check and update the CPU and memory reading
 */
static gboolean
la_check_load (gpointer data)
{
  guchar current_cpu_level;
  guchar current_mem_level;
  CpumemAppletStatusAreaItem *item = (CpumemAppletStatusAreaItem*)data;
  CpumemAppletStatusAreaItemPrivate *priv = (CpumemAppletStatusAreaItemPrivate*)item->priv;

  g_debug("la_check_load: running CPU/mem check");
  current_cpu_level = la_check_cpu(priv);
  current_mem_level = la_check_mem(priv);

  if ((current_mem_level != priv->last_mem_level) || (current_cpu_level != priv->last_cpu_level)) {
    g_debug("Levels changed: CPU %d -> %d, MEM %d -> %d", priv->last_cpu_level, current_cpu_level, priv->last_mem_level, current_mem_level);
    la_blit_memory_bars(current_mem_level, priv);
    la_blit_cpu_bars(current_cpu_level, priv);
    if (current_cpu_level == CPUMEM_CPU_MAX)
      priv->red = FALSE;
    hd_status_plugin_item_set_status_area_icon(HD_STATUS_PLUGIN_ITEM(data), priv->pixbuf);
    priv->last_mem_level = current_mem_level;
    priv->last_cpu_level = current_cpu_level;
  } else if (current_cpu_level == CPUMEM_CPU_MAX) {
    g_debug("CPU max level sustained, pulsating icon");
    la_blit_cpu_bars(current_cpu_level, priv);
    hd_status_plugin_item_set_status_area_icon(HD_STATUS_PLUGIN_ITEM(data), priv->pixbuf);
  }

  return TRUE;
}

/*
 * Get callback when display state changes
 */
static void
cpumem_applet_status_area_item_display_cb(osso_display_state_t state, gpointer user_data)
{
  CpumemAppletStatusAreaItem *item = CPUMEM_APPLET_STATUS_AREA_ITEM(user_data);
  g_return_if_fail(item != NULL && item->priv != NULL);

  g_debug("Display callback: state=%d", state);
  if (state == OSSO_DISPLAY_ON) {
    g_debug("Display ON: starting updates");
    if (item->priv->timeout_id == -1) {
      item->priv->timeout_id = gtk_timeout_add(1000, la_check_load, item);
    }
  } else {
    g_debug("Display OFF: stopping updates");
    if (g_source_remove(item->priv->timeout_id)) {
      item->priv->timeout_id = -1;
    }
  }
}

/*****************************************************************************
 *
 * Boilerplate code area
 *
 *****************************************************************************/

static void
cpumem_applet_status_area_item_set_area_icon (CpumemAppletStatusAreaItem *item)
{
  item->priv = CPUMEM_APPLET_STATUS_AREA_ITEM_GET_PRIVATE(item);
  g_debug("Setting initial status area icon");
  hd_status_plugin_item_set_status_area_icon(HD_STATUS_PLUGIN_ITEM(item), item->priv->pixbuf);
}

static void
cpumem_applet_status_area_item_class_finalize (CpumemAppletStatusAreaItemClass *klass)
{
}

static void
cpumem_applet_status_area_item_finalize (GObject *object)
{
  CpumemAppletStatusAreaItemPrivate *priv = CPUMEM_APPLET_STATUS_AREA_ITEM(object)->priv;
  g_debug("Finalizing object, cleaning resources");
  if (priv->osso) {
    osso_deinitialize(priv->osso);
    priv->osso = NULL;
  }
  G_OBJECT_CLASS(cpumem_applet_status_area_item_parent_class)->finalize(object);
}

static void
cpumem_applet_status_area_item_class_init (CpumemAppletStatusAreaItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = cpumem_applet_status_area_item_finalize;
  g_type_class_add_private(klass, sizeof(CpumemAppletStatusAreaItemPrivate));
}

static void
cpumem_applet_status_area_item_init (CpumemAppletStatusAreaItem *item)
{
  item->priv = CPUMEM_APPLET_STATUS_AREA_ITEM_GET_PRIVATE(item);

  item->priv->last_mem_level = -1;
  item->priv->last_cpu_level = -1;
  item->priv->timeout_id = -1;
  item->priv->red = FALSE;

  g_debug("Initializing pixbufs");
  item->priv->pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, CPUMEM_ICON_WIDTH, CPUMEM_ICON_HEIGHT);
  gdk_pixbuf_fill(item->priv->pixbuf, 0x00000000);
  item->priv->pixbuf_on = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT);
  gdk_pixbuf_fill(item->priv->pixbuf_on, 0xffffffff);
  item->priv->pixbuf_red = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT);
  gdk_pixbuf_fill(item->priv->pixbuf_red, 0xff0000ff);
  item->priv->pixbuf_off = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT);
  gdk_pixbuf_fill(item->priv->pixbuf_off, 0x777777ff);

  cpumem_applet_status_area_item_set_area_icon(item);

  g_debug("Initializing OSSO context");
  item->priv->osso = osso_initialize("cpumem_applet_status_area_item", "Maemo5", TRUE, NULL);
  g_debug("Starting periodic load check timer");
  item->priv->timeout_id = gtk_timeout_add(1000, la_check_load, item);
  osso_hw_set_display_event_cb(item->priv->osso, cpumem_applet_status_area_item_display_cb, item);
}
