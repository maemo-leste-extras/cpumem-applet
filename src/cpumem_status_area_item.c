/*
 * Cpumem-applet - status area plugin
 */


#include <gtk/gtk.h>
#include <hildon/hildon.h>
#include <glib/gerror.h>
#include <glib.h>
#include <string.h>
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

	//Open the memory info file and get current free memory
	fin = fopen(MEMFILE, "r");
	if (fin == NULL) {
		g_warning("Can't open "MEMFILE"\n");
		return TRUE;
	}
	while (fgets(read_buffer, MAX_READ_CHARS, fin) != NULL) {
		if (strncmp(read_buffer, "MemTotal", 8) == 0) {
			sscanf(read_buffer + 10, "%d", &mem_total);
		} else if (strncmp(read_buffer, "MemFree", 6) == 0) {
			sscanf(read_buffer + 9, "%d", &mem_free);
		} else if (strncmp(read_buffer, "Buffers", 6) == 0) {
			sscanf(read_buffer + 9, "%d", &mem_buffers);
		} else if (strncmp(read_buffer, "Cached", 6) == 0) {
			sscanf(read_buffer + 8, "%d", &mem_cached);
			break;
		}
	}
	fclose(fin);

	mem_used = mem_total - mem_free - mem_buffers - mem_cached;

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
	gint curU, curN, curIO, curI;
	gint deltaU, deltaN, deltaIO, deltaI;
	int load, idle;
	GError *error = NULL;
	gchar *contents;
	gsize lenght;
	gchar **splits;

	if (!g_file_get_contents (CPUFILE, &contents, &lenght, &error)) {
		fprintf (stderr, "ERR: can't read file %s: %s\n", CPUFILE, error->message);
		g_error_free (error);
		return 0;
	}
	
	splits = g_strsplit_set (contents, " ",  -1);

	sscanf(splits[2], "%d", &curU);
	sscanf(splits[3], "%d", &curN);
	sscanf(splits[4], "%d", &curIO);
	sscanf(splits[5], "%d", &curI);
	
	g_strfreev (splits);
	g_free (contents);
    
	idle = (curI - priv->lastI);
	if (idle == 0) load = 100;
	else load = 100-idle;
	if (load>100) load = 0;
	deltaU = curU - priv->lastU;
	deltaN = curN - priv->lastN;
	deltaIO = curIO - priv->lastIO;
	deltaI = curI - priv->lastI;
	priv->lastU = curU;
	priv->lastN = curN;
	priv->lastIO = curIO;
	priv->lastI = curI;

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
	
	gdk_pixbuf_fill(priv->pixbuf, 0x00000000);

	x = 9;
	y = 1;
	if (level > 4)
		gdk_pixbuf_composite(priv->pixbuf_on, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
	y = 5;
	if (level > 3)
		gdk_pixbuf_composite(priv->pixbuf_on, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
	y = 9;
	if (level > 2)
		gdk_pixbuf_composite(priv->pixbuf_on, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
	y = 13;
	if (level > 1)
		gdk_pixbuf_composite(priv->pixbuf_on, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
	else
		gdk_pixbuf_composite(priv->pixbuf_off, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
}


/* 
 * Compose and blit current status of CPU bars
 */
static void
la_blit_cpu_bars (const guchar level, CpumemAppletStatusAreaItemPrivate *priv)
{
	guint x, y;
	
	x = 2;
	y = 1;
	if (level > 4)
	{
		if (priv->red == TRUE) {
			gdk_pixbuf_composite(priv->pixbuf_red, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
			priv->red = FALSE;
		} else {
			gdk_pixbuf_composite(priv->pixbuf_on, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
			priv->red = TRUE;
		}
	}
	y = 5;
	if (level > 3)
		gdk_pixbuf_composite(priv->pixbuf_on, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
	y = 9;
	if (level > 2)
		gdk_pixbuf_composite(priv->pixbuf_on, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
	y = 13;
	if (level > 1)
		gdk_pixbuf_composite(priv->pixbuf_on, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
	else
		gdk_pixbuf_composite(priv->pixbuf_off, priv->pixbuf, x, y, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT, x, y, 1, 1, GDK_INTERP_NEAREST, 255);
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
   
	current_cpu_level = la_check_cpu(priv); 
	current_mem_level = la_check_mem(priv);
	//g_debug(g_strdup_printf("LOADAPLET - UPDATED CPU %d MEM %d", current_cpu_level, current_mem_level));
	
	//Update and blit only if data changed!
	if ((current_mem_level != priv->last_mem_level) || (current_cpu_level != priv->last_cpu_level)) {
		la_blit_memory_bars (current_mem_level, priv);
		la_blit_cpu_bars (current_cpu_level, priv);
		if (current_cpu_level == CPUMEM_CPU_MAX)
			priv->red = FALSE;
		hd_status_plugin_item_set_status_area_icon (HD_STATUS_PLUGIN_ITEM(data), priv->pixbuf);
		priv->last_mem_level = current_mem_level;
		priv->last_cpu_level = current_cpu_level;
	} else if (current_cpu_level == CPUMEM_CPU_MAX) {
		//Pulsate max CPU load icon also when CPU load stays at max
		la_blit_cpu_bars (current_cpu_level, priv);
		hd_status_plugin_item_set_status_area_icon (HD_STATUS_PLUGIN_ITEM(data), priv->pixbuf);
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

	g_return_if_fail (item != NULL && item->priv != NULL);

	if (state == OSSO_DISPLAY_ON)
    {
		//Restart the updates, do one right away
		if (item->priv->timeout_id == -1) 
		{
			item->priv->timeout_id = gtk_timeout_add(1000, la_check_load, item);
		}
    } else {
		//Suspend the updates - screen is off
		if (g_source_remove(item->priv->timeout_id) != TRUE)
		{
		} else {
			item->priv->timeout_id = -1;
		}
	}
}


/*****************************************************************************
 *
 * Boilerplate code area - do not enter
 *
 *****************************************************************************/

static void
cpumem_applet_status_area_item_set_area_icon (CpumemAppletStatusAreaItem *item)
{
	item->priv = CPUMEM_APPLET_STATUS_AREA_ITEM_GET_PRIVATE (item);
	
	hd_status_plugin_item_set_status_area_icon (HD_STATUS_PLUGIN_ITEM(item), item->priv->pixbuf);
}


static void
cpumem_applet_status_area_item_class_finalize (CpumemAppletStatusAreaItemClass *klass)
{
}



static void
cpumem_applet_status_area_item_finalize (GObject *object)
{
	CpumemAppletStatusAreaItemPrivate *priv = CPUMEM_APPLET_STATUS_AREA_ITEM(object)->priv;
	// Release and clean our stuff
	G_OBJECT_CLASS (cpumem_applet_status_area_item_parent_class)->finalize (object);
	if (priv->osso)
    {
		osso_deinitialize(priv->osso);
		priv->osso = NULL;
    }

}



static void
cpumem_applet_status_area_item_class_init (CpumemAppletStatusAreaItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = cpumem_applet_status_area_item_finalize;

	g_type_class_add_private (klass, sizeof (CpumemAppletStatusAreaItemPrivate));

}

static void
cpumem_applet_status_area_item_init (CpumemAppletStatusAreaItem *item)
{
	item->priv = CPUMEM_APPLET_STATUS_AREA_ITEM_GET_PRIVATE (item);
	
	item->priv->last_mem_level = -1;
	item->priv->last_cpu_level = -1;
	item->priv->timeout_id = -1;
	item->priv->red = FALSE;
	item->priv->pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, CPUMEM_ICON_WIDTH, CPUMEM_ICON_HEIGHT);
	gdk_pixbuf_fill(item->priv->pixbuf, 0x00000000);
	item->priv->pixbuf_on = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT);
	gdk_pixbuf_fill(item->priv->pixbuf_on, 0xffffffff);
	item->priv->pixbuf_red = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT);
	gdk_pixbuf_fill(item->priv->pixbuf_red, 0xff0000ff);	
	item->priv->pixbuf_off = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, CPUMEM_BOX_WIDTH, CPUMEM_BOX_HEIGHT);
	gdk_pixbuf_fill(item->priv->pixbuf_off, 0x777777ff);
	cpumem_applet_status_area_item_set_area_icon(item);

	item->priv->osso = osso_initialize ("cpumem_applet_status_area_item", "Maemo5", TRUE, NULL);
	item->priv->timeout_id = gtk_timeout_add(1000, la_check_load, item);
	osso_hw_set_display_event_cb (item->priv->osso, cpumem_applet_status_area_item_display_cb, item);
}

