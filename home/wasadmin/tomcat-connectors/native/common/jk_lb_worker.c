/*
 *  Licensed to the Apache Software Foundation (ASF) under one or more
 *  contributor license agreements.  See the NOTICE file distributed with
 *  this work for additional information regarding copyright ownership.
 *  The ASF licenses this file to You under the Apache License, Version 2.0
 *  (the "License"); you may not use this file except in compliance with
 *  the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/***************************************************************************
 * Description: Load balancer worker, knows how to load balance among      *
 *              several workers.                                           *
 * Author:      Gal Shachor <shachor@il.ibm.com>                           *
 * Author:      Mladen Turk <mturk@apache.org>                             *
 * Author:      Rainer Jung <rjung@apache.org>                             *
 ***************************************************************************/

#include "jk_pool.h"
#include "jk_service.h"
#include "jk_util.h"
#include "jk_worker.h"
#include "jk_lb_worker.h"
#include "jk_ajp13_worker.h"
#include "jk_ajp14_worker.h"
#include "jk_mt.h"
#include "jk_shm.h"

/*
 * The load balancing code in this
 */

/*
 * The following two macros need to be kept in sync with
 * the existing values for state and activation.
 * Note: state <= JK_LB_STATE_FORCE is equivalent to
 *       state is none of JK_LB_STATE_BUSY, JK_LB_STATE_ERROR, JK_LB_STATE_PROBE
 * Note: state <= JK_LB_STATE_BUSY is equivalent to
 *       state is none of JK_LB_STATE_ERROR, JK_LB_STATE_PROBE
 * Note: activation == JK_LB_ACTIVATION_ACTIVE is equivalent to
 *       activation is none of JK_LB_ACTIVATION_STOPPED, JK_LB_ACTIVATION_DISABLED
 */
#define JK_WORKER_USABLE(s, activation)   ((s) <= JK_LB_STATE_FORCE && activation == JK_LB_ACTIVATION_ACTIVE)
#define JK_WORKER_USABLE_STICKY(s, activation)   ((s) <= JK_LB_STATE_FORCE && activation != JK_LB_ACTIVATION_STOPPED)

static const char *lb_locking_type[] = {
    JK_LB_LOCK_TEXT_OPTIMISTIC,
    JK_LB_LOCK_TEXT_PESSIMISTIC,
    "unknown",
    NULL
};

static const char *lb_method_type[] = {
    JK_LB_METHOD_TEXT_REQUESTS,
    JK_LB_METHOD_TEXT_TRAFFIC,
    JK_LB_METHOD_TEXT_BUSYNESS,
    JK_LB_METHOD_TEXT_SESSIONS,
    JK_LB_METHOD_TEXT_NEXT,
    "unknown",
    NULL
};

static const char *lb_state_type[] = {
    JK_LB_STATE_TEXT_IDLE,
    JK_LB_STATE_TEXT_OK,
    JK_LB_STATE_TEXT_RECOVER,
    JK_LB_STATE_TEXT_FORCE,
    JK_LB_STATE_TEXT_BUSY,
    JK_LB_STATE_TEXT_ERROR,
    JK_LB_STATE_TEXT_PROBE,
    "unknown",
    NULL
};

static const char *lb_activation_type[] = {
    JK_LB_ACTIVATION_TEXT_ACTIVE,
    JK_LB_ACTIVATION_TEXT_DISABLED,
    JK_LB_ACTIVATION_TEXT_STOPPED,
    "unknown",
    NULL
};

static const char *lb_first_log_names[] = {
    JK_NOTE_LB_FIRST_NAME,
    JK_NOTE_LB_FIRST_VALUE,
    JK_NOTE_LB_FIRST_ACCESSED,
    JK_NOTE_LB_FIRST_SESSIONS,
    JK_NOTE_LB_FIRST_READ,
    JK_NOTE_LB_FIRST_TRANSFERRED,
    JK_NOTE_LB_FIRST_ERRORS,
    JK_NOTE_LB_FIRST_BUSY,
    JK_NOTE_LB_FIRST_ACTIVATION,
    JK_NOTE_LB_FIRST_STATE,
    NULL
};

static const char *lb_last_log_names[] = {
    JK_NOTE_LB_LAST_NAME,
    JK_NOTE_LB_LAST_VALUE,
    JK_NOTE_LB_LAST_ACCESSED,
    JK_NOTE_LB_LAST_SESSIONS,
    JK_NOTE_LB_LAST_READ,
    JK_NOTE_LB_LAST_TRANSFERRED,
    JK_NOTE_LB_LAST_ERRORS,
    JK_NOTE_LB_LAST_BUSY,
    JK_NOTE_LB_LAST_ACTIVATION,
    JK_NOTE_LB_LAST_STATE,
    NULL
};

struct lb_endpoint
{
    lb_worker_t     *worker;
    jk_endpoint_t    endpoint;
    int             *states;
};
typedef struct lb_endpoint lb_endpoint_t;


/* Calculate the greatest common divisor of two positive integers */
static jk_uint64_t gcd(jk_uint64_t a, jk_uint64_t b)
{
    jk_uint64_t r;
    if (b > a) {
        r = a;
        a = b;
        b = r;
    }
    while (b > 0) {
        r = a % b;
        a = b;
        b = r;
    }
    return a;
}

/* Calculate the smallest common multiple of two positive integers */
static jk_uint64_t scm(jk_uint64_t a, jk_uint64_t b)
{
    return a * b / gcd(a, b);
}

/* Return the string representation of the lb lock type */
/* based on the integer representation */
const char *jk_lb_get_lock_direct(int lblock, jk_log_context_t *l)
{
    return lb_locking_type[lblock];
}

/* Return the string representation of the lb lock type */
/* based on the lb worker struct */
const char *jk_lb_get_lock(lb_worker_t *p, jk_log_context_t *l)
{
    return lb_locking_type[p->lblock];
}

/* Return the int representation of the lb lock type */
int jk_lb_get_lock_code(const char *v)
{
    if (!v)
        return JK_LB_LOCK_DEF;
    if  (*v == 'o' || *v == 'O' || *v == '0')
        return JK_LB_LOCK_OPTIMISTIC;
    if  (*v == 'p' || *v == 'P' || *v == '1')
        return JK_LB_LOCK_PESSIMISTIC;
    return JK_LB_LOCK_DEF;
}

/* Return the string representation of the lb method type */
/* based on the integer representation */
const char *jk_lb_get_method_direct(int lbmethod, jk_log_context_t *l)
{
    return lb_method_type[lbmethod];
}

/* Return the string representation of the lb method type */
/* based on the lb worker struct */
const char *jk_lb_get_method(lb_worker_t *p, jk_log_context_t *l)
{
    return lb_method_type[p->lbmethod];
}

/* Return the int representation of the lb method type */
int jk_lb_get_method_code(const char *v)
{
    if (!v)
        return JK_LB_METHOD_DEF;
    if  (*v == 'r' || *v == 'R' || *v == '0')
        return JK_LB_METHOD_REQUESTS;
    if  (*v == 't' || *v == 'T' || *v == '1')
        return JK_LB_METHOD_TRAFFIC;
    if  (*v == 'b' || *v == 'B' || *v == '2')
        return JK_LB_METHOD_BUSYNESS;
    if  (*v == 's' || *v == 'S' || *v == '3')
        return JK_LB_METHOD_SESSIONS;
    if  (*v == 'n' || *v == 'N' || *v == '4')
        return JK_LB_METHOD_NEXT;
    return JK_LB_METHOD_DEF;
}

/* Return the string representation of the balance worker state */
/* based on the integer representation */
const char *jk_lb_get_state_direct(int state, jk_log_context_t *l)
{
    return lb_state_type[state];
}

/* Return the string representation of the balance worker state */
/* based on the sub worker struct */
const char *jk_lb_get_state(lb_sub_worker_t *p, jk_log_context_t *l)
{
    return lb_state_type[p->s->state];
}

/* Return the int representation of the lb state */
int jk_lb_get_state_code(const char *v)
{
    if (!v)
        return JK_LB_STATE_DEF;
    if  (*v == 'i' || *v == 'I' || *v == 'n' || *v == 'N' || *v == '0')
        return JK_LB_STATE_IDLE;
    if  (*v == 'o' || *v == 'O' || *v == '1')
        return JK_LB_STATE_OK;
    if  (*v == 'r' || *v == 'R' || *v == '2')
        return JK_LB_STATE_RECOVER;
    if  (*v == 'f' || *v == 'F' || *v == '3')
        return JK_LB_STATE_FORCE;
    if  (*v == 'b' || *v == 'B' || *v == '4')
        return JK_LB_STATE_BUSY;
    if  (*v == 'e' || *v == 'E' || *v == '5')
        return JK_LB_STATE_ERROR;
    if  (*v == 'p' || *v == 'P' || *v == '6')
        return JK_LB_STATE_PROBE;
    return JK_LB_STATE_DEF;
}

/* Return the string representation of the balance worker activation */
/* based on the integer representation */
const char *jk_lb_get_activation_direct(int activation, jk_log_context_t *l)
{
    return lb_activation_type[activation];
}

/* Return the string representation of the balance worker activation */
/* based on the sub worker struct */
const char *jk_lb_get_activation(lb_sub_worker_t *p, jk_log_context_t *l)
{
    return lb_activation_type[p->activation];
}

