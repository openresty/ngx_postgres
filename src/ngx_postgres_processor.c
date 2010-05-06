/*
 * Copyright (c) 2010, FRiCKLE Piotr Sikora <info@frickle.com>
 * Copyright (c) 2009-2010, Xiaozhe Wang <chaoslawful@gmail.com>
 * Copyright (c) 2009-2010, Yichun Zhang <agentzh@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define DDEBUG 0
#include "ngx_postgres_ddebug.h"
#include "ngx_postgres_output.h"
#include "ngx_postgres_processor.h"
#include "ngx_postgres_util.h"


void
ngx_postgres_process_events(ngx_http_request_t *r)
{
    ngx_postgres_upstream_peer_data_t  *pgdt;
    ngx_connection_t                   *pgxc;
    ngx_http_upstream_t                *u;
    ngx_int_t                           rc;

    dd("entering");

    u = r->upstream;
    pgxc = u->peer.connection;
    pgdt = u->peer.data;

    if (!ngx_postgres_upstream_is_my_peer(&u->peer)) {
        ngx_log_error(NGX_LOG_ERR, pgxc->log, 0,
                      "postgres: trying to connect to something that"
                      " isn't PostgreSQL database");

        goto failed;
    }

    switch (pgdt->state) {
    case state_db_connect:
        dd("state_db_connect");
        rc = ngx_postgres_upstream_connect(r, pgxc, pgdt);
        break;
    case state_db_send_query:
        dd("state_db_send_query");
        rc = ngx_postgres_upstream_send_query(r, pgxc, pgdt);
        break;
    case state_db_get_result:
        dd("state_db_get_result");
        rc = ngx_postgres_upstream_get_result(r, pgxc, pgdt);
        break;
    case state_db_get_ack:
        dd("state_db_get_ack");
        rc = ngx_postgres_upstream_get_ack(r, pgxc, pgdt);
        break;
    case state_db_idle:
        dd("state_db_idle, re-using keepalive connection");
        pgxc->log->action = "sending query to PostgreSQL database";
        pgdt->state = state_db_send_query;
        rc = ngx_postgres_upstream_send_query(r, pgxc, pgdt);
        break;
    default:
        dd("unknown state:%d", pgdt->state);
        ngx_log_error(NGX_LOG_ERR, pgxc->log, 0,
                      "postgres: unknown state:%d", pgdt->state);

        goto failed;
    }

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        ngx_postgres_upstream_finalize_request(r, u, rc);
    } else if (rc == NGX_ERROR) {
        goto failed;
    }

    dd("returning");
    return;

failed:
    ngx_postgres_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);

    dd("returning");
}

ngx_int_t
ngx_postgres_upstream_connect(ngx_http_request_t *r, ngx_connection_t *pgxc,
    ngx_postgres_upstream_peer_data_t *pgdt)
{
    PostgresPollingStatusType  pgrc;

    dd("entering");

    pgrc = PQconnectPoll(pgdt->pgconn);

    if (pgrc == PGRES_POLLING_READING || pgrc == PGRES_POLLING_WRITING) {

        /*
         * Fix for Linux issue found by chaoslawful (via agentzh):
         * "According to the source of libpq (around fe-connect.c:1215), during
         *  the state switch from CONNECTION_STARTED to CONNECTION_MADE, there's
         *  no socket read/write operations (just a plain getsockopt call and a
         *  getsockname call). Therefore, for edge-triggered event model, we
         *  have to call PQconnectPoll one more time (immediately) when we see
         *  CONNECTION_MADE is returned, or we're very likely to wait for a
         *  writable event that has already appeared and will never appear
         *  again :)"
         */
        if (PQstatus(pgdt->pgconn) == CONNECTION_MADE) {
            dd("re-polling on connection made");

            pgrc = PQconnectPoll(pgdt->pgconn);

            if (pgrc == PGRES_POLLING_READING || pgrc == PGRES_POLLING_WRITING)
            {
                dd("returning NGX_AGAIN");
                return NGX_AGAIN;
            }
        }

#if defined(DDEBUG) && (DDEBUG)
        switch (PQstatus(pgdt->pgconn)) {
        case CONNECTION_NEEDED:
             dd("connecting (waiting for connect()))");
             break;
        case CONNECTION_STARTED:
             dd("connecting (waiting for connection to be made)");
             break;
        case CONNECTION_MADE:
             dd("connecting (connection established)");
             break;
        case CONNECTION_AWAITING_RESPONSE:
             dd("connecting (credentials sent, waiting for response)");
             break;
        case CONNECTION_AUTH_OK:
             dd("connecting (authenticated)");
             break;
        case CONNECTION_SETENV:
             dd("connecting (negotiating envinroment)");
             break;
        case CONNECTION_SSL_STARTUP:
             dd("connecting (negotiating SSL)");
             break;
        default:
             /*
              * This cannot happen, PQconnectPoll would return
              * PGRES_POLLING_FAILED in that case.
              */
             dd("connecting (unknown state: %d)", (int) PQstatus(pgdt->pgconn));

             dd("returning NGX_ERROR");
             return NGX_ERROR;
        }
