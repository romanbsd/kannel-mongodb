/* Copyright (c) 2011 by Roman Shterenzon

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "gwlib/gwlib.h"
#include "gwlib/dbpool.h"
#include "dlr_p.h"

/*
 * dlr_mongodb.c - Implementation of handling delivery reports (DLRs)
 *                 for MongoDB
 *
 * Requires MongoDB C driver:
 * http://github.com/mongodb/mongo-c-driver
 */
#ifdef HAVE_MONGODB
#include <mongo/mongo.h>

/*
 * Our connection pool to mongodb.
 */
static DBPool *pool = NULL;

/*
 * Document fields, which we are using.
 */
static struct dlr_db_fields *fields = NULL;

/* Database and table */
static char *mongodb_database = NULL;
static char *mongodb_table = NULL;

/* Namespace which will be used on MongoDB */
static char *mongodb_namespace = NULL;

static void mongodb_error(const char *method, mongo_exception_type type)
{
    error(0, "MongoDB: %s: %s", method, type == MONGO_EXCEPT_NETWORK ? "network error" : "error in find");
}

/* Create index on smsc and ts fields, as these are used for retrieving the DLR */
static void dlr_mongodb_ensure_index(void)
{
    DBPoolConn *pconn;
    mongo_connection *conn = NULL;
    bson_buffer bb;
    bson key;

    pconn = dbpool_conn_consume(pool);
    if (pconn == NULL) {
        return;
    }
    conn = (mongo_connection*)pconn->conn;

    bson_buffer_init(&bb);
    bson_append_int(&bb, octstr_get_cstr(fields->field_smsc), 1);
    bson_append_int(&bb, octstr_get_cstr(fields->field_ts), 1);
    bson_from_buffer(&key, &bb);

    MONGO_TRY {
        mongo_create_index(conn, mongodb_namespace, &key, 0, NULL);
    } MONGO_CATCH {
        mongodb_error("dlr_mongodb_ensure_index", conn->exception.type);
    }

    dbpool_conn_produce(pconn);
    bson_destroy(&key);
}

static void dlr_mongodb_shutdown()
{
    dbpool_destroy(pool);
    dlr_db_fields_destroy(fields);
    mongodb_database = NULL;
    mongodb_table = NULL;
    if (mongodb_namespace) {
        gw_free(mongodb_namespace);
        mongodb_namespace = NULL;
    }
}

/* Add a new DLR entry to MongoDB */
static void dlr_mongodb_add(struct dlr_entry *entry)
{
    DBPoolConn *pconn;
    bson b;
    bson_buffer buf;
    mongo_connection *conn = NULL;

    pconn = dbpool_conn_consume(pool);
    if (pconn == NULL) {
        dlr_entry_destroy(entry);
        return;
    }
    conn = (mongo_connection*)pconn->conn;

    bson_buffer_init(&buf);
    bson_append_new_oid(&buf, "_id");

    bson_append_string(&buf, octstr_get_cstr(fields->field_smsc), octstr_get_cstr(entry->smsc));
    bson_append_string(&buf, octstr_get_cstr(fields->field_ts), octstr_get_cstr(entry->timestamp));
    bson_append_string(&buf, octstr_get_cstr(fields->field_src), octstr_get_cstr(entry->source));
    bson_append_string(&buf, octstr_get_cstr(fields->field_dst), octstr_get_cstr(entry->destination));
    bson_append_string(&buf, octstr_get_cstr(fields->field_serv), octstr_get_cstr(entry->service));
    bson_append_string(&buf, octstr_get_cstr(fields->field_url), octstr_get_cstr(entry->url));
    bson_append_int(&buf, octstr_get_cstr(fields->field_mask), entry->mask);
    bson_append_string(&buf, octstr_get_cstr(fields->field_boxc), octstr_get_cstr(entry->boxc_id));
    bson_append_int(&buf, octstr_get_cstr(fields->field_status), 0);

    bson_from_buffer(&b, &buf);

    /* TODO: namespace support */
    MONGO_TRY {
        mongo_insert(conn, mongodb_namespace, &b);
    } MONGO_CATCH {
        mongodb_error("dlr_mongodb_insert", conn->exception.type);
    }

    dbpool_conn_produce(pconn);

    bson_destroy(&b);
    dlr_entry_destroy(entry);
}

