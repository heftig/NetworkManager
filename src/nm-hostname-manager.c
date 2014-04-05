/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2014 Jan Steffens <jan.steffens@gmail.com>
 */

#include "config.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include "nm-dbus-manager.h"
#include "nm-dispatcher.h"
#include "nm-logging.h"
#include "gsystem-local-alloc.h"

#include "nm-hostname-manager.h"

#define H1_NAME              "org.freedesktop.hostname1"
#define H1_PATH              "/org/freedesktop/hostname1"
#define H1_INTERFACE         "org.freedesktop.hostname1"
#define FALLBACK_FILE        "/etc/hostname"
#define FALLBACK_HOSTNAME    "localhost.localdomain"

struct _NMHostnameManager {
	GObject parent_instance;

	NMDBusManager *dbus_manager;

	/* org.freedesktop.hostname1 */
	GDBusProxy    *proxy;

	/* fallback - used when hostname1 is unavailable */
	char          *static_hostname_cache;
	GFileMonitor  *static_hostname_monitor;
};

struct _NMHostnameManagerClass {
	GObjectClass parent_class;
};

enum {
	PROP_0,

	PROP_HOSTNAME,
	PROP_STATIC_HOSTNAME,

	N_PROPERTIES
};
static GParamSpec * properties[N_PROPERTIES] = {NULL};

G_DEFINE_TYPE (NMHostnameManager, nm_hostname_manager, G_TYPE_OBJECT);

/*** org.freedesktop.hostname1 implementation *********************************/

static char *
h1_get_hostname (NMHostnameManager *self)
{
	gs_unref_variant GVariant *prop = NULL;

	prop = g_dbus_proxy_get_cached_property (self->proxy, "Hostname");

	return g_variant_dup_string (prop, NULL);
}

static char *
h1_get_static_hostname (NMHostnameManager *self)
{
	gs_unref_variant GVariant *prop = NULL;

	prop = g_dbus_proxy_get_cached_property (self->proxy, "StaticHostname");

	return g_variant_dup_string (prop, NULL);
}

