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
 * Description: General purpose map object                                 *
 * Author:      Gal Shachor <shachor@il.ibm.com>                           *
 * Author:      Mladen Turk <mturk@apache.org>                             *
 ***************************************************************************/
#if defined(AS400) && !defined(AS400_UTF8)
#include "apr_xlate.h"
#endif

#include "jk_global.h"
#include "jk_pool.h"
#include "jk_util.h"
#include "jk_map.h"

#define CAPACITY_INC_SIZE   (50)
#define LENGTH_OF_LINE      (8192)
#define JK_MAP_RECURSION    (20)
#define JK_MAP_REFERENCE    (".reference")
#define JK_MAP_REFERENCE_SZ (strlen(JK_MAP_REFERENCE))

/* Taken from APR tables/apr_hash.c */
/*
 * This is the popular `times 33' hash algorithm which is used by
 * perl and also appears in Berkeley DB. This is one of the best
 * known hash functions for strings because it is both computed
 * very fast and distributes very well.
 *
 * The originator may be Dan Bernstein but the code in Berkeley DB
 * cites Chris Torek as the source. The best citation I have found
 * is "Chris Torek, Hash function for text in C, Usenet message
 * <27038@mimsy.umd.edu> in comp.lang.c , October, 1990." in Rich
 * Salz's USENIX 1992 paper about INN which can be found at
 * <http://citeseer.nj.nec.com/salz92internetnews.html>.
 *
 * The magic of number 33, i.e. why it works better than many other
 * constants, prime or not, has never been adequately explained by
 * anyone. So I try an explanation: if one experimentally tests all
 * multipliers between 1 and 256 (as I did while writing a low-level
 * data structure library some time ago) one detects that even
 * numbers are not useable at all. The remaining 128 odd numbers
 * (except for the number 1) work more or less all equally well.
 * They all distribute in an acceptable way and this way fill a hash
 * table with an average percent of approx. 86%.
 *
 * If one compares the chi^2 values of the variants (see
 * Bob Jenkins ``Hashing Frequently Asked Questions'' at
 * http://burtleburtle.net/bob/hash/hashfaq.html for a description
 * of chi^2), the number 33 not even has the best value. But the
 * number 33 and a few other equally good numbers like 17, 31, 63,
 * 127 and 129 have nevertheless a great advantage to the remaining
 * numbers in the large set of possible multipliers: their multiply
 * operation can be replaced by a faster operation based on just one
 * shift plus either a single addition or subtraction operation. And
 * because a hash function has to both distribute good _and_ has to
 * be very fast to compute, those few numbers should be preferred.
 *
 *                  -- Ralf S. Engelschall <rse@engelschall.com>
 */
#define COMPUTE_KEY_HASH(key, hash)    \
{                                                    \
    const unsigned char *p;                          \
    hash = 0;                                        \
    for (p = (const unsigned char *)key; *p; p++) {  \
        hash = hash * 33 + *p;                       \
    }                                                \
}

static volatile int global_map_id = 0;
static void trim_prp_comment(char *prp);
static size_t trim(char *s);
static int map_realloc(jk_map_t *m);
static char *jk_map_replace_properties(jk_map_t *m, jk_map_t *env, char *value);

int jk_map_alloc(jk_map_t **m)
{
    if (m) {
        *m = (jk_map_t *)calloc(1, sizeof(jk_map_t));
        if (*m)
            return jk_map_open(*m);
    }

    return JK_FALSE;
}

int jk_map_free(jk_map_t **m)
{
    int rc = JK_FALSE;

    if (m && *m) {
        jk_map_close(*m);
        free(*m);
        *m = NULL;
    }

    return rc;
}

int jk_map_open(jk_map_t *m)
{
    int rc = JK_FALSE;

    if (m) {
        jk_open_pool(&m->p, m->buf, sizeof(jk_pool_atom_t) * SMALL_POOL_SIZE);
        m->id = ++global_map_id;
        m->capacity = 0;
        m->size = 0;
        m->keys  = NULL;
        m->names = NULL;
        m->values = NULL;
        rc = JK_TRUE;
    }

    return rc;
}

int jk_map_close(jk_map_t *m)
{
    int rc = JK_FALSE;

    if (m) {
        jk_close_pool(&m->p);
        rc = JK_TRUE;
    }

    return rc;
}