static struct dlr_entry* dlr_mongodb_get(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    DBPoolConn *pconn;
    bson cond, obj;
    bson_buffer cond_buf;
    bson_iterator it;
    struct dlr_entry *res = NULL;
    bson_bool_t found = 0;
    mongo_connection *conn = NULL;

    pconn = dbpool_conn_consume(pool);
    if (pconn == NULL) {
        return NULL;
    }
    conn = (mongo_connection*)pconn->conn;

    bson_buffer_init(&cond_buf);
    bson_append_string(&cond_buf, octstr_get_cstr(fields->field_smsc), octstr_get_cstr(smsc));
    bson_append_string(&cond_buf, octstr_get_cstr(fields->field_ts), octstr_get_cstr(ts));

    if (dst) {
        bson_append_string(&cond_buf, octstr_get_cstr(fields->field_dst), octstr_get_cstr(dst));
    }

    bson_from_buffer(&cond, &cond_buf);

    memset(&obj, 0, sizeof(bson));
    MONGO_TRY {
        found = mongo_find_one(conn, mongodb_namespace, &cond, NULL, &obj);
    } MONGO_CATCH {
        mongodb_error("dlr_mongodb_get", conn->exception.type);
        found = 0;
    }

    if (found) {
        res = dlr_entry_create();
        gw_assert(res != NULL);

        bson_find(&it, &obj, octstr_get_cstr(fields->field_mask));
        res->mask = bson_iterator_int(&it);

        bson_find(&it, &obj, octstr_get_cstr(fields->field_serv));
        res->service = octstr_create(bson_iterator_string(&it));

        bson_find(&it, &obj, octstr_get_cstr(fields->field_url));
        res->url = octstr_create(bson_iterator_string(&it));

        bson_find(&it, &obj, octstr_get_cstr(fields->field_src));
        res->source = octstr_create(bson_iterator_string(&it));

        bson_find(&it, &obj, octstr_get_cstr(fields->field_dst));
        res->destination = octstr_create(bson_iterator_string(&it));

        bson_find(&it, &obj, octstr_get_cstr(fields->field_boxc));
        res->boxc_id = octstr_create(bson_iterator_string(&it));

        bson_find(&it, &obj, octstr_get_cstr(fields->field_smsc));
        res->smsc = octstr_create(bson_iterator_string(&it));
    }

    dbpool_conn_produce(pconn);
    bson_destroy(&cond);
    bson_destroy(&obj);

    return res;
}

/* Update DLR */
static void dlr_mongodb_update(const Octstr *smsc, const Octstr *ts, const Octstr *dst, int status)
{
    DBPoolConn *pconn;
    bson cond, op;
    bson_buffer cond_buf, op_buf;
    mongo_connection *conn = NULL;

    pconn = dbpool_conn_consume(pool);
    if (pconn == NULL) {
        return;
    }
    conn = (mongo_connection*)pconn->conn;

    bson_buffer_init(&cond_buf);
    bson_append_string(&cond_buf, octstr_get_cstr(fields->field_smsc), octstr_get_cstr(smsc));
    bson_append_string(&cond_buf, octstr_get_cstr(fields->field_ts), octstr_get_cstr(ts));

    if (dst) {
        bson_append_string(&cond_buf, octstr_get_cstr(fields->field_dst), octstr_get_cstr(dst));
    }

    bson_from_buffer(&cond, &cond_buf);

    bson_buffer_init(&op_buf);
    {
        bson_buffer *sub = bson_append_start_object(&op_buf, "$set");
        bson_append_int(sub, octstr_get_cstr(fields->field_status), status);
        bson_append_finish_object(sub);
    }
    bson_from_buffer(&op, &op_buf);

    MONGO_TRY {
        mongo_update(conn, mongodb_namespace, &cond, &op, 0);
    } MONGO_CATCH {
        mongodb_error("dlr_mongodb_update", conn->exception.type);
    }

    dbpool_conn_produce(pconn);
    bson_destroy(&cond);
    bson_destroy(&op);
}

/* Remove DLR */
static void dlr_mongodb_remove(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    DBPoolConn *pconn;
    bson cond;
    bson_buffer cond_buf;
    mongo_connection *conn = NULL;

    pconn = dbpool_conn_consume(pool);
    if (pconn == NULL) {
        return;
    }
    conn = (mongo_connection*)pconn->conn;

    bson_buffer_init(&cond_buf);
    bson_append_string(&cond_buf, octstr_get_cstr(fields->field_smsc), octstr_get_cstr(smsc));
    bson_append_string(&cond_buf, octstr_get_cstr(fields->field_ts), octstr_get_cstr(ts));

    if (dst) {
        bson_append_string(&cond_buf, octstr_get_cstr(fields->field_dst), octstr_get_cstr(dst));
    }

    bson_from_buffer(&cond, &cond_buf);

    MONGO_TRY {
        mongo_remove(conn, mongodb_namespace, &cond);
    } MONGO_CATCH {
        mongodb_error("dlr_mongodb_remove", conn->exception.type);
    }

    dbpool_conn_produce(pconn);
    bson_destroy(&cond);
}