static gboolean
h1_set_hostname (NMHostnameManager *self,
                 const char        *hostname)
{
	gs_free_error    GError   *error = NULL;
	gs_unref_variant GVariant *ret   = NULL;

	g_assert (hostname);

	ret = g_dbus_proxy_call_sync (self->proxy, "SetHostname",
	                              g_variant_new ("(sb)", hostname, FALSE),
	                              G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

	if (!ret) {
		nm_log_warn (LOGD_DNS, "couldn't set the hostname to '%s': %s",
		             hostname, error->message);
		return FALSE;
	}

	/* hostname1 doesn't send change notifications for Hostname :( */
	g_dbus_proxy_set_cached_property (self->proxy, "Hostname",
	                                  g_variant_new_string (hostname));

	return TRUE;
}

static gboolean
h1_set_static_hostname (NMHostnameManager *self,
                        const char        *hostname)
{
	gs_free_error    GError   *error = NULL;
	gs_unref_variant GVariant *ret   = NULL;

	g_assert (hostname);

	ret = g_dbus_proxy_call_sync (self->proxy, "SetStaticHostname",
	                              g_variant_new ("(sb)", hostname, FALSE),
	                              G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

	if (!ret) {
		nm_log_warn (LOGD_DNS, "couldn't set the static hostname to '%s': %s",
		             hostname, error->message);
		return FALSE;
	}

	return TRUE;
}

static void
h1_on_properties_changed (GDBusProxy        *proxy,
                          GVariant          *changed_props,
                          GStrv              invalidated_props,
                          NMHostnameManager *self)
{
	GVariantIter i;
	const gchar *name;

	g_object_freeze_notify (G_OBJECT (self));

	g_variant_iter_init (&i, changed_props);
	while (g_variant_iter_next (&i, "{&sv}", &name, NULL)) {
		if (g_str_equal (name, "Hostname")) {
			g_object_notify_by_pspec (G_OBJECT (self),
			                          properties[PROP_HOSTNAME]);
		} else if (g_str_equal (name, "StaticHostname")) {
			g_object_notify_by_pspec (G_OBJECT (self),
			                          properties[PROP_STATIC_HOSTNAME]);
		}
	}

	g_object_thaw_notify (G_OBJECT (self));
}

static gboolean
h1_init (NMHostnameManager *self)
{
	gs_free_error GError *error = NULL;

	g_return_val_if_fail (self->proxy == NULL, FALSE);

	self->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
	                                             G_DBUS_PROXY_FLAGS_NONE,
	                                             NULL,
	                                             H1_NAME, H1_PATH, H1_INTERFACE,
	                                             NULL, &error);

	if (self->proxy) {
		nm_log_info (LOGD_DNS, "Using %s", H1_NAME);
		g_signal_connect (self->proxy, "g-properties-changed",
		                  G_CALLBACK (h1_on_properties_changed), self);
		return TRUE;
	} else {
		nm_log_info (LOGD_DNS, "%s service not available: %s", H1_NAME, error->message);
		return FALSE;
	}
}

static void
h1_deinit (NMHostnameManager *self)
{
	g_return_if_fail (self->proxy);
	g_clear_object (&self->proxy);
}

/*** fallback implementation **************************************************/

static char *
fallback_get_hostname (NMHostnameManager *self)
{
	char hostname[HOST_NAME_MAX + 2] = { 0, };

	errno = 0;
	if (!gethostname (&hostname[0], HOST_NAME_MAX) == 0) {
		nm_log_warn (LOGD_DNS, "couldn't get the system hostname: (%d) %s",
		             errno, g_strerror (errno));
                return g_strdup ("");
	}

	return g_strdup (hostname);
}

static gboolean
fallback_set_hostname (NMHostnameManager *self,
                       const char        *hostname)
{
	errno = 0;

	g_assert (hostname);

	if (!sethostname (hostname, strlen (hostname))) {
		nm_log_warn (LOGD_DNS, "couldn't set the system hostname to '%s': (%d) %s",
		             hostname, errno, g_strerror (errno));
		return FALSE;
	}

	return TRUE;
}

static char *
fallback_get_static_hostname (NMHostnameManager *self)
{
	g_assert (self->static_hostname_cache);
	return g_strdup (self->static_hostname_cache);
}

static gboolean
fallback_set_static_hostname (NMHostnameManager *self,
                              const char        *hostname)
{
	gs_free_error GError *error = NULL;
	gs_free char *contents = NULL;
	gssize len;

	g_assert (hostname);

	if (strlen (hostname) == 0) {
		errno = 0;

		if (g_unlink (FALLBACK_FILE) < 0 && errno != ENOENT) {
			nm_log_warn (LOGD_DNS, "couldn't remove the static hostname file '%s': (%d) %s",
			             FALLBACK_FILE, errno, g_strerror (errno));
			return FALSE;
		}
	} else {
		/* EOL-terminate */
		contents = g_strdup (hostname);
		len = strlen (contents);
		contents[len] = '\n';

		if (!g_file_set_contents (FALLBACK_FILE, contents, len + 1, &error)) {
			nm_log_warn (LOGD_DNS, "couldn't set the static hostname in '%s' to '%s': %s",
			             FALLBACK_FILE, hostname, error->message);
			return FALSE;
		}
	}

	g_free (self->static_hostname_cache);
	self->static_hostname_cache = g_strdup (hostname);

	return TRUE;
}

static void
fallback_load_static_hostname (NMHostnameManager *self)
{
	gs_free_error GError *error = NULL;
	char *hostname;

	if (!g_file_get_contents (FALLBACK_FILE, &hostname, NULL, &error)) {
		if (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
			nm_log_info (LOGD_DNS, "no static hostname file '%s'", FALLBACK_FILE);
			hostname = g_strdup ("");
		} else {
			nm_log_warn (LOGD_DNS, "couldn't get the static hostname from '%s': %s",
			             FALLBACK_FILE, error->message);
			return;
		}
	}

	g_assert (hostname);
	g_strchomp (hostname);

	if (g_str_equal (hostname, self->static_hostname_cache)) {
		g_free (hostname);
		return;
	}

	g_free (self->static_hostname_cache);
	self->static_hostname_cache = hostname;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATIC_HOSTNAME]);
}

static void
fallback_on_file_changed (GFileMonitor      *monitor,
                          GFile             *file,
                          GFile             *other_file,
                          GFileMonitorEvent  event_type,
                          NMHostnameManager *self)
{
	fallback_load_static_hostname (self);
}

static gboolean
fallback_monitor_init (NMHostnameManager *self)
{
	gs_free_error GError *error = NULL;
	GFile *file;

	g_return_val_if_fail (self->static_hostname_monitor == NULL, FALSE);

	file = g_file_new_for_path (FALLBACK_FILE);
	self->static_hostname_monitor = g_file_monitor_file (file, G_FILE_MONITOR_NONE,
	                                                     NULL, &error);
	g_object_unref (file);

	if (self->static_hostname_monitor) {
		g_signal_connect (self->static_hostname_monitor, "changed",
		                  G_CALLBACK (fallback_on_file_changed), self);
		fallback_load_static_hostname (self);
		return TRUE;
	} else {
		nm_log_warn (LOGD_DNS, "Could not watch '%s': %s",
		             FALLBACK_FILE, error->message);
		return FALSE;
	}
}

static void
fallback_monitor_deinit (NMHostnameManager *self)
{
	g_return_if_fail (self->static_hostname_monitor);
	g_file_monitor_cancel (self->static_hostname_monitor);
	g_clear_object (&self->static_hostname_monitor);
}

/*** private functions ********************************************************/

static void
on_dbus_connection_changed (NMDBusManager     *dbus_manager,
                            DBusGConnection   *connection,
                            NMHostnameManager *self)
{
	g_object_freeze_notify (G_OBJECT (self));

	if (!connection) {
		h1_deinit (self);
		fallback_monitor_init (self);
	} else {
		fallback_monitor_deinit (self);
		if (!h1_init (self))
			fallback_monitor_init (self);
	}

	/* properties might have changed */
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOSTNAME]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATIC_HOSTNAME]);

	g_object_thaw_notify (G_OBJECT (self));
}