void *jk_map_get(jk_map_t *m, const char *name, const void *def)
{
    const void *rc = (void *)def;

    if (m && name) {
        unsigned int i;
        unsigned int hash;
        COMPUTE_KEY_HASH(name, hash)
        for (i = 0; i < m->size; i++) {
            if (m->keys[i] == hash && strcmp(m->names[i], name) == 0) {
                rc = m->values[i];
                break;
            }
        }
    }

    return (void *)rc;          /* DIRTY */
}

int jk_map_get_id(jk_map_t *m, const char *name)
{
    int rc = -1;
    if (m && name) {
        unsigned int i;
        unsigned int hash;
        COMPUTE_KEY_HASH(name, hash)
        for (i = 0; i < m->size; i++) {
            if (m->keys[i] == hash && strcmp(m->names[i], name) == 0) {
                rc = i;
                break;
            }
        }
    }

    return rc;
}

const char *jk_map_get_string(jk_map_t *m, const char *name, const char *def)
{
    const char *rc = def;

    if (m && name) {
        unsigned int i;
        unsigned int hash;
        COMPUTE_KEY_HASH(name, hash)
        for (i = 0; i < m->size; i++) {
            if (m->keys[i] == hash && strcmp(m->names[i], name) == 0) {
                rc = m->values[i];
                break;
            }
        }
    }

    return rc;
}


int jk_map_get_int(jk_map_t *m, const char *name, int def)
{
    char buf[100];
    const char *rc;
    size_t len;
    int int_res;

    sprintf(buf, "%d", def);
    rc = jk_map_get_string(m, name, buf);

    len = strlen(rc);
    if (len) {
        const char *lastchar = &rc[0] + len - 1;
        int multit = 1;
        if ('m' == *lastchar || 'M' == *lastchar) {
            multit = 1024 * 1024;
        }
        else if ('k' == *lastchar || 'K' == *lastchar) {
            multit = 1024;
        }
        /* Safe because atoi() will stop at any non-numeric lastchar */
        int_res = atoi(rc) * multit;
    }
    else
        int_res = def;

    return int_res;
}

double jk_map_get_double(jk_map_t *m, const char *name, double def)
{
    char buf[100];
    const char *rc;

    sprintf(buf, "%f", def);
    rc = jk_map_get_string(m, name, buf);

    return atof(rc);
}

int jk_map_get_bool(jk_map_t *m, const char *name, int def)
{
    char buf[100];
    const char *rc;

    sprintf(buf, "%d", def);
    rc = jk_map_get_string(m, name, buf);

    return jk_get_bool_code(rc, def);
}

char **jk_map_get_string_list(jk_map_t *m,
                              const char *name,
                              unsigned int *list_len, const char *def)
{
    const char *l = jk_map_get_string(m, name, def);
    char **ar = NULL;

#ifdef _MT_CODE_PTHREAD
    char *lasts;
#endif

    *list_len = 0;

    if (l) {
        unsigned capacity = 0;
        unsigned idex = 0;
        char *p;
        char *v = jk_pool_strdup(&m->p, l);

        if (!v) {
            return NULL;
        }

        /*
         * GS, in addition to VG's patch, we now need to
         * strtok also by a "*"
         */
#ifdef _MT_CODE_PTHREAD
        for (p = strtok_r(v, " \t,", &lasts);
             p; p = strtok_r(NULL, " \t,", &lasts))
#else
        for (p = strtok(v, " \t,"); p; p = strtok(NULL, " \t,"))
#endif

        {
            if (idex == capacity) {
                ar = jk_pool_realloc(&m->p,
                                     sizeof(char *) * (capacity + 5),
                                     ar, sizeof(char *) * capacity);
                if (!ar) {
                    return NULL;
                }
                capacity += 5;
            }
            ar[idex] = jk_pool_strdup(&m->p, p);
            idex++;
        }

        *list_len = idex;
    }

    return ar;
}

