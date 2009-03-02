/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*-
 *
 * * Copyright (C) 2008 Ali <aliov@xfce.org>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include <glib.h>
#include <libxfcegui4/libxfcegui4.h>
#include <libxfce4util/libxfce4util.h>

#include "xfpm-common.h"
#include "xfpm-debug.h"

static void
xfpm_link_browser(GtkAboutDialog *about,const gchar *link,gpointer data)
{
	gchar *cmd = g_strdup_printf("%s %s","xfbrowser4",link);
	xfce_exec(cmd,FALSE,FALSE,NULL);
	g_free(cmd);
	
}

static void
xfpm_link_mailto(GtkAboutDialog *about,const gchar *link,gpointer data)
{
	gchar *cmd = g_strdup_printf("%s %s","xdg-email",link);
	xfce_exec(cmd,FALSE,FALSE,NULL);
	g_free(cmd);
}
	

GdkPixbuf *
xfpm_load_icon(const char *icon_name,gint size)
{
    GdkPixbuf *icon;
    GError *error = NULL;
    
    icon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default (),
                                   icon_name,
                                   size,
                                   GTK_ICON_LOOKUP_FORCE_SVG,
                                   &error);
    if ( error )
    {
        XFPM_DEBUG("Error occured while loading icon %s: %s\n",icon_name,error->message);
        g_error_free(error);
    }
    return icon;                               
}

void       
xfpm_lock_screen(void)
{
    gboolean ret = g_spawn_command_line_async("xflock4",NULL);
    
    if ( !ret )
    {
        g_spawn_command_line_async("gnome-screensaver-command -l",NULL);
    }
    
    if ( !ret )
    {
        /* this should be the default*/
        ret = g_spawn_command_line_async("xdg-screensaver lock",NULL);
    }
    
    if ( !ret )
    {
        ret = g_spawn_command_line_async("xscreensaver-command -lock",NULL);
    }
    
    if ( !ret )
    {
        g_critical("Connot lock screen\n");
    }
}

void       
xfpm_preferences(void) 
{
    g_spawn_command_line_async("xfce4-power-manager-settings",NULL);
}

void       
xfpm_help(void)
{
	xfce_exec("xfhelp4 xfce4-power-manager.html", FALSE, FALSE, NULL);
}

void       
xfpm_about(GtkWidget *widget,gpointer data)
{
	const gchar* authors[3] = 
	{
		"Ali Abdallah <aliov@xfce.org>", 
		NULL
	};
								
	static const gchar *documenters[] =
	{
		"Ali Abdallah <aliov@xfce.org>",
		NULL,
	};

	gtk_about_dialog_set_url_hook (xfpm_link_browser, NULL, NULL);
	gtk_about_dialog_set_email_hook(xfpm_link_mailto,NULL,NULL);
	gtk_show_about_dialog (NULL,
                         "authors", authors,
                         "copyright", "Copyright \302\251 2008 Ali Abdallah",
                         "destroy-with-parent", TRUE,
                         "documenters", documenters,
                         "license", XFCE_LICENSE_GPL,
                         "name", _("Xfce4 Power Manager"),
                         "translator-credits", _("translator-credits"),
                         "version", PACKAGE_VERSION,
                         "website", "http://goodies.xfce.org",
                         NULL);
						 
}