int jk_lb_get_activation_code(const char *v)
{
    if (!v)
        return JK_LB_ACTIVATION_DEF;
    if (*v == 'a' || *v == 'A' || *v == '0')
        return JK_LB_ACTIVATION_ACTIVE;
    if (*v == 'd' || *v == 'D' || *v == '1')
        return JK_LB_ACTIVATION_DISABLED;
    if (*v == 's' || *v == 'S' || *v == '2')
        return JK_LB_ACTIVATION_STOPPED;
    return JK_LB_ACTIVATION_DEF;
}

/* Update the load multipliers wrt. lb_factor */
void update_mult(lb_worker_t *p, jk_log_context_t *l)
{
    unsigned int i = 0;
    jk_uint64_t s = 1;
    JK_TRACE_ENTER(l);
    for (i = 0; i < p->num_of_workers; i++) {
        s = scm(s, p->lb_workers[i].lb_factor);
    }
    for (i = 0; i < p->num_of_workers; i++) {
        p->lb_workers[i].lb_mult = s / p->lb_workers[i].lb_factor;
        if (JK_IS_DEBUG_LEVEL(l))
            jk_log(l, JK_LOG_DEBUG,
                   "worker %s gets multiplicity %"
                   JK_UINT64_T_FMT,
                   p->lb_workers[i].name,
                   p->lb_workers[i].lb_mult);
    }
    JK_TRACE_EXIT(l);
}

/* Reset all lb values.
 */
void reset_lb_values(lb_worker_t *p, jk_log_context_t *l)
{
    unsigned int i = 0;
    JK_TRACE_ENTER(l);
    if (p->lbmethod != JK_LB_METHOD_BUSYNESS) {
        for (i = 0; i < p->num_of_workers; i++) {
            p->lb_workers[i].s->lb_value = 0;
        }
    }
    JK_TRACE_EXIT(l);
}

static void jk_lb_pull_worker(lb_worker_t *p, int i, jk_log_context_t *l)
{
    lb_sub_worker_t *w = &p->lb_workers[i];
    if (w->sequence < w->s->h.sequence) {
        jk_worker_t *jw = w->worker;
        ajp_worker_t *aw = (ajp_worker_t *)jw->worker_private;

        if (JK_IS_DEBUG_LEVEL(l))
            jk_log(l, JK_LOG_DEBUG,
                   "syncing mem for member '%s' of lb '%s' from shm",
                   w->name, p->name);

        jk_ajp_pull(aw, JK_TRUE, l);
        jk_shm_str_copy(w->route, w->s->route, l);
        jk_shm_str_copy(w->domain, w->s->domain, l);
        jk_shm_str_copy(w->redirect, w->s->redirect, l);
        w->distance = w->s->distance;
        w->activation = w->s->activation;
        w->lb_factor = w->s->lb_factor;
        w->lb_mult = w->s->lb_mult;
        w->sequence = w->s->h.sequence;
    }
}

/* Syncing config values from shm */
void jk_lb_pull(lb_worker_t *p, int locked, jk_log_context_t *l)
{
    unsigned int i = 0;

    JK_TRACE_ENTER(l);

    if (JK_IS_DEBUG_LEVEL(l))
        jk_log(l, JK_LOG_DEBUG,
               "syncing mem for lb '%s' from shm (%u->%u)",
               p->name, p->sequence, p->s->h.sequence);
    if (locked == JK_FALSE)
        jk_shm_lock();
    if (p->sequence == p->s->h.sequence) {
        if (locked == JK_FALSE)
            jk_shm_unlock();
        return;
    }
    p->sticky_session = p->s->sticky_session;
    p->sticky_session_force = p->s->sticky_session_force;
    p->recover_wait_time = p->s->recover_wait_time;
    p->error_escalation_time = p->s->error_escalation_time;
    p->max_reply_timeouts = p->s->max_reply_timeouts;
    p->retries = p->s->retries;
    p->retry_interval = p->s->retry_interval;
    p->lbmethod = p->s->lbmethod;
    p->lblock = p->s->lblock;
    p->max_packet_size = p->s->max_packet_size;

    for (i = 0; i < p->num_of_workers; i++) {
        jk_lb_pull_worker(p, i, l);
    }
    p->sequence = p->s->h.sequence;
    if (locked == JK_FALSE)
        jk_shm_unlock();

    JK_TRACE_EXIT(l);
}

/* Syncing config values to shm */
void jk_lb_push(lb_worker_t *p, int locked, int push_all_members, jk_log_context_t *l)
{
    unsigned int i = 0;

    JK_TRACE_ENTER(l);

    if (JK_IS_DEBUG_LEVEL(l))
        jk_log(l, JK_LOG_DEBUG,
               "syncing shm for lb '%s' from mem (%u->%u)",
               p->name, p->s->h.sequence, p->sequence);
    if (locked == JK_FALSE)
        jk_shm_lock();
    p->s->sticky_session = p->sticky_session;
    p->s->sticky_session_force = p->sticky_session_force;
    p->s->recover_wait_time = p->recover_wait_time;
    p->s->error_escalation_time = p->error_escalation_time;
    p->s->max_reply_timeouts = p->max_reply_timeouts;
    p->s->retries = p->retries;
    p->s->retry_interval = p->retry_interval;
    p->s->lbmethod = p->lbmethod;
    p->s->lblock = p->lblock;
    p->s->max_packet_size = p->max_packet_size;

    for (i = 0; i < p->num_of_workers; i++) {
        lb_sub_worker_t *w = &p->lb_workers[i];
        if (push_all_members == JK_TRUE || w->sequence != w->s->h.sequence) {
            jk_worker_t *jw = w->worker;
            ajp_worker_t *aw = (ajp_worker_t *)jw->worker_private;

            if (JK_IS_DEBUG_LEVEL(l))
                jk_log(l, JK_LOG_DEBUG,
                       "syncing shm for member '%s' of lb '%s' from mem",
                       w->name, p->name);

            jk_ajp_push(aw, JK_TRUE, l);
            jk_shm_str_copy(w->s->route, w->route, l);
            jk_shm_str_copy(w->s->domain, w->domain, l);
            jk_shm_str_copy(w->s->redirect, w->redirect, l);
            w->s->distance = w->distance;
            w->s->activation = w->activation;
            w->s->lb_factor = w->lb_factor;
            w->s->lb_mult = w->lb_mult;
            w->s->h.sequence++;
            w->sequence = w->s->h.sequence;
        }
    }
    /* Increment the shared memory sequence number.
     * Our number can be behind the actual value in
     * shared memory.
     */
    p->s->h.sequence++;
    p->sequence = p->s->h.sequence;
    if (locked == JK_FALSE)
        jk_shm_unlock();

    JK_TRACE_EXIT(l);
}

/* Retrieve the parameter with the given name                                */
static char *get_path_param(jk_ws_service_t *s, const char *name)
{
    char *id_start = NULL;
    for (id_start = strstr(s->req_uri, name); id_start; id_start = strstr(id_start + 1, name)) {
        if (id_start[strlen(name)] == '=') {
            /*
             * Session path-cookie was found, get it's value
             */
            id_start += (1 + strlen(name));
            if (strlen(id_start)) {
                char *id_end;
                id_start = jk_pool_strdup(s->pool, id_start);
                /*
                 * The query string is not part of req_uri, however
                 * to be on the safe side lets remove the trailing query
                 * string if appended...
                 */
                if ((id_end = strchr(id_start, '?')) != NULL) {
                    *id_end = '\0';
                }
                /*
                 * Remove any trailing path element.
                 */
                if ((id_end = strchr(id_start, ';')) != NULL) {
                    *id_end = '\0';
                }
                /*
                 * Remove any trailing URI segments.
                 */
                if ((id_end = strchr(id_start, '/')) != NULL) {
                    *id_end = '\0';
                }
                return id_start;
            }
        }
    }

    return NULL;
}

/* Retrieve the cookie with the given name                                   */
static char *get_cookie(jk_ws_service_t *s, const char *name)
{
    unsigned i;
    char *result = NULL;

    for (i = 0; i < s->num_headers; i++) {
        if (strcasecmp(s->headers_names[i], "cookie") == 0) {

            char *id_start;
            for (id_start = strstr(s->headers_values[i], name);
                 id_start; id_start = strstr(id_start + 1, name)) {
                if (id_start == s->headers_values[i] ||
                    id_start[-1] == ';' ||
                    id_start[-1] == ',' || jk_isspace(id_start[-1])) {
                    id_start += strlen(name);
                    while (*id_start && jk_isspace(*id_start))
                        ++id_start;
                    if (*id_start == '=' && id_start[1]) {
                        /*
                         * Session cookie was found, get it's value
                         */
                        char *id_end;
                        size_t sz;
                        ++id_start;
                        if ((id_end = strpbrk(id_start, ";,")) != NULL)
                            sz = id_end - id_start;
                        else {
                            sz = strlen(id_start);
                            id_end = id_start + sz;
                        }
                        /* Chop off surrounding '"' (quoted cookie) */
                        if (sz > 1 && *id_start == '"' && *(id_start + sz - 1) == '"') {
                            id_start++;
                            sz -= 2;
                        }
                        if (result == NULL) {
                            result = jk_pool_alloc(s->pool, sz + 1);
                            memcpy(result, id_start, sz);
                            result[sz] = '\0';
                        }
                        else {
                            size_t osz = strlen(result) + 1;
                            result =
                                jk_pool_realloc(s->pool, osz + sz + 1, result, osz);
                            strcat(result, ";");
                            strncat(result, id_start, sz);
                        }
                        id_start = id_end;
                    }
                }
            }
        }
    }

    return result;
}


/* Retrieve session id from the cookie or the parameter
 * (parameter first)
 */
