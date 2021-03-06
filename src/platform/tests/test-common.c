#include "test-common.h"
#include "nm-glib-compat.h"

SignalData *
add_signal_full (const char *name, GCallback callback, int ifindex, const char *ifname)
{
	SignalData *data = g_new0 (SignalData, 1);

	data->name = name;
	data->received = FALSE;
	data->handler_id = g_signal_connect (nm_platform_get (), name, callback, data);
	data->ifindex = ifindex;
	data->ifname = ifname;

	g_assert (data->handler_id >= 0);

	return data;
}

void
accept_signal (SignalData *data)
{
	debug ("Accepting signal '%s' ifindex %d ifname %s.", data->name, data->ifindex, data->ifname);
	if (!data->received)
		g_error ("Attemted to accept a non-received signal '%s'.", data->name);

	data->received = FALSE;
}

void
wait_signal (SignalData *data)
{
	data->loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (data->loop);
	g_clear_pointer (&data->loop, g_main_loop_unref);

	accept_signal (data);
}

void
free_signal (SignalData *data)
{
	if (data->received)
		g_error ("Attempted to free received but not accepted signal '%s'.", data->name);

	g_signal_handler_disconnect (nm_platform_get (), data->handler_id);
	g_free (data);
}

void
link_callback (NMPlatform *platform, int ifindex, NMPlatformLink *received, NMPlatformReason reason, SignalData *data)
{
	
	GArray *links;
	NMPlatformLink *cached;
	int i;

	g_assert (received);
	g_assert_cmpint (received->ifindex, ==, ifindex);

	if (data->ifindex && data->ifindex != received->ifindex)
		return;
	if (data->ifname && g_strcmp0 (data->ifname, nm_platform_link_get_name (ifindex)) != 0)
		return;

	if (data->loop) {
		debug ("Quitting main loop.");
		g_main_loop_quit (data->loop);
	}

	if (data->received)
		g_error ("Received signal '%s' a second time.", data->name);

	debug ("Received signal '%s' ifindex %d ifname '%s'.", data->name, ifindex, received->name);
	data->received = TRUE;

	/* Check the data */
	g_assert (received->ifindex > 0);
	links = nm_platform_link_get_all ();
	for (i = 0; i < links->len; i++) {
		cached = &g_array_index (links, NMPlatformLink, i);
		if (cached->ifindex == received->ifindex) {
			g_assert (!memcmp (cached, received, sizeof (*cached)));
			if (!g_strcmp0 (data->name, NM_PLATFORM_LINK_REMOVED)) {
				g_error ("Deleted link still found in the local cache.");
			}
			g_array_unref (links);
			return;
		}
	}
	g_array_unref (links);

	if (g_strcmp0 (data->name, NM_PLATFORM_LINK_REMOVED))
		g_error ("Added/changed link not found in the local cache.");
}

void
run_command (const char *format, ...)
{
	char *command;
	va_list ap;

	va_start (ap, format);
	command = g_strdup_vprintf (format, ap);
	va_end (ap);
	debug ("Running command: %s", command);
	g_assert (!system (command));
	debug ("Command finished.");
	g_free (command);
}

int
main (int argc, char **argv)
{
	int result;

	openlog (G_LOG_DOMAIN, LOG_CONS | LOG_PERROR, LOG_DAEMON);
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	/* Enable debug messages if called with --debug */
	for (; *argv; argv++) {
		if (!g_strcmp0 (*argv, "--debug")) {
			nm_logging_setup ("debug", NULL, NULL, NULL);
		}
	}

	SETUP ();

	setup_tests ();

	result = g_test_run ();

	nm_platform_link_delete (nm_platform_link_get_ifindex (DEVICE_NAME));

	nm_platform_free ();
	return result;
}
