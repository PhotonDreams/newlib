/* cygserver_client.cc

   Copyright 2001, 2002 Red Hat Inc.

   Written by Egor Duda <deo@logos-m.ru>

   This file is part of Cygwin.

   This software is a copyrighted work licensed under the terms of the
   Cygwin license.  Please consult the file "CYGWIN_LICENSE" for
   details. */

/* to allow this to link into cygwin and the .dll, a little magic is needed. */
#ifdef __OUTSIDE_CYGWIN__
#include "woutsup.h"
#else
#include "winsup.h"
#endif

#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
//#include "security.h"
#include "cygwin/cygserver_transport.h"
#include "cygwin/cygserver_transport_pipes.h"
#include "cygwin/cygserver_transport_sockets.h"
#include "cygwin/cygserver.h"

/* 0 = untested, 1 = running, 2 = dead */
int cygserver_running=CYGSERVER_UNKNOWN;
/* on by default during development. For release, we probably want off by default */
int allow_daemon = TRUE;

client_request_get_version::client_request_get_version () : client_request (CYGSERVER_REQUEST_GET_VERSION, sizeof (version))
{
  buffer = (char *)&version;
}

#ifndef __INSIDE_CYGWIN__

client_request_attach_tty::client_request_attach_tty () : client_request (CYGSERVER_REQUEST_ATTACH_TTY, sizeof (req))
{
  buffer = (char *)&req;
  req.pid = 0;
  req.master_pid = 0;
  req.from_master = NULL;
  req.to_master = NULL;
}

#else /* __INSIDE_CYGWIN__ */

client_request_attach_tty::client_request_attach_tty (DWORD npid, DWORD nmaster_pid, HANDLE nfrom_master, HANDLE nto_master) : client_request (CYGSERVER_REQUEST_ATTACH_TTY, sizeof (req))
{
  buffer = (char *)&req;
  req.pid = npid;
  req.master_pid = nmaster_pid;
  req.from_master = nfrom_master;
  req.to_master = nto_master;
}

#endif /* __INSIDE_CYGWIN__ */

client_request_shutdown::client_request_shutdown () : client_request (CYGSERVER_REQUEST_SHUTDOWN, 0)
{
  buffer = NULL;
}

client_request::client_request (cygserver_request_code id, ssize_t buffer_size) : header (id, buffer_size)
{
}

client_request::~client_request ()
{
}

void
client_request::send (transport_layer_base *conn)
{
  if (!conn)
    return;
  debug_printf("this=%p, conn=%p",this, conn);
  ssize_t bytes_written, bytes_read;
  debug_printf("header.cb = %ld",header.cb);
  if ((bytes_written = conn->write ((char *)&header, sizeof (header)))
    != sizeof(header) || (header.cb &&
    (bytes_written = conn->write (buffer, header.cb)) != header.cb))
    {
      header.error_code = -1;
      debug_printf ("bytes written != request size");
      return;
    }

  debug_printf("Sent request, size (%ld)",bytes_written);

  if ((bytes_read = conn->read ((char *)&header, sizeof (header)))
    != sizeof (header) || (header.cb &&
    (bytes_read = conn->read (buffer, header.cb)) != header.cb))
    {
      header.error_code = -1;
      debug_printf("failed reading response ");
      return;
    }
  debug_printf ("completed ok");
}

#ifdef __INSIDE_CYGWIN__

/* Oh, BTW: Fix the procedural basis and make this more intuitive. */

int
cygserver_request (client_request * req)
{
  class transport_layer_base *transport;

  if (!req || allow_daemon != TRUE)
    return -1;

  /* dont' retry every request if the server's not there */
  if (cygserver_running==CYGSERVER_DEAD && req->header.req_id != CYGSERVER_REQUEST_GET_VERSION)
    return -1;

  transport = create_server_transport ();

  /* FIXME: have at most one connection per thread. use TLS to store the details */
  /* logic is:
   * if not tlskey->conn, new conn,
   * then; transport=conn;
   */
  if (!transport->connect ())
    {
      delete transport;
      return -1;
    }

  debug_printf ("connected to server %p", transport);

  req->send(transport);

  transport->close ();

  delete transport;

  return 0;
}

#if 0
BOOL
check_cygserver_available ()
{
  BOOL ret_val = FALSE;
  HANDLE pipe = CreateFile (pipe_name,
			    GENERIC_READ | GENERIC_WRITE,
			    FILE_SHARE_READ | FILE_SHARE_WRITE,
			    &sec_all_nih,
			    OPEN_EXISTING,
			    0,
			    NULL);
  if (pipe != INVALID_HANDLE_VALUE || GetLastError () != ERROR_PIPE_BUSY)
    ret_val = TRUE;

  if (pipe && pipe != INVALID_HANDLE_VALUE)
    CloseHandle (pipe);

  return (ret_val);
}
#endif

void
cygserver_init ()
{
  if (!allow_daemon)
    {
      cygserver_running = CYGSERVER_DEAD;
      return;
    }

  if (cygserver_running == CYGSERVER_OK)
    return;

  client_request_get_version req;

  // This indicates that we failed to connect to cygserver at all but
  // that's fine as cygwin doesn't need it to be running.
  if (cygserver_request (&req) == -1)
    {
      cygserver_running = CYGSERVER_DEAD;
      return;
    }

  // We connected to the server but something went wrong after that
  // (sending the message, cygserver itself, or receiving the reply).
  if (req.header.error_code != 0)
    {
      cygserver_running = CYGSERVER_DEAD;
      debug_printf ("failure in cygserver version request: %d",
		    req.header.error_code);
      debug_printf ("process will continue without cygserver support");
      return;
    }

  if (req.version.major != CYGWIN_SERVER_VERSION_MAJOR ||
      req.version.api != CYGWIN_SERVER_VERSION_API ||
      req.version.minor > CYGWIN_SERVER_VERSION_MINOR)
    api_fatal ("incompatible version of cygwin server:\n"
	       "client version %d.%d.%d.%d, server version %ld.%ld.%ld.%ld",
	       CYGWIN_SERVER_VERSION_MAJOR,
	       CYGWIN_SERVER_VERSION_API,
	       CYGWIN_SERVER_VERSION_MINOR,
	       CYGWIN_SERVER_VERSION_PATCH,
	       req.version.major,
	       req.version.api,
	       req.version.minor,
	       req.version.patch);

  cygserver_running = CYGSERVER_OK;
}

#endif /* __INSIDE_CYGWIN__ */