static char *get_sessionid(jk_ws_service_t *s, lb_worker_t *p, jk_log_context_t *l)
{
    char *val;
    char *session_path;
    char *session_cookie;

    /* If the web server sets a route, ignore the real session id
     * and fake a new one for that route.
     */
    if (s->route) {
        size_t sz = strlen(s->route) + 1;
        val = jk_pool_alloc(s->pool, sz + 1);
        val[0] = '.';
        memcpy(val + 1, s->route, sz);
        return val;
    }

    /* set session_path */
    session_path = (s->extension.session_path) ?
            s->extension.session_path : p->session_path;
    /* set session_cookie */
    session_cookie = (s->extension.session_cookie) ?
            s->extension.session_cookie : p->session_cookie;

    val = get_path_param(s, session_path);
    if (!val) {
        val = get_cookie(s, session_cookie);
    }
    if (val && !*val) {
        /* TODO: For now only log the empty sessions.
         *       However we should probably return 400
         *       (BAD_REQUEST) in this case
         */
        jk_log(l, JK_LOG_INFO,
               "Detected empty session identifier.");
        return NULL;
    }
    return val;
}

static void close_workers(lb_worker_t *p, int num_of_workers, jk_log_context_t *l)
{
    int i = 0;
    for (i = 0; i < num_of_workers; i++) {
        p->lb_workers[i].worker->destroy(&(p->lb_workers[i].worker), l);
    }
}

/* If the worker is in error state run
 * retry on that worker. It will be marked as
 * operational if the retry timeout is elapsed.
 * The worker might still be unusable, but we try
 * anyway.
 * If the worker is in ok state and got no requests
 * since the last global maintenance, we mark its
 * state as not available.
 * Return the number of workers not in error state.
 */
static int recover_workers(lb_worker_t *p,
                           jk_uint64_t curmax,
                           time_t now,
                           jk_log_context_t *l)
{
    unsigned int i;
    int non_error = 0;
    int elapsed;
    lb_sub_worker_t *w = NULL;
    ajp_worker_t *aw = NULL;
    JK_TRACE_ENTER(l);

    if (p->sequence < p->s->h.sequence)
        jk_lb_pull(p, JK_TRUE, l);
    for (i = 0; i < p->num_of_workers; i++) {
        w = &p->lb_workers[i];
        aw = (ajp_worker_t *)w->worker->worker_private;
        if (w->s->state == JK_LB_STATE_ERROR) {
            elapsed = (int)difftime(now, w->s->last_error_time);
            if (elapsed <= p->recover_wait_time) {
                if (JK_IS_DEBUG_LEVEL(l))
                    jk_log(l, JK_LOG_DEBUG,
                           "worker %s will recover in %d seconds",
                           w->name, p->recover_wait_time - elapsed);
            }
            else {
                if (JK_IS_DEBUG_LEVEL(l))
                    jk_log(l, JK_LOG_DEBUG,
                           "worker %s is marked for recovery",
                           w->name);
                if (p->lbmethod != JK_LB_METHOD_BUSYNESS)
                    w->s->lb_value = curmax;
                aw->s->reply_timeouts = 0;
                w->s->state = JK_LB_STATE_RECOVER;
                non_error++;
            }
        }
        else if (w->s->first_error_time > 0 &&
                 (int)difftime(now, w->s->first_error_time) >= p->error_escalation_time &&
                 w->s->state != JK_LB_STATE_RECOVER) {
            if (JK_IS_DEBUG_LEVEL(l))
                jk_log(l, JK_LOG_DEBUG,
                       "worker %s escalating local error to global error",
                       w->name);
            w->s->state = JK_LB_STATE_ERROR;
        }
        else {
            non_error++;
            if (w->s->state == JK_LB_STATE_OK &&
                aw->s->used == w->s->elected_snapshot)
                w->s->state = JK_LB_STATE_IDLE;
        }
        w->s->elected_snapshot = aw->s->used;
    }

    JK_TRACE_EXIT(l);
    return non_error;
}

static int force_recovery(lb_worker_t *p,
                          int *states,
                          jk_log_context_t *l)
{
    unsigned int i;
    int forced = 0;
    lb_sub_worker_t *w = NULL;
    ajp_worker_t *aw = NULL;
    JK_TRACE_ENTER(l);

    for (i = 0; i < p->num_of_workers; i++) {
        w = &p->lb_workers[i];
        if (w->s->state == JK_LB_STATE_ERROR) {
            if (JK_IS_DEBUG_LEVEL(l))
                jk_log(l, JK_LOG_INFO,
                       "worker %s is marked for forced recovery",
                       w->name);
            aw = (ajp_worker_t *)w->worker->worker_private;
            aw->s->reply_timeouts = 0;
            w->s->state = JK_LB_STATE_FORCE;
            if (states != NULL)
                states[i] = JK_LB_STATE_FORCE;
            forced++;
        }
    }

    JK_TRACE_EXIT(l);
    return forced;
}

/* Divide old load values by the decay factor,
 * such that older values get less important
 * for the routing decisions.
 */
static jk_uint64_t decay_load(lb_worker_t *p,
                              time_t exponent,
                              jk_log_context_t *l)
{
    unsigned int i;
    jk_uint64_t curmax = 0;
    jk_uint64_t curmin = 0;
    int no_curmin = JK_TRUE;
    lb_sub_worker_t *w;
    ajp_worker_t *aw;

    JK_TRACE_ENTER(l);
    for (i = 0; i < p->num_of_workers; i++) {
        w = &p->lb_workers[i];
        if (p->lbmethod != JK_LB_METHOD_BUSYNESS) {
            if (p->lbmethod != JK_LB_METHOD_NEXT) {
                w->s->lb_value >>= exponent;
            }
            if (w->s->lb_value > curmax) {
                curmax = w->s->lb_value;
            }
        }
        aw = (ajp_worker_t *)w->worker->worker_private;
        aw->s->reply_timeouts >>= exponent;
    }
    if (p->lbmethod == JK_LB_METHOD_NEXT) {
        for (i = 0; i < p->num_of_workers; i++) {
            w = &p->lb_workers[i];
            /* Take into account only the workers that are
             * not in error state, stopped, disabled or busy.
             * Unfortunately we can not respect activations
             * defined by mapping rules here.
             */
            if (JK_WORKER_USABLE(w->s->state, w->activation)) {
                if (w->s->lb_value < curmin || no_curmin == JK_TRUE) {
                    no_curmin = JK_FALSE;
                    curmin = w->s->lb_value;
                }
            }
        }
        for (i = 0; i < p->num_of_workers; i++) {
            w = &p->lb_workers[i];
            if (w->s->lb_value >= curmin)
                w->s->lb_value -= curmin;
            else
                w->s->lb_value = 0;
        }
    }
    JK_TRACE_EXIT(l);
    return curmax;
}

static int JK_METHOD maintain_workers(jk_worker_t *p, time_t now, int global, jk_log_context_t *l)
{
    unsigned int i = 0;
    jk_uint64_t curmax = 0;

    JK_TRACE_ENTER(l);
    if (p && p->worker_private) {
        lb_worker_t *lb = (lb_worker_t *)p->worker_private;

        /* Now we check for global maintenance (once for all processes).
         * Checking workers for recovery and applying decay to the
         * load values should not be done by each process individually.
         * Therefore we globally sync and we use a global timestamp.
         * Since it's possible that we come here a few milliseconds
         * before the interval has passed, we allow a little tolerance.
         */
        if (global == JK_TRUE) {
            time_t exponent = JK_LB_DECAY_MULT * ((long)difftime(now, lb->s->last_maintain_time)) / lb->maintain_time;
            lb->s->last_maintain_time = now;

            if (JK_IS_DEBUG_LEVEL(l))
                jk_log(l, JK_LOG_DEBUG,
                       "decay with 2^%d",
                       exponent);

            jk_shm_lock();

            curmax = decay_load(lb, exponent, l);

            if (!recover_workers(lb, curmax, now, l)) {
                force_recovery(lb, NULL, l);
            }

            /*
             * Checking workers for idleness.
             */
            for (i = 0; i < lb->num_of_workers; i++) {
                ajp_worker_t *aw = lb->lb_workers[i].worker->worker_private;
                if (aw->s->state == JK_AJP_STATE_OK &&
                    aw->s->used == aw->s->used_snapshot)
                    aw->s->state = JK_AJP_STATE_IDLE;
                aw->s->used_snapshot = aw->s->used;
            }

            jk_shm_unlock();
        }

        for (i = 0; i < lb->num_of_workers; i++) {
            if (lb->lb_workers[i].worker->maintain) {
                lb->lb_workers[i].worker->maintain(lb->lb_workers[i].worker, now, global, l);
            }
        }

    }
    else {
        JK_LOG_NULL_PARAMS(l);
    }

    JK_TRACE_EXIT(l);
    return JK_TRUE;
}

static int JK_METHOD shutdown_workers(jk_worker_t *p, jk_log_context_t *l)
{
    unsigned int i = 0;

    JK_TRACE_ENTER(l);
    if (p && p->worker_private) {
        lb_worker_t *lb = (lb_worker_t *)p->worker_private;

        for (i = 0; i < lb->num_of_workers; i++) {
            if (lb->lb_workers[i].worker->shutdown) {
                lb->lb_workers[i].worker->shutdown(lb->lb_workers[i].worker, l);
            }
        }

    }
    else {
        JK_LOG_NULL_PARAMS(l);
    }

    JK_TRACE_EXIT(l);
    return JK_TRUE;
}

static int find_by_session(jk_ws_service_t *s,
                           lb_worker_t *p,
                           const char *session_route,
                           jk_log_context_t *l)
{

    int rc = -1;
    unsigned int i;

    for (i = 0; i < p->num_of_workers; i++) {
        if (strcmp(p->lb_workers[i].route, session_route) == 0) {
            rc = i;
            break;
        }
    }
    return rc;
}

