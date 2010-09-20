/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#include <nspr.h>
#include <prio.h>
#include <prerror.h>
#include <prerr.h>

#include <glib/gi18n-lib.h>

#include "camel-file-utils.h"
#include "camel-net-utils.h"
#include "camel-operation.h"
#include "camel-tcp-stream-raw.h"

#define d(x)

#define IO_TIMEOUT (PR_TicksPerSecond() * 4 * 60)
#define CONNECT_TIMEOUT (PR_TicksPerSecond () * 4 * 60)

typedef struct _CamelTcpStreamRawPrivate {
	PRFileDesc *sockfd;
} CamelTcpStreamRawPrivate;

static CamelTcpStreamClass *parent_class = NULL;

/* Returns the class for a CamelTcpStreamRaw */
#define CTSR_CLASS(so) CAMEL_TCP_STREAM_RAW_CLASS (CAMEL_OBJECT_GET_CLASS (so))

static gssize stream_read (CamelStream *stream, gchar *buffer, gsize n);
static gssize stream_write (CamelStream *stream, const gchar *buffer, gsize n);
static gint stream_flush  (CamelStream *stream);
static gint stream_close  (CamelStream *stream);

static gint stream_connect (CamelTcpStream *stream, const char *host, const char *service, gint fallback_port, CamelException *ex);
static gint stream_getsockopt (CamelTcpStream *stream, CamelSockOptData *data);
static gint stream_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data);
static struct sockaddr *stream_get_local_address (CamelTcpStream *stream, socklen_t *len);
static struct sockaddr *stream_get_remote_address (CamelTcpStream *stream, socklen_t *len);
static PRFileDesc *stream_get_file_desc (CamelTcpStream *stream);

static void
camel_tcp_stream_raw_class_init (CamelTcpStreamRawClass *camel_tcp_stream_raw_class)
{
	CamelTcpStreamClass *camel_tcp_stream_class =
		CAMEL_TCP_STREAM_CLASS (camel_tcp_stream_raw_class);
	CamelStreamClass *camel_stream_class =
		CAMEL_STREAM_CLASS (camel_tcp_stream_raw_class);

	parent_class = CAMEL_TCP_STREAM_CLASS (camel_type_get_global_classfuncs (camel_tcp_stream_get_type ()));

	/* virtual method overload */
	camel_stream_class->read = stream_read;
	camel_stream_class->write = stream_write;
	camel_stream_class->flush = stream_flush;
	camel_stream_class->close = stream_close;

	camel_tcp_stream_class->connect = stream_connect;
	camel_tcp_stream_class->getsockopt = stream_getsockopt;
	camel_tcp_stream_class->setsockopt  = stream_setsockopt;
	camel_tcp_stream_class->get_local_address  = stream_get_local_address;
	camel_tcp_stream_class->get_remote_address = stream_get_remote_address;
	camel_tcp_stream_class->get_file_desc = stream_get_file_desc;
}

static void
camel_tcp_stream_raw_init (gpointer object, gpointer klass)
{
	CamelTcpStreamRaw *stream = CAMEL_TCP_STREAM_RAW (object);
	CamelTcpStreamRawPrivate *priv;

	stream->priv = g_new0 (CamelTcpStreamRawPrivate, 1);
	priv = stream->priv;

	priv->sockfd = NULL;
}

static void
camel_tcp_stream_raw_finalize (CamelObject *object)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (object);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	if (priv->sockfd != NULL) {
		PR_Shutdown (priv->sockfd, PR_SHUTDOWN_BOTH);
		PR_Close (priv->sockfd);
	}

	g_free (raw->priv);
	raw->priv = NULL;
}

CamelType
camel_tcp_stream_raw_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_tcp_stream_get_type (),
					    "CamelTcpStreamRaw",
					    sizeof (CamelTcpStreamRaw),
					    sizeof (CamelTcpStreamRawClass),
					    (CamelObjectClassInitFunc) camel_tcp_stream_raw_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_tcp_stream_raw_init,
					    (CamelObjectFinalizeFunc) camel_tcp_stream_raw_finalize);
	}

	return type;
}

#ifdef SIMULATE_FLAKY_NETWORK
static gssize
flaky_tcp_write (gint fd, const gchar *buffer, gsize buflen)
{
	gsize len = buflen;
	gssize nwritten;
	gint val;

	if (buflen == 0)
		return 0;

	val = 1 + (gint) (10.0 * rand () / (RAND_MAX + 1.0));

	switch (val) {
	case 1:
		printf ("flaky_tcp_write (%d, ..., %d): (-1) EINTR\n", fd, buflen);
		errno = EINTR;
		return -1;
	case 2:
		printf ("flaky_tcp_write (%d, ..., %d): (-1) EAGAIN\n", fd, buflen);
		errno = EAGAIN;
		return -1;
	case 3:
		printf ("flaky_tcp_write (%d, ..., %d): (-1) EWOULDBLOCK\n", fd, buflen);
		errno = EWOULDBLOCK;
		return -1;
	case 4:
	case 5:
	case 6:
		len = 1 + (gsize) (buflen * rand () / (RAND_MAX + 1.0));
		len = MIN (len, buflen);
		/* fall through... */
	default:
		printf ("flaky_tcp_write (%d, ..., %d): (%d) '%.*s'", fd, buflen, len, (gint) len, buffer);
		nwritten = write (fd, buffer, len);
		if (nwritten < 0)
			printf (" errno => %s\n", g_strerror (errno));
		else if (nwritten < len)
			printf (" only wrote %d bytes\n", nwritten);
		else
			printf ("\n");

		return nwritten;
	}
}