/* Number of DLRs in our namespace */
static long dlr_mongodb_messages(void)
{
    DBPoolConn *pconn;
    long count = 0;
    mongo_connection *conn = NULL;

    pconn = dbpool_conn_consume(pool);
    if (pconn == NULL) {
        return -1;
    }
    conn = (mongo_connection*)pconn->conn;

    /* TODO: namespace support */
    MONGO_TRY {
        count = mongo_count(conn, mongodb_database, mongodb_table, NULL);
    } MONGO_CATCH {
        mongodb_error("dlr_mongodb_messages", conn->exception.type);
    }

    dbpool_conn_produce(pconn);
    return count;
}

/* Remove all DLRs from our namespace */
static void dlr_mongodb_flush(void)
{
    DBPoolConn *pconn;
    bson b;
    mongo_connection *conn = NULL;

    pconn = dbpool_conn_consume(pool);
    if (pconn == NULL) {
        return;
    }
    conn = (mongo_connection*)pconn->conn;

    MONGO_TRY {
        mongo_remove(conn, mongodb_namespace, bson_empty(&b));
    } MONGO_CATCH {
        mongodb_error("dlr_mongodb_flush", conn->exception.type);
    }
    dbpool_conn_produce(pconn);
}

static struct dlr_storage handles = {
    .type = "mongodb",
    .dlr_add = dlr_mongodb_add,
    .dlr_get = dlr_mongodb_get,
    .dlr_update = dlr_mongodb_update,
    .dlr_remove = dlr_mongodb_remove,
    .dlr_shutdown = dlr_mongodb_shutdown,
    .dlr_messages = dlr_mongodb_messages,
    .dlr_flush = dlr_mongodb_flush
};

struct dlr_storage *dlr_init_mongodb(Cfg *cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *mongodb_host, *mongodb_user, *mongodb_pass, *mongodb_db, *mongodb_id;
    long pool_size;
    long mongodb_port = 27017;
    DBConf *db_conf = NULL;
    int found;

    if ((grp = cfg_get_single_group(cfg, octstr_imm("dlr-db"))) == NULL) {
        panic(0, "DLR: MongoDB: group 'dlr-db' is not specified!");
    }

    if (!(mongodb_id = cfg_get(grp, octstr_imm("id")))) {
        panic(0, "DLR: MongoDB: directive 'id' is not specified!");
    }

    /* initialize database fields */
    fields = dlr_db_fields_create(grp);
    gw_assert(fields != NULL);

    grplist = cfg_get_multi_group(cfg, octstr_imm("mongodb-connection"));
    found = 0;
    while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
        Octstr *p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, mongodb_id) == 0) {
            found = 1;
        }
        if (p != NULL) {
            octstr_destroy(p);
        }
        if (found == 1) {
            break;
        }
    }
    gwlist_destroy(grplist, NULL);

    if (found == 0) {
        panic(0, "DLR: MongoDB: connection settings for id '%s' are not specified!",
              octstr_get_cstr(mongodb_id));
    }

    if (!(mongodb_host = cfg_get(grp, octstr_imm("host")))) {
   	    panic(0, "DLR: MongoDB: directive 'host' is not specified!");
    }

    if (!(mongodb_db = cfg_get(grp, octstr_imm("database")))) {
   	    panic(0, "DLR: MongoDB: directive 'database' is not specified!");
    }
    /* Keep a global reference to the database and table */
    mongodb_database = octstr_get_cstr(mongodb_db);
    mongodb_table = octstr_get_cstr(fields->table);
    mongodb_namespace = (char *)gw_malloc(strlen(mongodb_database) + strlen(mongodb_table) + 2); /* . and \0 */
    sprintf(mongodb_namespace, "%s.%s", mongodb_database, mongodb_table);

    mongodb_user = cfg_get(grp, octstr_imm("username"));
    mongodb_pass = cfg_get(grp, octstr_imm("password"));
   	    
    cfg_get_integer(&mongodb_port, grp, octstr_imm("port"));  /* optional */
    
    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1) {
        pool_size = 1;
    }

    /* ok we are ready to create dbpool */
    db_conf = gw_malloc(sizeof(*db_conf));
    gw_assert(db_conf != NULL);

    db_conf->mongodb = gw_malloc(sizeof(MongoDBConf));
    gw_assert(db_conf->mongodb != NULL);

    db_conf->mongodb->host = mongodb_host;
    db_conf->mongodb->port = mongodb_port;
    db_conf->mongodb->username = mongodb_user;
    db_conf->mongodb->password = mongodb_pass;
    db_conf->mongodb->database = mongodb_db;

    pool = dbpool_create(DBPOOL_MONGODB, db_conf, pool_size);
    gw_assert(pool != NULL);

    if (dbpool_conn_count(pool) == 0) {
        panic(0, "DLR: MongoDB: Could not establish connection(s).");
    }

    dlr_mongodb_ensure_index();

    octstr_destroy(mongodb_id);

    return &handles;
}
#else
/*
 * Return NULL , so we point dlr-core that we were
 * not compiled in.
 */
struct dlr_storage *dlr_init_mongodb(Cfg *cfg)
{
	return NULL;
}
#endif