#endif /* DDEBUG */

        dd("returning NGX_AGAIN");
        return NGX_AGAIN;
    }

    /* remove connection timeout from new connection */
    if (pgxc->write->timer_set) {
        ngx_del_timer(pgxc->write);
    }

    if (pgrc != PGRES_POLLING_OK) {
        dd("connection failed");
        ngx_log_error(NGX_LOG_ERR, pgxc->log, 0,
                      "postgres: connection failed: %d: %s in upstream \"%V\"",
                      (int) pgrc, PQerrorMessage(pgdt->pgconn),
                      &r->upstream->peer.name);

       dd("returning NGX_ERROR");
       return NGX_ERROR;
    }

    dd("connected successfully");

    pgxc->log->action = "sending query to PostgreSQL database";
    pgdt->state = state_db_send_query;

    dd("returning");
    return ngx_postgres_upstream_send_query(r, pgxc, pgdt);
}

ngx_int_t
ngx_postgres_upstream_send_query(ngx_http_request_t *r, ngx_connection_t *pgxc,
    ngx_postgres_upstream_peer_data_t *pgdt)
{
    ngx_int_t   pgrc;
    u_char     *query;

    dd("entering");

    query = ngx_palloc(r->pool, pgdt->query.len + 1);
    if (query == NULL) {
        dd("returning NGX_ERROR");
        return NGX_ERROR;
    }

    (void) ngx_snprintf(query, pgdt->query.len, "%V", &pgdt->query);
    query[pgdt->query.len] = '\0';

    dd("sending query: %s", query);

    pgrc = PQsendQuery(pgdt->pgconn, (const char *) query);
    if (pgrc == 0) {
        //dd("query sent failed: %s", PQerrorMessage(pgdt->pgconn));
        dd("returning NGX_ERROR");
        return NGX_ERROR;
    }

    /* set result timeout */
    ngx_add_timer(pgxc->read, r->upstream->conf->read_timeout);

    dd("query sent successfully");

    pgxc->log->action = "waiting for result from PostgreSQL database";
    pgdt->state = state_db_get_result;

    dd("returning NGX_DONE");
    return NGX_DONE;
}

ngx_int_t
ngx_postgres_upstream_get_result(ngx_http_request_t *r, ngx_connection_t *pgxc,
    ngx_postgres_upstream_peer_data_t *pgdt)
{
    ExecStatusType   pgrc;
    PGresult        *res;
    ngx_int_t        rc;

    dd("entering");

    /* remove connection timeout from re-used keepalive connection */
    if (pgxc->write->timer_set) {
        ngx_del_timer(pgxc->write);
    }

    if (!PQconsumeInput(pgdt->pgconn)) {
        dd("returning NGX_ERROR");
        return NGX_ERROR;
    }

    if (PQisBusy(pgdt->pgconn)) {
        dd("returning NGX_AGAIN");
        return NGX_AGAIN;
    }

    dd("receiving result");

    res = PQgetResult(pgdt->pgconn);
    if (res == NULL) {
        dd("returning NGX_ERROR");
        return NGX_ERROR;
    }

    pgrc = PQresultStatus(res);
    if (pgrc == PGRES_FATAL_ERROR) {
        dd("returning NGX_HTTP_INTERNAL_SERVER_ERROR");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
 
    dd("result received successfully, cols:%d rows:%d",
       PQnfields(res), PQntuples(res));

    rc = ngx_postgres_output_rds(r, res);

    PQclear(res);

    if (rc != NGX_DONE) {
        dd("returning NGX_ERROR");
        return NGX_ERROR;
    }

    dd("result processed successfully");

    pgxc->log->action = "waiting for ACK from PostgreSQL database";
    pgdt->state = state_db_get_ack;

    dd("returning");
    return ngx_postgres_upstream_get_ack(r, pgxc, pgdt);
}

ngx_int_t
ngx_postgres_upstream_get_ack(ngx_http_request_t *r, ngx_connection_t *pgxc,
    ngx_postgres_upstream_peer_data_t *pgdt)
{
    PGresult        *res;

    dd("entering");

    if (!PQconsumeInput(pgdt->pgconn)) {
        dd("returning NGX_ERROR");
        return NGX_ERROR;
    }

    if (PQisBusy(pgdt->pgconn)) {
        dd("returning NGX_AGAIN");
        return NGX_AGAIN;
    }

    /* remove result timeout */
    if (pgxc->read->timer_set) {
        ngx_del_timer(pgxc->read);
    }

    dd("receiving ACK (ready for next query)");

    res = PQgetResult(pgdt->pgconn);
    if (res != NULL) {
        PQclear(res);

        dd("returning NGX_HTTP_INTERNAL_SERVER_ERROR");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    dd("ACK received successfully");

    pgxc->log->action = "being idle on PostgreSQL database";
    pgdt->state = state_db_idle;

    dd("returning");
    return ngx_postgres_upstream_done(r, r->upstream, pgdt);
}

ngx_int_t
ngx_postgres_upstream_done(ngx_http_request_t *r, ngx_http_upstream_t *u,
    ngx_postgres_upstream_peer_data_t *pgdt)
{
    dd("entering");

    /* needed for keepalive */
    u->header_sent = 1;
    u->length = 0;
    r->headers_out.status = NGX_HTTP_OK;
    u->headers_in.status_n = NGX_HTTP_OK;

    ngx_postgres_upstream_finalize_request(r, u, NGX_OK);

    dd("returning NGX_DONE");
    return NGX_DONE;
}