static void
nm_hostname_manager_init (NMHostnameManager *self)
{
	self->static_hostname_cache = g_strdup ("");

	self->dbus_manager = nm_dbus_manager_get ();
	g_assert (self->dbus_manager);

	g_signal_connect (self->dbus_manager, NM_DBUS_MANAGER_DBUS_CONNECTION_CHANGED,
	                  G_CALLBACK (on_dbus_connection_changed), self);

	if (!h1_init (self))
		fallback_monitor_init (self);
}

static void
dispose (GObject *object)
{
	NMHostnameManager *self = NM_HOSTNAME_MANAGER (object);

	if (self->proxy)
		h1_deinit (self);

	if (self->static_hostname_monitor)
		fallback_monitor_deinit (self);

	g_clear_object (&self->dbus_manager);

	G_OBJECT_CLASS (nm_hostname_manager_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	NMHostnameManager *self = NM_HOSTNAME_MANAGER (object);

	g_free (self->static_hostname_cache);

	G_OBJECT_CLASS (nm_hostname_manager_parent_class)->finalize (object);
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMHostnameManager *self = NM_HOSTNAME_MANAGER (object);

	switch (prop_id) {
	case PROP_HOSTNAME:
		g_value_take_string (value, nm_hostname_manager_get_hostname (self));
		break;
	case PROP_STATIC_HOSTNAME:
		g_value_take_string (value, nm_hostname_manager_get_static_hostname (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_hostname_manager_class_init (NMHostnameManagerClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->dispose      = dispose;
	gobject_class->finalize     = finalize;
	gobject_class->get_property = get_property;

	properties[PROP_HOSTNAME] =
		g_param_spec_string (NM_HOSTNAME_MANAGER_HOSTNAME,
		                     "Hostname", "Transient hostname",
		                     NULL,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	properties[PROP_STATIC_HOSTNAME] =
		g_param_spec_string (NM_HOSTNAME_MANAGER_STATIC_HOSTNAME,
		                     "Static hostname", "Persistent hostname",
		                     NULL,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (gobject_class, N_PROPERTIES, properties);
}

/*** public functions *********************************************************/

char *
nm_hostname_manager_get_hostname (NMHostnameManager *self)
{
	char *hostname;

	if (self->proxy) {
		hostname = h1_get_hostname (self);

		if (hostname)
			return hostname;
	}

	hostname = fallback_get_hostname (self);
	g_assert (hostname);
	return hostname;
}

char *
nm_hostname_manager_get_static_hostname (NMHostnameManager *self)
{
	char *hostname;

	if (self->proxy) {
		hostname = h1_get_static_hostname (self);

		if (hostname)
			return hostname;
	}

	hostname = fallback_get_static_hostname (self);
	g_assert (hostname);
	return hostname;
}

gboolean
nm_hostname_manager_set_hostname (NMHostnameManager *self,
                                  const char        *hostname)
{
	gs_free char *old_hostname = NULL;

	if (!hostname || !strlen (hostname))
		hostname = FALLBACK_HOSTNAME;

	old_hostname = nm_hostname_manager_get_hostname (self);

	if (g_str_equal (old_hostname, hostname))
		return FALSE;

	nm_log_info (LOGD_DNS, "Setting system hostname to '%s'", hostname);
	if ((!self->proxy || !h1_set_hostname (self, hostname)) &&
	     !fallback_set_hostname (self, hostname))
		return FALSE;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOSTNAME]);
	nm_dispatcher_call (DISPATCHER_ACTION_HOSTNAME, NULL, NULL, NULL, NULL, NULL);
	return TRUE;
}

gboolean
nm_hostname_manager_set_static_hostname (NMHostnameManager *self,
                                         const char        *hostname)
{
	gs_free char *old_hostname = NULL;

	if (!hostname)
		hostname = "";

	old_hostname = nm_hostname_manager_get_static_hostname (self);

	if (g_str_equal (old_hostname, hostname))
		return FALSE;

	nm_log_info (LOGD_DNS, "Setting static hostname to '%s'", hostname);
	if ((!self->proxy || !h1_set_static_hostname (self, hostname)) &&
	     !fallback_set_static_hostname (self, hostname))
		return FALSE;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATIC_HOSTNAME]);
	return TRUE;
}

/*** singleton ****************************************************************/

static NMHostnameManager *singleton = NULL;

NMHostnameManager *
nm_hostname_manager_get (void)
{
	g_assert (singleton);
	return singleton;
}

NMHostnameManager *
nm_hostname_manager_new (void)
{
	g_assert (singleton == NULL);
	singleton = NM_HOSTNAME_MANAGER (g_object_new (NM_TYPE_HOSTNAME_MANAGER, NULL));
	g_assert (singleton);
	return singleton;
}
