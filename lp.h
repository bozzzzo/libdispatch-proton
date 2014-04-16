#ifndef _LP_H_INCLUDED_
#define _LP_H_INCLUDED_

#include "proton/engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ldp_connection_t ldp_connection_t;

typedef void(*ldp_activity_f)(ldp_connection_t*, pn_collector_t*);

ldp_connection_t * ldp_connection(ldp_activity_f, void *);

void *ldp_connection_ctx(ldp_connection_t*);
void ldp_connection_execute(ldp_connection_t*, void*, void(*)(void*));

int ldp_connection_connect(ldp_connection_t *conn, const char *host, const char *port);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _LP_H_INCLUDED_
