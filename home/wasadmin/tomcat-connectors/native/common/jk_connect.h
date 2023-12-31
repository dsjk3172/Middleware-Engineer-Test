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
 * Description: Socket connections header file                             *
 * Author:      Gal Shachor <shachor@il.ibm.com>                           *
 ***************************************************************************/

#ifndef JK_CONNECT_H
#define JK_CONNECT_H

#include "jk_logger.h"
#include "jk_global.h"

#if !defined(WIN32)
#define closesocket         close
#endif

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

#define JK_SOCKET_EOF      (-2)

void jk_clone_sockaddr(jk_sockaddr_t *out, jk_sockaddr_t *in);

int jk_resolve(const char *host, int port, jk_sockaddr_t *rc, void *pool,
               int prefer_ipv6, jk_log_context_t *log_ctx);

jk_sock_t jk_open_socket(jk_sockaddr_t *addr, jk_sockaddr_t *source,
                         int keepalive,
                         int timeout, int connect_timeout,
                         int sock_buf, jk_log_context_t *log_ctx);

int jk_close_socket(jk_sock_t sd, jk_log_context_t *log_ctx);

int jk_shutdown_socket(jk_sock_t sd, jk_log_context_t *log_ctx);

int jk_tcp_socket_sendfull(jk_sock_t sd, const unsigned char *b, int len, jk_log_context_t *log_ctx);

int jk_tcp_socket_recvfull(jk_sock_t sd, unsigned char *b, int len, jk_log_context_t *log_ctx);

char *jk_dump_hinfo(jk_sockaddr_t *saddr, char *buf, size_t size);

char *jk_dump_sinfo(jk_sock_t sd, char *buf, size_t size);

int jk_is_input_event(jk_sock_t sd, int timeout, jk_log_context_t *log_ctx);

int jk_is_socket_connected(jk_sock_t sd, jk_log_context_t *log_ctx);


/***
 * i5/OS V5R4 need ASCII<->EBCDIC translation for inet_addr() call
 */
#if !defined(AS400_UTF8)

#define jk_inet_addr inet_addr

#endif


#ifdef __cplusplus
}
#endif                          /* __cplusplus */

#endif                          /* JK_CONNECT_H */