#define write(fd, buffer, buflen) flaky_tcp_write (fd, buffer, buflen)

static gssize
flaky_tcp_read (gint fd, gchar *buffer, gsize buflen)
{
	gsize len = buflen;
	gssize nread;
	gint val;

	if (buflen == 0)
		return 0;

	val = 1 + (gint) (10.0 * rand () / (RAND_MAX + 1.0));

	switch (val) {
	case 1:
		printf ("flaky_tcp_read (%d, ..., %d): (-1) EINTR\n", fd, buflen);
		errno = EINTR;
		return -1;
	case 2:
		printf ("flaky_tcp_read (%d, ..., %d): (-1) EAGAIN\n", fd, buflen);
		errno = EAGAIN;
		return -1;
	case 3:
		printf ("flaky_tcp_read (%d, ..., %d): (-1) EWOULDBLOCK\n", fd, buflen);
		errno = EWOULDBLOCK;
		return -1;
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
	case 10:
		len = 1 + (gsize) (10.0 * rand () / (RAND_MAX + 1.0));
		len = MIN (len, buflen);
		/* fall through... */
	default:
		printf ("flaky_tcp_read (%d, ..., %d): (%d)", fd, buflen, len);
		nread = read (fd, buffer, len);
		if (nread < 0)
			printf (" errno => %s\n", g_strerror (errno));
		else if (nread < len)
			printf (" only read %d bytes\n", nread);
		else
			printf ("\n");

		return nread;
	}
}

#define read(fd, buffer, buflen) flaky_tcp_read (fd, buffer, buflen)

#endif /* SIMULATE_FLAKY_NETWORK */

/**
 * camel_tcp_stream_raw_new:
 *
 * Create a new #CamelTcpStreamRaw object.
 *
 * Returns: a new #CamelTcpStream object
 **/
CamelStream *
camel_tcp_stream_raw_new (void)
{
	CamelTcpStreamRaw *stream;

	stream = CAMEL_TCP_STREAM_RAW (camel_object_new (camel_tcp_stream_raw_get_type ()));

	return CAMEL_STREAM (stream);
}

void
_set_errno_from_pr_error (gint pr_code)
{
	/* FIXME: this should handle more. */
	switch (pr_code) {
	case PR_INVALID_ARGUMENT_ERROR:
		errno = EINVAL;
		break;
	case PR_PENDING_INTERRUPT_ERROR:
		errno = EINTR;
		break;
	case PR_IO_PENDING_ERROR:
		errno = EAGAIN;
		break;
#ifdef EWOULDBLOCK
	case PR_WOULD_BLOCK_ERROR:
		errno = EWOULDBLOCK;
		break;
#endif
#ifdef EINPROGRESS
	case PR_IN_PROGRESS_ERROR:
		errno = EINPROGRESS;
		break;
#endif
#ifdef EALREADY
	case PR_ALREADY_INITIATED_ERROR:
		errno = EALREADY;
		break;
#endif
#ifdef EHOSTUNREACH
	case PR_NETWORK_UNREACHABLE_ERROR:
		errno = EHOSTUNREACH;
		break;
#endif
#ifdef ECONNREFUSED
	case PR_CONNECT_REFUSED_ERROR:
		errno = ECONNREFUSED;
		break;
#endif
#ifdef ETIMEDOUT
	case PR_CONNECT_TIMEOUT_ERROR:
	case PR_IO_TIMEOUT_ERROR:
		errno = ETIMEDOUT;
		break;
#endif
#ifdef ENOTCONN
	case PR_NOT_CONNECTED_ERROR:
		errno = ENOTCONN;
		break;
#endif
#ifdef ECONNRESET
	case PR_CONNECT_RESET_ERROR:
		errno = ECONNRESET;
		break;
#endif
	case PR_IO_ERROR:
	default:
		errno = EIO;
		break;
	}
}

