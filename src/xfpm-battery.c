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

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gtk/gtk.h>
#include <glib.h>

#include <libxfcegui4/libxfcegui4.h>
#include <libxfce4util/libxfce4util.h>
#include <xfconf/xfconf.h>

#include "xfpm-battery.h"
#include "xfpm-battery-icon.h"
#include "xfpm-hal.h"
#include "xfpm-debug.h"
#include "xfpm-common.h"
#include "xfpm-enum-types.h"
#include "xfpm-notify.h"
#include "xfpm-marshal.h"

#ifndef _
#define _(x) x
#endif

#define XFPM_BATTERY_GET_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE(o,XFPM_TYPE_BATTERY,XfpmBatteryPrivate))

static void xfpm_battery_init(XfpmBattery *battery);
static void xfpm_battery_class_init(XfpmBatteryClass *klass);
static void xfpm_battery_finalize(GObject *object);


static void xfpm_battery_set_property (GObject *object,
                                   guint prop_id,
                                   const GValue *value,
                                   GParamSpec *pspec);
static void xfpm_battery_get_property (GObject *object,
                                   guint prop_id,
                                   GValue *value,
                                   GParamSpec *pspec);

static void xfpm_battery_refresh_tray_icon(XfpmBattery *batt);
#ifdef HAVE_LIBNOTIFY
static void xfpm_battery_refresh_notification(XfpmBattery *batt);
#endif
static void xfpm_battery_refresh_critical_charge(XfpmBattery *batt);
static void xfpm_battery_refresh(XfpmBattery *batt);

static void xfpm_battery_notify_cb (GObject *object,
                                    GParamSpec *arg1,
                                    gpointer data);

static void xfpm_battery_load_config(XfpmBattery *batt);

static void xfpm_battery_handle_device_added(XfpmHal *hal,
                                             const gchar *udi,
                                             XfpmBattery *batt);
                                             
static void xfpm_battery_handle_device_removed(XfpmHal *hal,
                                               const gchar *udi,
                                               XfpmBattery *batt);
                                               
static void xfpm_battery_handle_device_property_changed(XfpmHal *hal,const gchar *udi,
                                                        const gchar *key,gboolean is_removed,
                                                        gboolean is_added,XfpmBattery *batt);
                                                        
static void xfpm_battery_show_critical_options(XfpmBattery *batt,
											   XfpmBatteryIcon *icon,
											   XfpmBatteryType battery_type);
static void xfpm_battery_handle_primary_critical(XfpmBattery *batt,
                                                XfpmBatteryIcon *icon);
static void xfpm_battery_handle_critical_charge(XfpmBattery *batt,
                                                XfpmBatteryIcon *icon);
                                                
static void xfpm_battery_state_change_cb(GObject *object,
                                         GParamSpec *arg1,
                                         gpointer data);

static XfpmBatteryType xfpm_battery_get_battery_type(const gchar *battery_type);

static gboolean xfpm_battery_check(XfpmBattery *batt,
                                    const gchar *udi);
static gboolean xfpm_battery_is_new(XfpmBattery *batt, 
                                    const gchar *udi, 
                                    XfpmBatteryType battery_type, 
                                    guint last_full);

static void xfpm_battery_hibernate_callback(GtkWidget *widget,
                                            XfpmBattery *batt);
static void xfpm_battery_suspend_callback(GtkWidget *widget,
                                          XfpmBattery *batt);
static void xfpm_battery_popup_tray_icon_menu(GtkStatusIcon *tray_icon,
                                             guint button,
                                             guint activate_time,
                                             XfpmBattery *batt);

static void xfpm_battery_new_device(XfpmBattery *batt,
                                    const gchar *udi);
static void xfpm_battery_get_devices(XfpmBattery *batt);
                                                        
struct XfpmBatteryPrivate
{
    XfpmHal *hal;
    GHashTable *batteries;

    guint8 power_management;
    
    gboolean ups_found;
        
};
                                
G_DEFINE_TYPE(XfpmBattery,xfpm_battery,G_TYPE_OBJECT)

enum 
{
    XFPM_SHOW_ADAPTER_ICON,
    XFPM_ACTION_REQUEST,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0,}; 

enum
{
    PROP_0,
    PROP_AC_ADAPTER,
    PROP_CRITICAL_CHARGE,
    PROP_CRITICAL_ACTION,
#ifdef HAVE_LIBNOTIFY
    PROP_ENABLE_NOTIFICATION,
#endif
    PROP_SHOW_TRAY_ICON    
};

