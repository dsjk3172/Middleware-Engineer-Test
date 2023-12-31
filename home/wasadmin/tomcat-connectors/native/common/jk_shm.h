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
 * Description: Shared Memory object header file                           *
 * Author:      Mladen Turk <mturk@jboss.com>                              *
 * Author:      Rainer Jung <rjung@apache.org>                             *
 ***************************************************************************/
#ifndef _JK_SHM_H
#define _JK_SHM_H

#include "jk_global.h"
#include "jk_pool.h"
#include "jk_util.h"

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

/**
 * @file jk_shm.h
 * @brief Jk shared memory management
 *
 *
 */

#define JK_SHM_MAJOR    '1'
#define JK_SHM_MINOR    '3'
#define JK_SHM_STR_SIZ  (JK_ALIGN(JK_MAX_NAME_LEN + 1, 8))
#define JK_SHM_MAGIC    '!', 'J', 'K', 'S', 'H', 'M', JK_SHM_MAJOR, JK_SHM_MINOR
#define JK_SHM_MAGIC_SIZ  8

/* XXX: Check if adding struct members for overflow */
#define JK_SHM_SLOT_SIZE          384
/* Really huge numbers, but 64 workers should be enough */
#define JK_SHM_DEF_WORKERS        64
#define JK_SHM_ALIGNMENT          JK_SHM_SLOT_SIZE
#define JK_SHM_ALIGN(x)           JK_ALIGN((x), JK_SHM_ALIGNMENT)
#define JK_SHM_MIN_SIZE           ((JK_SHM_SLOT_SIZE * JK_SHM_DEF_WORKERS * 3) + \
                                   JK_SHM_ALIGNMENT)

typedef char shm_str[JK_SHM_STR_SIZ];

/** jk shm generic worker record structure */
struct jk_shm_worker_header
{
    /* Shared memory slot id */
    int     id;
    /* JK_XXX_WORKER_TYPE */
    int     type;
    /* worker name */
    shm_str name;
    /* parent slot id.
     * Zero in case worker does not belong to balancer.
     */
    int     parent_id;
    /* Sequence counter starting at 0 and increasing
     * every time we change the config.
     */
    volatile unsigned int sequence;
};
typedef struct jk_shm_worker_header jk_shm_worker_header_t;

/** jk shm ajp13/ajp14 worker record structure */
struct jk_shm_ajp_worker
{
    jk_shm_worker_header_t h;
    shm_str host;
    int port;
    volatile int addr_sequence;

    /* Configuration data mirrored from ajp_worker */
    int cache_timeout;
    int connect_timeout;
    int ping_timeout;
    int reply_timeout;
    int prepost_timeout;
    unsigned int recovery_opts;
    int retries;
    int retry_interval;
    int busy_limit;
    unsigned int max_packet_size;
    /* current error state (runtime) of the worker */
    volatile int state;
    /* Statistical data */
    /* Number of currently connected channels */
    volatile int connected;
    /* Maximum number of connected channels */
    volatile int max_connected;
    /* Number of currently busy channels */
    volatile int busy;
    /* Maximum number of busy channels */
    volatile int max_busy;
    volatile time_t error_time;
    /* Number of bytes read from remote */
    volatile jk_uint64_t readed;
    /* Number of bytes transferred to remote */
    volatile jk_uint64_t transferred;
    /* Number of times the worker was used */
    volatile jk_uint64_t  used;
    /* Number of times the worker was used - snapshot during maintenance */
    volatile jk_uint64_t  used_snapshot;
    /* Number of non 200 responses */
    volatile jk_uint32_t  errors;
    /* Decayed number of reply_timeout errors */
    volatile jk_uint32_t  reply_timeouts;
    /* Number of client errors */
    volatile jk_uint32_t  client_errors;
    /* Last reset time */
    volatile time_t last_reset;
    volatile time_t last_maintain_time;
};
typedef struct jk_shm_ajp_worker jk_shm_ajp_worker_t;

