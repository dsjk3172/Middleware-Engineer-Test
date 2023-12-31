/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mod_proxy.h"
#include "http_config.h"

module AP_MODULE_DECLARE_DATA proxy_wstunnel_module;

typedef struct {
    unsigned int fallback_to_proxy_http     :1,
                 fallback_to_proxy_http_set :1;
} proxyws_dir_conf;

static int can_fallback_to_proxy_http;

static int proxy_wstunnel_check_trans(request_rec *r, const char *url)
{
    proxyws_dir_conf *dconf = ap_get_module_config(r->per_dir_config,
                                                   &proxy_wstunnel_module);

    if (can_fallback_to_proxy_http && dconf->fallback_to_proxy_http) {
        ap_log_rerror(APLOG_MARK, APLOG_TRACE5, 0, r, "check_trans fallback");
        return DECLINED;
    }

    if (ap_cstr_casecmpn(url, "ws:", 3) != 0
            && ap_cstr_casecmpn(url, "wss:", 4) != 0) {
        return DECLINED;
    }

    if (!apr_table_get(r->headers_in, "Upgrade")) {
        /* No Upgrade, let mod_proxy_http handle it (for instance).
         * Note: anything but OK/DECLINED will do (i.e. bypass wstunnel w/o
         * aborting the request), HTTP_UPGRADE_REQUIRED is documentary...
         */
        return HTTP_UPGRADE_REQUIRED;
    }

    return OK;
}

/*
 * Canonicalise http-like URLs.
 * scheme is the scheme for the URL
 * url is the URL starting with the first '/'
 * def_port is the default port for this scheme.
 */
static int proxy_wstunnel_canon(request_rec *r, char *url)
{
    proxyws_dir_conf *dconf = ap_get_module_config(r->per_dir_config,
                                                   &proxy_wstunnel_module);
    char *host, *path, sport[7];
    char *search = NULL;
    const char *err;
    char *scheme;
    apr_port_t port, def_port;

    if (can_fallback_to_proxy_http && dconf->fallback_to_proxy_http) {
        ap_log_rerror(APLOG_MARK, APLOG_TRACE5, 0, r, "canon fallback");
        return DECLINED;
    }

    /* ap_port_of_scheme() */
    if (ap_cstr_casecmpn(url, "ws:", 3) == 0) {
        url += 3;
        scheme = "ws:";
        def_port = apr_uri_port_of_scheme("http");
    }
    else if (ap_cstr_casecmpn(url, "wss:", 4) == 0) {
        url += 4;
        scheme = "wss:";
        def_port = apr_uri_port_of_scheme("https");
    }
    else {
        return DECLINED;
    }

    port = def_port;
    ap_log_rerror(APLOG_MARK, APLOG_TRACE1, 0, r, "canonicalising URL %s", url);

    /*
     * do syntactic check.
     * We break the URL into host, port, path, search
     */
    err = ap_proxy_canon_netloc(r->pool, &url, NULL, NULL, &host, &port);
    if (err) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(02439) "error parsing URL %s: %s",
                      url, err);
        return HTTP_BAD_REQUEST;
    }

    /*
     * now parse path/search args, according to rfc1738:
     * process the path. With proxy-nocanon set (by
     * mod_proxy) we use the raw, unparsed uri
     */
    if (apr_table_get(r->notes, "proxy-nocanon")) {
        path = url;   /* this is the raw path */
    }
    else if (apr_table_get(r->notes, "proxy-noencode")) {
        path = url;   /* this is the encoded path already */
        search = r->args;
    }
    else {
        core_dir_config *d = ap_get_core_module_config(r->per_dir_config);
        int flags = d->allow_encoded_slashes && !d->decode_encoded_slashes ? PROXY_CANONENC_NOENCODEDSLASHENCODING : 0;

        path = ap_proxy_canonenc_ex(r->pool, url, strlen(url), enc_path, flags,
                                    r->proxyreq);
        if (!path) {
            return HTTP_BAD_REQUEST;
        }
        search = r->args;
    }
    /*
     * If we have a raw control character or a ' ' in nocanon path or
     * r->args, correct encoding was missed.
     */
    if (path == url && *ap_scan_vchar_obstext(path)) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(10419)
                      "To be forwarded path contains control "
                      "characters or spaces");
        return HTTP_FORBIDDEN;
    }
    if (search && *ap_scan_vchar_obstext(search)) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(10409)
                      "To be forwarded query string contains control "
                      "characters or spaces");
        return HTTP_FORBIDDEN;
    }

    if (port != def_port)
        apr_snprintf(sport, sizeof(sport), ":%d", port);
    else
        sport[0] = '\0';

    if (ap_strchr_c(host, ':')) {
        /* if literal IPv6 address */
        host = apr_pstrcat(r->pool, "[", host, "]", NULL);
    }
    r->filename = apr_pstrcat(r->pool, "proxy:", scheme, "//", host, sport,
                              "/", path, (search) ? "?" : "",
                              (search) ? search : "", NULL);
    return OK;
}

