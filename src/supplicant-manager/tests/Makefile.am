AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libnm-util \
	-I$(top_builddir)/libnm-util \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/supplicant-manager \
	$(GLIB_CFLAGS) \
	$(DBUS_CFLAGS)

noinst_PROGRAMS = test-supplicant-config

test_supplicant_config_SOURCES = \
	test-supplicant-config.c

test_supplicant_config_LDADD = \
	$(top_builddir)/src/libNetworkManager.la

check-local: test-supplicant-config
	$(abs_builddir)/test-supplicant-config