static void
xfpm_battery_class_init(XfpmBatteryClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    
    gobject_class->set_property = xfpm_battery_set_property;
    gobject_class->get_property = xfpm_battery_get_property;
    
    gobject_class->finalize = xfpm_battery_finalize;
    
    signals[XFPM_SHOW_ADAPTER_ICON] = g_signal_new("xfpm-show-adapter-icon",
                                                   XFPM_TYPE_BATTERY,
                                                   G_SIGNAL_RUN_LAST,
                                                   G_STRUCT_OFFSET(XfpmBatteryClass,show_adapter_icon),
                                                   NULL,NULL,
                                                   g_cclosure_marshal_VOID__BOOLEAN,
                                                   G_TYPE_NONE,1,G_TYPE_BOOLEAN);
                                                   
    signals[XFPM_ACTION_REQUEST] = g_signal_new("xfpm-action-request",
                                               XFPM_TYPE_BATTERY,
                                               G_SIGNAL_RUN_LAST,
                                               G_STRUCT_OFFSET(XfpmBatteryClass,battery_action_request),
                                               NULL,NULL,
                                               _xfpm_marshal_VOID__ENUM_BOOLEAN ,
                                               G_TYPE_NONE,2,
                                               XFPM_TYPE_ACTION_REQUEST,G_TYPE_BOOLEAN);
                                                   
    g_object_class_install_property(gobject_class,
                                    PROP_AC_ADAPTER,
                                    g_param_spec_boolean("on-ac-adapter",
                                                         "On ac adapter",
                                                         "On Ac power",
                                                         TRUE,
                                                         G_PARAM_READWRITE));
    
    g_object_class_install_property(gobject_class,
                                    PROP_CRITICAL_CHARGE,
                                    g_param_spec_uint("critical-charge",
                                                      "Critical charge",
                                                      "Critical battery charge",
                                                      1,
                                                      20,
                                                      10,
                                                      G_PARAM_READWRITE));
    
    g_object_class_install_property(gobject_class,
                                    PROP_CRITICAL_ACTION,
                                    g_param_spec_enum("critical-action",
                                                      "Critical action",
                                                      "Battery critical charge action",
                                                      XFPM_TYPE_ACTION_REQUEST,
                                                      XFPM_DO_NOTHING,
                                                      G_PARAM_READWRITE));
    
#ifdef HAVE_LIBNOTIFY    
    g_object_class_install_property(gobject_class,
                                    PROP_ENABLE_NOTIFICATION,
                                    g_param_spec_boolean("enable-notification",
                                                         "Notify enabled",
                                                         "Notification",
                                                         FALSE,
                                                         G_PARAM_READWRITE));

#endif
     g_object_class_install_property(gobject_class,
                                    PROP_SHOW_TRAY_ICON,
                                    g_param_spec_enum("show-tray-icon",
                                                     "Show tray icon",
                                                     "Tray Icon",
                                                      XFPM_TYPE_SHOW_ICON,
                                                      0,
                                                      G_PARAM_READWRITE));
                                                         
    g_type_class_add_private(klass,sizeof(XfpmBatteryPrivate));                                                     
}

static void
xfpm_battery_init(XfpmBattery *battery)
{
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(battery);
    
    priv->batteries = g_hash_table_new(g_str_hash,g_str_equal);
    
    xfpm_battery_load_config(battery);
    
    priv->hal = xfpm_hal_new();
    
    if (xfpm_hal_connect_to_signals(priv->hal,TRUE,TRUE,TRUE,FALSE) )
    {
        g_signal_connect(G_OBJECT(priv->hal),"xfpm-device-added",
                         G_CALLBACK(xfpm_battery_handle_device_added),
                         battery);
         
        g_signal_connect(G_OBJECT(priv->hal),"xfpm-device-removed",
                         G_CALLBACK(xfpm_battery_handle_device_removed),
                         battery);
                                          
        g_signal_connect(G_OBJECT(priv->hal),"xfpm-device-property-changed",
                         G_CALLBACK(xfpm_battery_handle_device_property_changed),
                         battery);                 
        
    }
    g_signal_connect(G_OBJECT(battery),"notify",
                        G_CALLBACK(xfpm_battery_notify_cb),NULL);
}

static void xfpm_battery_set_property(GObject *object,
                                      guint prop_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
#ifdef DEBUG
    gchar *content;
    content = g_strdup_value_contents(value);
    XFPM_DEBUG("param:%s value contents:%s\n",pspec->name,content);
    g_free(content);
#endif      
    XfpmBattery *battery;
    battery = XFPM_BATTERY(object);
    
    switch (prop_id)
    {
    case PROP_AC_ADAPTER:
        battery->ac_adapter_present = g_value_get_boolean(value);
        break;   
    case PROP_CRITICAL_CHARGE:
        battery->critical_level = g_value_get_uint(value);
        break;
    case PROP_CRITICAL_ACTION:
        battery->critical_action = g_value_get_enum(value);
        break;
    case PROP_SHOW_TRAY_ICON:
        battery->show_tray = g_value_get_enum(value);
        break;    
#ifdef HAVE_LIBNOTIFY
    case PROP_ENABLE_NOTIFICATION:
        battery->notify_enabled = g_value_get_boolean(value);
        break;
#endif                        
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object,prop_id,pspec);
        break;
    }
}    
                               
static void xfpm_battery_get_property(GObject *object,
                                      guint prop_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
    XfpmBattery *battery;
    battery = XFPM_BATTERY(object);
        
    switch (prop_id)
    {
    case PROP_AC_ADAPTER:
        g_value_set_boolean(value,battery->ac_adapter_present);
        break;  
    case PROP_CRITICAL_CHARGE:
        g_value_set_uint(value,battery->critical_level);
        break;
    case PROP_CRITICAL_ACTION:
        g_value_set_enum(value,battery->critical_action );
        break;
    case PROP_SHOW_TRAY_ICON:
        g_value_set_enum(value, battery->show_tray);
        break;
#ifdef HAVE_LIBNOTIFY
    case PROP_ENABLE_NOTIFICATION:
        g_value_set_boolean(value, battery->notify_enabled);
        break;
#endif                         
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object,prop_id,pspec);
        break;
    }                         
    
#ifdef DEBUG
    gchar *content;
    content = g_strdup_value_contents(value);
    XFPM_DEBUG("param:%s value contents:%s\n",pspec->name,content);
    g_free(content);
#endif  
}

static void
xfpm_battery_finalize(GObject *object)
{
    XfpmBattery *battery = XFPM_BATTERY(object);
    battery->priv = XFPM_BATTERY_GET_PRIVATE(battery);

    if ( battery->priv->batteries )
    {
        g_hash_table_unref(battery->priv->batteries);
    }
    G_OBJECT_CLASS(xfpm_battery_parent_class)->finalize(object);
}