static int find_best_bydomain(jk_ws_service_t *s,
                              lb_worker_t *p,
                              const char *route_or_domain,
                              int *states,
                              jk_log_context_t *l)
{
    unsigned int i;
    int d = 0;
    jk_uint64_t curmin = 0;
    int candidate = -1;
    int activation;
    lb_sub_worker_t wr;
    char *idpart = strchr(route_or_domain, '.');
    size_t domain_len = 0;

    if (idpart) {
        domain_len = idpart - route_or_domain;
    }
    else {
        domain_len = strlen(route_or_domain);
    }
    /* First try to see if we have available candidate */
    for (i = 0; i < p->num_of_workers; i++) {
        /* Skip all workers that are not member of domain */
        wr = p->lb_workers[i];
        if (strlen(wr.domain) == 0 ||
            strlen(wr.domain) != domain_len ||
            strncmp(wr.domain, route_or_domain, domain_len))
            continue;
        /* Take into account only the workers that are
         * not in error state, stopped, disabled or busy.
         */
        activation = s->extension.activation ?
                     s->extension.activation[i] :
                     JK_LB_ACTIVATION_UNSET;
        if (activation == JK_LB_ACTIVATION_UNSET)
            activation = wr.activation;
        if (JK_WORKER_USABLE(states[wr.i], activation)) {
            if (candidate < 0 || wr.distance < d ||
                (wr.s->lb_value < curmin &&
                wr.distance == d)) {
                candidate = i;
                curmin = wr.s->lb_value;
                d = wr.distance;
            }
        }
    }
    return candidate;
}


static int find_best_byvalue(jk_ws_service_t *s,
                             lb_worker_t *p,
                             int *states,
                             jk_log_context_t *l)
{
    unsigned int i;
    unsigned int j;
    unsigned int offset;
    int d = 0;
    jk_uint64_t curmin = 0;

    /* find the least busy worker */
    int candidate = -1;
    int activation;
    lb_sub_worker_t wr;

    offset = p->next_offset;

    /* First try to see if we have available candidate */
    for (j = offset; j < p->num_of_workers + offset; j++) {
        i = j % p->num_of_workers;
        wr = p->lb_workers[i];
        activation = s->extension.activation ?
                     s->extension.activation[i] :
                     JK_LB_ACTIVATION_UNSET;
        if (activation == JK_LB_ACTIVATION_UNSET)
            activation = wr.activation;

        /* Take into account only the workers that are
         * not in error state, stopped, disabled or busy.
         */
        if (JK_WORKER_USABLE(states[wr.i], activation)) {
            if (candidate < 0 || wr.distance < d ||
                (s->extension.stateless != JK_TRUE &&
                 wr.s->lb_value < curmin &&
                 wr.distance == d)) {
                candidate = i;
                curmin = wr.s->lb_value;
                d = wr.distance;
                p->next_offset = i + 1;
            }
        }
    }
    return candidate;
}

static int find_bysession_route(jk_ws_service_t *s,
                                lb_worker_t *p,
                                const char *session_route,
                                int *states,
                                jk_log_context_t *l)
{
    int uses_domain  = 0;
    int candidate = -1;

    candidate = find_by_session(s, p, session_route, l);
    if (candidate < 0) {
        uses_domain = 1;
        candidate = find_best_bydomain(s, p, session_route, states, l);
    }
    else {
        s->sticky = JK_TRUE;
    }
    if (candidate >= 0) {
        lb_sub_worker_t wr = p->lb_workers[candidate];
        int activation;
        if (uses_domain)
            s->route = wr.domain;
        activation = s->extension.activation ?
                     s->extension.activation[candidate] :
                     JK_LB_ACTIVATION_UNSET;
        if (activation == JK_LB_ACTIVATION_UNSET)
            activation = wr.activation;
        if (!JK_WORKER_USABLE_STICKY(states[wr.i], activation)) {
            /* We have a worker that is error state or stopped.
             * If it has a redirection set use that redirection worker.
             * This enables to safely remove the member from the
             * balancer. Of course you will need a some kind of
             * session replication between those two remote.
             */
            s->sticky = JK_FALSE;
            if (p->sticky_session_force)
                candidate = -1;
            else if (*wr.redirect) {
                candidate = find_by_session(s, p, wr.redirect, l);
                s->route = NULL;
            }
            else if (*wr.domain && !uses_domain) {
                candidate = find_best_bydomain(s, p, wr.domain, states, l);
                if (candidate >= 0) {
                    s->route = wr.domain;
                }
                else {
                    s->route = NULL;
                }
            }
            if (candidate >= 0) {
                wr = p->lb_workers[candidate];
                activation = s->extension.activation ?
                             s->extension.activation[candidate] :
                             JK_LB_ACTIVATION_UNSET;
                if (activation == JK_LB_ACTIVATION_UNSET)
                    activation = wr.activation;
                if (!JK_WORKER_USABLE_STICKY(states[wr.i], activation))
                    candidate = -1;
            }
        }
    }
    return candidate;
}

static int find_failover_worker(jk_ws_service_t *s,
                                lb_worker_t *p,
                                int *states,
                                jk_log_context_t *l)
{
    int rc = -1;
    unsigned int i;
    char *redirect = NULL;

    for (i = 0; i < p->num_of_workers; i++) {
        if (strlen(p->lb_workers[i].redirect)) {
            redirect = p->lb_workers[i].redirect;
            break;
        }
    }
    if (redirect) {
        rc = find_bysession_route(s, p, redirect, states, l);
        s->sticky = JK_FALSE;
    }
    return rc;
}

static int find_best_worker(jk_ws_service_t *s,
                            lb_worker_t *p,
                            int *states,
                            jk_log_context_t *l)
{
    int rc = -1;

    rc = find_best_byvalue(s, p, states, l);
    /* By default use worker route as session route */
    if (rc < 0)
        rc = find_failover_worker(s, p, states, l);
    return rc;
}

static int get_most_suitable_worker(jk_ws_service_t *s,
                                    lb_worker_t *p,
                                    char *sessionid,
                                    int *states,
                                    jk_log_context_t *l)
{
    int rc = -1;

    JK_TRACE_ENTER(l);
    s->sticky = JK_FALSE;
    if (p->num_of_workers == 1) {
        /* No need to find the best worker
         * if there is a single one
         */
        int activation = s->extension.activation ?
                         s->extension.activation[0] :
                         JK_LB_ACTIVATION_UNSET;
        if (activation == JK_LB_ACTIVATION_UNSET)
            activation = p->lb_workers[0].activation;
        if (JK_WORKER_USABLE_STICKY(states[0], activation)) {
            if (activation != JK_LB_ACTIVATION_DISABLED) {
                s->sticky = JK_TRUE;
                JK_TRACE_EXIT(l);
                return 0;
            }
        }
        else {
            JK_TRACE_EXIT(l);
            return -1;
        }
    }
    if (p->lblock == JK_LB_LOCK_PESSIMISTIC) {
        if (!jk_shm_lock()) {
            jk_log(l, JK_LOG_ERROR, "locking failed (errno=%d)", errno);
            JK_TRACE_EXIT(l);
            return -1;
        }
    }
    else {
        JK_ENTER_CS(&p->cs);
    }
    if (sessionid) {
        char *session = sessionid;
        while (sessionid) {
            char *next = strchr(sessionid, ';');
            char *session_route = NULL;
            if (next)
               *next++ = '\0';
            if (JK_IS_DEBUG_LEVEL(l))
                jk_log(l, JK_LOG_DEBUG,
                       "searching worker for partial sessionid %s",
                       sessionid);
            session_route = strchr(sessionid, '.');
            if (session_route) {
                ++session_route;

                if (JK_IS_DEBUG_LEVEL(l))
                    jk_log(l, JK_LOG_DEBUG,
                           "searching worker for session route %s",
                           session_route);

                /* We have a session route. Whow! */
                rc = find_bysession_route(s, p, session_route, states, l);
                if (rc >= 0) {
                    lb_sub_worker_t *wr = &(p->lb_workers[rc]);
                    if (p->lblock == JK_LB_LOCK_PESSIMISTIC) {
                        jk_shm_unlock();
                    }
                    else {
                        JK_LEAVE_CS(&p->cs);
                    }
                    if (JK_IS_DEBUG_LEVEL(l))
                        jk_log(l, JK_LOG_DEBUG,
                               "found worker %s (%s) for route %s and partial sessionid %s",
                               wr->name, wr->route, session_route, sessionid);
                    JK_TRACE_EXIT(l);
                    return rc;
                }
            }
            /* Try next partial sessionid if present */
            sessionid = next;
            rc = -1;
        }
        if (rc < 0 && p->sticky_session_force) {
            if (p->lblock == JK_LB_LOCK_PESSIMISTIC) {
                jk_shm_unlock();
            }
            else {
                JK_LEAVE_CS(&p->cs);
            }
            jk_log(l, JK_LOG_INFO,
                   "all workers are in error state for session %s",
                   session);
            JK_TRACE_EXIT(l);
            return -1;
        }
    }
    rc = find_best_worker(s, p, states, l);
    if (p->lblock == JK_LB_LOCK_PESSIMISTIC) {
        jk_shm_unlock();
    }
    else {
        JK_LEAVE_CS(&p->cs);
    }
    if (rc >= 0) {
        lb_sub_worker_t *wr = &(p->lb_workers[rc]);
        if (JK_IS_DEBUG_LEVEL(l))
            jk_log(l, JK_LOG_DEBUG,
                   "found best worker %s (%s) using method '%s'",
                   wr->name, wr->route, jk_lb_get_method(p, l));
        JK_TRACE_EXIT(l);
        return rc;
    }
    JK_TRACE_EXIT(l);
    return -1;
}