int *jk_map_get_int_list(jk_map_t *m,
                         const char *name,
                         unsigned int *list_len,
                         const char *def)
{
    const char *l = jk_map_get_string(m, name, def);
    int *ar = NULL;

#ifdef _MT_CODE_PTHREAD
    char *lasts;
#endif

    if (l) {
        unsigned int capacity = 0;
        unsigned int idex = 0;
        char *p;
        char *v = jk_pool_strdup(&m->p, l);

        if (!v) {
            return NULL;
        }

        /*
         * GS, in addition to VG's patch, we now need to
         * strtok also by a "*"
         */
#ifdef _MT_CODE_PTHREAD
        for (p = strtok_r(v, " \t,", &lasts);
             p; p = strtok_r(NULL, " \t,", &lasts))
#else
        for (p = strtok(v, " \t,"); p; p = strtok(NULL, " \t,"))
#endif

        {
            if (idex == capacity) {
                ar = jk_pool_realloc(&m->p,
                                     sizeof(int) * (capacity + 5),
                                     ar, sizeof(int) * capacity);
                if (!ar) {
                    return NULL;
                }
                capacity += 5;
            }
            ar[idex] = atoi(p);
            idex++;
        }

        *list_len = idex;
    }
    return ar;
}

int jk_map_add(jk_map_t *m, const char *name, const void *value)
{
    int rc = JK_FALSE;

    if (m && name) {
        unsigned int hash;
        COMPUTE_KEY_HASH(name, hash)
        map_realloc(m);

        if (m->size < m->capacity) {
            m->values[m->size] = value;
            m->names[m->size] = jk_pool_strdup(&m->p, name);
            m->keys[m->size] = hash;
            m->size++;
            rc = JK_TRUE;
        }
    }

    return rc;
}

int jk_map_put(jk_map_t *m, const char *name, const void *value, void **old)
{
    int rc = JK_FALSE;

    if (m && name) {
        unsigned int i;
        unsigned int hash;
        COMPUTE_KEY_HASH(name, hash)
        for (i = 0; i < m->size; i++) {
            if (m->keys[i] == hash && strcmp(m->names[i], name) == 0) {
                break;
            }
        }

        if (i < m->size) {
            if (old)
                *old = (void *)m->values[i];        /* DIRTY */
            m->values[i] = value;
            rc = JK_TRUE;
        }
        else {
            rc = jk_map_add(m, name, value);
        }
    }

    return rc;
}


static int jk_map_validate_property(char *prp, jk_log_context_t *l)
{
    /* check the worker properties */
    if (!jk_is_valid_property(prp)) {
        jk_log(l, JK_LOG_ERROR,
               "The attribute '%s' is not supported - please check"
               " the documentation for the supported attributes.",
               prp);
        return JK_FALSE;
    }
    if (jk_is_deprecated_property(prp)) {
        jk_log(l, JK_LOG_WARNING,
               "The attribute '%s' is deprecated - please check"
               " the documentation for the correct replacement.",
               prp);
    }
    return JK_TRUE;
}

static int jk_map_handle_duplicates(jk_map_t *m, const char *prp, char **v,
                                    int treatment, jk_log_context_t *l)
{
    const char *oldv = jk_map_get_string(m, prp, NULL);
    if (oldv) {
        if ((treatment == JK_MAP_HANDLE_DUPLICATES)
            && jk_is_unique_property(prp) == JK_FALSE) {
            char *tmpv = jk_pool_alloc(&m->p,
                                       strlen(*v) + strlen(oldv) + 3);
            if (tmpv) {
                char sep = '*';
                if (jk_is_path_property(prp))
                    sep = PATH_SEPERATOR;
                else if (jk_is_cmd_line_property(prp))
                    sep = ' ';
                else if (jk_is_list_property(prp))
                    sep = ',';
                sprintf(tmpv, "%s%c%s", oldv, sep, *v);
            }
            *v = tmpv;
            if (JK_IS_DEBUG_LEVEL(l))
                jk_log(l, JK_LOG_DEBUG,
                       "Concatenated value is: %s -> %s",
                       prp, *v);
            return JK_FALSE;
        }
        else {
            jk_log(l, JK_LOG_WARNING,
                   "Duplicate key '%s' detected - previous value '%s'"
                   " will be overwritten with '%s'.",
                   prp, oldv ? oldv : "(null)", v ? *v : "(null)");
            return JK_TRUE;
        }
    }
    else {
        return JK_TRUE;
    }
}