static void
xfpm_battery_refresh_tray_icon(XfpmBattery *batt)
{
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);
    GList *icons_list;
    int i=0;
    GtkStatusIcon *icon;
    
    if ( batt->show_tray == ALWAYS && g_hash_table_size(priv->batteries) == 0 )
    {
        XFPM_DEBUG("batt->show_tray == ALWAYS && g_hash_table_size(priv->batteries) == 0\n");
        g_signal_emit(G_OBJECT(batt),signals[XFPM_SHOW_ADAPTER_ICON],0,TRUE);
    }
    else if ( batt->show_tray == CHARGING_OR_DISCHARGING && g_hash_table_size(priv->batteries) == 0 )
    {
        XFPM_DEBUG("batt->show_tray == CHARGING_OR_DISCHARGING && g_hash_table_size(priv->batteries) == 0 \n");
        g_signal_emit(G_OBJECT(batt),signals[XFPM_SHOW_ADAPTER_ICON],0,FALSE);
        
    }
    else if ( batt->show_tray == ALWAYS && g_hash_table_size(priv->batteries) != 0 )
    {
        XFPM_DEBUG("batt->show_tray == ALWAYS && g_hash_table_size(priv->batteries) != 0  \n");
        icons_list = g_hash_table_get_values(priv->batteries);
        g_signal_emit(G_OBJECT(batt),signals[XFPM_SHOW_ADAPTER_ICON],0,FALSE);
        for ( i = 0 ; i < g_list_length(icons_list) ; i++ )
        {
            icon = g_list_nth_data(icons_list,i);
            if ( icon ) 
            {
                g_object_set(G_OBJECT(icon),"visible",TRUE,NULL);
            }
        }
    }
    else if ( batt->show_tray == PRESENT )
    {
        g_signal_emit(G_OBJECT(batt),signals[XFPM_SHOW_ADAPTER_ICON],0,FALSE);
        XFPM_DEBUG("batt->show_tray == PRESENT \n");
        icons_list = g_hash_table_get_values(priv->batteries);
        for ( i = 0 ; i < g_list_length(icons_list) ; i++ )
        {
            icon = g_list_nth_data(icons_list,i);
            if ( icon ) 
            {
                g_object_set(G_OBJECT(icon),"visible",XFPM_BATTERY_ICON(icon)->battery_present,NULL);
            }
        }
    }
    else if ( batt->show_tray == CHARGING_OR_DISCHARGING )
    {
        g_signal_emit(G_OBJECT(batt),signals[XFPM_SHOW_ADAPTER_ICON],0,FALSE);
        XFPM_DEBUG("batt->show_tray == CHARGING_OR_DISCHARGING \n");
        icons_list = g_hash_table_get_values(priv->batteries);
        for ( i = 0 ; i < g_list_length(icons_list) ; i++ )
        {
            icon = g_list_nth_data(icons_list,i);
            if ( icon )
            {
                if (  XFPM_BATTERY_ICON(icon)->state == CHARGING || 
                      XFPM_BATTERY_ICON(icon)->state == DISCHARGING ||
                      XFPM_BATTERY_ICON(icon)->state == NOT_FULL   ||
                      XFPM_BATTERY_ICON(icon)->state == LOW        ||
                      XFPM_BATTERY_ICON(icon)->state == CRITICAL) 
                {
                    g_object_set(G_OBJECT(icon),"visible",TRUE,NULL);
                }
                else
                {
                    g_object_set(G_OBJECT(icon),"visible",FALSE,NULL);
                }
            }
        }
    }   
}

#ifdef HAVE_LIBNOTIFY
static void
xfpm_battery_refresh_notification(XfpmBattery *batt)
{
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);
    
    if ( g_hash_table_size(priv->batteries) == 0 ) 
    {
        return;
    }
    GList *icons_list;
    GtkStatusIcon *icon;
    int i;
    
    icons_list = g_hash_table_get_values(priv->batteries);
    for ( i = 0 ; i < g_list_length(icons_list) ; i++ )
    {
        icon = g_list_nth_data(icons_list,i);
        if ( icon ) 
        {
            g_object_set(G_OBJECT(icon),"systray-notify",batt->notify_enabled,NULL);
        }
    }
}
#endif

static void
xfpm_battery_refresh_critical_charge(XfpmBattery *batt)
{
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);
    
    if ( g_hash_table_size(priv->batteries) == 0 ) 
    {
        return;
    }
    GList *icons_list;
    GtkStatusIcon *icon;
    int i;
    
    icons_list = g_hash_table_get_values(priv->batteries);
    for ( i = 0 ; i < g_list_length(icons_list) ; i++ )
    {
        icon = g_list_nth_data(icons_list,i);
        if ( icon ) 
        {
            g_object_set(G_OBJECT(icon),"batt-critical-level",batt->critical_level,NULL);
        }
    }
}

static void
xfpm_battery_refresh(XfpmBattery *batt)
{
    xfpm_battery_refresh_tray_icon(batt);
    xfpm_battery_refresh_critical_charge(batt);
#ifdef HAVE_LIBNOTIFY
    xfpm_battery_refresh_notification(batt);
#endif        
}

static void 
xfpm_battery_notify_cb (GObject *object,
                        GParamSpec *arg1,
                        gpointer data)
{
    XFPM_DEBUG("arg1->name=%s\n",arg1->name);
    
    if (!strcmp(arg1->name,"show-tray-icon"))
    {
        xfpm_battery_refresh_tray_icon(XFPM_BATTERY(object));
    }
    else if ( !strcmp(arg1->name,"critical-charge"))
    {
        xfpm_battery_refresh_critical_charge(XFPM_BATTERY(object));
    }
#ifdef HAVE_LIBNOTIFY
    else if ( !strcmp(arg1->name,"enable-notification"))
    {
        xfpm_battery_refresh_notification(XFPM_BATTERY(object));
    }    
#endif 
}