/*
 * process the request and write the response.
 */
static int proxy_wstunnel_request(apr_pool_t *p, request_rec *r,
                                proxy_conn_rec *conn,
                                proxy_worker *worker,
                                proxy_server_conf *conf,
                                apr_uri_t *uri,
                                char *url, char *server_portstr)
{
    apr_status_t rv;
    apr_pollset_t *pollset;
    apr_pollfd_t pollfd;
    const apr_pollfd_t *signalled;
    apr_int32_t pollcnt, pi;
    apr_int16_t pollevent;
    conn_rec *c = r->connection;
    apr_socket_t *sock = conn->sock;
    conn_rec *backconn = conn->connection;
    char *buf;
    apr_bucket_brigade *header_brigade;
    apr_bucket *e;
    char *old_cl_val = NULL;
    char *old_te_val = NULL;
    apr_bucket_brigade *bb = apr_brigade_create(p, c->bucket_alloc);
    apr_socket_t *client_socket = ap_get_conn_socket(c);
    int done = 0, replied = 0;
    const char *upgrade_method = *worker->s->upgrade ? worker->s->upgrade : "WebSocket";

    header_brigade = apr_brigade_create(p, backconn->bucket_alloc);

    ap_log_rerror(APLOG_MARK, APLOG_TRACE2, 0, r, "sending request");

    rv = ap_proxy_create_hdrbrgd(p, header_brigade, r, conn,
                                 worker, conf, uri, url, server_portstr,
                                 &old_cl_val, &old_te_val);
    if (rv != OK) {
        return rv;
    }

    if (ap_cstr_casecmp(upgrade_method, "NONE") == 0) {
        buf = apr_pstrdup(p, "Upgrade: WebSocket" CRLF "Connection: Upgrade" CRLF CRLF);
    } else if (ap_cstr_casecmp(upgrade_method, "ANY") == 0) {
        const char *upgrade;
        upgrade = apr_table_get(r->headers_in, "Upgrade");
        buf = apr_pstrcat(p, "Upgrade: ", upgrade, CRLF "Connection: Upgrade" CRLF CRLF, NULL);
    } else {
        buf = apr_pstrcat(p, "Upgrade: ", upgrade_method, CRLF "Connection: Upgrade" CRLF CRLF, NULL);
    }
    ap_xlate_proto_to_ascii(buf, strlen(buf));
    e = apr_bucket_pool_create(buf, strlen(buf), p, c->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(header_brigade, e);

    if ((rv = ap_proxy_pass_brigade(backconn->bucket_alloc, r, conn, backconn,
                                    header_brigade, 1)) != OK)
        return rv;

    apr_brigade_cleanup(header_brigade);

    ap_log_rerror(APLOG_MARK, APLOG_TRACE2, 0, r, "setting up poll()");

    if ((rv = apr_pollset_create(&pollset, 2, p, 0)) != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, APLOGNO(02443)
                      "error apr_pollset_create()");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

#if 0
    apr_socket_opt_set(sock, APR_SO_NONBLOCK, 1);
    apr_socket_opt_set(sock, APR_SO_KEEPALIVE, 1);
    apr_socket_opt_set(client_socket, APR_SO_NONBLOCK, 1);
    apr_socket_opt_set(client_socket, APR_SO_KEEPALIVE, 1);
#endif

    pollfd.p = p;
    pollfd.desc_type = APR_POLL_SOCKET;
    pollfd.reqevents = APR_POLLIN | APR_POLLHUP;
    pollfd.desc.s = sock;
    pollfd.client_data = NULL;
    apr_pollset_add(pollset, &pollfd);

    pollfd.desc.s = client_socket;
    apr_pollset_add(pollset, &pollfd);

    ap_remove_input_filter_byhandle(c->input_filters, "reqtimeout");

    r->output_filters = c->output_filters;
    r->proto_output_filters = c->output_filters;
    r->input_filters = c->input_filters;
    r->proto_input_filters = c->input_filters;

    /* This handler should take care of the entire connection; make it so that
     * nothing else is attempted on the connection after returning. */
    c->keepalive = AP_CONN_CLOSE;

    do { /* Loop until done (one side closes the connection, or an error) */
        rv = apr_pollset_poll(pollset, -1, &pollcnt, &signalled);
        if (rv != APR_SUCCESS) {
            if (APR_STATUS_IS_EINTR(rv)) {
                continue;
            }
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, APLOGNO(02444) "error apr_poll()");
            return HTTP_INTERNAL_SERVER_ERROR;
        }
        ap_log_rerror(APLOG_MARK, APLOG_TRACE1, 0, r, APLOGNO(02445)
                      "woke from poll(), i=%d", pollcnt);

        for (pi = 0; pi < pollcnt; pi++) {
            const apr_pollfd_t *cur = &signalled[pi];

            if (cur->desc.s == sock) {
                pollevent = cur->rtnevents;
                if (pollevent & (APR_POLLIN | APR_POLLHUP)) {
                    ap_log_rerror(APLOG_MARK, APLOG_TRACE1, 0, r, APLOGNO(02446)
                                  "sock was readable");
                    done |= ap_proxy_transfer_between_connections(r, backconn,
                                                                  c,
                                                                  header_brigade,
                                                                  bb, "sock",
                                                                  &replied,
                                                                  AP_IOBUFSIZE,
                                                                  0)
                                                                 != APR_SUCCESS;
                }
                else if (pollevent & APR_POLLERR) {
                    ap_log_rerror(APLOG_MARK, APLOG_NOTICE, 0, r, APLOGNO(02447)
                            "error on backconn");
                    backconn->aborted = 1;
                    done = 1;
                }
                else { 
                    ap_log_rerror(APLOG_MARK, APLOG_NOTICE, 0, r, APLOGNO(02605)
                            "unknown event on backconn %d", pollevent);
                    done = 1;
                }
            }
            else if (cur->desc.s == client_socket) {
                pollevent = cur->rtnevents;
                if (pollevent & (APR_POLLIN | APR_POLLHUP)) {
                    ap_log_rerror(APLOG_MARK, APLOG_TRACE1, 0, r, APLOGNO(02448)
                                  "client was readable");
                    done |= ap_proxy_transfer_between_connections(r, c,
                                                                  backconn, bb,
                                                                  header_brigade,
                                                                  "client",
                                                                  NULL,
                                                                  AP_IOBUFSIZE,
                                                                  0)
                                                                 != APR_SUCCESS;
                }
                else if (pollevent & APR_POLLERR) {
                    ap_log_rerror(APLOG_MARK, APLOG_TRACE1, 0, r, APLOGNO(02607)
                            "error on client conn");
                    c->aborted = 1;
                    done = 1;
                }
                else { 
                    ap_log_rerror(APLOG_MARK, APLOG_NOTICE, 0, r, APLOGNO(02606)
                            "unknown event on client conn %d", pollevent);
                    done = 1;
                }
            }
            else {
                ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, APLOGNO(02449)
                              "unknown socket in pollset");
                done = 1;
            }

        }
    } while (!done);

    ap_log_rerror(APLOG_MARK, APLOG_TRACE2, 0, r,
                  "finished with poll() - cleaning up");

    if (!replied) {
        return HTTP_BAD_GATEWAY;
    }
    else {
        return OK;
    }

    return OK;
}