/** jk shm lb sub worker record structure */
struct jk_shm_lb_sub_worker
{
    jk_shm_worker_header_t h;

    /* route */
    shm_str route;
    /* worker domain */
    shm_str domain;
    /* worker redirect route */
    shm_str redirect;
    /* worker distance */
    volatile int distance;
    /* current activation state (config) of the worker */
    volatile int activation;
    /* current error state (runtime) of the worker */
    volatile int state;
    /* Current lb factor */
    volatile int lb_factor;
    /* Current lb reciprocal factor */
    volatile jk_uint64_t lb_mult;
    /* Current lb value  */
    volatile jk_uint64_t lb_value;
    /* First consecutive error time */
    volatile time_t first_error_time;
    /* Last consecutive error time */
    volatile time_t last_error_time;
    /* Number of times the worker was elected - snapshot during maintenance */
    volatile jk_uint64_t  elected_snapshot;
    /* Number of non-sticky requests handled, that were not marked as stateless */
    volatile jk_uint64_t  sessions;
    /* Number of non 200 responses */
    volatile jk_uint32_t  errors;
};
typedef struct jk_shm_lb_sub_worker jk_shm_lb_sub_worker_t;

/** jk shm lb worker record structure */
struct jk_shm_lb_worker
{
    jk_shm_worker_header_t h;

    /* Number of currently busy channels */
    volatile int busy;
    /* Maximum number of busy channels */
    volatile int max_busy;
    int     sticky_session;
    int     sticky_session_force;
    int     recover_wait_time;
    int     error_escalation_time;
    int     max_reply_timeouts;
    int     retries;
    int     retry_interval;
    int     lbmethod;
    int     lblock;
    unsigned int max_packet_size;
    /* Last reset time */
    volatile time_t last_reset;
    volatile time_t last_maintain_time;
};
typedef struct jk_shm_lb_worker jk_shm_lb_worker_t;

int jk_shm_str_init(shm_str dst, const char *src,
                    const char *name, jk_log_context_t *l);

int jk_shm_str_init_ne(shm_str dst, const char *src,
                       const char *name, jk_log_context_t *l);

void jk_shm_str_copy(shm_str dst, shm_str src,
                     jk_log_context_t *l);

const char *jk_shm_name(void);

/* Calculate needed shm size */
int jk_shm_calculate_size(jk_map_t *init_data, jk_log_context_t *log_ctx);

/* Open the shared memory creating file if needed
 */
int jk_shm_open(const char *fname, int sz, jk_log_context_t *log_ctx);

/* Close the shared memory
 */
void jk_shm_close(jk_log_context_t *log_ctx);

/* Attach the shared memory in child process.
 * File has to be opened in parent.
 */
int jk_shm_attach(const char *fname, int sz, jk_log_context_t *log_ctx);

/* allocate shm ajp worker record
 * If there is no shm present the pool will be used instead
 */
jk_shm_ajp_worker_t *jk_shm_alloc_ajp_worker(jk_pool_t *p, const char *name,
                                             jk_log_context_t *log_ctx);

/* allocate shm lb sub worker record
 * If there is no shm present the pool will be used instead
 */
jk_shm_lb_sub_worker_t *jk_shm_alloc_lb_sub_worker(jk_pool_t *p,
                                                   int lb_id, const char *name,
                                                   jk_log_context_t *log_ctx);

/* allocate shm lb worker record
 * If there is no shm present the pool will be used instead
 */
jk_shm_lb_worker_t *jk_shm_alloc_lb_worker(jk_pool_t *p, const char *name,
                                           jk_log_context_t *log_ctx);

int jk_shm_check_maintain(time_t trigger);

/* Lock shared memory for thread safe access */
int jk_shm_lock(void);

/* Unlock shared memory for thread safe access */
int jk_shm_unlock(void);


#ifdef __cplusplus
}
#endif  /* __cplusplus */
#endif  /* _JK_SHM_H */