static void
xfpm_battery_load_config(XfpmBattery *batt)
{
    XFPM_DEBUG("loading configuration\n");
    
    XfconfChannel *channel;
    channel = xfconf_channel_new(XFPM_CHANNEL_CFG);
    
    batt->critical_level  =  xfconf_channel_get_uint(channel,CRITICAL_BATT_CFG,10);
    batt->critical_action = xfconf_channel_get_uint(channel,CRITICAL_BATT_ACTION_CFG,XFPM_DO_NOTHING);
    batt->show_tray = xfconf_channel_get_uint(channel,SHOW_TRAY_ICON_CFG,ALWAYS);
#ifdef HAVE_LIBNOTIFY
     batt->notify_enabled = xfconf_channel_get_bool(channel,BATT_STATE_NOTIFICATION_CFG,TRUE);
#endif 
    g_object_unref(channel);
}

static void
xfpm_battery_handle_device_added(XfpmHal *hal,const gchar *udi,XfpmBattery *batt)
{
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);
    
    if ( xfpm_hal_device_have_key(priv->hal,udi,"battery.is_rechargeable") ) 
    {
        xfpm_battery_new_device(batt,udi);
        xfpm_battery_refresh(batt);
    }
}

static void
xfpm_battery_handle_device_removed(XfpmHal *hal,const gchar *udi,XfpmBattery *batt)
{
    GtkStatusIcon *icon;
    XfpmBatteryPrivate *priv;
    
    priv = XFPM_BATTERY_GET_PRIVATE(batt);
    icon = g_hash_table_lookup(priv->batteries,udi);
    if ( !icon )
    {
        return;
    }

    XFPM_DEBUG("Removing battery device %s\n",udi);    
    XfpmBatteryType battery_type;
    g_object_get(G_OBJECT(icon),"battery-type",&battery_type,NULL);
    
    if ( battery_type == UPS ) priv->ups_found = FALSE;

    g_hash_table_remove(priv->batteries,udi);
    g_object_unref(icon);
    xfpm_battery_refresh(batt);
}


static guint 
_get_battery_percentage(gint32 last_full,gint32 current)
{
    guint val = 100;
    
    if ( last_full <= current ) return val;
    
    float f = (float)current/last_full *100;
	
	val = (guint)f;
    return val;   
}
    
static void
xfpm_battery_handle_device_property_changed(XfpmHal *hal,const gchar *udi,
                                           const gchar *key,gboolean is_removed,
                                           gboolean is_added,XfpmBattery *batt)
{
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);
    
    if ( g_hash_table_size(priv->batteries) == 0 )
    {
        return;
    }
    
    /* A device can have a battery key but is useless to monitor unless it's rechargeable*/
    if ( xfpm_hal_device_have_key(priv->hal,udi,"battery.is_rechargeable") ) 
    {
        GtkStatusIcon *icon;
        icon = g_hash_table_lookup(priv->batteries,udi);
        if ( !icon )
        {
            return;
        }
        if ( !strcmp(key,"battery.charge_level.last_full") )
        {
            GError *error = NULL;
            guint last_full = xfpm_hal_get_int_info(priv->hal,
                                                    udi,
                                                    "battery.charge_level.last_full",
                                                    &error);
            if ( error )                                        
            {
                XFPM_DEBUG("%s\n",error->message);
                g_error_free(error);
            }                                        
            g_object_set(G_OBJECT(icon),"last-full",last_full,NULL);
            return;
        }

        if ( strcmp(key,"battery.charge_level.current")           &&
             strcmp(key,"battery.rechargeable.is_charging")       &&
             strcmp(key,"battery.rechargeable.is_discharging") )
        {
            return;
        }
        XFPM_DEBUG("Drive status change udi=%s key=%s\n",udi,key);

        gint32 current;
        guint percentage;
        gboolean is_charging;
        gboolean is_discharging;
        gboolean is_present;
        GError *error = NULL;
        current = xfpm_hal_get_int_info(priv->hal,
                                        udi,
                                        "battery.charge_level.current",
                                        &error);
        if ( error )                                        
        {
            XFPM_DEBUG("%s\n",error->message);
            g_error_free(error);
            return;
        }                                    

        if (xfpm_hal_device_have_key(priv->hal,udi,"battery.charge_level.persentage"))
        {
            if ( error ) 
            {
                XFPM_DEBUG("%s:\n",error->message);
                g_error_free(error);
                return;
            }                                      
            percentage = xfpm_hal_get_int_info(priv->hal,
                                              udi,
                                              "battery.charge_level.percentage",
                                              &error);
        }
        else
        {
            GError *error = NULL;
            guint last_full = xfpm_hal_get_int_info(priv->hal,
                                                    udi,
                                                    "battery.charge_level.last_full",
                                                    &error);
            if ( error )                                        
            {
                XFPM_DEBUG("%s\n",error->message);
                g_error_free(error);
                return;
            }                                        
            percentage = _get_battery_percentage(last_full,current);
        }
                                      
        is_present = xfpm_hal_get_bool_info(priv->hal,
                                             udi,
                                            "battery.present",&error);
        if ( error )                                        
        {
            XFPM_DEBUG("%s\n",error->message);
            g_error_free(error);
            return;
        }                                           
                                                                          
        is_charging = xfpm_hal_get_bool_info(priv->hal,
                                             udi,
                                            "battery.rechargeable.is_charging",
                                            &error);
        if ( error )                                        
        {
            XFPM_DEBUG("%s\n",error->message);
            g_error_free(error);
            return;
        }                                           
                                            
        is_discharging = xfpm_hal_get_bool_info(priv->hal,
                                               udi,
                                               "battery.rechargeable.is_discharging",
                                               &error);
        if ( error )                                        
        {
            XFPM_DEBUG("%s\n",error->message);
            g_error_free(error);
            return;
        }                                               
        
        gint32 remaining_time  = 0 ;
        if (xfpm_hal_device_have_key(priv->hal,udi,"battery.remaining_time"))
        {
            if ( error ) 
            {
                XFPM_DEBUG("%s:\n",error->message);
                g_error_free(error);
                return;
            }                                      
            remaining_time = xfpm_hal_get_int_info(priv->hal,
                                                   udi,
                                                   "battery.remaining_time",
                                                   &error);
        }                                       
        xfpm_battery_icon_set_state(XFPM_BATTERY_ICON(icon),current,percentage,remaining_time,
                                    is_present,is_charging,is_discharging,batt->ac_adapter_present);
    }
}