static gssize
read_from_prfd (PRFileDesc *fd, gchar *buffer, gsize n)
{
	PRFileDesc *cancel_fd;
	gssize nread;

	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}

	cancel_fd = camel_operation_cancel_prfd (NULL);
	if (cancel_fd == NULL) {
		do {
			nread = PR_Read (fd, buffer, n);
			if (nread == -1)
				_set_errno_from_pr_error (PR_GetError ());
		} while (nread == -1 && (PR_GetError () == PR_PENDING_INTERRUPT_ERROR ||
					 PR_GetError () == PR_IO_PENDING_ERROR ||
					 PR_GetError () == PR_WOULD_BLOCK_ERROR));
	} else {
		PRSocketOptionData sockopts;
		PRPollDesc pollfds[2];
		gboolean nonblock;
		gint error;

		/* get O_NONBLOCK options */
		sockopts.option = PR_SockOpt_Nonblocking;
		PR_GetSocketOption (fd, &sockopts);
		sockopts.option = PR_SockOpt_Nonblocking;
		nonblock = sockopts.value.non_blocking;
		sockopts.value.non_blocking = TRUE;
		PR_SetSocketOption (fd, &sockopts);

		pollfds[0].fd = fd;
		pollfds[0].in_flags = PR_POLL_READ;
		pollfds[1].fd = cancel_fd;
		pollfds[1].in_flags = PR_POLL_READ;

		do {
			PRInt32 res;

			pollfds[0].out_flags = 0;
			pollfds[1].out_flags = 0;
			nread = -1;

			res = PR_Poll(pollfds, 2, IO_TIMEOUT);
			if (res == -1)
				_set_errno_from_pr_error (PR_GetError());
			else if (res == 0) {
#ifdef ETIMEDOUT
				errno = ETIMEDOUT;
#else
				errno = EIO;
#endif
				goto failed;
			} else if (pollfds[1].out_flags == PR_POLL_READ) {
				errno = EINTR;
				goto failed;
			} else {
				do {
					nread = PR_Read (fd, buffer, n);
					if (nread == -1)
						_set_errno_from_pr_error (PR_GetError ());
				} while (nread == -1 && PR_GetError () == PR_PENDING_INTERRUPT_ERROR);
			}
		} while (nread == -1 && (PR_GetError () == PR_PENDING_INTERRUPT_ERROR ||
					 PR_GetError () == PR_IO_PENDING_ERROR ||
					 PR_GetError () == PR_WOULD_BLOCK_ERROR));

		/* restore O_NONBLOCK options */
	failed:
		error = errno;
		sockopts.option = PR_SockOpt_Nonblocking;
		sockopts.value.non_blocking = nonblock;
		PR_SetSocketOption (fd, &sockopts);
		errno = error;
	}

	return nread;
}

static gssize
stream_read (CamelStream *stream, gchar *buffer, gsize n)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	gssize result;

	d (g_print ("TcpStreamRaw %p: reading %" G_GSIZE_FORMAT " bytes...\n", ssl, n));

	result = read_from_prfd (priv->sockfd, buffer, n);

	d (g_print ("TcpStreamRaw %p: read %" G_GSSIZE_FORMAT " bytes, errno = %d\n", ssl, result, result == -1 ? errno : 0));

	return result;
}

static gssize
write_to_prfd (PRFileDesc *fd, const gchar *buffer, gsize n)
{
	gssize w, written = 0;
	PRFileDesc *cancel_fd;

	if (camel_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}

	cancel_fd = camel_operation_cancel_prfd (NULL);
	if (cancel_fd == NULL) {
		do {
			do {
				w = PR_Write (fd, buffer + written, n - written);
				if (w == -1)
					_set_errno_from_pr_error (PR_GetError ());
			} while (w == -1 && (PR_GetError () == PR_PENDING_INTERRUPT_ERROR ||
					     PR_GetError () == PR_IO_PENDING_ERROR ||
					     PR_GetError () == PR_WOULD_BLOCK_ERROR));

			if (w > 0)
				written += w;
		} while (w != -1 && written < n);
	} else {
		PRSocketOptionData sockopts;
		PRPollDesc pollfds[2];
		gboolean nonblock;
		gint error;

		/* get O_NONBLOCK options */
		sockopts.option = PR_SockOpt_Nonblocking;
		PR_GetSocketOption (fd, &sockopts);
		sockopts.option = PR_SockOpt_Nonblocking;
		nonblock = sockopts.value.non_blocking;
		sockopts.value.non_blocking = TRUE;
		PR_SetSocketOption (fd, &sockopts);

		pollfds[0].fd = fd;
		pollfds[0].in_flags = PR_POLL_WRITE;
		pollfds[1].fd = cancel_fd;
		pollfds[1].in_flags = PR_POLL_READ;

		do {
			PRInt32 res;

			pollfds[0].out_flags = 0;
			pollfds[1].out_flags = 0;
			w = -1;

			res = PR_Poll (pollfds, 2, IO_TIMEOUT);
			if (res == -1) {
				_set_errno_from_pr_error (PR_GetError());
				if (PR_GetError () == PR_PENDING_INTERRUPT_ERROR)
					w = 0;
			} else if (res == 0) {
#ifdef ETIMEDOUT
				errno = ETIMEDOUT;
#else
				errno = EIO;
#endif
			} else if (pollfds[1].out_flags == PR_POLL_READ) {
				errno = EINTR;
			} else {
				do {
					w = PR_Write (fd, buffer + written, n - written);
					if (w == -1)
						_set_errno_from_pr_error (PR_GetError ());
				} while (w == -1 && PR_GetError () == PR_PENDING_INTERRUPT_ERROR);

				if (w == -1) {
					if (PR_GetError () == PR_IO_PENDING_ERROR ||
					    PR_GetError () == PR_WOULD_BLOCK_ERROR)
						w = 0;
				} else
					written += w;
			}
		} while (w != -1 && written < n);

		/* restore O_NONBLOCK options */
		error = errno;
		sockopts.option = PR_SockOpt_Nonblocking;
		sockopts.value.non_blocking = nonblock;
		PR_SetSocketOption (fd, &sockopts);
		errno = error;
	}

	if (w == -1)
		return -1;

	return written;
}