int jk_map_read_property(jk_map_t *m, jk_map_t *env, const char *str,
                         int treatment, jk_log_context_t *l)
{
    int rc = JK_TRUE;
    char buf[LENGTH_OF_LINE + 1];
    char *prp = &buf[0];

    if (strlen(str) > LENGTH_OF_LINE) {
        jk_log(l, JK_LOG_ERROR,
               "Line too long (%d > %d), ignoring entry",
               strlen(str), LENGTH_OF_LINE);
        return JK_FALSE;
    }

    strcpy(prp, str);
    if (trim(prp)) {
        char *v = strchr(prp, '=');
        if (v) {
            *v = '\0';
            v++;
            if (trim(v) && trim(prp)) {
                if (treatment == JK_MAP_HANDLE_RAW) {
                    v = jk_pool_strdup(&m->p, v);
                }
                else {
                    if (jk_map_validate_property(prp, l) == JK_FALSE)
                        return JK_FALSE;
                    v = jk_map_replace_properties(m, env, v);
                    if (jk_map_handle_duplicates(m, prp, &v, treatment, l) == JK_TRUE)
                        v = jk_pool_strdup(&m->p, v);
                }
                if (v) {
                    if (JK_IS_DEBUG_LEVEL(l))
                        jk_log(l, JK_LOG_DEBUG,
                               "Adding property '%s' with value '%s' to map.",
                               prp, v);
                    jk_map_put(m, prp, v, NULL);
                }
                else {
                    JK_LOG_NULL_PARAMS(l);
                    rc = JK_FALSE;
                }
            }
        }
    }
    return rc;
}


int jk_map_read_properties(jk_map_t *m, jk_map_t *env, const char *f, time_t *modified,
                           int treatment, jk_log_context_t *l)
{
    int rc = JK_FALSE;

    if (m && f) {
        struct stat statbuf;
        FILE *fp;
        if (jk_stat(f, &statbuf) == -1)
            return JK_FALSE;
#if defined(AS400) && !defined(AS400_UTF8)
        fp = fopen(f, "r, o_ccsid=0");
#else
        fp = fopen(f, "r");
#endif

        if (fp) {
            char buf[LENGTH_OF_LINE + 1];
            char *prp;

            rc = JK_TRUE;

            while (NULL != (prp = fgets(buf, LENGTH_OF_LINE, fp))) {
                trim_prp_comment(prp);
                if (*prp) {
                    if ((rc = jk_map_read_property(m, env, prp, treatment, l)) == JK_FALSE)
                        break;
                }
            }
            fclose(fp);
            if (modified)
                *modified = statbuf.st_mtime;
        }
    }

    return rc;
}


int jk_map_size(jk_map_t *m)
{
    if (m) {
        return m->size;
    }

    return -1;
}

const char *jk_map_name_at(jk_map_t *m, int idex)
{
    if (m && idex >= 0) {
        return m->names[idex];  /* DIRTY */
    }

    return NULL;
}

void *jk_map_value_at(jk_map_t *m, int idex)
{
    if (m && idex >= 0) {
        return (void *)m->values[idex]; /* DIRTY */
    }

    return NULL;
}

void jk_map_dump(jk_map_t *m, jk_log_context_t *l)
{
    if (m) {
        int s = jk_map_size(m);
        int i;
        for (i=0;i<s;i++) {
            if (!jk_map_name_at(m, i)) {
                jk_log(l, JK_LOG_WARNING,
                       "Map contains empty name at index %d\n", i);
            }
            if (!jk_map_value_at(m, i)) {
                jk_log(l, JK_LOG_WARNING,
                       "Map contains empty value for name '%s' at index %d\n",
                       jk_map_name_at(m, i), i);
            }
            if (JK_IS_DEBUG_LEVEL(l)) {
                jk_log(l, JK_LOG_DEBUG,
                       "Dump of map %d: '%s' -> '%s'", m->id,
                       jk_map_name_at(m, i) ? jk_map_name_at(m, i) : "(null)",
                       jk_map_value_at(m, i) ? jk_map_value_at(m, i) : "(null)");
            }
        }
    }
}

