SUBDIRS = lib \
	message folder smime misc mime-filter

EXTRA_DIST = data

dist-hook:
	rm -rf `find $(distdir)/data -name .svn`

-include $(top_srcdir)/git.mk