static gssize
stream_write (CamelStream *stream, const gchar *buffer, gsize n)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	gssize result;

	d (g_print ("TcpStreamRaw %p: writing %" G_GSIZE_FORMAT " bytes...\n", ssl, n));

	result = write_to_prfd (priv->sockfd, buffer, n);

	d (g_print ("TcpStreamRaw %p: wrote %" G_GSSIZE_FORMAT " bytes, errno = %d\n", ssl, result, result == -1 ? errno : 0));

	return result;
}

static gint
stream_flush (CamelStream *stream)
{
#if 0
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	
	return PR_Sync (priv->sockfd);
#endif
	return 0;
}

static gint
stream_close (CamelStream *stream)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	d (g_print ("TcpStreamRaw %p: closing\n", stream));

	if (priv->sockfd == NULL) {
		errno = EINVAL;
		return -1;
	}

	PR_Shutdown (priv->sockfd, PR_SHUTDOWN_BOTH);
	if (PR_Close (priv->sockfd) == PR_FAILURE)
		return -1;

	priv->sockfd = NULL;

	return 0;
}

static gint
sockaddr_to_praddr(struct sockaddr *s, gint len, PRNetAddr *addr)
{
	/* We assume the ip addresses are the same size - they have to be anyway.
	   We could probably just use memcpy *shrug* */

	memset(addr, 0, sizeof(*addr));

	if (s->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)s;

		if (len < sizeof(*sin))
			return -1;

		addr->inet.family = PR_AF_INET;
		addr->inet.port = sin->sin_port;
		memcpy(&addr->inet.ip, &sin->sin_addr, sizeof(addr->inet.ip));

		return 0;
	}
#ifdef ENABLE_IPv6
	else if (s->sa_family == PR_AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *)s;

		if (len < sizeof(*sin))
			return -1;

		addr->ipv6.family = PR_AF_INET6;
		addr->ipv6.port = sin->sin6_port;
		addr->ipv6.flowinfo = sin->sin6_flowinfo;
		memcpy(&addr->ipv6.ip, &sin->sin6_addr, sizeof(addr->ipv6.ip));
		addr->ipv6.scope_id = sin->sin6_scope_id;

		return 0;
	}
#endif

	return -1;
}

static PRFileDesc *
socket_connect(struct addrinfo *host)
{
	PRNetAddr netaddr;
	PRFileDesc *fd, *cancel_fd;

	if (sockaddr_to_praddr(host->ai_addr, host->ai_addrlen, &netaddr) != 0) {
		errno = EINVAL;
		return NULL;
	}

	fd = PR_OpenTCPSocket(netaddr.raw.family);
	if (fd == NULL) {
		_set_errno_from_pr_error (PR_GetError ());
		return NULL;
	}

	cancel_fd = camel_operation_cancel_prfd(NULL);

	if (PR_Connect (fd, &netaddr, cancel_fd?0:CONNECT_TIMEOUT) == PR_FAILURE) {
		gint errnosave;

		_set_errno_from_pr_error (PR_GetError ());
		if (PR_GetError () == PR_IN_PROGRESS_ERROR ||
		    (cancel_fd && (PR_GetError () == PR_CONNECT_TIMEOUT_ERROR ||
				   PR_GetError () == PR_IO_TIMEOUT_ERROR))) {
			gboolean connected = FALSE;
			PRPollDesc poll[2];

			poll[0].fd = fd;
			poll[0].in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
			poll[1].fd = cancel_fd;
			poll[1].in_flags = PR_POLL_READ;

			do {
				poll[0].out_flags = 0;
				poll[1].out_flags = 0;

				if (PR_Poll (poll, cancel_fd?2:1, CONNECT_TIMEOUT) == PR_FAILURE) {
					_set_errno_from_pr_error (PR_GetError ());
					goto exception;
				}

				if (poll[1].out_flags == PR_POLL_READ) {
					errno = EINTR;
					goto exception;
				}

				if (PR_ConnectContinue(fd, poll[0].out_flags) == PR_FAILURE) {
					_set_errno_from_pr_error (PR_GetError ());
					if (PR_GetError () != PR_IN_PROGRESS_ERROR)
						goto exception;
				} else {
					connected = TRUE;
				}
			} while (!connected);
		} else {
		exception:
			errnosave = errno;
			PR_Shutdown (fd, PR_SHUTDOWN_BOTH);
			PR_Close (fd);
			errno = errnosave;

			return NULL;
		}

		errno = 0;
	}

	return fd;
}