int jk_map_copy(jk_map_t *src, jk_map_t *dst)
{
    int sz = jk_map_size(src);
    int i;
    for (i = 0; i < sz; i++) {
        const char *name = jk_map_name_at(src, i);
        if (jk_map_get(dst, name, NULL) == NULL) {
            if (!jk_map_put(dst, name,
                            jk_pool_strdup(&dst->p, jk_map_get_string(src, name, NULL)),
                            NULL)) {
                return JK_FALSE;
            }
        }
    }
    return JK_TRUE;
}


static void trim_prp_comment(char *prp)
{
#if defined(AS400) && !defined(AS400_UTF8)
    char *comment;
    /* lots of lines that translate a '#' realtime deleted   */
    comment = strchr(prp, *APR_NUMBERSIGN);
#else
    char *comment = strchr(prp, '#');
#endif
    if (comment) {
        *comment = '\0';
    }
}

static size_t trim(char *s)
{
    size_t first;
    size_t len;

    /* check for empty strings */
    if (!(len = strlen(s)))
        return 0;
    for (len = len - 1; (len > 0) &&
        jk_isspace(s[len]); len--);
    if ((len > 0) || !jk_isspace(s[len])) {
        len++;
    }

    s[len] = '\0';
    len++;

    for (first = 0; (s[first] != '\0') &&
        jk_isspace(s[first]); first++);

    if (first > 0) {
        memmove(s, s + first, len - first);
    }

    return len;
}

static int map_realloc(jk_map_t *m)
{
    if (m->size == m->capacity) {
        const char **names;
        const void **values;
        unsigned int *keys;
        int capacity  = m->capacity + CAPACITY_INC_SIZE;
        size_t old_sz = m->capacity * sizeof(void *);
        size_t new_sz = capacity * sizeof(void *);

        names  = (const char  **)jk_pool_realloc(&m->p, new_sz, m->names, old_sz);
        values = (const void  **)jk_pool_realloc(&m->p, new_sz, m->values, old_sz);
        keys   = (unsigned int *)jk_pool_realloc(&m->p, new_sz, m->keys, old_sz);

        if (values && names && keys) {

            m->names  = names;
            m->values = values;
            m->keys   = keys;
            m->capacity = capacity;

            return JK_TRUE;
        }
    }

    return JK_FALSE;
}

/**
 *  Replace $(property) in value.
 *
 */
static char *jk_map_replace_properties(jk_map_t *m, jk_map_t *env, char *value)
{
    char *rc = value;
    char *env_start = rc;
    int rec = 0;

    while ((env_start = strstr(env_start, "$(")) != NULL) {
        char *env_end = strstr(env_start, ")");
        if (rec++ > 20)
            return rc;
        if (env_end) {
            char env_name[LENGTH_OF_LINE + 1] = "";
            const char *env_value;
#if defined(WIN32)
            char env_buf[LENGTH_OF_LINE + 1];
#endif
            *env_end = '\0';
            strcpy(env_name, env_start + 2);
            *env_end = ')';

            env_value = jk_map_get_string(m, env_name, NULL);
            if (!env_value) {
                env_value = getenv(env_name);
            }
            if (!env_value && env) {
                /* Search inside local environment table */
                env_value = jk_map_get_string(env, env_name, NULL);
            }

#if defined(WIN32)
            if (!env_value) {
                /* Try the env block from calling process */
                if (GetEnvironmentVariable(env_name, env_buf,
                                           sizeof(env_buf)))
                    env_value = &env_buf[0];
            }
#endif
            if (env_value) {
                size_t offset = 0;
                char *new_value = jk_pool_alloc(&m->p,
                                                (sizeof(char) *
                                                (strlen(rc) +
                                                strlen(env_value))));
                if (!new_value) {
                    break;
                }
                *env_start = '\0';
                strcpy(new_value, rc);
                strcat(new_value, env_value);
                strcat(new_value, env_end + 1);
                offset = env_start - rc + strlen(env_value);
                rc = new_value;
                /* Avoid recursive subst */
                env_start = rc + offset;
            }
            else {
                env_start = env_end;
            }
        }
        else {
            break;
        }
    }

    return rc;
}

/**
 *  Resolve references
 *
 */