#ifdef HAVE_LIBNOTIFY
static void
_do_critical_action(NotifyNotification *n,gchar *action,XfpmBattery *batt)
{
    if (!strcmp(action,"shutdown"))
    {
        XFPM_DEBUG("Sending shutdown request\n");
        g_signal_emit(G_OBJECT(batt),signals[XFPM_ACTION_REQUEST],0,XFPM_DO_SHUTDOWN,TRUE);
    }
    else if ( !strcmp(action,"hibernate"))
    {
        XFPM_DEBUG("Sending hibernate request\n");
        g_signal_emit(G_OBJECT(batt),signals[XFPM_ACTION_REQUEST],0,XFPM_DO_HIBERNATE,TRUE);
    }
}
#endif

static void
xfpm_battery_show_critical_options(XfpmBattery *batt,XfpmBatteryIcon *icon,XfpmBatteryType battery_type)
{
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);
    
    const gchar *message;
    message = _("Your battery is almost empty. "\
              "Save your work to avoid losing data");
              
#ifdef HAVE_LIBNOTIFY            
                                    
    NotifyNotification *n = xfpm_notify_new(_("Xfce power manager"),
                                             message,
                                             20000,
                                             NOTIFY_URGENCY_CRITICAL,
                                             GTK_STATUS_ICON(icon),
                                             battery_type == PRIMARY ? "gpm-primary-000" : "gpm-ups-000");
    if (priv->power_management & SYSTEM_CAN_SHUTDOWN )
    {
        xfpm_notify_add_action(n,
                               "shutdown",
                               _("Shutdown the system"),
                               (NotifyActionCallback)_do_critical_action,
                               batt);   
    }                          
    if ( priv->power_management & SYSTEM_CAN_HIBERNATE )
    {
        xfpm_notify_add_action(n,
                               "hibernate",
                               _("Hibernate the system"),
                               (NotifyActionCallback)_do_critical_action,
                               batt);      
    }
    xfpm_notify_show_notification(n,6);      
#else
	GtkWidget *dialog;
	dialog = gtk_message_dialog_new (NULL,
                                     GTK_DIALOG_MODAL,
                                     GTK_MESSAGE_WARNING,
                                     GTK_BUTTONS_CANCEL,
                                     message,
                                     NULL);
     if ( priv->power_management & SYSTEM_CAN_HIBERNATE )
	 {                                     
		GtkWidget *hibernate = gtk_button_new_with_label(_("Hibernate"));
		gtk_widget_show(hibernate);                                     
		gtk_dialog_add_action_widget(GTK_DIALOG(dialog),hibernate,GTK_RESPONSE_OK); 
	 }
	 
	 if ( priv->power_management & SYSTEM_CAN_SHUTDOWN )
	 {                                     
		GtkWidget *shutdown = gtk_button_new_with_label(_("Shutdown"));
		gtk_widget_show(shutdown);                                     
		gtk_dialog_add_action_widget(GTK_DIALOG(dialog),shutdown,GTK_RESPONSE_ACCEPT); 
	 }
     
     switch(gtk_dialog_run(GTK_DIALOG(dialog)))
     {
     	case GTK_RESPONSE_OK:
			g_signal_emit(G_OBJECT(batt),signals[XFPM_ACTION_REQUEST],0,XFPM_DO_HIBERNATE,TRUE);
     		break;
		case GTK_RESPONSE_ACCEPT:
			g_signal_emit(G_OBJECT(batt),signals[XFPM_ACTION_REQUEST],0,XFPM_DO_SHUTDOWN,TRUE);
			break;
     	default:
     		break;
     }
	 gtk_widget_destroy(dialog);
#endif
}

static void
xfpm_battery_handle_primary_critical(XfpmBattery *batt,XfpmBatteryIcon *icon)
{
    if ( batt->critical_action == XFPM_DO_HIBERNATE )
    {
        XFPM_DEBUG("Sending Hibernate request\n");
        g_signal_emit(G_OBJECT(batt),signals[XFPM_ACTION_REQUEST],0,XFPM_DO_HIBERNATE,TRUE);
        return;
    }
    
    if ( batt->critical_action == XFPM_DO_SHUTDOWN )
    {
        XFPM_DEBUG("Sending shutdown request\n");
        g_signal_emit(G_OBJECT(batt),signals[XFPM_ACTION_REQUEST],0,XFPM_DO_SHUTDOWN,TRUE);
        return;
    }
    
    if ( batt->critical_action == XFPM_DO_NOTHING )
    {
        xfpm_battery_show_critical_options(batt,icon,PRIMARY);
    }
}