static void lb_add_log_items(jk_ws_service_t *s,
                             const char *const *log_names,
                             lb_sub_worker_t *w,
                             jk_log_context_t *l)
{
    /* ADJUST JK_LB_NOTES_COUNT WHEN ADDING MORE NOTES HERE! */
    ajp_worker_t *aw = (ajp_worker_t *)w->worker->worker_private;
    const char **log_values = jk_pool_alloc(s->pool, sizeof(char *) * JK_LB_NOTES_COUNT);
    char *buf = jk_pool_alloc(s->pool, sizeof(char *) * JK_LB_NOTES_COUNT * JK_LB_UINT64_STR_SZ);
    if (log_values && buf) {
        /* JK_NOTE_LB_FIRST/LAST_NAME */
        log_values[0] = w->name;
        snprintf(buf, JK_LB_UINT64_STR_SZ, "%" JK_UINT64_T_FMT, w->s->lb_value);
        /* JK_NOTE_LB_FIRST/LAST_VALUE */
        log_values[1] = buf;
        buf += JK_LB_UINT64_STR_SZ;
        snprintf(buf, JK_LB_UINT64_STR_SZ, "%" JK_UINT64_T_FMT, aw->s->used);
        /* JK_NOTE_LB_FIRST/LAST_ACCESSED */
        log_values[2] = buf;
        buf += JK_LB_UINT64_STR_SZ;
        snprintf(buf, JK_LB_UINT64_STR_SZ, "%" JK_UINT64_T_FMT, w->s->sessions);
        /* JK_NOTE_LB_FIRST/LAST_SESSIONS */
        log_values[3] = buf;
        buf += JK_LB_UINT64_STR_SZ;
        snprintf(buf, JK_LB_UINT64_STR_SZ, "%" JK_UINT64_T_FMT, aw->s->readed);
        /* JK_NOTE_LB_FIRST/LAST_READ */
        log_values[4] = buf;
        buf += JK_LB_UINT64_STR_SZ;
        snprintf(buf, JK_LB_UINT64_STR_SZ, "%" JK_UINT64_T_FMT, aw->s->transferred);
        /* JK_NOTE_LB_FIRST/LAST_TRANSFERRED */
        log_values[5] = buf;
        buf += JK_LB_UINT64_STR_SZ;
        snprintf(buf, JK_LB_UINT64_STR_SZ, "%" JK_UINT32_T_FMT, w->s->errors);
        /* JK_NOTE_LB_FIRST/LAST_ERRORS */
        log_values[6] = buf;
        buf += JK_LB_UINT64_STR_SZ;
        snprintf(buf, JK_LB_UINT64_STR_SZ, "%d", aw->s->busy);
        /* JK_NOTE_LB_FIRST/LAST_BUSY */
        log_values[7] = buf;
        /* JK_NOTE_LB_FIRST/LAST_ACTIVATION */
        log_values[8] = jk_lb_get_activation(w, l);
        /* JK_NOTE_LB_FIRST/LAST_STATE */
        log_values[9] = jk_lb_get_state(w, l);
        s->add_log_items(s, log_names, log_values, JK_LB_NOTES_COUNT);
    }
}