/* Just opens a TCP socket to a (presumed) SOCKS proxy.  Does not actually
 * negotiate anything with the proxy; this is just to create the socket and connect.
 */
static PRFileDesc *
connect_to_proxy (CamelTcpStreamRaw *raw, const char *proxy_host, gint proxy_port, CamelException *ex)
{
	struct addrinfo *addr, *ai, hints;
	gchar serv[16];
	PRFileDesc *fd;
	gint save_errno;

	g_assert (proxy_host != NULL);

	d (g_print ("TcpStreamRaw %p: connecting to proxy %s:%d {\n  resolving proxy host\n", raw, proxy_host, proxy_port));

	sprintf (serv, "%d", proxy_port);

	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;

	addr = camel_getaddrinfo (proxy_host, serv, &hints, ex);
	if (!addr) {
		d (g_print ("  camel_getaddrinfo() for the proxy failed\n}\n"));
		return NULL;
	}

	d (g_print ("  creating socket and connecting\n"));

	ai = addr;
	while (ai) {
		fd = socket_connect (ai);
		if (fd)
			goto out;

		ai = ai->ai_next;
	}

out:
	save_errno = errno;

	camel_freeaddrinfo (addr);

	if (!fd) {
		errno = save_errno;
		d (g_print ("  could not connect: errno %d\n", errno));
	}

	return fd;
}

/* Returns the FD of a socket, already connected to and validated by the SOCKS4
 * proxy that is configured in the stream.  Otherwise returns NULL.  Assumes that
 * a proxy *is* configured with camel_tcp_stream_set_socks_proxy().  Only tries the first
 * connect_addr; if you want to traverse all the addrinfos, call this function for each of them.
 */
static PRFileDesc *
connect_to_socks4_proxy (CamelTcpStreamRaw *raw, const gchar *proxy_host, gint proxy_port, struct addrinfo *connect_addr, CamelException *ex)
{
	PRFileDesc *fd;
	gchar request[9];
	struct sockaddr_in *sin;
	gchar reply[8]; /* note that replies are 8 bytes, even if only the first 2 are used */
	gint save_errno;

	g_assert (connect_addr->ai_addr->sa_family == AF_INET);

	fd = connect_to_proxy (raw, proxy_host, proxy_port, ex);
	if (!fd)
		goto error;

	sin = (struct sockaddr_in *) connect_addr->ai_addr;

	request[0] = 0x04;				/* SOCKS4 */
	request[1] = 0x01;				/* CONNECT */
	memcpy (request + 2, &sin->sin_port, 2);	/* port in network byte order */
	memcpy (request + 4, &sin->sin_addr.s_addr, 4);	/* address in network byte order */
	request[8] = 0x00;				/* terminator */

	d (g_print ("  writing SOCKS4 request to connect to actual host\n"));
	if (write_to_prfd (fd, request, sizeof (request)) != sizeof (request)) {
		d (g_print ("  failed: %d\n", errno));
		goto error;
	}

	d (g_print ("  reading SOCKS4 reply\n"));
	if (read_from_prfd (fd, reply, sizeof (reply)) != sizeof (reply)) {
		d (g_print ("  failed: %d\n", errno));
		camel_exception_set (ex, CAMEL_EXCEPTION_PROXY_NOT_SUPPORTED, _("The proxy host does not support SOCKS4"));
		goto error;
	}

	if (reply[0] != 0) { /* version of reply code is 0 */
		errno = ECONNREFUSED;
		camel_exception_set (ex, CAMEL_EXCEPTION_PROXY_NOT_SUPPORTED, _("The proxy host does not support SOCKS4"));
		goto error;
	}

	if (reply[1] != 90) {   /* 90 means "request granted" */
               errno = ECONNREFUSED;
	       camel_exception_set (ex, CAMEL_EXCEPTION_PROXY_CANT_AUTHENTICATE,
				    _("The proxy host denied our request: code %d"),
				    reply[1]);
               goto error;
        }

	/* We are now proxied; we are ready to send "normal" data through the socket */

	d (g_print ("  success\n"));

	goto out;

error:
	if (fd) {
		save_errno = errno;
		PR_Shutdown (fd, PR_SHUTDOWN_BOTH);
		PR_Close (fd);
		errno = save_errno;
		fd = NULL;
	}

	d (g_print ("  returning errno %d\n", errno));

out:

	d (g_print ("}\n"));

	return fd;
}