static gboolean
xfpm_battery_is_critical_charge(XfpmBattery *batt)
{
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);
    
    if ( g_hash_table_size(priv->batteries ) == 1 ) return TRUE;
    if ( g_hash_table_size(priv->batteries)  == 0 ) return FALSE;
        
    GList *icons_list;
    int i=0;
    icons_list = g_hash_table_get_values(priv->batteries);
        
    gboolean critical = FALSE;
    
    for ( i = 0 ; i < g_list_length(icons_list) ; i++ )
    {
	XfpmBatteryType type;
	XfpmBatteryState state;
	XfpmBatteryIcon *icon;
        icon = g_list_nth_data(icons_list,i);
	if ( icon )
	{
		g_object_get(G_OBJECT(icon),"battery-type", &type, "battery-state", &state, NULL);
		if ( type != PRIMARY || type != UPS ) continue;
		
		if ( state == CRITICAL ) critical = TRUE;
		else                     critical = FALSE;
	}
    }
    
    return critical;
}

static void
xfpm_battery_handle_critical_charge(XfpmBattery *batt,XfpmBatteryIcon *icon)
{
    XfpmBatteryType battery_type;
    g_object_get(G_OBJECT(icon),"battery-type",&battery_type,NULL);
    
    if ( battery_type == PRIMARY || battery_type == UPS )
    {
	if ( xfpm_battery_is_critical_charge(batt) )
        	xfpm_battery_handle_primary_critical(batt,icon);
    }
}

static void
xfpm_battery_state_change_cb(GObject *object,GParamSpec *arg1,gpointer data)
{
    if ( !strcmp(arg1->name,"battery-state") ) 
    {
        XfpmBatteryState state;
        XfpmBattery *batt = XFPM_BATTERY(data);
        g_object_get(object,"battery-state",&state,NULL);
        
        /* refresh here for the tray icon hiding if option
         * show icon is charging or discharging and battery is fully charged
         */
        xfpm_battery_refresh_tray_icon(batt);
#ifdef DEBUG
        gchar *content;
        GValue value = { 0, };
        g_value_init(&value,XFPM_TYPE_BATTERY_STATE);
        g_value_set_enum(&value,state);
        content = g_strdup_value_contents(&value);
        XFPM_DEBUG("param:%s value:%s\n",arg1->name,content);
        g_free(content);
#endif  
        if ( state == CRITICAL )
        {
            xfpm_battery_handle_critical_charge(batt,XFPM_BATTERY_ICON(object));
        }
    }
}

static void
xfpm_battery_hibernate_callback(GtkWidget *widget,XfpmBattery *batt)
{
    gboolean ret = 
    xfce_confirm(_("Are you sure you want to hibernate the system?"),
                GTK_STOCK_YES,
                _("Hibernate"));
    
    if ( ret ) 
    {
        g_signal_emit(G_OBJECT(batt),signals[XFPM_ACTION_REQUEST],0,XFPM_DO_HIBERNATE,FALSE);
	}
}


static void
xfpm_battery_suspend_callback(GtkWidget *widget,XfpmBattery *batt)
{
    gboolean ret = 
    xfce_confirm(_("Are you sure you want to suspend the system?"),
                GTK_STOCK_YES,
                _("Suspend"));
    
    if ( ret ) 
    {
        g_signal_emit(G_OBJECT(batt),signals[XFPM_ACTION_REQUEST],0,XFPM_DO_SUSPEND,FALSE);
    }
}

static void xfpm_battery_popup_tray_icon_menu(GtkStatusIcon *tray_icon,
                                             guint button,
                                             guint activate_time,
                                             XfpmBattery *batt)
{
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);
    
    GtkWidget *menu,*mi,*img;
	
	menu = gtk_menu_new();
	// Hibernate menu option
	mi = gtk_image_menu_item_new_with_label(_("Hibernate"));
	img = gtk_image_new_from_icon_name("gpm-hibernate",GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi),img);
	gtk_widget_set_sensitive(mi,FALSE);
	if ( priv->power_management & SYSTEM_CAN_HIBERNATE )
	{
		gtk_widget_set_sensitive(mi,TRUE);
		g_signal_connect(mi,"activate",
					 	 G_CALLBACK(xfpm_battery_hibernate_callback),
					 	 batt);
	}
	gtk_widget_show(mi);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu),mi);
	
	// Suspend menu option
	mi = gtk_image_menu_item_new_with_label(_("Suspend"));
	img = gtk_image_new_from_icon_name("gpm-suspend",GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi),img);
	
	gtk_widget_set_sensitive(mi,FALSE);
	if ( priv->power_management & SYSTEM_CAN_SUSPEND )
    {	
		gtk_widget_set_sensitive(mi,TRUE);
		g_signal_connect(mi,"activate",
					     G_CALLBACK(xfpm_battery_suspend_callback),
					     batt);
	}
	gtk_widget_show(mi);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu),mi);
	
	// Separotor
	mi = gtk_separator_menu_item_new();
	gtk_widget_show(mi);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu),mi);
	
	mi = gtk_image_menu_item_new_from_stock(GTK_STOCK_HELP,NULL);
	gtk_widget_set_sensitive(mi,TRUE);
	gtk_widget_show(mi);
	g_signal_connect(mi,"activate",G_CALLBACK(xfpm_help),NULL);
	
	gtk_menu_shell_append(GTK_MENU_SHELL(menu),mi);
	
	mi = gtk_image_menu_item_new_from_stock(GTK_STOCK_ABOUT,NULL);
	gtk_widget_set_sensitive(mi,TRUE);
	gtk_widget_show(mi);
	g_signal_connect(mi,"activate",G_CALLBACK(xfpm_about),NULL);
	
	gtk_menu_shell_append(GTK_MENU_SHELL(menu),mi);
	
	mi = gtk_image_menu_item_new_from_stock(GTK_STOCK_PREFERENCES,NULL);
	gtk_widget_set_sensitive(mi,TRUE);
	gtk_widget_show(mi);
	g_signal_connect(mi,"activate",G_CALLBACK(xfpm_preferences),NULL);
	
	gtk_menu_shell_append(GTK_MENU_SHELL(menu),mi);
	
	// Popup the menu
	gtk_menu_popup(GTK_MENU(menu),NULL,NULL,
		       gtk_status_icon_position_menu,tray_icon,button,activate_time);
}