static int JK_METHOD service(jk_endpoint_t *e,
                             jk_ws_service_t *s,
                             jk_log_context_t *l, int *is_error)
{
    lb_endpoint_t *p;
    int attempt = 0;
    lb_sub_worker_t *prec = NULL;
    int num_of_workers;
    int first = 1;
    int was_forced = 0;
    int recoverable = JK_TRUE;
    int rc = JK_UNSET;
    char *sessionid = NULL;
    int i;
    int retry = 0;

    JK_TRACE_ENTER(l);

    if (!e || !e->endpoint_private || !s || !is_error) {
        JK_LOG_NULL_PARAMS(l);
        if (is_error)
            *is_error = JK_HTTP_SERVER_ERROR;
        JK_TRACE_EXIT(l);
        return JK_FALSE;
    }

    p = e->endpoint_private;
    num_of_workers = p->worker->num_of_workers;

    /* Set returned error to OK */
    *is_error = JK_HTTP_OK;

    if (p->worker->sequence < p->worker->s->h.sequence)
        jk_lb_pull(p->worker, JK_FALSE, l);
    for (i = 0; i < num_of_workers; i++) {
        lb_sub_worker_t *rec;
        ajp_worker_t *aw;
        jk_log(l, JK_LOG_DEBUG, "LB - num_of_workers: %d, retry: %d, lb_retries: %d", num_of_workers, i, p->worker->lb_retries);
        rec = &(p->worker->lb_workers[i]);
        aw = (ajp_worker_t *)rec->worker->worker_private;
        if (rec->s->state == JK_LB_STATE_BUSY) {
            if ((aw->busy_limit <= 0 || aw->s->busy < aw->busy_limit) &&
                ajp_has_endpoint(rec->worker, l)) {
                if (JK_IS_DEBUG_LEVEL(l))
                    jk_log(l, JK_LOG_DEBUG,
                           "worker %s busy state ended",
                           rec->name);
                rec->s->state = JK_LB_STATE_OK;
            }
        }
        /* Copy the shared state info */
        p->states[i] = rec->s->state;
    }

    /* set the recovery post, for LB mode */
    s->reco_buf = jk_b_new(s->pool);
    if (!s->reco_buf) {
        *is_error = JK_HTTP_SERVER_ERROR;
        jk_log(l, JK_LOG_ERROR,
               "Failed allocating AJP message");
        JK_TRACE_EXIT(l);
        return JK_SERVER_ERROR;
    }
    if (jk_b_set_buffer_size(s->reco_buf, p->worker->max_packet_size)) {
        *is_error = JK_HTTP_SERVER_ERROR;
        jk_log(l, JK_LOG_ERROR,
               "Failed allocating AJP message buffer of %d bytes.", p->worker->max_packet_size);
        JK_TRACE_EXIT(l);
        return JK_SERVER_ERROR;
    }
    jk_b_reset(s->reco_buf);
    s->reco_status = RECO_INITED;

    if (p->worker->sticky_session && s->extension.sticky_ignore != JK_TRUE) {
        /* Use sessionid only if sticky_session is
         * defined and not overwritten for this load balancer
         */
        sessionid = get_sessionid(s, p->worker, l);
    }
    if (JK_IS_DEBUG_LEVEL(l))
        jk_log(l, JK_LOG_DEBUG,
               "service sticky_session=%d id='%s'",
               p->worker->sticky_session, sessionid ? sessionid : "empty");

    while (recoverable == JK_TRUE) {
        if (JK_IS_DEBUG_LEVEL(l))
            jk_log(l, JK_LOG_DEBUG, "attempt %d, max attempts %d, worker count %d",
                   attempt, p->worker->lb_retries, num_of_workers);
        if (attempt >= num_of_workers || attempt >= p->worker->lb_retries) {
            retry++;
            if (retry >= p->worker->retries) {
                /* Done with retrying */
                break;
            }
            if (JK_IS_DEBUG_LEVEL(l))
                jk_log(l, JK_LOG_DEBUG,
                       "retry %d, sleeping for %d ms before retrying",
                       retry, p->worker->retry_interval);
            jk_sleep(p->worker->retry_interval);
            /* Pull shared memory if something changed during sleep */
            if (p->worker->sequence < p->worker->s->h.sequence)
                jk_lb_pull(p->worker, JK_FALSE, l);
            for (i = 0; i < num_of_workers; i++) {
                /* Copy the shared state info */
                p->states[i] = p->worker->lb_workers[i].s->state;
            }
            attempt = 0;
        }
        rc = JK_FALSE;
        *is_error = JK_HTTP_SERVER_BUSY;
        i = get_most_suitable_worker(s, p->worker, sessionid, p->states, l);
        if (i >= 0) {
            int r;
            int is_service_error = JK_HTTP_OK;
            lb_sub_worker_t *rec = &(p->worker->lb_workers[i]);
            ajp_worker_t *aw = (ajp_worker_t *)rec->worker->worker_private;
            jk_endpoint_t *end = NULL;
            int activation = s->extension.activation ?
                             s->extension.activation[i] :
                             JK_LB_ACTIVATION_UNSET;
            if (activation == JK_LB_ACTIVATION_UNSET)
                activation = rec->activation;
            if (!s->route)
                s->route = rec->route;
            s->activation = jk_lb_get_activation_direct(activation, l);
            prec = rec;

            if (JK_IS_DEBUG_LEVEL(l))
                jk_log(l, JK_LOG_DEBUG,
                       "service worker=%s route=%s failover=%s",
                       rec->name, s->route, s->sticky ? "false" : "true");

            if (p->worker->lblock == JK_LB_LOCK_PESSIMISTIC)
                jk_shm_lock();
            if (rec->s->state == JK_LB_STATE_RECOVER) {
                rec->s->state  = JK_LB_STATE_PROBE;
                p->states[rec->i] = JK_LB_STATE_PROBE;
            }
            if (p->worker->lblock == JK_LB_LOCK_PESSIMISTIC)
                jk_shm_unlock();

            r = rec->worker->get_endpoint(rec->worker, &end, l);
            if (!r || !end) {
                /* If we can not get the endpoint
                 * mark the worker as busy rather then
                 * as in error if the retry number is
                 * greater then the number of retries.
                 */
                if (p->worker->lblock == JK_LB_LOCK_PESSIMISTIC)
                    jk_shm_lock();
                if (rec->s->state != JK_LB_STATE_ERROR) {
                    rec->s->state  = JK_LB_STATE_BUSY;
                    p->states[rec->i] = JK_LB_STATE_BUSY;
                }
                if (p->worker->lblock == JK_LB_LOCK_PESSIMISTIC)
                    jk_shm_unlock();
                jk_log(l, JK_LOG_INFO,
                       "could not get free endpoint for worker %s (%d retries)",
                       rec->name, retry);
            }
            else {
                int service_stat = JK_UNSET;
                jk_uint64_t rd = 0;
                jk_uint64_t wr = 0;
                int busy;
                if (p->worker->lblock == JK_LB_LOCK_PESSIMISTIC)
                    jk_shm_lock();

                /* Increment the number of workers serving request */
                busy = JK_ATOMIC_INCREMENT(&(p->worker->s->busy));
                if (busy > p->worker->s->max_busy)
                    p->worker->s->max_busy = busy;
                if (p->worker->lbmethod == JK_LB_METHOD_REQUESTS ||
                    p->worker->lbmethod == JK_LB_METHOD_BUSYNESS ||
                    (!sessionid &&
                     s->extension.stateless != JK_TRUE &&
                     (p->worker->lbmethod == JK_LB_METHOD_SESSIONS ||
                      p->worker->lbmethod == JK_LB_METHOD_NEXT)))
                    rec->s->lb_value += rec->lb_mult;
                if (!sessionid && s->extension.stateless != JK_TRUE) {
                    rec->s->sessions++;
                }
                if (p->worker->lblock == JK_LB_LOCK_PESSIMISTIC)
                    jk_shm_unlock();

                if (!s->sticky && (s->extension.set_session_cookie || p->worker->set_session_cookie)) {
                    char **old_names = s->resp_headers_names;
                    char **old_values = s->resp_headers_values;
                    s->resp_headers_names  = jk_pool_alloc(s->pool,
                                                      (s->num_resp_headers + 1) * sizeof(char *));
                    s->resp_headers_values = jk_pool_alloc(s->pool,
                                                      (s->num_resp_headers + 1) * sizeof(char *));
                    if (!s->resp_headers_names || !s->resp_headers_values) {
                        jk_log(l, JK_LOG_ERROR,
                               "Failed allocating %d new response headers.", s->num_resp_headers + 1);
                        s->resp_headers_names = old_names;
                        s->resp_headers_values = old_values;
                    } else if (s->num_resp_headers) {
                        memcpy(s->resp_headers_names, old_names, s->num_resp_headers * sizeof(char *));
                        memcpy(s->resp_headers_values, old_values, s->num_resp_headers * sizeof(char *));
                    }
                    s->resp_headers_names[s->num_resp_headers] = "Set-Cookie";
                    s->resp_headers_values[s->num_resp_headers] = jk_pool_strcatv(s->pool, p->worker->session_cookie,
                                                                                  "=.", rec->route,
                                                                                  NULL);
                    if (p->worker->session_cookie_path && *p->worker->session_cookie_path) {
                        s->resp_headers_values[s->num_resp_headers] = jk_pool_strcatv(s->pool, s->resp_headers_values[s->num_resp_headers],
                                                                                      ";PATH=", p->worker->session_cookie_path,
                                                                                      NULL);
                    }
                    s->resp_headers_values[s->num_resp_headers] = jk_pool_strcatv(s->pool, s->resp_headers_values[s->num_resp_headers],
                                                                                  ";HttpOnly",
                                                                                  (s->is_ssl ? ";Secure" : ""),
                                                                                  NULL);
                    if (JK_IS_DEBUG_LEVEL(l))
                        jk_log(l, JK_LOG_DEBUG, "Added cookie header '%s' with value '%s' ",
                               s->resp_headers_names[s->num_resp_headers],
                               s->resp_headers_values[s->num_resp_headers]);
                    s->num_resp_headers++;
                }
                service_stat = end->service(end, s, l, &is_service_error);
                rd = end->rd;
                wr = end->wr;
                recoverable = end->recoverable;
                *is_error = is_service_error;
                end->done(&end, l);

                if (p->worker->lblock == JK_LB_LOCK_PESSIMISTIC)
                    jk_shm_lock();

                /* Update partial reads and writes if any */
                if (p->worker->lbmethod == JK_LB_METHOD_TRAFFIC) {
                    rec->s->lb_value += (rd+wr)*rec->lb_mult;
                }
                else if (p->worker->lbmethod == JK_LB_METHOD_BUSYNESS) {
                    if (rec->s->lb_value >= rec->lb_mult) {
                        rec->s->lb_value -= rec->lb_mult;
                    }
                    else {
                        rec->s->lb_value = 0;
                        if (JK_IS_DEBUG_LEVEL(l)) {
                            jk_log(l, JK_LOG_DEBUG,
                                   "worker %s has load value to low (%"
                                   JK_UINT64_T_FMT
                                   " < %"
                                   JK_UINT64_T_FMT
                                   ") ",
                                   "- correcting to 0",
                                   rec->name,
                                   rec->s->lb_value,
                                   rec->lb_mult);
                        }
                    }
                }

                /* When have an endpoint and we ran a request, assume
                 * we are OK, unless we last were in error.
                 * We will below explicitely set OK or ERROR according
                 * to the returned service_stat.
                 */
                if (rec->s->state != JK_LB_STATE_ERROR) {
                    rec->s->state  = JK_LB_STATE_OK;
                    p->states[rec->i] = JK_LB_STATE_OK;
                }
                /* Decrement the busy worker count.
                 * Check if the busy was reset to zero by graceful
                 * restart of the server.
                 */
                JK_ATOMIC_DECREMENT(&(p->worker->s->busy));
                if (service_stat == JK_TRUE) {
                    /*
                     * Successful request.
                     */
                    rec->s->state  = JK_LB_STATE_OK;
                    p->states[rec->i] = JK_LB_STATE_OK;
                    rec->s->first_error_time = 0;
                    rec->s->last_error_time = 0;
                    rc = JK_TRUE;
                    recoverable = JK_UNSET;
                }
                else if (service_stat == JK_CLIENT_ERROR) {
                    /*
                     * Client error !!!
                     * Since this is bad request do not fail over.
                     */
                    rec->s->state  = JK_LB_STATE_OK;
                    p->states[rec->i] = JK_LB_STATE_ERROR;
                    rec->s->first_error_time = 0;
                    rec->s->last_error_time = 0;
                    rc = JK_CLIENT_ERROR;
                    recoverable = JK_FALSE;
                }
                else if (service_stat == JK_SERVER_ERROR) {
                    /*
                     * Internal JK server error.
                     * Keep previous global state.
                     * Do not try to reuse the same node for the same request.
                     * Failing over to another node could help.
                     */
                    p->states[rec->i] = JK_LB_STATE_ERROR;
                    rc = JK_FALSE;
                }
                else if (service_stat == JK_AJP_PROTOCOL_ERROR) {
                    /*
                     * We've received a bad AJP message from the backend.
                     * Keep previous global state.
                     * Do not try to reuse the same node for the same request.
                     * Failing over to another node could help.
                     */
                    p->states[rec->i] = JK_LB_STATE_ERROR;
                    rc = JK_FALSE;
                }
                else if (service_stat == JK_STATUS_ERROR) {
                    /*
                     * Status code configured as service is down.
                     * The node is fine.
                     * Do not try to reuse the same node for the same request.
                     * Failing over to another node could help.
                     */
                    rec->s->state  = JK_LB_STATE_OK;
                    p->states[rec->i] = JK_LB_STATE_ERROR;
                    rec->s->first_error_time = 0;
                    rec->s->last_error_time = 0;
                    rc = JK_FALSE;
                }
                else if (service_stat == JK_BUSY_ERROR) {
                    /*
                     * Node was busy.
                     * Do not try to reuse the same node for the same request.
                     * Failing over to another node could help.
                     */
                    rec->s->state  = JK_LB_STATE_BUSY;
                    p->states[rec->i] = JK_LB_STATE_BUSY;
                    rc = JK_FALSE;
                }
                else if (service_stat == JK_STATUS_FATAL_ERROR) {
                    /*
                     * Status code configured as service is down.
                     * Mark the node as bad.
                     * Do not try to reuse the same node for the same request.
                     * Failing over to another node could help.
                     */
                    rec->s->errors++;
                    rec->s->state = JK_LB_STATE_ERROR;
                    p->states[rec->i] = JK_LB_STATE_ERROR;
                    rec->s->first_error_time = time(NULL);
                    rec->s->last_error_time = rec->s->first_error_time;
                    rc = JK_FALSE;
                }
                else if (service_stat == JK_REPLY_TIMEOUT) {
                    if (aw->s->reply_timeouts > (unsigned)p->worker->max_reply_timeouts) {
                        /*
                         * Service failed - to many reply timeouts
                         * Mark the node as bad.
                         * Do not try to reuse the same node for the same request.
                         * Failing over to another node could help.
                         */
                        rec->s->errors++;
                        rec->s->state = JK_LB_STATE_ERROR;
                        p->states[rec->i] = JK_LB_STATE_ERROR;
                        rec->s->first_error_time = time(NULL);
                        rec->s->last_error_time = rec->s->first_error_time;
                    }
                    else {
                        /*
                         * Reply timeout, but not yet too many of them.
                         * Keep previous global state.
                         * Do not try to reuse the same node for the same request.
                         * Failing over to another node could help.
                         */
                        p->states[rec->i] = JK_LB_STATE_ERROR;
                    }
                    rc = JK_FALSE;
                }
                else {
                    /*
                     * Various unspecific error cases.
                     * Keep previous global state, if we are not in local error since to long.
                     * Do not try to reuse the same node for the same request.
                     * Failing over to another node could help.
                     */
                    time_t now = time(NULL);
                    rec->s->errors++;
                    if (aw->s->busy == 0 ||
                        p->worker->error_escalation_time == 0 ||
                        (rec->s->first_error_time > 0 &&
                         (int)difftime(now, rec->s->first_error_time) >= p->worker->error_escalation_time)) {
                        if (JK_IS_DEBUG_LEVEL(l))
                            jk_log(l, JK_LOG_DEBUG,
                                   "worker %s escalating local error to global error",
                                   rec->name);
                        rec->s->state = JK_LB_STATE_ERROR;
                    }
                    p->states[rec->i] = JK_LB_STATE_ERROR;
                    if (rec->s->first_error_time == 0) {
                        rec->s->first_error_time = now;
                    }
                    rec->s->last_error_time = now;
                    rc = JK_FALSE;
                }
                if (p->worker->lblock == JK_LB_LOCK_PESSIMISTIC)
                    jk_shm_unlock();
                if (p->states[rec->i] == JK_LB_STATE_ERROR)
                    jk_log(l, JK_LOG_INFO,
                           "service failed, worker %s is in %serror state",
                           rec->name,
                           rec->s->state == JK_LB_STATE_ERROR ? "" : "local ");
            }
            if (recoverable == JK_TRUE) {
                /*
                 * Error is recoverable by submitting the request to
                 * another worker... Lets try to do that.
                 */
                if (JK_IS_DEBUG_LEVEL(l))
                    jk_log(l, JK_LOG_DEBUG,
                           "recoverable error... will try to recover on other worker");
            }
            else {
                /*
                 * Error is not recoverable - break with an error.
                 */
                if (rc == JK_CLIENT_ERROR)
                    jk_log(l, JK_LOG_INFO,
                           "unrecoverable error %d, request failed."
                           " Client failed in the middle of request,"
                           " we can't recover to another instance.",
                           *is_error);
                else if (rc != JK_TRUE)
                    jk_log(l, JK_LOG_ERROR,
                           "unrecoverable error %d, request failed."
                           " Tomcat failed in the middle of request,"
                           " we can't recover to another instance.",
                           *is_error);
            }
            if (first == 1 && s->add_log_items) {
                first = 0;
                lb_add_log_items(s, lb_first_log_names, prec, l);
            }
        }
        else {
            /* No more workers left ... */
            if (!was_forced) {
                int nf;
                /* Force recovery only once.
                 * If it still fails, Tomcat is still disconnected.
                 */
                jk_shm_lock();
                nf = force_recovery(p->worker, p->states, l);
                jk_shm_unlock();
                was_forced = 1;
                if (nf) {
                    /* We have forced recovery.
                     * Reset the service loop and go again
                     */
                    prec = NULL;
                    jk_log(l, JK_LOG_INFO,
                           "Forcing recovery once for %d workers", nf);
                    continue;
                }
                else {
                    /* No workers in error state.
                     * Somebody set them all to disabled?
                     */
                    jk_log(l, JK_LOG_INFO,
                           "All tomcat instances failed, no more workers "
                           "left for recovery (attempt=%d, retry=%d)",
                           attempt + 1, retry);
                    *is_error = JK_HTTP_SERVER_BUSY;
                    rc = JK_FALSE;
                }
            }
            else {
                jk_log(l, JK_LOG_INFO,
                       "All tomcat instances failed, no more workers "
                       "left (attempt=%d, retry=%d)",
                       attempt + 1, retry);
                *is_error = JK_HTTP_SERVER_BUSY;
                rc = JK_FALSE;
            }
        }
        attempt++;
    }
    if (recoverable == JK_TRUE) {
        jk_log(l, JK_LOG_INFO,
               "All tomcat instances are busy or in error state");
        /* rc and http error must be set above */
    }
    if (rc == JK_FALSE) {
        jk_log(l, JK_LOG_ERROR,
               "All tomcat instances failed, no more workers left");
    }
    if (prec && s->add_log_items) {
        lb_add_log_items(s, lb_last_log_names, prec, l);
    }

    JK_TRACE_EXIT(l);
    return rc;
}