int jk_map_resolve_references(jk_map_t *m, const char *prefix,
                              int wildcard, int depth, jk_log_context_t *l)
{
    int rc = JK_FALSE;

    JK_TRACE_ENTER(l);

    if (m && prefix) {
        if (depth <= JK_MAP_RECURSION) {
            size_t prelen = strlen(prefix);
            unsigned int i;
            rc = JK_TRUE;
            if (JK_IS_DEBUG_LEVEL(l))
                jk_log(l, JK_LOG_DEBUG,
                       "Checking for references with prefix %s with%s wildcard (recursion %d)",
                       prefix, wildcard? "" : "out", depth);
            for (i = 0; i < m->size; i++) {
                char *v = (char *)m->values[i];
                if (v && *v &&
                    !strncmp(m->names[i], prefix, prelen)) {
                    size_t remain = strlen(m->names[i]) - prelen;
                    if ((remain == JK_MAP_REFERENCE_SZ) || (wildcard && remain > JK_MAP_REFERENCE_SZ)) {
                        remain = strlen(m->names[i]) - JK_MAP_REFERENCE_SZ;
                        if (!strncmp(m->names[i] + remain, JK_MAP_REFERENCE, JK_MAP_REFERENCE_SZ)) {
                            char *from = jk_pool_alloc(&m->p,
                                                       (sizeof(char) *
                                                       (strlen(v) + 2)));
                            char *to = jk_pool_alloc(&m->p,
                                                     (sizeof(char) *
                                                     (remain + 2)));
                            if (!from || !to) {
                                jk_log(l, JK_LOG_ERROR,
                                       "Error in string allocation");
                                rc = JK_FALSE;
                                break;
                            }
                            strcpy(from, v);
                            *(from+strlen(v))   = '.';
                            *(from+strlen(v)+1) = '\0';
                            strncpy(to, m->names[i], remain);
                            *(to+remain)   = '.';
                            *(to+remain+1) = '\0';

                            rc = jk_map_resolve_references(m, v, 0, depth+1, l);
                            if (rc == JK_FALSE) {
                                break;
                            }
                            if (JK_IS_DEBUG_LEVEL(l))
                                jk_log(l, JK_LOG_DEBUG,
                                       "Copying values from %s to %s",
                                       from, to);
                            rc = jk_map_inherit_properties(m, from, to, l);
                            if (rc == JK_FALSE) {
                                break;
                            }
                        }
                    }
                }
            }
        }
        else {
            jk_log(l, JK_LOG_ERROR,
                   "Recursion limit %d for worker references with prefix '%s' reached",
                   JK_MAP_RECURSION, prefix);
        }
    }
    else {
        JK_LOG_NULL_PARAMS(l);
    }
    JK_TRACE_EXIT(l);
    return rc;
}

/**
 *  Inherit properties
 *
 */
int jk_map_inherit_properties(jk_map_t *m, const char *from, const char *to, jk_log_context_t *l)
{
    int rc = JK_FALSE;
    const char *prp;
    char *to_prp;

    if (m && from && to) {
        unsigned int i;
        for (i = 0; i < m->size; i++) {
            if (!strncmp(m->names[i], from, strlen(from))) {
                rc = JK_TRUE;
                prp = m->names[i] + strlen(from);
                to_prp = jk_pool_alloc(&m->p,
                                       (sizeof(char) *
                                       (strlen(to) +
                                       strlen(prp) + 1)));
                if (!to_prp) {
                    jk_log(l, JK_LOG_ERROR,
                           "Error in string allocation for attribute '%s.%s'",
                           to, prp);
                    rc = JK_FALSE;
                    break;
                }
                strcpy(to_prp, to);
                strcat(to_prp, prp);
                if (jk_map_get_id(m, to_prp) < 0) {
                    rc = jk_map_add(m, to_prp, m->values[i]);
                    if (rc == JK_FALSE) {
                        jk_log(l, JK_LOG_ERROR,
                               "Error when adding attribute '%s'",
                               to_prp);
                        break;
                    }
                }
            }
        }
        if (rc == JK_FALSE) {
            jk_log(l, JK_LOG_ERROR,
                   "Reference '%s' not found",
                   from);
        }
    }
    else {
        JK_LOG_NULL_PARAMS(l);
    }
    return rc;
}