static XfpmBatteryType
xfpm_battery_get_battery_type(const gchar *battery_type)
{
    if ( !strcmp("primary",battery_type) )
    {
        return PRIMARY;
    }
    else if ( !strcmp("keyboard",battery_type) )
    {
        return KEYBOARD;
    }
    else if ( !strcmp("mouse",battery_type) ) 
    {
        return MOUSE;
    }
    else if ( !strcmp("ups",battery_type) ) 
    {
        return UPS; 
    }
    
    return UNKNOWN;
}


static gboolean
xfpm_battery_check(XfpmBattery *batt,const gchar *udi)
{
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);
    
    // Sanity check
    // All those keys are flagged as always exist on device battery, except for battery.is_rechargeable
    if ( !xfpm_hal_device_have_key(priv->hal,udi,"battery.is_rechargeable") ||
         !xfpm_hal_device_have_key(priv->hal,udi,"battery.charge_level.last_full") ||
         !xfpm_hal_device_have_key(priv->hal,udi,"battery.charge_level.current")   ||
         !xfpm_hal_device_have_key(priv->hal,udi,"battery.present")  ||
         !xfpm_hal_device_have_key(priv->hal,udi,"battery.rechargeable.is_charging") ||
         !xfpm_hal_device_have_key(priv->hal,udi,"battery.rechargeable.is_discharging") ||
         !xfpm_hal_device_have_key(priv->hal,udi,"battery.type") )
    {
        return FALSE;
    }
    
    return TRUE;
}

static gboolean
xfpm_battery_is_new(XfpmBattery *batt, const gchar *udi, 
                    XfpmBatteryType battery_type, guint last_full)
{
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);
    
    GList *keys;
    
    keys = g_hash_table_get_keys(priv->batteries);
    
    if ( !keys ) return TRUE;
    
    if ( g_list_length(keys) == 0 ) return TRUE;
    
    int i = 0;
    
    for ( i = 0; i < g_list_length(keys); i++)
    {
        GError *error = NULL;
        gchar *udi_b = (gchar *) g_list_nth_data(keys,i);
        guint last_full_b = xfpm_hal_get_int_info(priv->hal,
                                            udi_b,
                                            "battery.charge_level.last_full",
                                            &error);
        if ( error )
        {
            XFPM_DEBUG("%s:\n",error->message);
            g_error_free(error);
            return TRUE;
        }
        
        gchar *batt_type_b = xfpm_hal_get_string_info(priv->hal,
                                                      udi_b,
                                                      "battery.type",
                                                      &error);
        if ( error ) 
        {
            XFPM_DEBUG("%s:\n",error->message);
            g_error_free(error);
            return TRUE;
        } 
        
         XfpmBatteryType type_b = 
            xfpm_battery_get_battery_type(batt_type_b);
            if ( batt_type_b )
            libhal_free_string(batt_type_b);
        
        if ( type_b == battery_type && last_full_b == last_full ) return FALSE;
    }       
    
    return TRUE;
}

static void
xfpm_battery_new_device(XfpmBattery *batt,const gchar *udi)
{
    if ( !xfpm_battery_check(batt,udi) )
    {
        return;
    }
    
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);
    
    GError *error = NULL;      
    guint last_full = xfpm_hal_get_int_info(priv->hal,
                                          udi,
                                          "battery.charge_level.last_full",
                                          &error);
    if ( error ) 
    {
        XFPM_DEBUG("%s:\n",error->message);
        g_error_free(error);
        return;
    }
    gint32 current   = xfpm_hal_get_int_info(priv->hal,
                                          udi,
                                          "battery.charge_level.current",
                                          &error);
    if ( error ) 
    {
        XFPM_DEBUG("%s:\n",error->message);
        g_error_free(error);
        return;
    }                                      
    gboolean is_present = xfpm_hal_get_bool_info(priv->hal,
                                                udi,
                                               "battery.present",
                                               &error);
    if ( error ) 
    {
        XFPM_DEBUG("%s:\n",error->message);
        g_error_free(error);
        return;
    }                                             
    gboolean is_charging = xfpm_hal_get_bool_info(priv->hal,
                                                  udi,
                                                  "battery.rechargeable.is_charging",
                                                  &error);
    if ( error ) 
    {
        XFPM_DEBUG("%s:\n",error->message);
        g_error_free(error);
        return;
    }                                              
    gboolean is_discharging = xfpm_hal_get_bool_info(priv->hal,
                                                     udi,
                                                     "battery.rechargeable.is_discharging",
                                                     &error);
    if ( error ) 
    {
        XFPM_DEBUG("%s:\n",error->message);
        g_error_free(error);
        return;
    }                                                 
    gchar *battery_type = xfpm_hal_get_string_info(priv->hal,
                                                  udi,
                                                  "battery.type",
                                                  &error);
    if ( error ) 
    {
        XFPM_DEBUG("%s:\n",error->message);
        g_error_free(error);
        return;
    } 
    
    guint percentage;            
    if (xfpm_hal_device_have_key(priv->hal,udi,"battery.charge_level.persentage"))
    {
        if ( error ) 
        {
            XFPM_DEBUG("%s:\n",error->message);
            g_error_free(error);
            return;
        }                                      
        percentage = xfpm_hal_get_int_info(priv->hal,
                                          udi,
                                          "battery.charge_level.percentage",
                                          &error);
    }
    else
    {
        percentage = _get_battery_percentage(last_full,current);
    }
   
    gint32 remaining_time  = 0 ;
    if (xfpm_hal_device_have_key(priv->hal,udi,"battery.remaining_time"))
    {
        if ( error ) 
        {
            XFPM_DEBUG("%s:\n",error->message);
            g_error_free(error);
            return;
        }                                      
        remaining_time = xfpm_hal_get_int_info(priv->hal,
                                               udi,
                                               "battery.remaining_time",
                                               &error);
    }
    
    XfpmBatteryType type = 
    xfpm_battery_get_battery_type(battery_type);
    if ( battery_type )
        libhal_free_string(battery_type);
    if ( type == UPS ) priv->ups_found = TRUE;    
    
    /* Check if this battery is new, udevadm trigger cause hal to make a new udi
     * for a existing battery */
    
    if ( !xfpm_battery_is_new(batt, udi, type, last_full) )  
    {
        XFPM_DEBUG("Battery with udi=%s already monitored\n",udi);
        return;
    } 
    
    GtkStatusIcon *batt_icon;
    batt_icon = xfpm_battery_icon_new(last_full,
                                      type,
                                      batt->critical_level,
                                      TRUE);
                                      