/* Resolves a port number using getaddrinfo().  Returns 0 if the port can't be resolved or if the operation is cancelled */
static gint
resolve_port (const char *service, gint fallback_port, CamelException *ex)
{
	struct addrinfo *ai;
	CamelException my_ex;
	gint port;

	port = 0;

	camel_exception_init (&my_ex);
	/* FIXME: camel_getaddrinfo() does not take NULL hostnames.  This is different
	 * from the standard getaddrinfo(), which lets you pass a NULL hostname
	 * if you just want to resolve a port number.
	 */
	ai = camel_getaddrinfo ("localhost", service, NULL, &my_ex);
	if (ai == NULL && fallback_port != 0 && camel_exception_get_id (&my_ex) != CAMEL_EXCEPTION_USER_CANCEL)
		port = fallback_port;
	else if (ai == NULL) {
		camel_exception_xfer (ex, &my_ex);
	} else if (ai) {
		if (ai->ai_family == AF_INET) {
			port = ((struct sockaddr_in *) ai->ai_addr)->sin_port;
		}
#ifdef ENABLE_IPv6
		else if (ai->ai_family == AF_INET6) {
			port = ((struct sockaddr_in6 *) ai->ai_addr)->sin6_port;
		}
#endif
		else {
			g_assert_not_reached ();
		}

		camel_freeaddrinfo (ai);

		port = g_ntohs (port);
	}

	return port;
}

static gboolean
socks5_initiate_and_request_authentication (CamelTcpStreamRaw *raw, PRFileDesc *fd, CamelException *ex)
{
	gchar request[3];
	gchar reply[2];

	request[0] = 0x05;	/* SOCKS5 */
	request[1] = 1;		/* Number of authentication methods.  We just support "unauthenticated" for now. */
	request[2] = 0;		/* no authentication, please - extending this is left as an exercise for the reader */

	d (g_print ("  writing SOCKS5 request for authentication\n"));
	if (write_to_prfd (fd, request, sizeof (request)) != sizeof (request)) {
		d (g_print ("  failed: %d\n", errno));
		return FALSE;
	}

	d (g_print ("  reading SOCKS5 reply\n"));
	if (read_from_prfd (fd, reply, sizeof (reply)) != sizeof (reply)) {
		d (g_print ("  failed: %d\n", errno));
		camel_exception_set (ex, CAMEL_EXCEPTION_PROXY_NOT_SUPPORTED, _("The proxy host does not support SOCKS5"));
		return FALSE;
	}

	if (reply[0] != 5) {		/* server supports SOCKS5 */
		camel_exception_set (ex, CAMEL_EXCEPTION_PROXY_NOT_SUPPORTED, _("The proxy host does not support SOCKS5"));
		return FALSE;
	}

	if (reply[1] != 0) {		/* and it grants us no authentication (see request[2]) */
		camel_exception_setv (ex, CAMEL_EXCEPTION_PROXY_CANT_AUTHENTICATE,
				      _("Could not find a suitable authentication type: code 0x%x"),
				      reply[1]);
		return FALSE;
	}

	return TRUE;
}

static const char *
socks5_reply_error_to_string (gchar error_code)
{
	switch (error_code) {
	case 0x01: return _("General SOCKS server failure");
	case 0x02: return _("SOCKS server's rules do not allow connection");
	case 0x03: return _("Network is unreachable from SOCKS server");
	case 0x04: return _("Host is unreachable from SOCKS server");
	case 0x05: return _("Connection refused");
	case 0x06: return _("Time-to-live expired");
	case 0x07: return _("Command not supported by SOCKS server");
	case 0x08: return _("Address type not supported by SOCKS server");
	default: return _("Unknown error from SOCKS server");
	}
}

static gboolean
socks5_consume_reply_address (CamelTcpStreamRaw *raw, PRFileDesc *fd, CamelException *ex)
{
	gchar address_type;
	gint bytes_to_consume;
	gchar *address_and_port;

	address_and_port = NULL;

	if (read_from_prfd (fd, &address_type, sizeof (address_type)) != sizeof (address_type))
		goto incomplete_reply;

	if (address_type == 0x01)
		bytes_to_consume = 4; /* IPv4 address */
	else if (address_type == 0x04)
		bytes_to_consume = 16; /* IPv6 address */
	else if (address_type == 0x03) {
		guchar address_len;

		/* we'll get an octet with the address length, and then the address itself */

		if (read_from_prfd (fd, (gchar *) &address_len, sizeof (address_len)) != sizeof (address_len))
			goto incomplete_reply;

		bytes_to_consume = address_len;
	} else {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_NOT_CONNECTED, _("Got unknown address type from SOCKS server"));
		return FALSE;
	}

	bytes_to_consume += 2; /* 2 octets for port number */
	address_and_port = g_new (gchar, bytes_to_consume);

	if (read_from_prfd (fd, address_and_port, bytes_to_consume) != bytes_to_consume)
		goto incomplete_reply;

	g_free (address_and_port); /* Currently we don't do anything to these; maybe some authenticated method will need them later */

	return TRUE;

