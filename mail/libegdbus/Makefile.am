GDBUS_XML_FILES =					\
	../daemon/e-mail-data-session.xml	\
	../daemon/e-mail-data-store.xml	\
	../daemon/e-mail-data-folder.xml

# TODO: This won't work because it doesn't get the generated filename properly. murrayc.
gdbus-files: $(GDBUS_XML_FILES)
# $(GDBUS_CODEGEN) --namespace=EGdbus --strip-prefix=org.gnome.evolution.dataserver.mail --generate-c-code=$^ $^.xml
	$(GDBUS_CODEGEN) --c-namespace=EGdbus --interface-prefix=org.gnome.evolution.dataserver.mail --generate-c-code=$^ $^.xml	


# $(GDBUS_CODEGEN) --c-namespace=EGdbusSession --interface-prefix=org.gnome.evolution.dataserver.mail. --generate-c-code=e-gdbus-emailsession ../daemon/e-mail-data-session.xml
# $(GDBUS_CODEGEN) --c-namespace=EGdbusFolder --interface-prefix=org.gnome.evolution.dataserver.mail. --generate-c-code=e-gdbus-emailfolder ../daemon/e-mail-data-folder.xml 
# $(GDBUS_CODEGEN) --c-namespace=EGdbusStore --interface-prefix=org.gnome.evolution.dataserver.mail. --generate-c-code=e-gdbus-emailstore ../daemon/e-mail-data-store.xml

# The library
noinst_LTLIBRARIES = libegdbus-mail.la

libegdbus_mail_la_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	$(CAMEL_CFLAGS)				\
	-DG_LOG_DOMAIN=\"libegdbus-mail\"	\
	$(E_MAIL_GDBUS_UTILS_CFLAGS)		\
	$(GDBUS_CODEGEN_CFLAGS)			\
	$(FFI_CFLAGS)		

libegdbus_mail_la_SOURCES =			\
	e-gdbus-emailfolder.h			\
	e-gdbus-emailfolder.c			\
	e-gdbus-emailstore.h		\
	e-gdbus-emailstore.c		\
	e-gdbus-emailsession.h		\
	e-gdbus-emailsession.c		

libegdbus_mail_la_LIBADD =			\
	$(E_MAIL_GDBUS_UTILS_LIBS)		\
	$(top_builddir)/camel/libcamel-1.2.la			\
	$(top_builddir)/camel/libcamel-provider-1.2.la			\
	$(top_builddir)/libedataserver/libedataserver-1.2.la
	

libegdbus_mail_la_LDFLAGS = $(NO_UNDEFINED)


-include $(top_srcdir)/git.mk
