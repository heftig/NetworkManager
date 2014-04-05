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

#ifndef NM_HOSTNAME_MANAGER_H
#define NM_HOSTNAME_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define NM_TYPE_HOSTNAME_MANAGER         (nm_hostname_manager_get_type ())
#define NM_HOSTNAME_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), NM_TYPE_HOSTNAME_MANAGER, NMHostnameManager))
#define NM_HOSTNAME_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), NM_TYPE_HOSTNAME_MANAGER, NMHostnameManagerClass))
#define NM_HOSTNAME_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), NM_TYPE_HOSTNAME_MANAGER, NMHostnameManagerClass))
#define NM_IS_HOSTNAME_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), NM_TYPE_HOSTNAME_MANAGER))
#define NM_IS_HOSTNAME_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), NM_TYPE_HOSTNAME_MANAGER))

#define NM_HOSTNAME_MANAGER_HOSTNAME        "hostname"
#define NM_HOSTNAME_MANAGER_STATIC_HOSTNAME "static-hostname"

typedef struct _NMHostnameManager         NMHostnameManager;
typedef struct _NMHostnameManagerClass    NMHostnameManagerClass;

GType               nm_hostname_manager_get_type (void) G_GNUC_CONST;
NMHostnameManager * nm_hostname_manager_get      (void);
NMHostnameManager * nm_hostname_manager_new      (void);

char *  nm_hostname_manager_get_hostname (NMHostnameManager *manager);

gboolean nm_hostname_manager_set_hostname (NMHostnameManager *manager,
                                           const char        *hostname);

char *  nm_hostname_manager_get_static_hostname (NMHostnameManager *manager);

gboolean nm_hostname_manager_set_static_hostname (NMHostnameManager *manager,
                                                  const char        *hostname);

G_END_DECLS

#endif /* NM_HOSTNAME_MANAGER_H */
