#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <WinSock2.h>
#endif
#include <proton/engine.h>
#include <proton/io.h>
#include <proton/event.h>
#include <proton/selectable.h>
#include <proton/selector.h>
#include <proton/message.h>
#include <proton/buffer.h>
#include <proton/types.h>

#include <dispatch/dispatch.h>

#include "lp.h"

void process_connection(ldp_connection_t *conn, pn_event_t *event) {
    fprintf(stderr, "connection event %s\n", pn_event_type_name(pn_event_type(event)));

    if (pn_event_type(event) == PN_CONNECTION_REMOTE_STATE) {
        pn_session_t *session = pn_session(pn_event_connection(event));
        pn_session_open(session);
    }
}

void process_session(ldp_connection_t *conn, pn_event_t *event) {
    fprintf(stderr, "session event %s\n", pn_event_type_name(pn_event_type(event)));

    if (pn_event_type(event) == PN_SESSION_REMOTE_STATE) {
        pn_session_t *session = pn_event_session(event);
        pn_link_t *sender = pn_sender(session, "sender-xxx");
        pn_terminus_set_address(pn_link_source(sender), "hello-world");
        pn_link_open(sender);
    }
}

void process_link(ldp_connection_t *conn, pn_event_t *event) {
    fprintf(stderr, "link event %s\n", pn_event_type_name(pn_event_type(event)));
}

void process_flow(ldp_connection_t *conn, pn_event_t *event) {
    fprintf(stderr, "flow event %s\n", pn_event_type_name(pn_event_type(event)));

    pn_link_t *sender = pn_event_link(event);

    pn_message_t *message = pn_message();
    pn_message_set_address(message, "amqp://foo/bar");
    pn_data_t *body = pn_message_body(message);
    char *msgtext = "hello world!";
    pn_data_put_string(body, pn_bytes(strlen(msgtext), msgtext));
    pn_buffer_t *buffer = pn_buffer(1000);
    char *encoded = pn_buffer_bytes(buffer).start;
    size_t size = pn_buffer_capacity(buffer);
    int err = pn_message_encode(message, encoded, &size);
    if (err) {
        fprintf(stderr, "trouble encoding message\n");
    } else {
        char tag[8];
        static uint64_t next_tag;
        *((uint64_t*)tag) = ++next_tag;
        pn_delivery_t *d = pn_delivery(sender, pn_dtag(tag, 8));
        pn_link_send(sender, encoded, size);
        pn_link_advance(sender);
    }
    pn_buffer_free(buffer);
    pn_message_free(message);
}

void process_delivery(ldp_connection_t *conn, pn_event_t *event) {
    fprintf(stderr, "delivery event %s\n", pn_event_type_name(pn_event_type(event)));
}

void process_transport(ldp_connection_t *conn, pn_event_t *event) {
    fprintf(stderr, "transport event %s\n", pn_event_type_name(pn_event_type(event)));
}

void events(ldp_connection_t *conn, pn_collector_t *coll) {
    pn_event_t *event;
    while((event = pn_collector_peek(coll))) {
        switch (pn_event_type(event)) {
        case PN_EVENT_NONE:
            break;
        case PN_CONNECTION_REMOTE_STATE:
        case PN_CONNECTION_LOCAL_STATE:
            process_connection(conn, event);
            break;
        case PN_SESSION_REMOTE_STATE:
        case PN_SESSION_LOCAL_STATE:
            process_session(conn, event);
            break;
        case PN_LINK_REMOTE_STATE:
        case PN_LINK_LOCAL_STATE:
            process_link(conn, event);
            break;
        case PN_LINK_FLOW:
            process_flow(conn, event);
            break;
        case PN_DELIVERY:
            process_delivery(conn, event);
            break;
        case PN_TRANSPORT:
            process_transport(conn, event);
            break;

        }
        pn_collector_pop(coll);
    }
}

int main(int argc, const char *argv[]) {
#ifdef _WIN32
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);
#endif
    ldp_connection_t *conn = ldp_connection(events, NULL);
    const char *host = argc > 1 ? argv[1] : "host";
    const char *port = argc > 2 ? argv[2] : "8194";
    ldp_connection_connect(conn, host, port);
    dispatch_main();
}