incomplete_reply:
	g_free (address_and_port);

	camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_NOT_CONNECTED, _("Incomplete reply from SOCKS server"));
	return FALSE;
}

static gboolean
socks5_request_connect (CamelTcpStreamRaw *raw, PRFileDesc *fd, const char *host, gint port, CamelException *ex)
{
	gchar *request;
	gchar reply[3];
	gint host_len;
	gint request_len;
	gint num_written;

	host_len = strlen (host);
	if (host_len > 255) {
		camel_exception_set (ex, CAMEL_EXCEPTION_INVALID_PARAM, _("Hostname is too long (maximum is 255 characters)"));
		return FALSE;
	}

	request_len = 4 + 1 + host_len + 2; /* Request header + octect for host_len + host + 2 octets for port */
	request = g_new (gchar, request_len);

	request[0] = 0x05;	/* Version - SOCKS5 */
	request[1] = 0x01;	/* Command - CONNECT */
	request[2] = 0x00;	/* Reserved */
	request[3] = 0x03;	/* ATYP - address type - DOMAINNAME */
	request[4] = host_len;
	memcpy (request + 5, host, host_len);
	request[5 + host_len] = (port & 0xff00) >> 8; /* high byte of port */
	request[5 + host_len + 1] = port & 0xff;      /* low byte of port */

	d (g_print ("  writing SOCKS5 request for connection\n"));
	num_written = write_to_prfd (fd, request, request_len);
	g_free (request);

	if (num_written != request_len) {
		d (g_print ("  failed: %d\n", errno));
		return FALSE;
	}

	d (g_print ("  reading SOCKS5 reply\n"));
	if (read_from_prfd (fd, reply, sizeof (reply)) != sizeof (reply)) {
		d (g_print ("  failed: %d\n", errno));
		return FALSE;
	}

	if (reply[0] != 0x05) {	/* SOCKS5 */
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_NOT_CONNECTED, _("Invalid reply from proxy server"));
		return FALSE;
	}

	if (reply[1] != 0x00) {	/* error code */
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_NOT_CONNECTED, socks5_reply_error_to_string (reply[1]));
		return FALSE;
	}

	if (reply[2] != 0x00) { /* reserved - must be 0 */
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_NOT_CONNECTED, _("Invalid reply from proxy server"));
		return FALSE;
	}

	/* The rest of the reply is the address that the SOCKS server uses to
	 * identify to the final host.  This is of variable length, so we must
	 * consume it by hand.
	 */
	if (!socks5_consume_reply_address (raw, fd, ex))
		return FALSE;
	
	return TRUE;
}

/* RFC 1928 - SOCKS protocol version 5 */
static PRFileDesc *
connect_to_socks5_proxy (CamelTcpStreamRaw *raw,
			 const char *proxy_host, gint proxy_port,
			 const char *host, const char *service, gint fallback_port,
			 CamelException *ex)
{
	PRFileDesc *fd;
	gint port;

	fd = connect_to_proxy (raw, proxy_host, proxy_port, ex);
	if (!fd)
		goto error;

	port = resolve_port (service, fallback_port, ex);
	if (port == 0)
		goto error;

	if (!socks5_initiate_and_request_authentication (raw, fd, ex))
		goto error;

	if (!socks5_request_connect (raw, fd, host, port, ex))
		goto error;

	d (g_print ("  success\n"));

	goto out;

error:
	if (fd) {
		gint save_errno;

		save_errno = errno;
		PR_Shutdown (fd, PR_SHUTDOWN_BOTH);
		PR_Close (fd);
		errno = save_errno;
		fd = NULL;
	}

	d (g_print ("  returning errno %d\n", errno));

out:

	d (g_print ("}\n"));

	return fd;
	
}

