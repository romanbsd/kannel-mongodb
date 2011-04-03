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

/*
 * dbpool_mongodb.c - implement MongoDB operations for generic database connection pool
 *
 */

#ifdef HAVE_MONGODB
#include <mongo/mongo.h>

/* Our pointer to the configuration */
static MongoDBConf *mongo_conf = NULL;

static void *mongodb_open_conn(const DBConf *db_conf)
{
    MongoDBConf *conf = db_conf->mongodb;
    mongo_connection *conn; /* ptr */
    mongo_connection_options opts[1];
    mongo_conn_return status;
    mongo_conf = conf;
/*
    octstr_get_cstr(conf->username),
    octstr_get_cstr(conf->password),
    octstr_get_cstr(conf->database),
*/
    conn = gw_malloc(sizeof(mongo_connection));
    gw_assert(conn != NULL);

    strcpy(opts->host, octstr_get_cstr(conf->host));
    opts->port = conf->port;
    info(0, "MongoDB: connecting to %s:%u", opts->host, opts->port);

    status = mongo_connect(conn, opts);

    switch (status) {
    case mongo_conn_success:
        info(0, "MongoDB: connected");
        break;
    case mongo_conn_bad_arg:
        error(0, "MongoDB: bad arguments");
        goto failed;
    case mongo_conn_no_socket:
        error(0, "MongoDB: no socket");
        goto failed;
    case mongo_conn_fail:
        error(0, "MongoDB: connection failed");
        goto failed;
    case mongo_conn_not_master:
        error(0, "MongoDB: not master");
        goto failed;
    }

/*
    if (conf->username && conf->password &&
        !mongo_cmd_authenticate(conn, octstr_get_cstr(conf->database), octstr_get_cstr(conf->username), octstr_get_cstr(conf->password))) {
        error(0, "MongoDB: authentication failed");
        goto failed;
    }
*/

    return conn;

failed:
    if (conn != NULL) {
        mongo_destroy(conn);
        gw_free(conn);
    }
    return NULL;
}

/* Close connection and deallocate */
static void mongodb_close_conn(void *conn)
{
    if (conn == NULL) {
        return;
    }
    mongo_destroy((mongo_connection*)conn);
    gw_free(conn);
}

static bson_bool_t mongodb_cmd_ping(mongo_connection *conn, const char *db)
{
    bson out;
    bson_bool_t res;
    bson_iterator it;

    memset(&out, 0, sizeof(bson));
    res = mongo_simple_int_command(conn, db, "ping", 1, &out);
    if (res) {
        if (bson_find(&it, &out, "ok") == bson_eoo) {
            res = 0;
        } else {
            res = bson_iterator_bool(&it);
        }
    }

    bson_destroy(&out);
    return res;
}

/* Check if the connection is alive and usable */
static int mongodb_check_conn(void *mconn)
{
    int res = 0;
    mongo_connection *conn = (mongo_connection*)mconn;

    if (conn == NULL || mongo_conf == NULL) {
        return -1;
    }

    if (!conn->connected) {
        return -1;
    }

    MONGO_TRY {
        if (!mongodb_cmd_ping(conn, octstr_get_cstr(mongo_conf->database))) {
            res = -1;
        }
    } MONGO_CATCH {
        error(0, "MongoDB: mongodb_check_conn failed!");
        res = -1;
    }

    return res;
}

/* Why is it needed? */
static int mongodb_select(void *conn, const Octstr *sql, List *binds, List **res)
{
    gw_assert(0);
    return 0;
}

/* Why is it needed? */
static int mongodb_update(void *conn, const Octstr *stmt, List *binds)
{
    gw_assert(0);
    return 0;
}

/* Free memory allocated by MongoDB configuration */
static void mongodb_conf_destroy(DBConf *db_conf)
{
    MongoDBConf *conf = db_conf->mongodb;

    octstr_destroy(conf->host);
    octstr_destroy(conf->username);
    octstr_destroy(conf->password);
    octstr_destroy(conf->database);

    gw_free(conf);
    gw_free(db_conf);
    mongo_conf = NULL;
}

static struct db_ops mongodb_ops = {
    .open = mongodb_open_conn,
    .close = mongodb_close_conn,
    .check = mongodb_check_conn,
    .conf_destroy = mongodb_conf_destroy,
    .update = mongodb_update,
    .select = mongodb_select
};

#endif