#ifdef HAVE_LIBNOTIFY    
    g_object_set(batt_icon,"systray-notify",batt->notify_enabled,NULL);
#endif    
                                                  
    xfpm_battery_icon_set_state(XFPM_BATTERY_ICON(batt_icon),
                                current,
                                percentage,
                                remaining_time,
                                is_present,
                                is_charging,
                                is_discharging,
                                batt->ac_adapter_present);
                                
    g_signal_connect(batt_icon,"notify",G_CALLBACK(xfpm_battery_state_change_cb),batt);                            
    g_signal_connect(batt_icon,"popup-menu",G_CALLBACK(xfpm_battery_popup_tray_icon_menu),batt);
    
    g_hash_table_insert(priv->batteries,g_strdup(udi),batt_icon);
    xfpm_battery_refresh(batt);
}

static void
xfpm_battery_get_devices(XfpmBattery *batt)
{
    XFPM_DEBUG("Getting batteries\n");
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);
    
    int num,i;
    gchar **udi = NULL;
    GError *error = NULL;
    
    udi = xfpm_hal_get_device_udi_by_capability(priv->hal,"battery",&num,&error);
    if ( error ) 
    {
        XFPM_DEBUG("%s:\n",error->message);
        g_error_free(error);
        return;
    }
    if ( num == 0 ) return;
    if ( !udi )     return; 

    for ( i = 0 ; udi[i]; i++)
    {
        if ( xfpm_hal_device_have_key(priv->hal,udi[i],"battery.is_rechargeable"))
        {
            XFPM_DEBUG("found battery %s\n",udi[i]);
            xfpm_battery_new_device(batt,udi[i]);
        }
    }
    libhal_free_string_array(udi);
}

XfpmBattery *
xfpm_battery_new(void)
{
    XfpmBattery *battery = NULL;
    battery = g_object_new(XFPM_TYPE_BATTERY,NULL);
    return battery;
}

void           
xfpm_battery_monitor(XfpmBattery *batt)
{
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);
    xfpm_battery_get_devices(batt);
    xfpm_battery_refresh(batt);
}


#ifdef HAVE_LIBNOTIFY
void           xfpm_battery_show_error(XfpmBattery *batt,
                                       const gchar *icon_name,
                                       const gchar *error)
{
    g_return_if_fail(XFPM_IS_BATTERY(batt));
    
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);     
    
    GtkStatusIcon *icon = NULL;
    
    GList *icons_list;
    int i=0;
    icons_list = g_hash_table_get_values(priv->batteries);
    for ( i = 0 ; i < g_list_length(icons_list) ; i++ )
    {
        icon = g_list_nth_data(icons_list,i);
        if ( icon ) 
        {
            gboolean visible;
            XfpmBatteryType type;
            g_object_get(G_OBJECT(icon),"visible",&visible,"battery-type",&type,NULL);
            if ( visible && type == PRIMARY )
            {
                break;
            }
        }
        icon = NULL;
    }
    NotifyNotification *n =
    xfpm_notify_new(_("Xfce power manager"),
                    error,
                    14000,
                    NOTIFY_URGENCY_CRITICAL,
                    icon != NULL ? GTK_STATUS_ICON(icon) : NULL,
                    icon_name);
    xfpm_notify_show_notification(n,0);                 
}                                       
#endif

void           
xfpm_battery_set_power_info(XfpmBattery *batt,guint8 power_management)
{
    g_return_if_fail(XFPM_IS_BATTERY(batt));
    
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt);                            

    priv->power_management = power_management;  
}                                        
                                            
gboolean       
xfpm_battery_ups_found     (XfpmBattery *batt)
{
    g_return_val_if_fail(XFPM_IS_BATTERY(batt),FALSE);
    XfpmBatteryPrivate *priv;
    priv = XFPM_BATTERY_GET_PRIVATE(batt); 
    
    return priv->ups_found;
}
