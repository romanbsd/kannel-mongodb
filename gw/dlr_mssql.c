/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2013 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

/*
 * dlr_mssql.c - MSSQL dlr storage implementation.
 *
 * Author: Alejandro Guerrieri <aguerrieri at kannel dot org>
 * 
 * Based on dlr_oracle.c
 * Alexander Malysh <a.malysh@centrium.de>
 * Robert Ga�ach <robert.galach@my.tenbit.pl>
 *
 * Copyright: See COPYING file that comes with this distribution
 */

#include "gwlib/gwlib.h"
#include "gwlib/dbpool.h"
#include "dlr_p.h"


#ifdef HAVE_MSSQL

/*
 * Our connection pool to mssql.
 */
static DBPool *pool = NULL;

/*
 * Database fields, which we are use.
 */
static struct dlr_db_fields *fields = NULL;


static long dlr_messages_mssql()
{
    List *result, *row;
    Octstr *sql;
    DBPoolConn *conn;
    long msgs = -1;

    conn = dbpool_conn_consume(pool);
    if (conn == NULL)
        return -1;

    sql = octstr_format("SELECT COUNT(*) FROM %S", fields->table);
#if defined(DLR_TRACE)
    debug("dlr.mssql", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    if (dbpool_conn_select(conn, sql, NULL, &result) != 0) {
        octstr_destroy(sql);
        dbpool_conn_produce(conn);
        return -1;
    }
    dbpool_conn_produce(conn);
    octstr_destroy(sql);

    if (gwlist_len(result) > 0) {
        row = gwlist_extract_first(result);
        msgs = strtol(octstr_get_cstr(gwlist_get(row,0)), NULL, 10);
        gwlist_destroy(row, octstr_destroy_item);
    }
    gwlist_destroy(result, NULL);

    return msgs;
}

static void dlr_shutdown_mssql()
{
    dbpool_destroy(pool);
    dlr_db_fields_destroy(fields);
}

static void dlr_add_mssql(struct dlr_entry *entry)
{
    Octstr *sql;
    DBPoolConn *pconn;
    debug("dlr.mssql", 0, "adding DLR entry into database");
    int res;

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL) {
        dlr_entry_destroy(entry);
        return;
    }
    
    sql = octstr_format("INSERT INTO %S (%S, %S, %S, %S, %S, %S, %S, %S, %S) VALUES "
                "('%S', '%S', '%S', '%S', '%S', '%S', '%d', '%S', '%d')",
                fields->table, fields->field_smsc, fields->field_ts, fields->field_src,
                fields->field_dst, fields->field_serv, fields->field_url,
                fields->field_mask, fields->field_boxc, fields->field_status,
                entry->smsc, entry->timestamp, entry->source, entry->destination,
                entry->service, entry->url, entry->mask, entry->boxc_id, 0);

#if defined(DLR_TRACE)
    debug("dlr.mssql", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    if ((res = dbpool_conn_update(pconn, sql, NULL)) == -1)
        error(0, "DLR: MSSQL: Error while adding dlr entry for DST<%s>", octstr_get_cstr(entry->destination));
    else if (!res)
        warning(0, "DLR: MSSQL: No dlr inserted for DST<%s>", octstr_get_cstr(entry->destination));

    dbpool_conn_produce(pconn);
    octstr_destroy(sql);
    dlr_entry_destroy(entry);
}

static void dlr_remove_mssql(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql, *like;
    DBPoolConn *pconn;
    int res;

    debug("dlr.mssql", 0, "removing DLR from database");

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL)
        return;

    if (dst)
        like = octstr_format("AND %S LIKE '%%%S'", fields->field_dst, dst);
    else
        like = octstr_imm("");

    sql = octstr_format("SET ROWCOUNT 1\nDELETE FROM %S WHERE %S='%S' AND "
         "%S='%S' %S \nSET ROWCOUNT 0", fields->table, fields->field_smsc,
         smsc, fields->field_ts, ts, like);

#if defined(DLR_TRACE)
    debug("dlr.mssql", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    if ((res = dbpool_conn_update(pconn, sql, NULL)) == -1)
        error(0, "DLR: MSSQL: Error while removing dlr entry for DST<%s>", octstr_get_cstr(dst));
    else if (!res)
        warning(0, "DLR: MSSQL: No dlr deleted for DST<%s>", octstr_get_cstr(dst));

    dbpool_conn_produce(pconn);
    octstr_destroy(sql);
    octstr_destroy(like);
}

static struct dlr_entry* dlr_get_mssql(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql, *like;
    DBPoolConn *pconn;
    List *result = NULL, *row;
    struct dlr_entry *res = NULL;

    pconn = dbpool_conn_consume(pool);
    if (pconn == NULL) /* should not happens, but sure is sure */
        return NULL;

    if (dst)
        like = octstr_format("AND %S LIKE '%%%S'", fields->field_dst, dst);
    else
        like = octstr_imm("");

    sql = octstr_format("SELECT %S, %S, %S, %S, %S, %S FROM %S WHERE %S='%S'"
          " AND %S='%S' %S", fields->field_mask, fields->field_serv,
          fields->field_url, fields->field_src, fields->field_dst,
          fields->field_boxc, fields->table, fields->field_smsc, smsc,
          fields->field_ts, ts, like);

#if defined(DLR_TRACE)
    debug("dlr.mssql", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    if (dbpool_conn_select(pconn, sql, NULL, &result) != 0) {
        octstr_destroy(sql);
        dbpool_conn_produce(pconn);
        return NULL;
    }
    octstr_destroy(sql);
    octstr_destroy(like);
    dbpool_conn_produce(pconn);

#define LO2CSTR(r, i) octstr_get_cstr(gwlist_get(r, i))

    if (gwlist_len(result) > 0) {
        row = gwlist_extract_first(result);
        res = dlr_entry_create();
        gw_assert(res != NULL);
        res->mask = atoi(LO2CSTR(row,0));
        res->service = octstr_create(LO2CSTR(row, 1));
        res->url = octstr_create(LO2CSTR(row,2));
        res->source = octstr_create(LO2CSTR(row, 3));
        res->destination = octstr_create(LO2CSTR(row, 4));
        res->boxc_id = octstr_create(LO2CSTR(row, 5));
        gwlist_destroy(row, octstr_destroy_item);
        res->smsc = octstr_duplicate(smsc);
    }
    gwlist_destroy(result, NULL);

#undef LO2CSTR

    return res;
}

static void dlr_update_mssql(const Octstr *smsc, const Octstr *ts, const Octstr *dst, int status)
{
    Octstr *sql, *like;
    DBPoolConn *pconn;
    int res;

    debug("dlr.mssql", 0, "updating DLR status in database");

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL)
        return;

    if (dst)
        like = octstr_format("AND %S LIKE '%%%S'", fields->field_dst, dst);
    else
        like = octstr_imm("");

    sql = octstr_format("SET ROWCOUNT 1\nUPDATE %S SET %S=%d WHERE %S='%S' "
        "AND %S='%S' %S\nSET ROWCOUNT 0",
        fields->table, fields->field_status, status, fields->field_smsc, smsc,
        fields->field_ts, ts, like);

#if defined(DLR_TRACE)
    debug("dlr.mssql", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    if ((res = dbpool_conn_update(pconn, sql, NULL)) == -1)
        error(0, "DLR: MSSQL: Error while updating dlr entry for DST<%s>", octstr_get_cstr(dst));
    else if (!res)
        warning(0, "DLR: MSSQL: No dlr found to update for DST<%s> (status: %d)", octstr_get_cstr(dst), status);

    dbpool_conn_produce(pconn);
    octstr_destroy(sql);
    octstr_destroy(like);
}

static void dlr_flush_mssql (void)
{
    Octstr *sql;
    DBPoolConn *pconn;
    int rows;

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL)
        return;

    sql = octstr_format("DELETE FROM %S", fields->table);
#if defined(DLR_TRACE)
    debug("dlr.mssql", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    rows = dbpool_conn_update(pconn, sql, NULL);
    if (rows == -1)
        error(0, "DLR: MSSQL: Error while flushing dlr entries from database");
    else
        debug("dlr.mssql", 0, "Flushing %d DLR entries from database", rows);
    dbpool_conn_produce(pconn);
    octstr_destroy(sql);
}

static struct dlr_storage handles = {
    .type = "mssql",
    .dlr_messages = dlr_messages_mssql,
    .dlr_shutdown = dlr_shutdown_mssql,
    .dlr_add = dlr_add_mssql,
    .dlr_get = dlr_get_mssql,
    .dlr_remove = dlr_remove_mssql,
    .dlr_update = dlr_update_mssql,
    .dlr_flush = dlr_flush_mssql
};

struct dlr_storage *dlr_init_mssql(Cfg *cfg)
{
    CfgGroup *grp;
    List *grplist;
    long pool_size;
    DBConf *db_conf = NULL;
    Octstr *id, *username, *password, *server, *database;
    int found;

    if ((grp = cfg_get_single_group(cfg, octstr_imm("dlr-db"))) == NULL)
        panic(0, "DLR: MSSQL: group 'dlr-db' is not specified!");

    if (!(id = cfg_get(grp, octstr_imm("id"))))
       panic(0, "DLR: MSSQL: directive 'id' is not specified!");

    /* initialize database fields */
    fields = dlr_db_fields_create(grp);
    gw_assert(fields != NULL);

    grplist = cfg_get_multi_group(cfg, octstr_imm("mssql-connection"));
    found = 0;
    while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
        Octstr *p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, id) == 0) {
            found = 1;
        }
        if (p != NULL) 
            octstr_destroy(p);
        if (found == 1) 
            break;
    }
    gwlist_destroy(grplist, NULL);

    if (found == 0)
        panic(0, "DLR: MSSQL: connection settings for id '%s' are not specified!",
              octstr_get_cstr(id));

    username = cfg_get(grp, octstr_imm("username"));
    password = cfg_get(grp, octstr_imm("password"));
    server = cfg_get(grp, octstr_imm("server"));
    database = cfg_get(grp, octstr_imm("database"));
    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1)
        pool_size = 1;

    if (username == NULL || password == NULL || server == NULL || database == NULL) {
        panic(0, "DLR: MSSQL: connection settings missing for id '%s'. "
              "Please check you configuration.", octstr_get_cstr(id));
    }
    /* ok we are ready to create dbpool */
    db_conf = gw_malloc(sizeof(*db_conf));
    db_conf->mssql = gw_malloc(sizeof(MSSQLConf));

    db_conf->mssql->username = username;
    db_conf->mssql->password = password;
    db_conf->mssql->server = server;
    db_conf->mssql->database = database;

    pool = dbpool_create(DBPOOL_MSSQL, db_conf, pool_size);
    gw_assert(pool != NULL);

    if (dbpool_conn_count(pool) == 0)
        panic(0, "DLR: MSSQL: Could not establish mssql connection(s).");

    octstr_destroy(id);

    return &handles;
}
#else
/* no mssql support build in */
struct dlr_storage *dlr_init_mssql(Cfg *cfg)
{
    return NULL;
}
#endif /* HAVE_MSSQL */
