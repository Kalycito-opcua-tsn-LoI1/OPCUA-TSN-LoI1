/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2017-2018 Fraunhofer IOSB (Author: Andreas Ebner)
 * Copyright (c) 2018-2019 Kalycito Infotech Private Limited
 */

#include "ua_pubsub_ns0.h"
#include "ua_pubsub_manager.h"

#ifdef UA_ENABLE_PUBSUB
/* conditional compilation */
#ifdef UA_ENABLE_PUBSUB_CUSTOM_PUBLISH_INTERRUPT

/* Add a callback for cyclic repetition */
UA_StatusCode
UA_PubSubManager_addRepeatedCallback(UA_Server* server,
                                     UA_ServerCallback callback,
                                     void* data,
                                     UA_UInt32 interval,
                                     UA_UInt64* callbackId)
{
    return UA_Timer_addRepeatedCallback(&server->timer,
                                        (UA_TimerCallback)callback, data,
                                        interval, callbackId);
}

/* Modify the interval of the callback for cyclic repetition */
UA_StatusCode
UA_PubSubManager_changeRepeatedCallbackInterval(UA_Server* server,
                                                UA_UInt64 callbackId,
                                                UA_UInt32 interval)
{
    return UA_Timer_changeRepeatedCallbackInterval(&server->timer,
                                                   callbackId, interval);
}

/* Remove the callback added for cyclic repetition */
UA_StatusCode
UA_PubSubManager_removeRepeatedPubSubCallback(UA_Server* server,
                                              UA_UInt64 callbackId)
{
    return UA_Timer_removeRepeatedCallback(&server->timer, callbackId);
}
#endif
#endif /* UA_ENABLE_PUBSUB */