static int JK_METHOD done(jk_endpoint_t **e, jk_log_context_t *l)
{
    JK_TRACE_ENTER(l);

    if (e && *e && (*e)->endpoint_private) {
        lb_endpoint_t *p = (*e)->endpoint_private;
        free(p->states);
        free(p);
        *e = NULL;
        JK_TRACE_EXIT(l);
        return JK_TRUE;
    }

    JK_LOG_NULL_PARAMS(l);
    JK_TRACE_EXIT(l);
    return JK_FALSE;
}

static int JK_METHOD validate(jk_worker_t *pThis,
                              jk_map_t *props,
                              jk_worker_env_t *we, jk_log_context_t *l)
{
    JK_TRACE_ENTER(l);

    if (pThis && pThis->worker_private) {
        lb_worker_t *p = pThis->worker_private;
        char **worker_names;
        unsigned int num_of_workers;
        const char *secret;

        p->worker.we = we;
        p->sticky_session = jk_get_is_sticky_session(props, p->name);
        p->sticky_session_force = jk_get_is_sticky_session_force(props, p->name);
        secret = jk_get_worker_secret(props, p->name);

        if (jk_get_lb_worker_list(props,
                                  p->name,
                                  &worker_names,
                                  &num_of_workers) && num_of_workers) {
            unsigned int i = 0;
            unsigned int j = 0;
            p->lb_workers = jk_pool_alloc(&p->p,
                                          num_of_workers *
                                          sizeof(lb_sub_worker_t));
            if (!p->lb_workers) {
                JK_TRACE_EXIT(l);
                return JK_FALSE;
            }
            memset(p->lb_workers, 0, num_of_workers * sizeof(lb_sub_worker_t));
            for (i = 0; i < num_of_workers; i++) {
                if (jk_shm_str_init(p->lb_workers[i].name, worker_names[i], "name", l) == JK_FALSE) {
                    JK_TRACE_EXIT(l);
                    return JK_FALSE;
                }
                jk_shm_str_copy(p->lb_workers[i].route, p->lb_workers[i].name, l);
                p->lb_workers[i].s = jk_shm_alloc_lb_sub_worker(&p->p, p->s->h.id, worker_names[i], l);
                if (p->lb_workers[i].s == NULL) {
                    jk_log(l, JK_LOG_ERROR,
                           "allocating lb sub worker record from shared memory");
                    JK_TRACE_EXIT(l);
                    return JK_FALSE;
                }
                p->lb_workers[i].i = i;
            }

            for (i = 0; i < num_of_workers; i++) {
                const char *s;
                unsigned int ms;

                if (p->lb_workers[i].s->h.sequence != 0) {
                    /* Somebody already setup this worker.
                     * Just pull the data.
                     */
                    if (JK_IS_DEBUG_LEVEL(l)) {
                        jk_log(l, JK_LOG_DEBUG,
                               "Balanced worker %s already configured (sequence=%d)",
                               p->lb_workers[i].name, p->lb_workers[i].s->h.sequence);
                    }
                    if (!wc_create_worker(p->lb_workers[i].name, 0,
                                          props,
                                          &(p->lb_workers[i].worker),
                                          we, l) || !p->lb_workers[i].worker) {
                        break;
                    }
                    jk_lb_pull_worker(p, i, l);
                    continue;
                }
                p->lb_workers[i].lb_factor =
                    jk_get_lb_factor(props, worker_names[i]);
                if (p->lb_workers[i].lb_factor < 1) {
                    p->lb_workers[i].lb_factor = 1;
                }
                /* Calculate the maximum packet size from all workers
                 * for the recovery buffer.
                 */
                ms = jk_get_max_packet_size(props, worker_names[i]);
                if (ms > p->max_packet_size)
                    p->max_packet_size = ms;
                p->lb_workers[i].distance =
                    jk_get_distance(props, worker_names[i]);
                if ((s = jk_get_worker_route(props, worker_names[i], NULL))) {
                    if (jk_shm_str_init(p->lb_workers[i].route, s, "route", l) == JK_FALSE) {
                        JK_TRACE_EXIT(l);
                        return JK_FALSE;
                    }
                }
                if ((s = jk_get_worker_domain(props, worker_names[i], NULL))) {
                    if (jk_shm_str_init(p->lb_workers[i].domain, s, "domain", l) == JK_FALSE) {
                        JK_TRACE_EXIT(l);
                        return JK_FALSE;
                    }
                }
                if ((s = jk_get_worker_redirect(props, worker_names[i], NULL))) {
                    if (jk_shm_str_init(p->lb_workers[i].redirect, s, "redirect", l) == JK_FALSE) {
                        JK_TRACE_EXIT(l);
                        return JK_FALSE;
                    }
                }

                p->lb_workers[i].s->lb_value = 0;
                p->lb_workers[i].s->state = JK_LB_STATE_IDLE;
                p->lb_workers[i].s->first_error_time = 0;
                p->lb_workers[i].s->last_error_time = 0;
                p->lb_workers[i].s->elected_snapshot = 0;
                p->lb_workers[i].s->sessions = 0;
                p->lb_workers[i].activation =
                    jk_get_worker_activation(props, worker_names[i]);
                if (!wc_create_worker(p->lb_workers[i].name, 0,
                                      props,
                                      &(p->lb_workers[i].worker),
                                      we, l) || !p->lb_workers[i].worker) {
                    break;
                }
                if (secret && (p->lb_workers[i].worker->type == JK_AJP13_WORKER_TYPE ||
                    p->lb_workers[i].worker->type == JK_AJP14_WORKER_TYPE)) {
                    ajp_worker_t *aw = (ajp_worker_t *)p->lb_workers[i].worker->worker_private;
                    if (!aw->secret)
                        aw->secret = secret;
                }
                if (p->lb_workers[i].worker->type == JK_AJP13_WORKER_TYPE ||
                    p->lb_workers[i].worker->type == JK_AJP14_WORKER_TYPE) {
                    ajp_worker_t *aw = (ajp_worker_t *)p->lb_workers[i].worker->worker_private;
                    if (aw->port == 0) {
                        p->lb_workers[i].activation = JK_LB_ACTIVATION_STOPPED;
                    }
                }
            }

            if (i != num_of_workers) {
                jk_log(l, JK_LOG_ERROR,
                       "Failed creating worker %s",
                       p->lb_workers[i].name);
                close_workers(p, i, l);
            }
            else {
                /* Update domain names if route contains period '.' */
                for (i = 0; i < num_of_workers; i++) {
                    if (!p->lb_workers[i].domain[0]) {
                        char *id_domain = strchr(p->lb_workers[i].route, '.');
                        if (id_domain) {
                            *id_domain = '\0';
                            strcpy(p->lb_workers[i].domain, p->lb_workers[i].route);
                            *id_domain = '.';
                        }
                    }
                    if (JK_IS_DEBUG_LEVEL(l)) {
                        jk_log(l, JK_LOG_DEBUG,
                               "Balanced worker %d has name %s and route %s in domain %s",
                               i,
                               p->lb_workers[i].name,
                               p->lb_workers[i].route,
                               p->lb_workers[i].domain);
                    }
                }
                p->num_of_workers = num_of_workers;
                update_mult(p, l);
                for (i = 0; i < num_of_workers; i++) {
                    for (j = 0; j < i; j++) {
                        if (strcmp(p->lb_workers[i].route, p->lb_workers[j].route) == 0) {
                            jk_log(l, JK_LOG_ERROR,
                                   "Balanced workers number %d (%s) and %d (%s) share the same route %s - aborting configuration!",
                                   i,
                                   p->lb_workers[i].name,
                                   j,
                                   p->lb_workers[j].name,
                                   p->lb_workers[i].route);
                            JK_TRACE_EXIT(l);
                            return JK_FALSE;
                        }
                    }
                }
                JK_TRACE_EXIT(l);
                return JK_TRUE;
            }
        }
    }

    JK_LOG_NULL_PARAMS(l);
    JK_TRACE_EXIT(l);
    return JK_FALSE;
}