/*
 */
static int proxy_wstunnel_handler(request_rec *r, proxy_worker *worker,
                             proxy_server_conf *conf,
                             char *url, const char *proxyname,
                             apr_port_t proxyport)
{
    proxyws_dir_conf *dconf = ap_get_module_config(r->per_dir_config,
                                                   &proxy_wstunnel_module);
    int status;
    char server_portstr[32];
    proxy_conn_rec *backend = NULL;
    const char *upgrade;
    char *scheme;
    apr_pool_t *p = r->pool;
    char *locurl = url;
    apr_uri_t *uri;
    int is_ssl = 0;

    if (can_fallback_to_proxy_http && dconf->fallback_to_proxy_http) {
        ap_log_rerror(APLOG_MARK, APLOG_TRACE5, 0, r, "handler fallback");
        return DECLINED;
    }

    if (ap_cstr_casecmpn(url, "wss:", 4) == 0) {
        scheme = "WSS";
        is_ssl = 1;
    }
    else if (ap_cstr_casecmpn(url, "ws:", 3) == 0) {
        scheme = "WS";
    }
    else {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(02450)
                      "declining URL %s", url);
        return DECLINED;
    }
    ap_log_rerror(APLOG_MARK, APLOG_TRACE1, 0, r, "serving URL %s", url);

    upgrade = apr_table_get(r->headers_in, "Upgrade");
    if (!upgrade || !ap_proxy_worker_can_upgrade(p, worker, upgrade,
                                                 "WebSocket")) {
        const char *worker_upgrade = *worker->s->upgrade ? worker->s->upgrade
                                                         : "WebSocket";
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, APLOGNO(02900)
                      "require upgrade for URL %s "
                      "(Upgrade header is %s, expecting %s)", 
                      url, upgrade ? upgrade : "missing", worker_upgrade);
        apr_table_setn(r->err_headers_out, "Connection", "Upgrade");
        apr_table_setn(r->err_headers_out, "Upgrade", worker_upgrade);
        return HTTP_UPGRADE_REQUIRED;
    }

    uri = apr_palloc(p, sizeof(*uri));
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(02451) "serving URL %s", url);

    /* create space for state information */
    status = ap_proxy_acquire_connection(scheme, &backend, worker, r->server);
    if (status != OK) {
        goto cleanup;
    }

    backend->is_ssl = is_ssl;
    backend->close = 0;

    /* Step One: Determine Who To Connect To */
    status = ap_proxy_determine_connection(p, r, conf, worker, backend,
                                           uri, &locurl, proxyname, proxyport,
                                           server_portstr,
                                           sizeof(server_portstr));
    if (status != OK) {
        goto cleanup;
    }

    /* Step Two: Make the Connection */
    if (ap_proxy_connect_backend(scheme, backend, worker, r->server)) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(02452)
                      "failed to make connection to backend: %s",
                      backend->hostname);
        status = HTTP_SERVICE_UNAVAILABLE;
        goto cleanup;
    }

    /* Step Three: Create conn_rec */
    status = ap_proxy_connection_create_ex(scheme, backend, r);
    if (status != OK) {
        goto cleanup;
    }

    /* Step Four: Process the Request */
    status = proxy_wstunnel_request(p, r, backend, worker, conf, uri, locurl,
                                  server_portstr);

