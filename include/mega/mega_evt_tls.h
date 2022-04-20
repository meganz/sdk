//%LICENSE////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2015 Devchandra M. Leishangthem (dlmeetei at gmail dot com)
//
// Distributed under the MIT License (See accompanying file LICENSE)
//
//////////////////////////////////////////////////////////////////////////
//
//%///////////////////////////////////////////////////////////////////////////

#ifndef EVT_TLS_H
#define EVT_TLS_H

#ifdef USE_OPENSSL
#define ENABLE_EVT_TLS 1
#endif

#ifdef ENABLE_EVT_TLS

#ifdef __cplusplus
extern "C" {
#endif


#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/conf.h>
#include "mega_evt_queue.h"

#ifdef _WIN32
#if !defined(_SSIZE_T_) && !defined(_SSIZE_T_DEFINED)
typedef intptr_t ssize_t;
# define _SSIZE_T_
# define _SSIZE_T_DEFINED
#endif
#endif

typedef struct evt_tls_s evt_tls_t;

//callback used for handshake completion notificat6ion
//common for both client and server role
typedef void (*evt_handshake_cb)(evt_tls_t *con, int status);
typedef void (*evt_read_cb)(evt_tls_t *con, char *buf, int size);
typedef void (*evt_write_cb)(evt_tls_t *con, int status);
typedef void (*evt_close_cb)(evt_tls_t *con, int status);

typedef int (*net_wrtr)(evt_tls_t *tls, void *edata, int len);
typedef int (*net_rdr)(evt_tls_t *tls, void *edata, int len);


/*
 * The TLS context, similar to openSSL's SSl_CTX
*/

typedef struct evt_ctx_s
{
    //find better place for it , should be one time init
    SSL_CTX *ctx;

    //is cert set
    int cert_set;

    //is key set
    int key_set;

    //flag to signify if ssl error has occured
    int ssl_err_;

    //list of live connections created from this ctx
    void *live_con[2];

    //function used to updating peer with SSL data
    net_wrtr writer;

    //function for reading network data and feeding to evt
    net_rdr reader;

} evt_ctx_t;

struct evt_tls_s {

    void    *data;
    //Our BIO, all IO should be through this
    BIO     *app_bio;
    SSL     *ssl;

    //this can be changed per connections
    net_wrtr writer;
    net_rdr reader;

    //callbacks
    evt_handshake_cb hshake_cb;
    evt_read_cb read_cb;
    evt_write_cb write_cb;
    evt_close_cb close_cb;

    //back handle to parent
    evt_ctx_t *evt_ctx;

    QUEUE q;
    BIO     *ssl_bio; //the ssl BIO used only by openSSL
};


//supported TLS operation
enum tls_op_type {
    EVT_TLS_OP_HANDSHAKE
   ,EVT_TLS_OP_READ
   ,EVT_TLS_OP_WRITE
   ,EVT_TLS_OP_SHUTDOWN
};

/*configure the tls state machine */
int evt_ctx_init(evt_ctx_t *tls);

/*configure the tls state machine
This apart from configuring state machine also set up cert and key */
int evt_ctx_init_ex(evt_ctx_t *tls, const char *crtf, const char *key);

/* set the certifcate and key in orderi. This need more breakup */

int evt_ctx_set_crt_key(evt_ctx_t *tls, const char *crtf, const char *key);

/* test if the certificate is set*/
int evt_ctx_is_crtf_set(evt_ctx_t *t);

/* test if the key is set */
int evt_ctx_is_key_set(evt_ctx_t *t);

/*get a new async tls endpoint from tls engine */
evt_tls_t *evt_ctx_get_tls(evt_ctx_t *d_eng);

/*evt-tls is based on BIO pair wherein user takes control of network io
writer(tested) and reader(currently untested) is responsible for networt io.
This set up the writer and reader which is inherited by all endpoints
*/
void evt_ctx_set_writer(evt_ctx_t *ctx, net_wrtr my_writer);
void evt_ctx_set_reader(evt_ctx_t *ctx, net_rdr my_reader);
void evt_ctx_set_nio(evt_ctx_t *ctx, net_rdr my_reader, net_wrtr my_writer);

/*clean up the resources held by async tls engine, This also closes endpoints
if any left */
void evt_ctx_free(evt_ctx_t *ctx);


/*entry point to the tls world, Call this function whenever network read happen
Experimental state with network reader concept, but this is tested*/
int evt_tls_feed_data(evt_tls_t *c, void *data, int sz);

/*set up the writer and reader for this particular endpoint*/
void evt_tls_set_writer(evt_tls_t *tls, net_wrtr my_writer);
void evt_tls_set_reader(evt_tls_t *tls, net_rdr my_reader);

/*Check if handshake is over, return 1 if handshake is done otherwise 0 */
int evt_tls_is_handshake_over(const evt_tls_t *evt);

/*Perform a handshake for client role endpoint, equivalent of `SSL_connect`
Upon completion, `evt_handshake_cb is called, status == 0 for failure and
1 otherwise */
int evt_tls_connect(evt_tls_t *con, evt_handshake_cb cb);

/*Perform a handshake for server role endpoint, equivalent of `SSL_accept`
Upon completion, `evt_handshake_cb is called, status == 0 for failure and
1 otherwise */
int evt_tls_accept( evt_tls_t *tls, evt_handshake_cb cb);


/*Perform wrapping of text and do network write, `evt_write_cb` is called on
completion and status is used for status */
int evt_tls_write(evt_tls_t *c, void *msg, size_t str_len, evt_write_cb on_write);

/*Perform a unwrapping of network received data, equivalent of `SSL_read` and
`evt_read_cb is called on completion */
int evt_tls_read(evt_tls_t *c, evt_read_cb on_read );
/* equivalent of SSL_shutwdown, This performs Two-way SSL_dhutdown */
int evt_tls_close(evt_tls_t *c, evt_close_cb cls);

/*XXX: should not be API, should be performed by evt_tls_close */
int evt_tls_free(evt_tls_t *tls);


/******************************************************************************
SSL helper API
******************************************************************************/

//openssl>=1.0.2 has SSL_is_server API to check if the ssl connection is server.
//Older versions does not have this function. Hence this function is introduced.

enum evt_endpt_t {
    ENDPT_IS_CLIENT
   ,ENDPT_IS_SERVER
};
typedef enum evt_endpt_t evt_endpt_t;

/*Tells if the tls endpoint is client or server */
evt_endpt_t evt_tls_get_role(const evt_tls_t *t);

/* set role to endpoint either server role or client role */
void evt_tls_set_role(evt_tls_t *t, enum evt_endpt_t role);

/*Gives the ptr to SSL_CTX usable raw openSSL programming */
SSL_CTX *evt_get_SSL_CTX(const evt_ctx_t *ctx);

/*Gives the ssl usable for doing raw OpenSSL programming */
SSL *evt_get_ssl(const evt_tls_t *tls);


/*check if incoming data is TLS clientHello.
return 1 if the stream is TLS and 0 otherwise
*/
int evt_is_tls_stream(const char *bfr, const ssize_t nrd);

#ifdef __cplusplus
}
#endif

#endif

#endif //define EVT_TLS_H