static gint
stream_connect (CamelTcpStream *stream, const char *host, const char *service, gint fallback_port, CamelException *ex)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	struct addrinfo *addr, *ai;
	struct addrinfo hints;
	CamelException my_ex;
	gint retval;
	const gchar *proxy_host;
	gint proxy_port;

	camel_tcp_stream_peek_socks_proxy (stream, &proxy_host, &proxy_port);

	if (proxy_host) {
		/* First, try SOCKS5, which does name resolution itself */

		camel_exception_init (&my_ex);
		priv->sockfd = connect_to_socks5_proxy (raw, proxy_host, proxy_port, host, service, fallback_port, &my_ex);
		if (priv->sockfd)
			return 0;
		else if (camel_exception_get_id (&my_ex) == CAMEL_EXCEPTION_PROXY_CANT_AUTHENTICATE
			 || camel_exception_get_id (&my_ex) != CAMEL_EXCEPTION_PROXY_NOT_SUPPORTED) {
			camel_exception_xfer (ex, &my_ex);
			return -1;
		}
	}

	/* Second, do name resolution ourselves and try SOCKS4 or a normal connection */

	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = PF_UNSPEC;

	camel_exception_init (&my_ex);
	addr = camel_getaddrinfo (host, service, &hints, &my_ex);
	if (addr == NULL && fallback_port != 0 && camel_exception_get_id (&my_ex) != CAMEL_EXCEPTION_USER_CANCEL) {
		char str_port[16];

		camel_exception_clear (&my_ex);
		sprintf (str_port, "%d", fallback_port);
		addr = camel_getaddrinfo (host, str_port, &hints, &my_ex);
	}

	if (addr == NULL) {
		camel_exception_xfer (ex, &my_ex);
		return -1;
	}

	ai = addr;

	while (ai) {
		if (proxy_host) {
			/* SOCKS4 only does IPv4 */
			if (ai->ai_addr->sa_family == AF_INET)
				priv->sockfd = connect_to_socks4_proxy (raw, proxy_host, proxy_port, ai, ex);
		} else
			priv->sockfd = socket_connect (ai);

		if (priv->sockfd) {
			retval = 0;
			goto out;
		}

		if (ai->ai_next != NULL)
			camel_exception_clear (ex); /* Only preserve the error from the last try, in case no tries are successful */

		ai = ai->ai_next;
	}

	retval = -1;

out:

	camel_freeaddrinfo (addr);

	return retval;
}

static gint
stream_getsockopt (CamelTcpStream *stream, CamelSockOptData *data)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	PRSocketOptionData sodata;

	memset ((gpointer) &sodata, 0, sizeof (sodata));
	memcpy ((gpointer) &sodata, (gpointer) data, sizeof (CamelSockOptData));

	if (PR_GetSocketOption (priv->sockfd, &sodata) == PR_FAILURE)
		return -1;

	memcpy ((gpointer) data, (gpointer) &sodata, sizeof (CamelSockOptData));

	return 0;
}

static gint
stream_setsockopt (CamelTcpStream *stream, const CamelSockOptData *data)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	PRSocketOptionData sodata;

	memset ((gpointer) &sodata, 0, sizeof (sodata));
	memcpy ((gpointer) &sodata, (gpointer) data, sizeof (CamelSockOptData));

	if (PR_SetSocketOption (priv->sockfd, &sodata) == PR_FAILURE)
		return -1;

	return 0;
}

static struct sockaddr *
sockaddr_from_praddr(PRNetAddr *addr, socklen_t *len)
{
	/* We assume the ip addresses are the same size - they have to be anyway */

	if (addr->raw.family == PR_AF_INET) {
		struct sockaddr_in *sin = g_malloc0(sizeof(*sin));

		sin->sin_family = AF_INET;
		sin->sin_port = addr->inet.port;
		memcpy(&sin->sin_addr, &addr->inet.ip, sizeof(sin->sin_addr));
		*len = sizeof(*sin);

		return (struct sockaddr *)sin;
	}
#ifdef ENABLE_IPv6
	else if (addr->raw.family == PR_AF_INET6) {
		struct sockaddr_in6 *sin = g_malloc0(sizeof(*sin));

		sin->sin6_family = AF_INET6;
		sin->sin6_port = addr->ipv6.port;
		sin->sin6_flowinfo = addr->ipv6.flowinfo;
		memcpy(&sin->sin6_addr, &addr->ipv6.ip, sizeof(sin->sin6_addr));
		sin->sin6_scope_id = addr->ipv6.scope_id;
		*len = sizeof(*sin);

		return (struct sockaddr *)sin;
	}
#endif

	return NULL;
}

static struct sockaddr *
stream_get_local_address(CamelTcpStream *stream, socklen_t *len)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	PRNetAddr addr;

	if (PR_GetSockName(priv->sockfd, &addr) != PR_SUCCESS)
		return NULL;

	return sockaddr_from_praddr(&addr, len);
}

static struct sockaddr *
stream_get_remote_address (CamelTcpStream *stream, socklen_t *len)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;
	PRNetAddr addr;

	if (PR_GetPeerName(priv->sockfd, &addr) != PR_SUCCESS)
		return NULL;

	return sockaddr_from_praddr(&addr, len);
}

static PRFileDesc *
stream_get_file_desc (CamelTcpStream *stream)
{
	CamelTcpStreamRaw *raw = CAMEL_TCP_STREAM_RAW (stream);
	CamelTcpStreamRawPrivate *priv = raw->priv;

	return priv->sockfd;
}

void
_camel_tcp_stream_raw_replace_file_desc (CamelTcpStreamRaw *raw, PRFileDesc *new_file_desc)
{
	CamelTcpStreamRawPrivate *priv = raw->priv;

	priv->sockfd = new_file_desc;
}
