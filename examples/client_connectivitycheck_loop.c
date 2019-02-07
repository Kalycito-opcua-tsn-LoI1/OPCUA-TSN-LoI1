/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information. */

/* Enable POSIX features */
#if !defined(_XOPEN_SOURCE) && !defined(_WRS_KERNEL)
# define _XOPEN_SOURCE 600
#endif
#ifndef _DEFAULT_SOURCE
# define _DEFAULT_SOURCE
#endif
/* On older systems we need to define _BSD_SOURCE.
 * _DEFAULT_SOURCE is an alias for that. */
#ifndef _BSD_SOURCE
# define _BSD_SOURCE
#endif

#include "open62541.h"
#include <signal.h>

#ifdef _WIN32
# include <windows.h>
# define UA_sleep_ms(X) Sleep(X)
#else
# include <unistd.h>
# define UA_sleep_ms(X) usleep(X * 1000)
#endif

UA_Boolean running = true;
UA_Logger logger = UA_Log_Stdout;

static void stopHandler(int sign) {
    UA_LOG_INFO(logger, UA_LOGCATEGORY_USERLAND, "Received Ctrl-C");
    running = 0;
}

static void
inactivityCallback (UA_Client *client) {
    UA_LOG_INFO(logger, UA_LOGCATEGORY_USERLAND, "Server Inactivity");
}

int main(void) {
    signal(SIGINT, stopHandler); /* catches ctrl-c */

    UA_ClientConfig config = UA_ClientConfig_default;
    /* Set stateCallback */
    config.inactivityCallback = inactivityCallback;

    /* Perform a connectivity check every 2 seconds */
    config.connectivityCheckInterval = 2000;

    UA_Client *client = UA_Client_new(config);

    /* Endless loop runAsync */
    while (running) {
        /* if already connected, this will return GOOD and do nothing */
        /* if the connection is closed/errored, the connection will be reset and then reconnected */
        /* Alternatively you can also use UA_Client_getState to get the current state */
        UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
        if(retval != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(logger, UA_LOGCATEGORY_USERLAND, "Not connected. Retrying to connect in 1 second");
            /* The connect may timeout after 1 second (see above) or it may fail immediately on network errors */
            /* E.g. name resolution errors or unreachable network. Thus there should be a small sleep here */
            UA_sleep_ms(1000);
            continue;
        }

        UA_Client_run_iterate(client, 1000);
    };

    /* Clean up */
    UA_Client_delete(client); /* Disconnects the client internally */
    return UA_STATUSCODE_GOOD;
}
