#include <stdio.h>
#include <stdlib.h>
#include "lp.h"

#include "proton/object.h"
#include "proton/engine.h"
#include "proton/io.h"
#include "dispatch/dispatch.h"
#include "dispatch/once.h"

struct ldp_connection_t {

    ldp_activity_f events;

    pn_connection_t *conn;
    pn_collector_t *coll;
    pn_transport_t *transp;
    pn_io_t *io;
    pn_socket_t sock;

    dispatch_queue_t dq;
    bool is_connecting;
    bool is_reading;
    dispatch_source_t readable;
    bool is_writing;
    dispatch_source_t writable;
};

#define ldp_connection_initialize NULL
#define ldp_connection_hashcode NULL
#define ldp_connection_compare NULL
#define ldp_connection_inspect NULL
#define LDP_DEBUG

#ifdef LDP_DEBUG
#define ldp_debug(x, ...) do { fprintf(stderr, x, __VA_ARGS__); } while(0)
#else
#define ldp_debug(x, ...) do { } while(0)
#endif

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
    if (pn_collector_peek(conn->coll)) {
        conn->events(conn, conn->coll);
    }
    bool can_read = pn_transport_capacity(conn->transp) > 0;
    bool can_write = pn_transport_pending(conn->transp) > 0;
    if (can_read != conn->is_reading && !conn->is_connecting) {
        conn->is_reading = can_read;
        if (can_read) {
            dispatch_resume(conn->readable);
            ldp_debug("resume read\n");
        } else {
            dispatch_suspend(conn->readable);
            ldp_debug("suspend read\n");
        }
    } else {
        ldp_debug("read stays %s\n", conn->is_reading ? "resumed" : "suspended");
    }
    if (can_write != conn->is_writing) {
        conn->is_writing = can_write;
        if (can_write) {
            dispatch_resume(conn->writable);
            ldp_debug("resume write\n");
        } else {
            dispatch_suspend(conn->writable);
            ldp_debug("suspend write\n");
        }
    } else {
        ldp_debug("write stays %s\n", conn->is_writing ? "resumed" : "suspended");
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
        int processed = pn_transport_process(conn->transp, n);
        ldp_debug("recvd %d processed %d\n", (int)n, processed);
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
        ldp_debug("sent %d remaining to send %d\n",
            (int)n, (int)pn_transport_pending(conn->transp));
    }
    conn->is_connecting = false;
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
    pn_connection_open(conn->conn);

    conn->sock = pn_connect(conn->io, pn_string_get(host), pn_string_get(port));

    conn->is_connecting = true;
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
    pn_free(host);
    pn_free(port);
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
