#include <stdio.h>
#include <stdlib.h>
#include "lp.h"

#include "proton/object.h"
#include "proton/engine.h"
#include "proton/io.h"
#include "dispatch/dispatch.h"

struct ldp_connection_t {

    ldp_activity_f events;

    pn_connection_t *conn;
    pn_collector_t *coll;
    pn_transport_t *transp;
    pn_io_t *io;
    pn_socket_t sock;

    dispatch_queue_t dq;
    bool is_reading;
    dispatch_source_t readable;
    bool is_writing;
    dispatch_source_t writable;
};

#define ldp_connection_initialize NULL
#define ldp_connection_hashcode NULL
#define ldp_connection_compare NULL
#define ldp_connection_inspect NULL

static void ldp_connection_finalize(void *object) {
    ldp_connection_t *conn = (ldp_connection_t*)object;
    pn_free(conn->conn);
    pn_free(conn->coll);
    pn_free(conn->transp);
    pn_free(conn->io);
    dispatch_release(conn->dq);
    dispatch_release(conn->readable);
    dispatch_release(conn->writable);
}

ldp_connection_t * ldp_connection(ldp_activity_f events) {
    static pn_class_t clazz = PN_CLASS(ldp_connection);
    ldp_connection_t *conn = (ldp_connection_t*)pn_new(sizeof(*conn), &clazz);

    conn->events = events;

    conn->conn = pn_connection();
    conn->coll = pn_collector();
    pn_connection_collect(conn->conn, conn->coll);
    conn->transp = NULL;
    conn->io = pn_io();
    conn->sock = PN_INVALID_SOCKET;

    conn->dq = dispatch_queue_create("ldp", NULL);
    dispatch_set_context(conn->dq, conn);

    conn->readable = NULL;
    conn->writable = NULL;
    return conn;
}

void connection_pump(ldp_connection_t *conn) {
    if (pn_event_type(pn_collector_peek(conn->coll)) != PN_EVENT_NONE) {
        conn->events(conn, conn->coll);
    }
    bool can_read = pn_transport_capacity(conn->transp) > 0;
    bool can_write = pn_transport_pending(conn->transp) > 0;
    if (can_read != conn->is_reading) {
        conn->is_reading = can_read;
        if (can_read) {
            dispatch_resume(conn->readable);
        } else {
            dispatch_suspend(conn->readable);
        }
    }
    if (can_write != conn->is_writing) {
        conn->is_writing = can_write;
        if (can_write) {
            dispatch_resume(conn->writable);
        } else {
            dispatch_suspend(conn->writable);
        }
    }
}

struct ldp_connection_connect_args {
    ldp_connection_t *conn;
    pn_string_t *host;
    pn_string_t *port;
};

static void ldp_error_report(ldp_connection_t *conn, const char *pfx, const char *error) {
    fprintf(stderr, "%s ERROR %s\n", pfx, error);
}

static void connection_readable(void *vconn) {
    ldp_connection_t *conn = (ldp_connection_t*)vconn;
    ssize_t capacity = pn_transport_capacity(conn->transp);
    ssize_t n = pn_recv(conn->io, conn->sock, pn_transport_tail(conn->transp), capacity);
    if (n <= 0) {
        if (n == 0 || !pn_wouldblock(conn->io)) {
            if (n < 0) perror("recv");
            pn_transport_close_tail(conn->transp);
            if (!(pn_connection_state(conn->conn) & PN_REMOTE_CLOSED)) {
                ldp_error_report(conn, "CONNECTION", "connection aborted (remote)");
            }
        }
    } else {
        pn_transport_process(conn->transp, n);
    }
    connection_pump(conn);
}

static void connection_writable(void *vconn) {
    ldp_connection_t *conn = (ldp_connection_t*)vconn;
    ssize_t pending = pn_transport_pending(conn->transp);
    ssize_t n = pn_send(conn->io, conn->sock, pn_transport_head(conn->transp), pending);
    if (n < 0) {
        if (!pn_wouldblock(conn->io)) {
            ldp_error_report(conn, "CONNECTION", "send");
            pn_transport_close_head(conn->transp);
        }
    } else {
        pn_transport_pop(conn->transp, n);
    }
    connection_pump(conn);
}

static void connection_connect(void* vargs) {
    struct ldp_connection_connect_args *args = (struct ldp_connection_connect_args*)vargs;
    ldp_connection_t *conn = args->conn;
    pn_string_t *host = args->host;
    pn_string_t *port = args->port;
    if (conn->transp != NULL) {
        pn_connection_close(conn->conn);
        pn_transport_unbind(conn->transp);
        pn_connection_reset(conn->conn);
        conn->transp = NULL;
    }
    if (conn->sock != PN_INVALID_SOCKET) {
        pn_close(conn->io, conn->sock);
        conn->sock = PN_INVALID_SOCKET;
    }
    conn->transp = pn_transport();
    pn_transport_bind(conn->transp, conn->conn);

    conn->sock = pn_connect(conn->io, pn_string_get(host), pn_string_get(port));

    conn->is_reading = false;
    conn->readable = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, conn->sock, 0, conn->dq);
    dispatch_set_context(conn->readable, conn);
    dispatch_source_set_event_handler_f(conn->readable, connection_readable);

    conn->is_writing = false;
    conn->writable = dispatch_source_create(DISPATCH_SOURCE_TYPE_WRITE, conn->sock, 0, conn->dq);
    dispatch_set_context(conn->writable, conn);
    dispatch_source_set_event_handler_f(conn->writable, connection_writable);

    connection_pump(conn);

    pn_decref(conn);
    free(host);
    free(port);
    free(args);
}

int ldp_connection_connect(ldp_connection_t *conn, const char *host, const char *port) {
    struct ldp_connection_connect_args *args = (struct ldp_connection_connect_args*)malloc(sizeof(*args));
    args->conn = (ldp_connection_t*)pn_incref(conn);
    args->host = pn_string(host);
    args->port = pn_string(port);
    dispatch_async_f(conn->dq, args, connection_connect);
    return 0;
}