cleanup:
    /* Do not close the socket */
    if (backend) { 
        backend->close = 1;
        ap_proxy_release_connection(scheme, backend, r->server);
    }
    return status;
}

static void *create_proxyws_dir_config(apr_pool_t *p, char *dummy)
{
    proxyws_dir_conf *new =
        (proxyws_dir_conf *) apr_pcalloc(p, sizeof(proxyws_dir_conf));

    new->fallback_to_proxy_http = 1;

    return (void *) new;
}

static void *merge_proxyws_dir_config(apr_pool_t *p, void *vbase, void *vadd)
{
    proxyws_dir_conf *new = apr_pcalloc(p, sizeof(proxyws_dir_conf)),
                     *add = vadd, *base = vbase;

    new->fallback_to_proxy_http = (add->fallback_to_proxy_http_set)
                                  ? add->fallback_to_proxy_http
                                  : base->fallback_to_proxy_http;
    new->fallback_to_proxy_http_set = (add->fallback_to_proxy_http_set
                                       || base->fallback_to_proxy_http_set);

    return new;
}

static const char * proxyws_fallback_to_proxy_http(cmd_parms *cmd, void *conf, int arg)
{
    proxyws_dir_conf *dconf = conf;
    dconf->fallback_to_proxy_http = !!arg;
    dconf->fallback_to_proxy_http_set = 1;
    return NULL;
}

static int proxy_wstunnel_post_config(apr_pool_t *pconf, apr_pool_t *plog,
                                      apr_pool_t *ptemp, server_rec *s)
{
    can_fallback_to_proxy_http =
        (ap_find_linked_module("mod_proxy_http.c") != NULL);

    return OK;
}

static const command_rec ws_proxy_cmds[] =
{
    AP_INIT_FLAG("ProxyWebsocketFallbackToProxyHttp",
                 proxyws_fallback_to_proxy_http, NULL, RSRC_CONF|ACCESS_CONF,
                 "whether to let mod_proxy_http handle the upgrade and tunneling, "
                 "On by default"),

    {NULL}
};

static void ws_proxy_hooks(apr_pool_t *p)
{
    static const char * const aszSucc[] = { "mod_proxy_http.c", NULL};
    ap_hook_post_config(proxy_wstunnel_post_config, NULL, NULL, APR_HOOK_MIDDLE);
    proxy_hook_scheme_handler(proxy_wstunnel_handler, NULL, aszSucc, APR_HOOK_FIRST);
    proxy_hook_check_trans(proxy_wstunnel_check_trans, NULL, aszSucc, APR_HOOK_MIDDLE);
    proxy_hook_canon_handler(proxy_wstunnel_canon, NULL, aszSucc, APR_HOOK_FIRST);
}

AP_DECLARE_MODULE(proxy_wstunnel) = {
    STANDARD20_MODULE_STUFF,
    create_proxyws_dir_config,  /* create per-directory config structure */
    merge_proxyws_dir_config,   /* merge per-directory config structures */
    NULL,                       /* create per-server config structure */
    NULL,                       /* merge per-server config structures */
    ws_proxy_cmds,              /* command apr_table_t */
    ws_proxy_hooks              /* register hooks */
};
