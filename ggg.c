#include <stdio.h>
#include <proton/engine.h>
#include <proton/io.h>
#include <proton/event.h>
#include <proton/selectable.h>
#include <proton/selector.h>

#include <dispatch/dispatch.h>

#include "lp.h"

void process_connection(ldp_connection_t *conn, pn_event_t *event) {
    fprintf(stderr, "connection event\n");
}

void process_session(ldp_connection_t *conn, pn_event_t *event) {
    fprintf(stderr, "session event\n");
}

void process_link(ldp_connection_t *conn, pn_event_t *event) {
    fprintf(stderr, "link event\n");
}

void process_flow(ldp_connection_t *conn, pn_event_t *event) {
    fprintf(stderr, "flow event\n");
}

void process_delivery(ldp_connection_t *conn, pn_event_t *event) {
    fprintf(stderr, "delivery event\n");
}

void process_transport(ldp_connection_t *conn, pn_event_t *event) {
    fprintf(stderr, "transport event\n");
}

void events(ldp_connection_t *conn, pn_collector_t *coll) {
    pn_event_t *event;
    while((event = pn_collector_peek(coll))) {
        switch (pn_event_type(event)) {
        case PN_EVENT_NONE:
            break;
        case PN_CONNECTION_STATE:
            process_connection(conn, event);
            break;
        case PN_SESSION_STATE:
            process_session(conn, event);
            break;
        case PN_LINK_STATE:
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

int main() {
	ldp_connection_t *conn = ldp_connection(events);
	ldp_connection_connect(conn, "host", "80");
    dispatch_main();
}