static int JK_METHOD init(jk_worker_t *pThis,
                          jk_map_t *props,
                          jk_worker_env_t *we, jk_log_context_t *l)
{
    int i;
    const char *s;

    lb_worker_t *p = (lb_worker_t *)pThis->worker_private;
    JK_TRACE_ENTER(l);

    p->worker.we = we;
    p->retries = jk_get_worker_retries(props, p->name,
                                       JK_RETRIES);
    p->lb_retries = jk_get_worker_lb_retries(props, p->name,
                                       JK_LB_RETRIES);
    p->retry_interval =
            jk_get_worker_retry_interval(props, p->name,
                                        JK_SLEEP_DEF);
    p->recover_wait_time = jk_get_worker_recover_timeout(props, p->name,
                                                         WAIT_BEFORE_RECOVER);
    if (p->recover_wait_time < 1)
        p->recover_wait_time = 1;
    p->error_escalation_time = jk_get_worker_error_escalation_time(props, p->name,
                                                                   p->recover_wait_time / 2);
    p->max_reply_timeouts = jk_get_worker_max_reply_timeouts(props, p->name,
                                                             0);
    p->maintain_time = jk_get_worker_maintain_time(props);
    if(p->maintain_time < 0)
        p->maintain_time = 0;
    p->s->last_maintain_time = time(NULL);
    p->s->last_reset = p->s->last_maintain_time;

    p->lbmethod = jk_get_lb_method(props, p->name);
#ifdef JK_ATOMIC_MISSING
    if (p->lbmethod == JK_LB_METHOD_BUSYNESS) {
        jk_log(l, JK_LOG_WARNING, "Missing support for atomics: "
               "LB method 'busyness' not recommended");
    }
#endif
    p->lblock   = jk_get_lb_lock(props, p->name);
    s = jk_get_lb_session_cookie(props, p->name, JK_SESSION_IDENTIFIER);
    if (jk_shm_str_init(p->session_cookie, s, "session_cookie", l) == JK_FALSE) {
        JK_TRACE_EXIT(l);
        return JK_FALSE;
    }
    s = jk_get_lb_session_path(props, p->name, JK_PATH_SESSION_IDENTIFIER);
    if (jk_shm_str_init(p->session_path, s, "session_path", l) == JK_FALSE) {
        JK_TRACE_EXIT(l);
        return JK_FALSE;
    }
    p->set_session_cookie = jk_get_lb_set_session_cookie(props, p->name, JK_FALSE);
    s = jk_get_lb_session_cookie_path(props, p->name, "/");
    if (jk_shm_str_init(p->session_cookie_path, s, "session_cookie_path", l) == JK_FALSE) {
        JK_TRACE_EXIT(l);
        return JK_FALSE;
    }

    JK_INIT_CS(&(p->cs), i);
    if (i == JK_FALSE) {
        jk_log(l, JK_LOG_ERROR,
               "creating thread lock (errno=%d)",
               errno);
        JK_TRACE_EXIT(l);
        return JK_FALSE;
    }
    if (p->s->h.sequence == 0) {
        /* Set configuration data to shared memory
         */
        jk_lb_push(p, JK_TRUE, JK_FALSE, l);
    }
    else {
        /* Shared memory for this worker is already configured.
         * Update with runtime data
         */
        jk_lb_pull(p, JK_TRUE, l);
    }
    JK_TRACE_EXIT(l);
    return JK_TRUE;
}

static int JK_METHOD get_endpoint(jk_worker_t *pThis,
                                  jk_endpoint_t **pend, jk_log_context_t *l)
{
    JK_TRACE_ENTER(l);

    if (pThis && pThis->worker_private && pend) {
        lb_endpoint_t *p = (lb_endpoint_t *) malloc(sizeof(lb_endpoint_t));
        p->worker = pThis->worker_private;
        p->endpoint.endpoint_private = p;
        p->endpoint.service = service;
        p->endpoint.done = done;
        p->states = (int *)malloc((p->worker->num_of_workers + 1) * sizeof(int));
        if (!p->states) {
            free(p);
            jk_log(l, JK_LOG_ERROR,
                   "Failed allocating private worker state memory");
            JK_TRACE_EXIT(l);
            return JK_FALSE;
        }
        *pend = &p->endpoint;
        JK_TRACE_EXIT(l);
        return JK_TRUE;
    }
    else {
        JK_LOG_NULL_PARAMS(l);
    }

    JK_TRACE_EXIT(l);
    return JK_FALSE;
}

static int JK_METHOD destroy(jk_worker_t **pThis, jk_log_context_t *l)
{
    JK_TRACE_ENTER(l);

    if (pThis && *pThis && (*pThis)->worker_private) {
        lb_worker_t *private_data = (*pThis)->worker_private;

        close_workers(private_data, private_data->num_of_workers, l);
        JK_DELETE_CS(&private_data->cs);
        jk_close_pool(&private_data->p);
        free(private_data);

        JK_TRACE_EXIT(l);
        return JK_TRUE;
    }

    JK_LOG_NULL_PARAMS(l);
    JK_TRACE_EXIT(l);
    return JK_FALSE;
}

int JK_METHOD lb_worker_factory(jk_worker_t **w,
                                const char *name, jk_log_context_t *l)
{
    JK_TRACE_ENTER(l);

    if (NULL != name && NULL != w) {
        lb_worker_t *private_data =
            (lb_worker_t *) calloc(1, sizeof(lb_worker_t));


        jk_open_pool(&private_data->p,
                        private_data->buf,
                        sizeof(jk_pool_atom_t) * TINY_POOL_SIZE);

        private_data->s = jk_shm_alloc_lb_worker(&private_data->p, name, l);
        if (!private_data->s) {
            free(private_data);
            JK_TRACE_EXIT(l);
            return 0;
        }
        if (jk_shm_str_init(private_data->name, name, "name", l) == JK_FALSE) {
            JK_TRACE_EXIT(l);
            return 0;
        }
        private_data->lb_workers = NULL;
        private_data->num_of_workers = 0;
        private_data->worker.worker_private = private_data;
        private_data->worker.validate = validate;
        private_data->worker.init = init;
        private_data->worker.get_endpoint = get_endpoint;
        private_data->worker.destroy = destroy;
        private_data->worker.maintain = maintain_workers;
        private_data->worker.shutdown = shutdown_workers;
        private_data->recover_wait_time = WAIT_BEFORE_RECOVER;
        private_data->error_escalation_time = private_data->recover_wait_time / 2;
        private_data->max_reply_timeouts = 0;
        private_data->max_packet_size = AJP13_DEF_PACKET_SIZE;
        private_data->sequence = 0;
        private_data->next_offset = 0;
        *w = &private_data->worker;
        JK_TRACE_EXIT(l);
        return JK_LB_WORKER_TYPE;
    }
    else {
        JK_LOG_NULL_PARAMS(l);
    }

    JK_TRACE_EXIT(l);
    return 0;
}
