/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 *
 * Portions of the attached software ("Contribution") are developed by
 * SUN MICROSYSTEMS, INC., and are contributed to the OpenSSL project.
 *
 * The Contribution is licensed pursuant to the OpenSSL open source
 * license provided above.
 *
 * ECC cipher suite support in OpenSSL originally written by
 * Vipul Gupta and Sumit Gupta of Sun Microsystems Laboratories.
 *
 */
/* ====================================================================
 * Copyright 2005 Nokia. All rights reserved.
 *
 * The portions of the attached software ("Contribution") is developed by
 * Nokia Corporation and is licensed pursuant to the OpenSSL open source
 * license.
 *
 * The Contribution, originally written by Mika Kousa and Pasi Eronen of
 * Nokia Corporation, consists of the "PSK" (Pre-Shared Key) ciphersuites
 * support (see RFC 4279) to OpenSSL.
 *
 * No patent licenses or other rights except those expressly stated in
 * the OpenSSL open source license shall be deemed granted or received
 * expressly, by implication, estoppel, or otherwise.
 *
 * No assurances are provided by Nokia that the Contribution does not
 * infringe the patent or other intellectual property rights of any third
 * party or that the license provides you with all the necessary rights
 * to make use of the Contribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. IN
 * ADDITION TO THE DISCLAIMERS INCLUDED IN THE LICENSE, NOKIA
 * SPECIFICALLY DISCLAIMS ANY LIABILITY FOR CLAIMS BROUGHT BY YOU OR ANY
 * OTHER ENTITY BASED ON INFRINGEMENT OF INTELLECTUAL PROPERTY RIGHTS OR
 * OTHERWISE.
 */

#include <stdio.h>
#include <time.h>
#include "../ssl_locl.h"
#include "statem_locl.h"
#include <openssl/buffer.h>
#include <openssl/rand.h>
#include <openssl/objects.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/dh.h>
#include <openssl/bn.h>
#include <openssl/engine.h>

static MSG_PROCESS_RETURN tls_process_encrypted_extensions(SSL *s, PACKET *pkt);

static ossl_inline int cert_req_allowed(SSL *s);
static int key_exchange_expected(SSL *s);
static int ca_dn_cmp(const X509_NAME *const *a, const X509_NAME *const *b);
static int ssl_cipher_list_to_bytes(SSL *s, STACK_OF(SSL_CIPHER) *sk,
                                    WPACKET *pkt);

/*
 * Is a CertificateRequest message allowed at the moment or not?
 *
 *  Return values are:
 *  1: Yes
 *  0: No
 */
static ossl_inline int cert_req_allowed(SSL *s)
{
    /* TLS does not like anon-DH with client cert */
    if ((s->version > SSL3_VERSION
         && (s->s3->tmp.new_cipher->algorithm_auth & SSL_aNULL))
        || (s->s3->tmp.new_cipher->algorithm_auth & (SSL_aSRP | SSL_aPSK)))
        return 0;

    return 1;
}

/*
 * Should we expect the ServerKeyExchange message or not?
 *
 *  Return values are:
 *  1: Yes
 *  0: No
 */
static int key_exchange_expected(SSL *s)
{
    long alg_k = s->s3->tmp.new_cipher->algorithm_mkey;

    /*
     * Can't skip server key exchange if this is an ephemeral
     * ciphersuite or for SRP
     */
    if (alg_k & (SSL_kDHE | SSL_kECDHE | SSL_kDHEPSK | SSL_kECDHEPSK
                 | SSL_kSRP)) {
        return 1;
    }

    return 0;
}

/*
 * ossl_statem_client_read_transition() encapsulates the logic for the allowed
 * handshake state transitions when a TLS1.3 client is reading messages from the
 * server. The message type that the server has sent is provided in |mt|. The
 * current state is in |s->statem.hand_state|.
 *
 * Return values are 1 for success (transition allowed) and  0 on error
 * (transition not allowed)
 */
static int ossl_statem_client13_read_transition(SSL *s, int mt)
{
    OSSL_STATEM *st = &s->statem;

    /*
     * TODO(TLS1.3): This is still based on the TLSv1.2 state machine. Over time
     * we will update this to look more like real TLSv1.3
     */

    /*
     * Note: There is no case for TLS_ST_CW_CLNT_HELLO, because we haven't
     * yet negotiated TLSv1.3 at that point so that is handled by
     * ossl_statem_client_read_transition()
     */

    switch (st->hand_state) {
    default:
        break;

    case TLS_ST_CR_SRVR_HELLO:
        if (mt == SSL3_MT_ENCRYPTED_EXTENSIONS) {
            st->hand_state = TLS_ST_CR_ENCRYPTED_EXTENSIONS;
            return 1;
        }
        break;

    case TLS_ST_CR_ENCRYPTED_EXTENSIONS:
        if (s->hit) {
            if (mt == SSL3_MT_FINISHED) {
                st->hand_state = TLS_ST_CR_FINISHED;
                return 1;
            }
        } else {
            if (mt == SSL3_MT_CERTIFICATE_REQUEST) {
                st->hand_state = TLS_ST_CR_CERT_REQ;
                return 1;
            }
            if (mt == SSL3_MT_CERTIFICATE) {
                st->hand_state = TLS_ST_CR_CERT;
                return 1;
            }
        }
        break;

    case TLS_ST_CR_CERT_REQ:
        if (mt == SSL3_MT_CERTIFICATE) {
            st->hand_state = TLS_ST_CR_CERT;
            return 1;
        }
        break;

    case TLS_ST_CR_CERT:
        if (mt == SSL3_MT_CERTIFICATE_VERIFY) {
            st->hand_state = TLS_ST_CR_CERT_VRFY;
            return 1;
        }
        break;

    case TLS_ST_CR_CERT_VRFY:
        if (mt == SSL3_MT_FINISHED) {
            st->hand_state = TLS_ST_CR_FINISHED;
            return 1;
        }
        break;

    case TLS_ST_OK:
        if (mt == SSL3_MT_NEWSESSION_TICKET) {
            st->hand_state = TLS_ST_CR_SESSION_TICKET;
            return 1;
        }
        break;
    }

    /* No valid transition found */
    return 0;
}

/*
 * ossl_statem_client_read_transition() encapsulates the logic for the allowed
 * handshake state transitions when the client is reading messages from the
 * server. The message type that the server has sent is provided in |mt|. The
 * current state is in |s->statem.hand_state|.
 *
 * Return values are 1 for success (transition allowed) and  0 on error
 * (transition not allowed)
 */
int ossl_statem_client_read_transition(SSL *s, int mt)
{
    OSSL_STATEM *st = &s->statem;
    int ske_expected;

    /*
     * Note that after a ClientHello we don't know what version we are going
     * to negotiate yet, so we don't take this branch until later
     */
    if (SSL_IS_TLS13(s)) {
        if (!ossl_statem_client13_read_transition(s, mt))
            goto err;
        return 1;
    }

    switch (st->hand_state) {
    default:
        break;

    case TLS_ST_CW_CLNT_HELLO:
        if (mt == SSL3_MT_SERVER_HELLO) {
            st->hand_state = TLS_ST_CR_SRVR_HELLO;
            return 1;
        }

        if (SSL_IS_DTLS(s)) {
            if (mt == DTLS1_MT_HELLO_VERIFY_REQUEST) {
                st->hand_state = DTLS_ST_CR_HELLO_VERIFY_REQUEST;
                return 1;
            }
        }
        break;

    case TLS_ST_CR_SRVR_HELLO:
        if (s->hit) {
            if (s->ext.ticket_expected) {
                if (mt == SSL3_MT_NEWSESSION_TICKET) {
                    st->hand_state = TLS_ST_CR_SESSION_TICKET;
                    return 1;
                }
            } else if (mt == SSL3_MT_CHANGE_CIPHER_SPEC) {
                st->hand_state = TLS_ST_CR_CHANGE;
                return 1;
            }
        } else {
            if (SSL_IS_DTLS(s) && mt == DTLS1_MT_HELLO_VERIFY_REQUEST) {
                st->hand_state = DTLS_ST_CR_HELLO_VERIFY_REQUEST;
                return 1;
            } else if (s->version >= TLS1_VERSION
                       && s->ext.session_secret_cb != NULL
                       && s->session->ext.tick != NULL
                       && mt == SSL3_MT_CHANGE_CIPHER_SPEC) {
                /*
                 * Normally, we can tell if the server is resuming the session
                 * from the session ID. EAP-FAST (RFC 4851), however, relies on
                 * the next server message after the ServerHello to determine if
                 * the server is resuming.
                 */
                s->hit = 1;
                st->hand_state = TLS_ST_CR_CHANGE;
                return 1;
            } else if (!(s->s3->tmp.new_cipher->algorithm_auth
                         & (SSL_aNULL | SSL_aSRP | SSL_aPSK))) {
                if (mt == SSL3_MT_CERTIFICATE) {
                    st->hand_state = TLS_ST_CR_CERT;
                    return 1;
                }
            } else {
                ske_expected = key_exchange_expected(s);
                /* SKE is optional for some PSK ciphersuites */
                if (ske_expected
                    || ((s->s3->tmp.new_cipher->algorithm_mkey & SSL_PSK)
                        && mt == SSL3_MT_SERVER_KEY_EXCHANGE)) {
                    if (mt == SSL3_MT_SERVER_KEY_EXCHANGE) {
                        st->hand_state = TLS_ST_CR_KEY_EXCH;
                        return 1;
                    }
                } else if (mt == SSL3_MT_CERTIFICATE_REQUEST
                           && cert_req_allowed(s)) {
                    st->hand_state = TLS_ST_CR_CERT_REQ;
                    return 1;
                } else if (mt == SSL3_MT_SERVER_DONE) {
                    st->hand_state = TLS_ST_CR_SRVR_DONE;
                    return 1;
                }
            }
        }
        break;

    case TLS_ST_CR_CERT:
        /*
         * The CertificateStatus message is optional even if
         * |ext.status_expected| is set
         */
        if (s->ext.status_expected && mt == SSL3_MT_CERTIFICATE_STATUS) {
            st->hand_state = TLS_ST_CR_CERT_STATUS;
            return 1;
        }
        /* Fall through */

    case TLS_ST_CR_CERT_STATUS:
        ske_expected = key_exchange_expected(s);
        /* SKE is optional for some PSK ciphersuites */
        if (ske_expected || ((s->s3->tmp.new_cipher->algorithm_mkey & SSL_PSK)
                             && mt == SSL3_MT_SERVER_KEY_EXCHANGE)) {
            if (mt == SSL3_MT_SERVER_KEY_EXCHANGE) {
                st->hand_state = TLS_ST_CR_KEY_EXCH;
                return 1;
            }
            goto err;
        }
        /* Fall through */

    case TLS_ST_CR_KEY_EXCH:
        if (mt == SSL3_MT_CERTIFICATE_REQUEST) {
            if (cert_req_allowed(s)) {
                st->hand_state = TLS_ST_CR_CERT_REQ;
                return 1;
            }
            goto err;
        }
        /* Fall through */

    case TLS_ST_CR_CERT_REQ:
        if (mt == SSL3_MT_SERVER_DONE) {
            st->hand_state = TLS_ST_CR_SRVR_DONE;
            return 1;
        }
        break;

    case TLS_ST_CW_FINISHED:
        if (s->ext.ticket_expected) {
            if (mt == SSL3_MT_NEWSESSION_TICKET) {
                st->hand_state = TLS_ST_CR_SESSION_TICKET;
                return 1;
            }
        } else if (mt == SSL3_MT_CHANGE_CIPHER_SPEC) {
            st->hand_state = TLS_ST_CR_CHANGE;
            return 1;
        }
        break;

    case TLS_ST_CR_SESSION_TICKET:
        if (mt == SSL3_MT_CHANGE_CIPHER_SPEC) {
            st->hand_state = TLS_ST_CR_CHANGE;
            return 1;
        }
        break;

    case TLS_ST_CR_CHANGE:
        if (mt == SSL3_MT_FINISHED) {
            st->hand_state = TLS_ST_CR_FINISHED;
            return 1;
        }
        break;

    case TLS_ST_OK:
        if (mt == SSL3_MT_HELLO_REQUEST) {
            st->hand_state = TLS_ST_CR_HELLO_REQ;
            return 1;
        }
        break;
    }

 err:
    /* No valid transition found */
    ssl3_send_alert(s, SSL3_AL_FATAL, SSL3_AD_UNEXPECTED_MESSAGE);
    SSLerr(SSL_F_OSSL_STATEM_CLIENT_READ_TRANSITION, SSL_R_UNEXPECTED_MESSAGE);
    return 0;
}

/*
 * ossl_statem_client13_write_transition() works out what handshake state to
 * move to next when the TLSv1.3 client is writing messages to be sent to the
 * server.
 */
static WRITE_TRAN ossl_statem_client13_write_transition(SSL *s)
{
    OSSL_STATEM *st = &s->statem;

    /*
     * TODO(TLS1.3): This is still based on the TLSv1.2 state machine. Over time
     * we will update this to look more like real TLSv1.3
     */

    /*
     * Note: There are no cases for TLS_ST_BEFORE or TLS_ST_CW_CLNT_HELLO,
     * because we haven't negotiated TLSv1.3 yet at that point. They are
     * handled by ossl_statem_client_write_transition().
     */
    switch (st->hand_state) {
    default:
        /* Shouldn't happen */
        return WRITE_TRAN_ERROR;

    case TLS_ST_CR_FINISHED:
        st->hand_state = (s->s3->tmp.cert_req != 0) ? TLS_ST_CW_CERT
                                                    : TLS_ST_CW_FINISHED;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_CW_CERT:
        /* If a non-empty Certificate we also send CertificateVerify */
        st->hand_state = (s->s3->tmp.cert_req == 1) ? TLS_ST_CW_CERT_VRFY
                                                    : TLS_ST_CW_FINISHED;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_CW_CERT_VRFY:
        st->hand_state = TLS_ST_CW_FINISHED;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_CR_SESSION_TICKET:
    case TLS_ST_CW_FINISHED:
        st->hand_state = TLS_ST_OK;
        ossl_statem_set_in_init(s, 0);
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_OK:
        /* Just go straight to trying to read from the server */
        return WRITE_TRAN_FINISHED;
    }
}

/*
 * ossl_statem_client_write_transition() works out what handshake state to
 * move to next when the client is writing messages to be sent to the server.
 */
WRITE_TRAN ossl_statem_client_write_transition(SSL *s)
{
    OSSL_STATEM *st = &s->statem;

    /*
     * Note that immediately before/after a ClientHello we don't know what
     * version we are going to negotiate yet, so we don't take this branch until
     * later
     */
    if (SSL_IS_TLS13(s))
        return ossl_statem_client13_write_transition(s);

    switch (st->hand_state) {
    default:
        /* Shouldn't happen */
        return WRITE_TRAN_ERROR;

    case TLS_ST_OK:
        if (!s->renegotiate) {
            /*
             * We haven't requested a renegotiation ourselves so we must have
             * received a message from the server. Better read it.
             */
            return WRITE_TRAN_FINISHED;
        }
        /* Renegotiation - fall through */
    case TLS_ST_BEFORE:
        st->hand_state = TLS_ST_CW_CLNT_HELLO;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_CW_CLNT_HELLO:
        /*
         * No transition at the end of writing because we don't know what
         * we will be sent
         */
        return WRITE_TRAN_FINISHED;

    case DTLS_ST_CR_HELLO_VERIFY_REQUEST:
        st->hand_state = TLS_ST_CW_CLNT_HELLO;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_CR_SRVR_DONE:
        if (s->s3->tmp.cert_req)
            st->hand_state = TLS_ST_CW_CERT;
        else
            st->hand_state = TLS_ST_CW_KEY_EXCH;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_CW_CERT:
        st->hand_state = TLS_ST_CW_KEY_EXCH;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_CW_KEY_EXCH:
        /*
         * For TLS, cert_req is set to 2, so a cert chain of nothing is
         * sent, but no verify packet is sent
         */
        /*
         * XXX: For now, we do not support client authentication in ECDH
         * cipher suites with ECDH (rather than ECDSA) certificates. We
         * need to skip the certificate verify message when client's
         * ECDH public key is sent inside the client certificate.
         */
        if (s->s3->tmp.cert_req == 1) {
            st->hand_state = TLS_ST_CW_CERT_VRFY;
        } else {
            st->hand_state = TLS_ST_CW_CHANGE;
        }
        if (s->s3->flags & TLS1_FLAGS_SKIP_CERT_VERIFY) {
            st->hand_state = TLS_ST_CW_CHANGE;
        }
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_CW_CERT_VRFY:
        st->hand_state = TLS_ST_CW_CHANGE;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_CW_CHANGE:
#if defined(OPENSSL_NO_NEXTPROTONEG)
        st->hand_state = TLS_ST_CW_FINISHED;
#else
        if (!SSL_IS_DTLS(s) && s->s3->npn_seen)
            st->hand_state = TLS_ST_CW_NEXT_PROTO;
        else
            st->hand_state = TLS_ST_CW_FINISHED;
#endif
        return WRITE_TRAN_CONTINUE;

#if !defined(OPENSSL_NO_NEXTPROTONEG)
    case TLS_ST_CW_NEXT_PROTO:
        st->hand_state = TLS_ST_CW_FINISHED;
        return WRITE_TRAN_CONTINUE;
#endif

    case TLS_ST_CW_FINISHED:
        if (s->hit) {
            st->hand_state = TLS_ST_OK;
            ossl_statem_set_in_init(s, 0);
            return WRITE_TRAN_CONTINUE;
        } else {
            return WRITE_TRAN_FINISHED;
        }

    case TLS_ST_CR_FINISHED:
        if (s->hit) {
            st->hand_state = TLS_ST_CW_CHANGE;
            return WRITE_TRAN_CONTINUE;
        } else {
            st->hand_state = TLS_ST_OK;
            ossl_statem_set_in_init(s, 0);
            return WRITE_TRAN_CONTINUE;
        }

    case TLS_ST_CR_HELLO_REQ:
        /*
         * If we can renegotiate now then do so, otherwise wait for a more
         * convenient time.
         */
        if (ssl3_renegotiate_check(s, 1)) {
            if (!tls_setup_handshake(s)) {
                ossl_statem_set_error(s);
                return WRITE_TRAN_ERROR;
            }
            st->hand_state = TLS_ST_CW_CLNT_HELLO;
            return WRITE_TRAN_CONTINUE;
        }
        st->hand_state = TLS_ST_OK;
        ossl_statem_set_in_init(s, 0);
        return WRITE_TRAN_CONTINUE;
    }
}

/*
 * Perform any pre work that needs to be done prior to sending a message from
 * the client to the server.
 */
WORK_STATE ossl_statem_client_pre_work(SSL *s, WORK_STATE wst)
{
    OSSL_STATEM *st = &s->statem;

    switch (st->hand_state) {
    default:
        /* No pre work to be done */
        break;

    case TLS_ST_CW_CLNT_HELLO:
        s->shutdown = 0;
        if (SSL_IS_DTLS(s)) {
            /* every DTLS ClientHello resets Finished MAC */
            if (!ssl3_init_finished_mac(s)) {
                ossl_statem_set_error(s);
                return WORK_ERROR;
            }
        }
        break;

    case TLS_ST_CW_CHANGE:
        if (SSL_IS_DTLS(s)) {
            if (s->hit) {
                /*
                 * We're into the last flight so we don't retransmit these
                 * messages unless we need to.
                 */
                st->use_timer = 0;
            }
#ifndef OPENSSL_NO_SCTP
            if (BIO_dgram_is_sctp(SSL_get_wbio(s)))
                return dtls_wait_for_dry(s);
#endif
        }
        break;

    case TLS_ST_OK:
        return tls_finish_handshake(s, wst, 1);
    }

    return WORK_FINISHED_CONTINUE;
}

/*
 * Perform any work that needs to be done after sending a message from the
 * client to the server.
    case TLS_ST_SR_CERT_VRFY:
        return SSL3_RT_MAX_PLAIN_LENGTH;
 */
WORK_STATE ossl_statem_client_post_work(SSL *s, WORK_STATE wst)
{
    OSSL_STATEM *st = &s->statem;

    s->init_num = 0;

    switch (st->hand_state) {
    default:
        /* No post work to be done */
        break;

    case TLS_ST_CW_CLNT_HELLO:
        if (wst == WORK_MORE_A && statem_flush(s) != 1)
            return WORK_MORE_A;

        if (SSL_IS_DTLS(s)) {
            /* Treat the next message as the first packet */
            s->first_packet = 1;
        }
        break;

    case TLS_ST_CW_KEY_EXCH:
        if (tls_client_key_exchange_post_work(s) == 0)
            return WORK_ERROR;
        break;

    case TLS_ST_CW_CHANGE:
        s->session->cipher = s->s3->tmp.new_cipher;
#ifdef OPENSSL_NO_COMP
        s->session->compress_meth = 0;
#else
        if (s->s3->tmp.new_compression == NULL)
            s->session->compress_meth = 0;
        else
            s->session->compress_meth = s->s3->tmp.new_compression->id;
#endif
        if (!s->method->ssl3_enc->setup_key_block(s))
            return WORK_ERROR;

        if (!s->method->ssl3_enc->change_cipher_state(s,
                                                      SSL3_CHANGE_CIPHER_CLIENT_WRITE))
            return WORK_ERROR;

        if (SSL_IS_DTLS(s)) {
#ifndef OPENSSL_NO_SCTP
            if (s->hit) {
                /*
                 * Change to new shared key of SCTP-Auth, will be ignored if
                 * no SCTP used.
                 */
                BIO_ctrl(SSL_get_wbio(s), BIO_CTRL_DGRAM_SCTP_NEXT_AUTH_KEY,
                         0, NULL);
            }
#endif

            dtls1_reset_seq_numbers(s, SSL3_CC_WRITE);
        }
        break;

    case TLS_ST_CW_FINISHED:
#ifndef OPENSSL_NO_SCTP
        if (wst == WORK_MORE_A && SSL_IS_DTLS(s) && s->hit == 0) {
            /*
             * Change to new shared key of SCTP-Auth, will be ignored if
             * no SCTP used.
             */
            BIO_ctrl(SSL_get_wbio(s), BIO_CTRL_DGRAM_SCTP_NEXT_AUTH_KEY,
                     0, NULL);
        }
#endif
        if (statem_flush(s) != 1)
            return WORK_MORE_B;

        if (SSL_IS_TLS13(s)) {
            if (!s->method->ssl3_enc->change_cipher_state(s,
                        SSL3_CC_APPLICATION | SSL3_CHANGE_CIPHER_CLIENT_WRITE))
            return WORK_ERROR;
        }
        break;
    }

    return WORK_FINISHED_CONTINUE;
}

/*
 * Get the message construction function and message type for sending from the
 * client
 *
 * Valid return values are:
 *   1: Success
 *   0: Error
 */
int ossl_statem_client_construct_message(SSL *s, WPACKET *pkt,
                                         confunc_f *confunc, int *mt)
{
    OSSL_STATEM *st = &s->statem;

    switch (st->hand_state) {
    default:
        /* Shouldn't happen */
        return 0;

    case TLS_ST_CW_CHANGE:
        if (SSL_IS_DTLS(s))
            *confunc = dtls_construct_change_cipher_spec;
        else
            *confunc = tls_construct_change_cipher_spec;
        *mt = SSL3_MT_CHANGE_CIPHER_SPEC;
        break;

    case TLS_ST_CW_CLNT_HELLO:
        *confunc = tls_construct_client_hello;
        *mt = SSL3_MT_CLIENT_HELLO;
        break;

    case TLS_ST_CW_CERT:
        *confunc = tls_construct_client_certificate;
        *mt = SSL3_MT_CERTIFICATE;
        break;

    case TLS_ST_CW_KEY_EXCH:
        *confunc = tls_construct_client_key_exchange;
        *mt = SSL3_MT_CLIENT_KEY_EXCHANGE;
        break;

    case TLS_ST_CW_CERT_VRFY:
        *confunc = tls_construct_cert_verify;
        *mt = SSL3_MT_CERTIFICATE_VERIFY;
        break;

#if !defined(OPENSSL_NO_NEXTPROTONEG)
    case TLS_ST_CW_NEXT_PROTO:
        *confunc = tls_construct_next_proto;
        *mt = SSL3_MT_NEXT_PROTO;
        break;
#endif
    case TLS_ST_CW_FINISHED:
        *confunc = tls_construct_finished;
        *mt = SSL3_MT_FINISHED;
        break;
    }

    return 1;
}

/*
 * Returns the maximum allowed length for the current message that we are
 * reading. Excludes the message header.
 */
size_t ossl_statem_client_max_message_size(SSL *s)
{
    OSSL_STATEM *st = &s->statem;

    switch (st->hand_state) {
    default:
        /* Shouldn't happen */
        return 0;

    case TLS_ST_CR_SRVR_HELLO:
        return SERVER_HELLO_MAX_LENGTH;

    case DTLS_ST_CR_HELLO_VERIFY_REQUEST:
        return HELLO_VERIFY_REQUEST_MAX_LENGTH;

    case TLS_ST_CR_CERT:
        return s->max_cert_list;

    case TLS_ST_CR_CERT_VRFY:
        return SSL3_RT_MAX_PLAIN_LENGTH;

    case TLS_ST_CR_CERT_STATUS:
        return SSL3_RT_MAX_PLAIN_LENGTH;

    case TLS_ST_CR_KEY_EXCH:
        return SERVER_KEY_EXCH_MAX_LENGTH;

    case TLS_ST_CR_CERT_REQ:
        /*
         * Set to s->max_cert_list for compatibility with previous releases. In
         * practice these messages can get quite long if servers are configured
         * to provide a long list of acceptable CAs
         */
        return s->max_cert_list;

    case TLS_ST_CR_SRVR_DONE:
        return SERVER_HELLO_DONE_MAX_LENGTH;

    case TLS_ST_CR_CHANGE:
        if (s->version == DTLS1_BAD_VER)
            return 3;
        return CCS_MAX_LENGTH;

    case TLS_ST_CR_SESSION_TICKET:
        return SSL3_RT_MAX_PLAIN_LENGTH;

    case TLS_ST_CR_FINISHED:
        return FINISHED_MAX_LENGTH;

    case TLS_ST_CR_ENCRYPTED_EXTENSIONS:
        return ENCRYPTED_EXTENSIONS_MAX_LENGTH;
    }
}

/*
 * Process a message that the client has been received from the server.
 */
MSG_PROCESS_RETURN ossl_statem_client_process_message(SSL *s, PACKET *pkt)
{
    OSSL_STATEM *st = &s->statem;

    switch (st->hand_state) {
    default:
        /* Shouldn't happen */
        return MSG_PROCESS_ERROR;

    case TLS_ST_CR_SRVR_HELLO:
        return tls_process_server_hello(s, pkt);

    case DTLS_ST_CR_HELLO_VERIFY_REQUEST:
        return dtls_process_hello_verify(s, pkt);

    case TLS_ST_CR_CERT:
        return tls_process_server_certificate(s, pkt);

    case TLS_ST_CR_CERT_VRFY:
        return tls_process_cert_verify(s, pkt);

    case TLS_ST_CR_CERT_STATUS:
        return tls_process_cert_status(s, pkt);

    case TLS_ST_CR_KEY_EXCH:
        return tls_process_key_exchange(s, pkt);

    case TLS_ST_CR_CERT_REQ:
        return tls_process_certificate_request(s, pkt);

    case TLS_ST_CR_SRVR_DONE:
        return tls_process_server_done(s, pkt);

    case TLS_ST_CR_CHANGE:
        return tls_process_change_cipher_spec(s, pkt);

    case TLS_ST_CR_SESSION_TICKET:
        return tls_process_new_session_ticket(s, pkt);

    case TLS_ST_CR_FINISHED:
        return tls_process_finished(s, pkt);

    case TLS_ST_CR_HELLO_REQ:
        return tls_process_hello_req(s, pkt);

    case TLS_ST_CR_ENCRYPTED_EXTENSIONS:
        return tls_process_encrypted_extensions(s, pkt);
    }
}

/*
 * Perform any further processing required following the receipt of a message
 * from the server
 */
WORK_STATE ossl_statem_client_post_process_message(SSL *s, WORK_STATE wst)
{
    OSSL_STATEM *st = &s->statem;

    switch (st->hand_state) {
    default:
        /* Shouldn't happen */
        return WORK_ERROR;

    case TLS_ST_CR_CERT_REQ:
        return tls_prepare_client_certificate(s, wst);

#ifndef OPENSSL_NO_SCTP
    case TLS_ST_CR_SRVR_DONE:
        /* We only get here if we are using SCTP and we are renegotiating */
        if (BIO_dgram_sctp_msg_waiting(SSL_get_rbio(s))) {
            s->s3->in_read_app_data = 2;
            s->rwstate = SSL_READING;
            BIO_clear_retry_flags(SSL_get_rbio(s));
            BIO_set_retry_read(SSL_get_rbio(s));
            ossl_statem_set_sctp_read_sock(s, 1);
            return WORK_MORE_A;
        }
        ossl_statem_set_sctp_read_sock(s, 0);
        return WORK_FINISHED_STOP;
#endif
    }
}

int tls_construct_client_hello(SSL *s, WPACKET *pkt)
{
    unsigned char *p;
    size_t sess_id_len;
    int i, protverr;
    int al = SSL_AD_HANDSHAKE_FAILURE;
#ifndef OPENSSL_NO_COMP
    SSL_COMP *comp;
#endif
    SSL_SESSION *sess = s->session;

    if (!WPACKET_set_max_size(pkt, SSL3_RT_MAX_PLAIN_LENGTH)) {
        /* Should not happen */
        SSLerr(SSL_F_TLS_CONSTRUCT_CLIENT_HELLO, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    /* Work out what SSL/TLS/DTLS version to use */
    protverr = ssl_set_client_hello_version(s);
    if (protverr != 0) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CLIENT_HELLO, protverr);
        return 0;
    }

    if ((sess == NULL) || !ssl_version_supported(s, sess->ssl_version) ||
        /*
         * In the case of EAP-FAST, we can have a pre-shared
         * "ticket" without a session ID.
         */
        (!sess->session_id_length && !sess->ext.tick) ||
        (sess->not_resumable)) {
        if (!ssl_get_new_session(s, 0))
            return 0;
    }
    /* else use the pre-loaded session */

    /* This is a real handshake so make sure we clean it up at the end */
    s->statem.cleanuphand = 1;

    p = s->s3->client_random;

    /*
     * for DTLS if client_random is initialized, reuse it, we are
     * required to use same upon reply to HelloVerify
     */
    if (SSL_IS_DTLS(s)) {
        size_t idx;
        i = 1;
        for (idx = 0; idx < sizeof(s->s3->client_random); idx++) {
            if (p[idx]) {
                i = 0;
                break;
            }
        }
    } else
        i = 1;

    if (i && ssl_fill_hello_random(s, 0, p, sizeof(s->s3->client_random)) <= 0)
        return 0;

    /*-
     * version indicates the negotiated version: for example from
     * an SSLv2/v3 compatible client hello). The client_version
     * field is the maximum version we permit and it is also
     * used in RSA encrypted premaster secrets. Some servers can
     * choke if we initially report a higher version then
     * renegotiate to a lower one in the premaster secret. This
     * didn't happen with TLS 1.0 as most servers supported it
     * but it can with TLS 1.1 or later if the server only supports
     * 1.0.
     *
     * Possible scenario with previous logic:
     *      1. Client hello indicates TLS 1.2
     *      2. Server hello says TLS 1.0
     *      3. RSA encrypted premaster secret uses 1.2.
     *      4. Handshake proceeds using TLS 1.0.
     *      5. Server sends hello request to renegotiate.
     *      6. Client hello indicates TLS v1.0 as we now
     *         know that is maximum server supports.
     *      7. Server chokes on RSA encrypted premaster secret
     *         containing version 1.0.
     *
     * For interoperability it should be OK to always use the
     * maximum version we support in client hello and then rely
     * on the checking of version to ensure the servers isn't
     * being inconsistent: for example initially negotiating with
     * TLS 1.0 and renegotiating with TLS 1.2. We do this by using
     * client_version in client hello and not resetting it to
     * the negotiated version.
     *
     * For TLS 1.3 we always set the ClientHello version to 1.2 and rely on the
     * supported_versions extension for the real supported versions.
     */
    if (!WPACKET_put_bytes_u16(pkt, s->client_version)
            || !WPACKET_memcpy(pkt, s->s3->client_random, SSL3_RANDOM_SIZE)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CLIENT_HELLO, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    /* Session ID */
    if (s->new_session || s->session->ssl_version == TLS1_3_VERSION)
        sess_id_len = 0;
    else
        sess_id_len = s->session->session_id_length;
    if (sess_id_len > sizeof(s->session->session_id)
            || !WPACKET_start_sub_packet_u8(pkt)
            || (sess_id_len != 0 && !WPACKET_memcpy(pkt, s->session->session_id,
                                                    sess_id_len))
            || !WPACKET_close(pkt)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CLIENT_HELLO, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    /* cookie stuff for DTLS */
    if (SSL_IS_DTLS(s)) {
        if (s->d1->cookie_len > sizeof(s->d1->cookie)
                || !WPACKET_sub_memcpy_u8(pkt, s->d1->cookie,
                                          s->d1->cookie_len)) {
            SSLerr(SSL_F_TLS_CONSTRUCT_CLIENT_HELLO, ERR_R_INTERNAL_ERROR);
            return 0;
        }
    }

    /* Ciphers supported */
    if (!WPACKET_start_sub_packet_u16(pkt)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CLIENT_HELLO, ERR_R_INTERNAL_ERROR);
        return 0;
    }
    /* ssl_cipher_list_to_bytes() raises SSLerr if appropriate */
    if (!ssl_cipher_list_to_bytes(s, SSL_get_ciphers(s), pkt))
        return 0;
    if (!WPACKET_close(pkt)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CLIENT_HELLO, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    /* COMPRESSION */
    if (!WPACKET_start_sub_packet_u8(pkt)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CLIENT_HELLO, ERR_R_INTERNAL_ERROR);
        return 0;
    }
#ifndef OPENSSL_NO_COMP
    if (ssl_allow_compression(s) && s->ctx->comp_methods) {
        int compnum = sk_SSL_COMP_num(s->ctx->comp_methods);
        for (i = 0; i < compnum; i++) {
            comp = sk_SSL_COMP_value(s->ctx->comp_methods, i);
            if (!WPACKET_put_bytes_u8(pkt, comp->id)) {
                SSLerr(SSL_F_TLS_CONSTRUCT_CLIENT_HELLO, ERR_R_INTERNAL_ERROR);
                return 0;
            }
        }
    }
#endif
    /* Add the NULL method */
    if (!WPACKET_put_bytes_u8(pkt, 0) || !WPACKET_close(pkt)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CLIENT_HELLO, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    /* TLS extensions */
    if (!tls_construct_extensions(s, pkt, EXT_CLIENT_HELLO, NULL, 0, &al)) {
        ssl3_send_alert(s, SSL3_AL_FATAL, al);
        SSLerr(SSL_F_TLS_CONSTRUCT_CLIENT_HELLO, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    return 1;
}

MSG_PROCESS_RETURN dtls_process_hello_verify(SSL *s, PACKET *pkt)
{
    int al;
    size_t cookie_len;
    PACKET cookiepkt;

    if (!PACKET_forward(pkt, 2)
        || !PACKET_get_length_prefixed_1(pkt, &cookiepkt)) {
        al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_DTLS_PROCESS_HELLO_VERIFY, SSL_R_LENGTH_MISMATCH);
        goto f_err;
    }

    cookie_len = PACKET_remaining(&cookiepkt);
    if (cookie_len > sizeof(s->d1->cookie)) {
        al = SSL_AD_ILLEGAL_PARAMETER;
        SSLerr(SSL_F_DTLS_PROCESS_HELLO_VERIFY, SSL_R_LENGTH_TOO_LONG);
        goto f_err;
    }

    if (!PACKET_copy_bytes(&cookiepkt, s->d1->cookie, cookie_len)) {
        al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_DTLS_PROCESS_HELLO_VERIFY, SSL_R_LENGTH_MISMATCH);
        goto f_err;
    }
    s->d1->cookie_len = cookie_len;

    return MSG_PROCESS_FINISHED_READING;
 f_err:
    ssl3_send_alert(s, SSL3_AL_FATAL, al);
    ossl_statem_set_error(s);
    return MSG_PROCESS_ERROR;
}

MSG_PROCESS_RETURN tls_process_server_hello(SSL *s, PACKET *pkt)
{
    STACK_OF(SSL_CIPHER) *sk;
    const SSL_CIPHER *c;
    PACKET session_id, extpkt;
    size_t session_id_len;
    const unsigned char *cipherchars;
    int i, al = SSL_AD_INTERNAL_ERROR;
    unsigned int compression;
    unsigned int sversion;
    unsigned int context;
    int protverr;
    RAW_EXTENSION *extensions = NULL;
#ifndef OPENSSL_NO_COMP
    SSL_COMP *comp;
#endif

    if (!PACKET_get_net_2(pkt, &sversion)) {
        al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO, SSL_R_LENGTH_MISMATCH);
        goto f_err;
    }

    /* We do this immediately so we know what format the ServerHello is in */
    protverr = ssl_choose_client_version(s, sversion);
    if (protverr != 0) {
        al = SSL_AD_PROTOCOL_VERSION;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO, protverr);
        goto f_err;
    }

    /* load the server hello data */
    /* load the server random */
    if (!PACKET_copy_bytes(pkt, s->s3->server_random, SSL3_RANDOM_SIZE)) {
        al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO, SSL_R_LENGTH_MISMATCH);
        goto f_err;
    }

    /* Get the session-id. */
    if (!SSL_IS_TLS13(s)) {
        if (!PACKET_get_length_prefixed_1(pkt, &session_id)) {
            al = SSL_AD_DECODE_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO, SSL_R_LENGTH_MISMATCH);
            goto f_err;
        }
        session_id_len = PACKET_remaining(&session_id);
        if (session_id_len > sizeof s->session->session_id
            || session_id_len > SSL3_SESSION_ID_SIZE) {
            al = SSL_AD_ILLEGAL_PARAMETER;
            SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO,
                   SSL_R_SSL3_SESSION_ID_TOO_LONG);
            goto f_err;
        }
    } else {
        PACKET_null_init(&session_id);
        session_id_len = 0;
    }

    if (!PACKET_get_bytes(pkt, &cipherchars, TLS_CIPHER_LEN)) {
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO, SSL_R_LENGTH_MISMATCH);
        al = SSL_AD_DECODE_ERROR;
        goto f_err;
    }

    if (!SSL_IS_TLS13(s)) {
        if (!PACKET_get_1(pkt, &compression)) {
            SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO, SSL_R_LENGTH_MISMATCH);
            al = SSL_AD_DECODE_ERROR;
            goto f_err;
        }
    } else {
        compression = 0;
    }

    /* TLS extensions */
    if (PACKET_remaining(pkt) == 0) {
        PACKET_null_init(&extpkt);
    } else if (!PACKET_as_length_prefixed_2(pkt, &extpkt)) {
        al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO, SSL_R_BAD_LENGTH);
        goto f_err;
    }

    context = SSL_IS_TLS13(s) ? EXT_TLS1_3_SERVER_HELLO
                              : EXT_TLS1_2_SERVER_HELLO;
    if (!tls_collect_extensions(s, &extpkt, context, &extensions, &al))
        goto f_err;

    s->hit = 0;

    if (SSL_IS_TLS13(s)) {
        /* This will set s->hit if we are resuming */
        if (!tls_parse_extension(s, TLSEXT_IDX_psk,
                                 EXT_TLS1_3_SERVER_HELLO,
                                 extensions, NULL, 0, &al))
            goto f_err;
    } else {
        /*
         * Check if we can resume the session based on external pre-shared
         * secret. EAP-FAST (RFC 4851) supports two types of session resumption.
         * Resumption based on server-side state works with session IDs.
         * Resumption based on pre-shared Protected Access Credentials (PACs)
         * works by overriding the SessionTicket extension at the application
         * layer, and does not send a session ID. (We do not know whether
         * EAP-FAST servers would honour the session ID.) Therefore, the session
         * ID alone is not a reliable indicator of session resumption, so we
         * first check if we can resume, and later peek at the next handshake
         * message to see if the server wants to resume.
         */
        if (s->version >= TLS1_VERSION
                && s->ext.session_secret_cb != NULL && s->session->ext.tick) {
            const SSL_CIPHER *pref_cipher = NULL;
            /*
             * s->session->master_key_length is a size_t, but this is an int for
             * backwards compat reasons
             */
            int master_key_length;
            master_key_length = sizeof(s->session->master_key);
            if (s->ext.session_secret_cb(s, s->session->master_key,
                                         &master_key_length,
                                         NULL, &pref_cipher,
                                         s->ext.session_secret_cb_arg)
                     && master_key_length > 0) {
                s->session->master_key_length = master_key_length;
                s->session->cipher = pref_cipher ?
                    pref_cipher : ssl_get_cipher_by_char(s, cipherchars);
            } else {
                SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO, ERR_R_INTERNAL_ERROR);
                al = SSL_AD_INTERNAL_ERROR;
                goto f_err;
            }
        }

        if (session_id_len != 0
                && session_id_len == s->session->session_id_length
                && memcmp(PACKET_data(&session_id), s->session->session_id,
                          session_id_len) == 0)
            s->hit = 1;
    }

    if (s->hit) {
        if (s->sid_ctx_length != s->session->sid_ctx_length
                || memcmp(s->session->sid_ctx, s->sid_ctx, s->sid_ctx_length)) {
            /* actually a client application bug */
            al = SSL_AD_ILLEGAL_PARAMETER;
            SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO,
                   SSL_R_ATTEMPT_TO_REUSE_SESSION_IN_DIFFERENT_CONTEXT);
            goto f_err;
        }
    } else {
        /*
         * If we were trying for session-id reuse but the server
         * didn't resume, make a new SSL_SESSION.
         * In the case of EAP-FAST and PAC, we do not send a session ID,
         * so the PAC-based session secret is always preserved. It'll be
         * overwritten if the server refuses resumption.
         */
        if (s->session->session_id_length > 0
                || (SSL_IS_TLS13(s)
                    && s->session->ext.tick_identity
                       != TLSEXT_PSK_BAD_IDENTITY)) {
            s->ctx->stats.sess_miss++;
            if (!ssl_get_new_session(s, 0)) {
                goto f_err;
            }
        }

        s->session->ssl_version = s->version;
        s->session->session_id_length = session_id_len;
        /* session_id_len could be 0 */
        if (session_id_len > 0)
            memcpy(s->session->session_id, PACKET_data(&session_id),
                   session_id_len);
    }

    /* Session version and negotiated protocol version should match */
    if (s->version != s->session->ssl_version) {
        al = SSL_AD_PROTOCOL_VERSION;

        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO,
               SSL_R_SSL_SESSION_VERSION_MISMATCH);
        goto f_err;
    }

    c = ssl_get_cipher_by_char(s, cipherchars);
    if (c == NULL) {
        /* unknown cipher */
        al = SSL_AD_ILLEGAL_PARAMETER;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO, SSL_R_UNKNOWN_CIPHER_RETURNED);
        goto f_err;
    }
    /*
     * Now that we know the version, update the check to see if it's an allowed
     * version.
     */
    s->s3->tmp.min_ver = s->version;
    s->s3->tmp.max_ver = s->version;
    /*
     * If it is a disabled cipher we either didn't send it in client hello,
     * or it's not allowed for the selected protocol. So we return an error.
     */
    if (ssl_cipher_disabled(s, c, SSL_SECOP_CIPHER_CHECK)) {
        al = SSL_AD_ILLEGAL_PARAMETER;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO, SSL_R_WRONG_CIPHER_RETURNED);
        goto f_err;
    }

    sk = ssl_get_ciphers_by_id(s);
    i = sk_SSL_CIPHER_find(sk, c);
    if (i < 0) {
        /* we did not say we would use this cipher */
        al = SSL_AD_ILLEGAL_PARAMETER;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO, SSL_R_WRONG_CIPHER_RETURNED);
        goto f_err;
    }

    /*
     * Depending on the session caching (internal/external), the cipher
     * and/or cipher_id values may not be set. Make sure that cipher_id is
     * set and use it for comparison.
     */
    if (s->session->cipher)
        s->session->cipher_id = s->session->cipher->id;
    if (s->hit && (s->session->cipher_id != c->id)) {
        al = SSL_AD_ILLEGAL_PARAMETER;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO,
               SSL_R_OLD_SESSION_CIPHER_NOT_RETURNED);
        goto f_err;
    }
    s->s3->tmp.new_cipher = c;

#ifdef OPENSSL_NO_COMP
    if (compression != 0) {
        al = SSL_AD_ILLEGAL_PARAMETER;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO,
               SSL_R_UNSUPPORTED_COMPRESSION_ALGORITHM);
        goto f_err;
    }
    /*
     * If compression is disabled we'd better not try to resume a session
     * using compression.
     */
    if (s->session->compress_meth != 0) {
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO, SSL_R_INCONSISTENT_COMPRESSION);
        goto f_err;
    }
#else
    if (s->hit && compression != s->session->compress_meth) {
        al = SSL_AD_ILLEGAL_PARAMETER;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO,
               SSL_R_OLD_SESSION_COMPRESSION_ALGORITHM_NOT_RETURNED);
        goto f_err;
    }
    if (compression == 0)
        comp = NULL;
    else if (!ssl_allow_compression(s)) {
        al = SSL_AD_ILLEGAL_PARAMETER;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO, SSL_R_COMPRESSION_DISABLED);
        goto f_err;
    } else {
        comp = ssl3_comp_find(s->ctx->comp_methods, compression);
    }

    if (compression != 0 && comp == NULL) {
        al = SSL_AD_ILLEGAL_PARAMETER;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO,
               SSL_R_UNSUPPORTED_COMPRESSION_ALGORITHM);
        goto f_err;
    } else {
        s->s3->tmp.new_compression = comp;
    }
#endif

    if (!tls_parse_all_extensions(s, context, extensions, NULL, 0, &al))
        goto f_err;

#ifndef OPENSSL_NO_SCTP
    if (SSL_IS_DTLS(s) && s->hit) {
        unsigned char sctpauthkey[64];
        char labelbuffer[sizeof(DTLS1_SCTP_AUTH_LABEL)];

        /*
         * Add new shared key for SCTP-Auth, will be ignored if
         * no SCTP used.
         */
        memcpy(labelbuffer, DTLS1_SCTP_AUTH_LABEL,
               sizeof(DTLS1_SCTP_AUTH_LABEL));

        if (SSL_export_keying_material(s, sctpauthkey,
                                       sizeof(sctpauthkey),
                                       labelbuffer,
                                       sizeof(labelbuffer), NULL, 0, 0) <= 0)
            goto f_err;

        BIO_ctrl(SSL_get_wbio(s),
                 BIO_CTRL_DGRAM_SCTP_ADD_AUTH_KEY,
                 sizeof(sctpauthkey), sctpauthkey);
    }
#endif

    /*
     * In TLSv1.3 we have some post-processing to change cipher state, otherwise
     * we're done with this message
     */
    if (SSL_IS_TLS13(s)
            && (!s->method->ssl3_enc->setup_key_block(s)
                || !s->method->ssl3_enc->change_cipher_state(s,
                    SSL3_CC_HANDSHAKE | SSL3_CHANGE_CIPHER_CLIENT_WRITE)
                || !s->method->ssl3_enc->change_cipher_state(s,
                    SSL3_CC_HANDSHAKE | SSL3_CHANGE_CIPHER_CLIENT_READ))) {
        al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_HELLO, SSL_R_CANNOT_CHANGE_CIPHER);
        goto f_err;
    }

    OPENSSL_free(extensions);
    return MSG_PROCESS_CONTINUE_READING;
 f_err:
    ssl3_send_alert(s, SSL3_AL_FATAL, al);
    ossl_statem_set_error(s);
    OPENSSL_free(extensions);
    return MSG_PROCESS_ERROR;
}

MSG_PROCESS_RETURN tls_process_server_certificate(SSL *s, PACKET *pkt)
{
    int al, i, ret = MSG_PROCESS_ERROR, exp_idx;
    unsigned long cert_list_len, cert_len;
    X509 *x = NULL;
    const unsigned char *certstart, *certbytes;
    STACK_OF(X509) *sk = NULL;
    EVP_PKEY *pkey = NULL;
    size_t chainidx;
    unsigned int context = 0;

    if ((sk = sk_X509_new_null()) == NULL) {
        SSLerr(SSL_F_TLS_PROCESS_SERVER_CERTIFICATE, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    if ((SSL_IS_TLS13(s) && !PACKET_get_1(pkt, &context))
            || context != 0
            || !PACKET_get_net_3(pkt, &cert_list_len)
            || PACKET_remaining(pkt) != cert_list_len) {
        al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_CERTIFICATE, SSL_R_LENGTH_MISMATCH);
        goto f_err;
    }
    for (chainidx = 0; PACKET_remaining(pkt); chainidx++) {
        if (!PACKET_get_net_3(pkt, &cert_len)
            || !PACKET_get_bytes(pkt, &certbytes, cert_len)) {
            al = SSL_AD_DECODE_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_SERVER_CERTIFICATE,
                   SSL_R_CERT_LENGTH_MISMATCH);
            goto f_err;
        }

        certstart = certbytes;
        x = d2i_X509(NULL, (const unsigned char **)&certbytes, cert_len);
        if (x == NULL) {
            al = SSL_AD_BAD_CERTIFICATE;
            SSLerr(SSL_F_TLS_PROCESS_SERVER_CERTIFICATE, ERR_R_ASN1_LIB);
            goto f_err;
        }
        if (certbytes != (certstart + cert_len)) {
            al = SSL_AD_DECODE_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_SERVER_CERTIFICATE,
                   SSL_R_CERT_LENGTH_MISMATCH);
            goto f_err;
        }

        if (SSL_IS_TLS13(s)) {
            RAW_EXTENSION *rawexts = NULL;
            PACKET extensions;

            if (!PACKET_get_length_prefixed_2(pkt, &extensions)) {
                al = SSL_AD_DECODE_ERROR;
                SSLerr(SSL_F_TLS_PROCESS_SERVER_CERTIFICATE, SSL_R_BAD_LENGTH);
                goto f_err;
            }
            if (!tls_collect_extensions(s, &extensions, EXT_TLS1_3_CERTIFICATE,
                                        &rawexts, &al)
                    || !tls_parse_all_extensions(s, EXT_TLS1_3_CERTIFICATE,
                                                 rawexts, x, chainidx, &al)) {
                OPENSSL_free(rawexts);
                goto f_err;
            }
            OPENSSL_free(rawexts);
        }

        if (!sk_X509_push(sk, x)) {
            SSLerr(SSL_F_TLS_PROCESS_SERVER_CERTIFICATE, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        x = NULL;
    }

    i = ssl_verify_cert_chain(s, sk);
    /*
     * The documented interface is that SSL_VERIFY_PEER should be set in order
     * for client side verification of the server certificate to take place.
     * However, historically the code has only checked that *any* flag is set
     * to cause server verification to take place. Use of the other flags makes
     * no sense in client mode. An attempt to clean up the semantics was
     * reverted because at least one application *only* set
     * SSL_VERIFY_FAIL_IF_NO_PEER_CERT. Prior to the clean up this still caused
     * server verification to take place, after the clean up it silently did
     * nothing. SSL_CTX_set_verify()/SSL_set_verify() cannot validate the flags
     * sent to them because they are void functions. Therefore, we now use the
     * (less clean) historic behaviour of performing validation if any flag is
     * set. The *documented* interface remains the same.
     */
    if (s->verify_mode != SSL_VERIFY_NONE && i <= 0) {
        al = ssl_verify_alarm_type(s->verify_result);
        SSLerr(SSL_F_TLS_PROCESS_SERVER_CERTIFICATE,
               SSL_R_CERTIFICATE_VERIFY_FAILED);
        goto f_err;
    }
    ERR_clear_error();          /* but we keep s->verify_result */
    if (i > 1) {
        SSLerr(SSL_F_TLS_PROCESS_SERVER_CERTIFICATE, i);
        al = SSL_AD_HANDSHAKE_FAILURE;
        goto f_err;
    }

    s->session->peer_chain = sk;
    /*
     * Inconsistency alert: cert_chain does include the peer's certificate,
     * which we don't include in statem_srvr.c
     */
    x = sk_X509_value(sk, 0);
    sk = NULL;
    /*
     * VRS 19990621: possible memory leak; sk=null ==> !sk_pop_free() @end
     */

    pkey = X509_get0_pubkey(x);

    if (pkey == NULL || EVP_PKEY_missing_parameters(pkey)) {
        x = NULL;
        al = SSL3_AL_FATAL;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_CERTIFICATE,
               SSL_R_UNABLE_TO_FIND_PUBLIC_KEY_PARAMETERS);
        goto f_err;
    }

    i = ssl_cert_type(x, pkey);
    if (i < 0) {
        x = NULL;
        al = SSL3_AL_FATAL;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_CERTIFICATE,
               SSL_R_UNKNOWN_CERTIFICATE_TYPE);
        goto f_err;
    }
    /*
     * Check certificate type is consistent with ciphersuite. For TLS 1.3
     * skip check since TLS 1.3 ciphersuites can be used with any certificate
     * type.
     */
    if (!SSL_IS_TLS13(s)) {
        exp_idx = ssl_cipher_get_cert_index(s->s3->tmp.new_cipher);
        if (exp_idx >= 0 && i != exp_idx
            && (exp_idx != SSL_PKEY_GOST_EC ||
                (i != SSL_PKEY_GOST12_512 && i != SSL_PKEY_GOST12_256
                 && i != SSL_PKEY_GOST01))) {
            x = NULL;
            al = SSL_AD_ILLEGAL_PARAMETER;
            SSLerr(SSL_F_TLS_PROCESS_SERVER_CERTIFICATE,
                   SSL_R_WRONG_CERTIFICATE_TYPE);
            goto f_err;
        }
    }
    s->session->peer_type = i;

    X509_free(s->session->peer);
    X509_up_ref(x);
    s->session->peer = x;
    s->session->verify_result = s->verify_result;
    x = NULL;

    /* Save the current hash state for when we receive the CertificateVerify */
    if (SSL_IS_TLS13(s)
            && !ssl_handshake_hash(s, s->cert_verify_hash,
                                   sizeof(s->cert_verify_hash),
                                   &s->cert_verify_hash_len)) {
        al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_CERTIFICATE, ERR_R_INTERNAL_ERROR);
        goto f_err;
    }

    ret = MSG_PROCESS_CONTINUE_READING;
    goto done;

 f_err:
    ssl3_send_alert(s, SSL3_AL_FATAL, al);
 err:
    ossl_statem_set_error(s);
 done:
    X509_free(x);
    sk_X509_pop_free(sk, X509_free);
    return ret;
}

static int tls_process_ske_psk_preamble(SSL *s, PACKET *pkt, int *al)
{
#ifndef OPENSSL_NO_PSK
    PACKET psk_identity_hint;

    /* PSK ciphersuites are preceded by an identity hint */

    if (!PACKET_get_length_prefixed_2(pkt, &psk_identity_hint)) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_PSK_PREAMBLE, SSL_R_LENGTH_MISMATCH);
        return 0;
    }

    /*
     * Store PSK identity hint for later use, hint is used in
     * tls_construct_client_key_exchange.  Assume that the maximum length of
     * a PSK identity hint can be as long as the maximum length of a PSK
     * identity.
     */
    if (PACKET_remaining(&psk_identity_hint) > PSK_MAX_IDENTITY_LEN) {
        *al = SSL_AD_HANDSHAKE_FAILURE;
        SSLerr(SSL_F_TLS_PROCESS_SKE_PSK_PREAMBLE, SSL_R_DATA_LENGTH_TOO_LONG);
        return 0;
    }

    if (PACKET_remaining(&psk_identity_hint) == 0) {
        OPENSSL_free(s->session->psk_identity_hint);
        s->session->psk_identity_hint = NULL;
    } else if (!PACKET_strndup(&psk_identity_hint,
                               &s->session->psk_identity_hint)) {
        *al = SSL_AD_INTERNAL_ERROR;
        return 0;
    }

    return 1;
#else
    SSLerr(SSL_F_TLS_PROCESS_SKE_PSK_PREAMBLE, ERR_R_INTERNAL_ERROR);
    *al = SSL_AD_INTERNAL_ERROR;
    return 0;
#endif
}

static int tls_process_ske_srp(SSL *s, PACKET *pkt, EVP_PKEY **pkey, int *al)
{
#ifndef OPENSSL_NO_SRP
    PACKET prime, generator, salt, server_pub;

    if (!PACKET_get_length_prefixed_2(pkt, &prime)
        || !PACKET_get_length_prefixed_2(pkt, &generator)
        || !PACKET_get_length_prefixed_1(pkt, &salt)
        || !PACKET_get_length_prefixed_2(pkt, &server_pub)) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_SRP, SSL_R_LENGTH_MISMATCH);
        return 0;
    }

    /* TODO(size_t): Convert BN_bin2bn() calls */
    if ((s->srp_ctx.N =
         BN_bin2bn(PACKET_data(&prime),
                   (int)PACKET_remaining(&prime), NULL)) == NULL
        || (s->srp_ctx.g =
            BN_bin2bn(PACKET_data(&generator),
                      (int)PACKET_remaining(&generator), NULL)) == NULL
        || (s->srp_ctx.s =
            BN_bin2bn(PACKET_data(&salt),
                      (int)PACKET_remaining(&salt), NULL)) == NULL
        || (s->srp_ctx.B =
            BN_bin2bn(PACKET_data(&server_pub),
                      (int)PACKET_remaining(&server_pub), NULL)) == NULL) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_SRP, ERR_R_BN_LIB);
        return 0;
    }

    if (!srp_verify_server_param(s, al)) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_SRP, SSL_R_BAD_SRP_PARAMETERS);
        return 0;
    }

    /* We must check if there is a certificate */
    if (s->s3->tmp.new_cipher->algorithm_auth & (SSL_aRSA | SSL_aDSS))
        *pkey = X509_get0_pubkey(s->session->peer);

    return 1;
#else
    SSLerr(SSL_F_TLS_PROCESS_SKE_SRP, ERR_R_INTERNAL_ERROR);
    *al = SSL_AD_INTERNAL_ERROR;
    return 0;
#endif
}

static int tls_process_ske_dhe(SSL *s, PACKET *pkt, EVP_PKEY **pkey, int *al)
{
#ifndef OPENSSL_NO_DH
    PACKET prime, generator, pub_key;
    EVP_PKEY *peer_tmp = NULL;

    DH *dh = NULL;
    BIGNUM *p = NULL, *g = NULL, *bnpub_key = NULL;

    int check_bits = 0;

    if (!PACKET_get_length_prefixed_2(pkt, &prime)
        || !PACKET_get_length_prefixed_2(pkt, &generator)
        || !PACKET_get_length_prefixed_2(pkt, &pub_key)) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_DHE, SSL_R_LENGTH_MISMATCH);
        return 0;
    }

    peer_tmp = EVP_PKEY_new();
    dh = DH_new();

    if (peer_tmp == NULL || dh == NULL) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_DHE, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    /* TODO(size_t): Convert these calls */
    p = BN_bin2bn(PACKET_data(&prime), (int)PACKET_remaining(&prime), NULL);
    g = BN_bin2bn(PACKET_data(&generator), (int)PACKET_remaining(&generator),
                  NULL);
    bnpub_key = BN_bin2bn(PACKET_data(&pub_key),
                          (int)PACKET_remaining(&pub_key), NULL);
    if (p == NULL || g == NULL || bnpub_key == NULL) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_DHE, ERR_R_BN_LIB);
        goto err;
    }

    /* test non-zero pupkey */
    if (BN_is_zero(bnpub_key)) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_DHE, SSL_R_BAD_DH_VALUE);
        goto err;
    }

    if (!DH_set0_pqg(dh, p, NULL, g)) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_DHE, ERR_R_BN_LIB);
        goto err;
    }
    p = g = NULL;

    if (DH_check_params(dh, &check_bits) == 0 || check_bits != 0) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_DHE, SSL_R_BAD_DH_VALUE);
        goto err;
    }

    if (!DH_set0_key(dh, bnpub_key, NULL)) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_DHE, ERR_R_BN_LIB);
        goto err;
    }
    bnpub_key = NULL;

    if (!ssl_security(s, SSL_SECOP_TMP_DH, DH_security_bits(dh), 0, dh)) {
        *al = SSL_AD_HANDSHAKE_FAILURE;
        SSLerr(SSL_F_TLS_PROCESS_SKE_DHE, SSL_R_DH_KEY_TOO_SMALL);
        goto err;
    }

    if (EVP_PKEY_assign_DH(peer_tmp, dh) == 0) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_DHE, ERR_R_EVP_LIB);
        goto err;
    }

    s->s3->peer_tmp = peer_tmp;

    /*
     * FIXME: This makes assumptions about which ciphersuites come with
     * public keys. We should have a less ad-hoc way of doing this
     */
    if (s->s3->tmp.new_cipher->algorithm_auth & (SSL_aRSA | SSL_aDSS))
        *pkey = X509_get0_pubkey(s->session->peer);
    /* else anonymous DH, so no certificate or pkey. */

    return 1;

 err:
    BN_free(p);
    BN_free(g);
    BN_free(bnpub_key);
    DH_free(dh);
    EVP_PKEY_free(peer_tmp);

    return 0;
#else
    SSLerr(SSL_F_TLS_PROCESS_SKE_DHE, ERR_R_INTERNAL_ERROR);
    *al = SSL_AD_INTERNAL_ERROR;
    return 0;
#endif
}

static int tls_process_ske_ecdhe(SSL *s, PACKET *pkt, EVP_PKEY **pkey, int *al)
{
#ifndef OPENSSL_NO_EC
    PACKET encoded_pt;
    const unsigned char *ecparams;
    int curve_nid;
    unsigned int curve_flags;
    EVP_PKEY_CTX *pctx = NULL;

    /*
     * Extract elliptic curve parameters and the server's ephemeral ECDH
     * public key. For now we only support named (not generic) curves and
     * ECParameters in this case is just three bytes.
     */
    if (!PACKET_get_bytes(pkt, &ecparams, 3)) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_ECDHE, SSL_R_LENGTH_TOO_SHORT);
        return 0;
    }
    /*
     * Check curve is one of our preferences, if not server has sent an
     * invalid curve. ECParameters is 3 bytes.
     */
    if (!tls1_check_curve(s, ecparams, 3)) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_ECDHE, SSL_R_WRONG_CURVE);
        return 0;
    }

    curve_nid = tls1_ec_curve_id2nid(*(ecparams + 2), &curve_flags);

    if (curve_nid == 0) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_ECDHE,
               SSL_R_UNABLE_TO_FIND_ECDH_PARAMETERS);
        return 0;
    }

    if ((curve_flags & TLS_CURVE_TYPE) == TLS_CURVE_CUSTOM) {
        EVP_PKEY *key = EVP_PKEY_new();

        if (key == NULL || !EVP_PKEY_set_type(key, curve_nid)) {
            *al = SSL_AD_INTERNAL_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_SKE_ECDHE, ERR_R_EVP_LIB);
            EVP_PKEY_free(key);
            return 0;
        }
        s->s3->peer_tmp = key;
    } else {
        /* Set up EVP_PKEY with named curve as parameters */
        pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
        if (pctx == NULL
            || EVP_PKEY_paramgen_init(pctx) <= 0
            || EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, curve_nid) <= 0
            || EVP_PKEY_paramgen(pctx, &s->s3->peer_tmp) <= 0) {
            *al = SSL_AD_INTERNAL_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_SKE_ECDHE, ERR_R_EVP_LIB);
            EVP_PKEY_CTX_free(pctx);
            return 0;
        }
        EVP_PKEY_CTX_free(pctx);
        pctx = NULL;
    }

    if (!PACKET_get_length_prefixed_1(pkt, &encoded_pt)) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_ECDHE, SSL_R_LENGTH_MISMATCH);
        return 0;
    }

    if (!EVP_PKEY_set1_tls_encodedpoint(s->s3->peer_tmp,
                                        PACKET_data(&encoded_pt),
                                        PACKET_remaining(&encoded_pt))) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SKE_ECDHE, SSL_R_BAD_ECPOINT);
        return 0;
    }

    /*
     * The ECC/TLS specification does not mention the use of DSA to sign
     * ECParameters in the server key exchange message. We do support RSA
     * and ECDSA.
     */
    if (s->s3->tmp.new_cipher->algorithm_auth & SSL_aECDSA)
        *pkey = X509_get0_pubkey(s->session->peer);
    else if (s->s3->tmp.new_cipher->algorithm_auth & SSL_aRSA)
        *pkey = X509_get0_pubkey(s->session->peer);
    /* else anonymous ECDH, so no certificate or pkey. */

    return 1;
#else
    SSLerr(SSL_F_TLS_PROCESS_SKE_ECDHE, ERR_R_INTERNAL_ERROR);
    *al = SSL_AD_INTERNAL_ERROR;
    return 0;
#endif
}

MSG_PROCESS_RETURN tls_process_key_exchange(SSL *s, PACKET *pkt)
{
    int al = -1;
    long alg_k;
    EVP_PKEY *pkey = NULL;
    EVP_MD_CTX *md_ctx = NULL;
    EVP_PKEY_CTX *pctx = NULL;
    PACKET save_param_start, signature;

    alg_k = s->s3->tmp.new_cipher->algorithm_mkey;

    save_param_start = *pkt;

#if !defined(OPENSSL_NO_EC) || !defined(OPENSSL_NO_DH)
    EVP_PKEY_free(s->s3->peer_tmp);
    s->s3->peer_tmp = NULL;
#endif

    if (alg_k & SSL_PSK) {
        if (!tls_process_ske_psk_preamble(s, pkt, &al))
            goto err;
    }

    /* Nothing else to do for plain PSK or RSAPSK */
    if (alg_k & (SSL_kPSK | SSL_kRSAPSK)) {
    } else if (alg_k & SSL_kSRP) {
        if (!tls_process_ske_srp(s, pkt, &pkey, &al))
            goto err;
    } else if (alg_k & (SSL_kDHE | SSL_kDHEPSK)) {
        if (!tls_process_ske_dhe(s, pkt, &pkey, &al))
            goto err;
    } else if (alg_k & (SSL_kECDHE | SSL_kECDHEPSK)) {
        if (!tls_process_ske_ecdhe(s, pkt, &pkey, &al))
            goto err;
    } else if (alg_k) {
        al = SSL_AD_UNEXPECTED_MESSAGE;
        SSLerr(SSL_F_TLS_PROCESS_KEY_EXCHANGE, SSL_R_UNEXPECTED_MESSAGE);
        goto err;
    }

    /* if it was signed, check the signature */
    if (pkey != NULL) {
        PACKET params;
        int maxsig;
        const EVP_MD *md = NULL;

        /*
         * |pkt| now points to the beginning of the signature, so the difference
         * equals the length of the parameters.
         */
        if (!PACKET_get_sub_packet(&save_param_start, &params,
                                   PACKET_remaining(&save_param_start) -
                                   PACKET_remaining(pkt))) {
            al = SSL_AD_INTERNAL_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_KEY_EXCHANGE, ERR_R_INTERNAL_ERROR);
            goto err;
        }

        if (SSL_USE_SIGALGS(s)) {
            unsigned int sigalg;
            int rv;

            if (!PACKET_get_net_2(pkt, &sigalg)) {
                al = SSL_AD_DECODE_ERROR;
                SSLerr(SSL_F_TLS_PROCESS_KEY_EXCHANGE, SSL_R_LENGTH_TOO_SHORT);
                goto err;
            }
            rv = tls12_check_peer_sigalg(s, sigalg, pkey);
            if (rv == -1) {
                al = SSL_AD_INTERNAL_ERROR;
                goto err;
            } else if (rv == 0) {
                al = SSL_AD_DECODE_ERROR;
                goto err;
            }
            md = ssl_md(s->s3->tmp.peer_sigalg->hash_idx);
#ifdef SSL_DEBUG
            fprintf(stderr, "USING TLSv1.2 HASH %s\n", EVP_MD_name(md));
#endif
        } else if (EVP_PKEY_id(pkey) == EVP_PKEY_RSA) {
            md = EVP_md5_sha1();
        } else {
            md = EVP_sha1();
        }

        if (!PACKET_get_length_prefixed_2(pkt, &signature)
            || PACKET_remaining(pkt) != 0) {
            al = SSL_AD_DECODE_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_KEY_EXCHANGE, SSL_R_LENGTH_MISMATCH);
            goto err;
        }
        maxsig = EVP_PKEY_size(pkey);
        if (maxsig < 0) {
            al = SSL_AD_INTERNAL_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_KEY_EXCHANGE, ERR_R_INTERNAL_ERROR);
            goto err;
        }

        /*
         * Check signature length
         */
        if (PACKET_remaining(&signature) > (size_t)maxsig) {
            /* wrong packet length */
            al = SSL_AD_DECODE_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_KEY_EXCHANGE,
                   SSL_R_WRONG_SIGNATURE_LENGTH);
            goto err;
        }

        md_ctx = EVP_MD_CTX_new();
        if (md_ctx == NULL) {
            al = SSL_AD_INTERNAL_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_KEY_EXCHANGE, ERR_R_MALLOC_FAILURE);
            goto err;
        }

        if (EVP_DigestVerifyInit(md_ctx, &pctx, md, NULL, pkey) <= 0) {
            al = SSL_AD_INTERNAL_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_KEY_EXCHANGE, ERR_R_EVP_LIB);
            goto err;
        }
        if (SSL_USE_PSS(s)) {
            if (EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) <= 0
                || EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx,
                                                RSA_PSS_SALTLEN_DIGEST) <= 0) {
                al = SSL_AD_INTERNAL_ERROR;
                SSLerr(SSL_F_TLS_PROCESS_KEY_EXCHANGE, ERR_R_EVP_LIB);
                goto err;
            }
        }
        if (EVP_DigestVerifyUpdate(md_ctx, &(s->s3->client_random[0]),
                                   SSL3_RANDOM_SIZE) <= 0
                || EVP_DigestVerifyUpdate(md_ctx, &(s->s3->server_random[0]),
                                          SSL3_RANDOM_SIZE) <= 0
                || EVP_DigestVerifyUpdate(md_ctx, PACKET_data(&params),
                                          PACKET_remaining(&params)) <= 0) {
            al = SSL_AD_INTERNAL_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_KEY_EXCHANGE, ERR_R_EVP_LIB);
            goto err;
        }
        if (EVP_DigestVerifyFinal(md_ctx, PACKET_data(&signature),
                                  PACKET_remaining(&signature)) <= 0) {
            /* bad signature */
            al = SSL_AD_DECRYPT_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_KEY_EXCHANGE, SSL_R_BAD_SIGNATURE);
            goto err;
        }
        EVP_MD_CTX_free(md_ctx);
        md_ctx = NULL;
    } else {
        /* aNULL, aSRP or PSK do not need public keys */
        if (!(s->s3->tmp.new_cipher->algorithm_auth & (SSL_aNULL | SSL_aSRP))
            && !(alg_k & SSL_PSK)) {
            /* Might be wrong key type, check it */
            if (ssl3_check_cert_and_algorithm(s)) {
                /* Otherwise this shouldn't happen */
                al = SSL_AD_INTERNAL_ERROR;
                SSLerr(SSL_F_TLS_PROCESS_KEY_EXCHANGE, ERR_R_INTERNAL_ERROR);
            } else {
                al = SSL_AD_DECODE_ERROR;
            }
            goto err;
        }
        /* still data left over */
        if (PACKET_remaining(pkt) != 0) {
            al = SSL_AD_DECODE_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_KEY_EXCHANGE, SSL_R_EXTRA_DATA_IN_MESSAGE);
            goto err;
        }
    }

    return MSG_PROCESS_CONTINUE_READING;
 err:
    if (al != -1)
        ssl3_send_alert(s, SSL3_AL_FATAL, al);
    ossl_statem_set_error(s);
    EVP_MD_CTX_free(md_ctx);
    return MSG_PROCESS_ERROR;
}

MSG_PROCESS_RETURN tls_process_certificate_request(SSL *s, PACKET *pkt)
{
    int ret = MSG_PROCESS_ERROR;
    unsigned int list_len, ctype_num, i, name_len;
    X509_NAME *xn = NULL;
    const unsigned char *data;
    const unsigned char *namestart, *namebytes;
    STACK_OF(X509_NAME) *ca_sk = NULL;

    if ((ca_sk = sk_X509_NAME_new(ca_dn_cmp)) == NULL) {
        SSLerr(SSL_F_TLS_PROCESS_CERTIFICATE_REQUEST, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    /* get the certificate types */
    if (!PACKET_get_1(pkt, &ctype_num)
        || !PACKET_get_bytes(pkt, &data, ctype_num)) {
        ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
        SSLerr(SSL_F_TLS_PROCESS_CERTIFICATE_REQUEST, SSL_R_LENGTH_MISMATCH);
        goto err;
    }
    OPENSSL_free(s->cert->ctypes);
    s->cert->ctypes = NULL;
    if (ctype_num > SSL3_CT_NUMBER) {
        /* If we exceed static buffer copy all to cert structure */
        s->cert->ctypes = OPENSSL_malloc(ctype_num);
        if (s->cert->ctypes == NULL) {
            SSLerr(SSL_F_TLS_PROCESS_CERTIFICATE_REQUEST, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        memcpy(s->cert->ctypes, data, ctype_num);
        s->cert->ctype_num = ctype_num;
        ctype_num = SSL3_CT_NUMBER;
    }
    for (i = 0; i < ctype_num; i++)
        s->s3->tmp.ctype[i] = data[i];

    if (SSL_USE_SIGALGS(s)) {
        PACKET sigalgs;

        if (!PACKET_get_length_prefixed_2(pkt, &sigalgs)) {
            ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
            SSLerr(SSL_F_TLS_PROCESS_CERTIFICATE_REQUEST,
                   SSL_R_LENGTH_MISMATCH);
            goto err;
        }

        /* Clear certificate digests and validity flags */
        for (i = 0; i < SSL_PKEY_NUM; i++) {
            s->s3->tmp.md[i] = NULL;
            s->s3->tmp.valid_flags[i] = 0;
        }
        if (!tls1_save_sigalgs(s, &sigalgs)) {
            ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
            SSLerr(SSL_F_TLS_PROCESS_CERTIFICATE_REQUEST,
                   SSL_R_SIGNATURE_ALGORITHMS_ERROR);
            goto err;
        }
        if (!tls1_process_sigalgs(s)) {
            ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_INTERNAL_ERROR);
            SSLerr(SSL_F_TLS_PROCESS_CERTIFICATE_REQUEST, ERR_R_MALLOC_FAILURE);
            goto err;
        }
    } else {
        ssl_set_default_md(s);
    }

    /* get the CA RDNs */
    if (!PACKET_get_net_2(pkt, &list_len)
        || PACKET_remaining(pkt) != list_len) {
        ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
        SSLerr(SSL_F_TLS_PROCESS_CERTIFICATE_REQUEST, SSL_R_LENGTH_MISMATCH);
        goto err;
    }

    while (PACKET_remaining(pkt)) {
        if (!PACKET_get_net_2(pkt, &name_len)
            || !PACKET_get_bytes(pkt, &namebytes, name_len)) {
            ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
            SSLerr(SSL_F_TLS_PROCESS_CERTIFICATE_REQUEST,
                   SSL_R_LENGTH_MISMATCH);
            goto err;
        }

        namestart = namebytes;

        if ((xn = d2i_X509_NAME(NULL, (const unsigned char **)&namebytes,
                                name_len)) == NULL) {
            ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
            SSLerr(SSL_F_TLS_PROCESS_CERTIFICATE_REQUEST, ERR_R_ASN1_LIB);
            goto err;
        }

        if (namebytes != (namestart + name_len)) {
            ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
            SSLerr(SSL_F_TLS_PROCESS_CERTIFICATE_REQUEST,
                   SSL_R_CA_DN_LENGTH_MISMATCH);
            goto err;
        }
        if (!sk_X509_NAME_push(ca_sk, xn)) {
            SSLerr(SSL_F_TLS_PROCESS_CERTIFICATE_REQUEST, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        xn = NULL;
    }

    /* we should setup a certificate to return.... */
    s->s3->tmp.cert_req = 1;
    s->s3->tmp.ctype_num = ctype_num;
    sk_X509_NAME_pop_free(s->s3->tmp.ca_names, X509_NAME_free);
    s->s3->tmp.ca_names = ca_sk;
    ca_sk = NULL;

    ret = MSG_PROCESS_CONTINUE_PROCESSING;
    goto done;
 err:
    ossl_statem_set_error(s);
 done:
    X509_NAME_free(xn);
    sk_X509_NAME_pop_free(ca_sk, X509_NAME_free);
    return ret;
}

static int ca_dn_cmp(const X509_NAME *const *a, const X509_NAME *const *b)
{
    return (X509_NAME_cmp(*a, *b));
}

MSG_PROCESS_RETURN tls_process_new_session_ticket(SSL *s, PACKET *pkt)
{
    int al = SSL_AD_DECODE_ERROR;
    unsigned int ticklen;
    unsigned long ticket_lifetime_hint, age_add = 0;
    unsigned int sess_len;
    RAW_EXTENSION *exts = NULL;

    if (!PACKET_get_net_4(pkt, &ticket_lifetime_hint)
        || (SSL_IS_TLS13(s) && !PACKET_get_net_4(pkt, &age_add))
        || !PACKET_get_net_2(pkt, &ticklen)
        || (!SSL_IS_TLS13(s) && PACKET_remaining(pkt) != ticklen)
        || (SSL_IS_TLS13(s)
            && (ticklen == 0 || PACKET_remaining(pkt) < ticklen))) {
        SSLerr(SSL_F_TLS_PROCESS_NEW_SESSION_TICKET, SSL_R_LENGTH_MISMATCH);
        goto f_err;
    }

    /*
     * Server is allowed to change its mind (in <=TLSv1.2) and send an empty
     * ticket. We already checked this TLSv1.3 case above, so it should never
     * be 0 here in that instance
     */
    if (ticklen == 0)
        return MSG_PROCESS_CONTINUE_READING;

    /* TODO(TLS1.3): Is this a suitable test for TLS1.3? */
    if (s->session->session_id_length > 0) {
        int i = s->session_ctx->session_cache_mode;
        SSL_SESSION *new_sess;
        /*
         * We reused an existing session, so we need to replace it with a new
         * one
         */
        if (i & SSL_SESS_CACHE_CLIENT) {
            /*
             * Remove the old session from the cache. We carry on if this fails
             */
            SSL_CTX_remove_session(s->session_ctx, s->session);
        }

        if ((new_sess = ssl_session_dup(s->session, 0)) == 0) {
            al = SSL_AD_INTERNAL_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_NEW_SESSION_TICKET, ERR_R_MALLOC_FAILURE);
            goto f_err;
        }

        SSL_SESSION_free(s->session);
        s->session = new_sess;
    }

    /*
     * Technically the cast to long here is not guaranteed by the C standard -
     * but we use it elsewhere, so this should be ok.
     */
    s->session->time = (long)time(NULL);

    OPENSSL_free(s->session->ext.tick);
    s->session->ext.tick = NULL;
    s->session->ext.ticklen = 0;

    s->session->ext.tick = OPENSSL_malloc(ticklen);
    if (s->session->ext.tick == NULL) {
        SSLerr(SSL_F_TLS_PROCESS_NEW_SESSION_TICKET, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    if (!PACKET_copy_bytes(pkt, s->session->ext.tick, ticklen)) {
        al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_NEW_SESSION_TICKET, SSL_R_LENGTH_MISMATCH);
        goto f_err;
    }

    s->session->ext.tick_lifetime_hint = ticket_lifetime_hint;
    s->session->ext.tick_age_add = age_add;
    s->session->ext.ticklen = ticklen;

    if (SSL_IS_TLS13(s)) {
        PACKET extpkt;

        if (!PACKET_as_length_prefixed_2(pkt, &extpkt)
                || !tls_collect_extensions(s, &extpkt,
                                           EXT_TLS1_3_NEW_SESSION_TICKET,
                                           &exts, &al)
                || !tls_parse_all_extensions(s, EXT_TLS1_3_NEW_SESSION_TICKET,
                                             exts, NULL, 0, &al)) {
            SSLerr(SSL_F_TLS_PROCESS_NEW_SESSION_TICKET, SSL_R_BAD_EXTENSION);
            goto f_err;
        }
    }

    /*
     * There are two ways to detect a resumed ticket session. One is to set
     * an appropriate session ID and then the server must return a match in
     * ServerHello. This allows the normal client session ID matching to work
     * and we know much earlier that the ticket has been accepted. The
     * other way is to set zero length session ID when the ticket is
     * presented and rely on the handshake to determine session resumption.
     * We choose the former approach because this fits in with assumptions
     * elsewhere in OpenSSL. The session ID is set to the SHA256 (or SHA1 is
     * SHA256 is disabled) hash of the ticket.
     */
    /*
     * TODO(size_t): we use sess_len here because EVP_Digest expects an int
     * but s->session->session_id_length is a size_t
     */
    if (!EVP_Digest(s->session->ext.tick, ticklen,
                    s->session->session_id, &sess_len,
                    EVP_sha256(), NULL)) {
        SSLerr(SSL_F_TLS_PROCESS_NEW_SESSION_TICKET, ERR_R_EVP_LIB);
        goto err;
    }
    s->session->session_id_length = sess_len;

    /* This is a standalone message in TLSv1.3, so there is no more to read */
    if (SSL_IS_TLS13(s)) {
        OPENSSL_free(exts);
        ssl_update_cache(s, SSL_SESS_CACHE_CLIENT);
        return MSG_PROCESS_FINISHED_READING;
    }

    return MSG_PROCESS_CONTINUE_READING;
 f_err:
    ssl3_send_alert(s, SSL3_AL_FATAL, al);
 err:
    ossl_statem_set_error(s);
    OPENSSL_free(exts);
    return MSG_PROCESS_ERROR;
}

/*
 * In TLSv1.3 this is called from the extensions code, otherwise it is used to
 * parse a separate message. Returns 1 on success or 0 on failure. On failure
 * |*al| is populated with a suitable alert code.
 */
int tls_process_cert_status_body(SSL *s, PACKET *pkt, int *al)
{
    size_t resplen;
    unsigned int type;

    if (!PACKET_get_1(pkt, &type)
        || type != TLSEXT_STATUSTYPE_ocsp) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CERT_STATUS_BODY,
               SSL_R_UNSUPPORTED_STATUS_TYPE);
        return 0;
    }
    if (!PACKET_get_net_3_len(pkt, &resplen)
        || PACKET_remaining(pkt) != resplen) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CERT_STATUS_BODY, SSL_R_LENGTH_MISMATCH);
        return 0;
    }
    s->ext.ocsp.resp = OPENSSL_malloc(resplen);
    if (s->ext.ocsp.resp == NULL) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CERT_STATUS_BODY, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    if (!PACKET_copy_bytes(pkt, s->ext.ocsp.resp, resplen)) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CERT_STATUS_BODY, SSL_R_LENGTH_MISMATCH);
        return 0;
    }
    s->ext.ocsp.resp_len = resplen;

    return 1;
}


MSG_PROCESS_RETURN tls_process_cert_status(SSL *s, PACKET *pkt)
{
    int al;

    if (!tls_process_cert_status_body(s, pkt, &al)) {
        ssl3_send_alert(s, SSL3_AL_FATAL, al);
        ossl_statem_set_error(s);
        return MSG_PROCESS_ERROR;
    }

    return MSG_PROCESS_CONTINUE_READING;
}

/*
 * Perform miscellaneous checks and processing after we have received the
 * server's initial flight. In TLS1.3 this is after the Server Finished message.
 * In <=TLS1.2 this is after the ServerDone message. Returns 1 on success or 0
 * on failure.
 */
int tls_process_initial_server_flight(SSL *s, int *al)
{
    /*
     * at this point we check that we have the required stuff from
     * the server
     */
    if (!ssl3_check_cert_and_algorithm(s)) {
        *al = SSL_AD_HANDSHAKE_FAILURE;
        return 0;
    }

    /*
     * Call the ocsp status callback if needed. The |ext.ocsp.resp| and
     * |ext.ocsp.resp_len| values will be set if we actually received a status
     * message, or NULL and -1 otherwise
     */
    if (s->ext.status_type != TLSEXT_STATUSTYPE_nothing
            && s->ctx->ext.status_cb != NULL) {
        int ret = s->ctx->ext.status_cb(s, s->ctx->ext.status_arg);

        if (ret == 0) {
            *al = SSL_AD_BAD_CERTIFICATE_STATUS_RESPONSE;
            SSLerr(SSL_F_TLS_PROCESS_INITIAL_SERVER_FLIGHT,
                   SSL_R_INVALID_STATUS_RESPONSE);
            return 0;
        }
        if (ret < 0) {
            *al = SSL_AD_INTERNAL_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_INITIAL_SERVER_FLIGHT,
                   ERR_R_MALLOC_FAILURE);
            return 0;
        }
    }
#ifndef OPENSSL_NO_CT
    if (s->ct_validation_callback != NULL) {
        /* Note we validate the SCTs whether or not we abort on error */
        if (!ssl_validate_ct(s) && (s->verify_mode & SSL_VERIFY_PEER)) {
            *al = SSL_AD_HANDSHAKE_FAILURE;
            return 0;
        }
    }
#endif

    return 1;
}

MSG_PROCESS_RETURN tls_process_server_done(SSL *s, PACKET *pkt)
{
    int al = SSL_AD_INTERNAL_ERROR;

    if (PACKET_remaining(pkt) > 0) {
        /* should contain no data */
        al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_SERVER_DONE, SSL_R_LENGTH_MISMATCH);
        goto err;
    }
#ifndef OPENSSL_NO_SRP
    if (s->s3->tmp.new_cipher->algorithm_mkey & SSL_kSRP) {
        if (SRP_Calc_A_param(s) <= 0) {
            SSLerr(SSL_F_TLS_PROCESS_SERVER_DONE, SSL_R_SRP_A_CALC);
            goto err;
        }
    }
#endif

    /*
     * Error queue messages are generated directly by this function
     */
    if (!tls_process_initial_server_flight(s, &al))
        goto err;

#ifndef OPENSSL_NO_SCTP
    /* Only applies to renegotiation */
    if (SSL_IS_DTLS(s) && BIO_dgram_is_sctp(SSL_get_wbio(s))
        && s->renegotiate != 0)
        return MSG_PROCESS_CONTINUE_PROCESSING;
    else
#endif
        return MSG_PROCESS_FINISHED_READING;

 err:
    ssl3_send_alert(s, SSL3_AL_FATAL, al);
    ossl_statem_set_error(s);
    return MSG_PROCESS_ERROR;
}

static int tls_construct_cke_psk_preamble(SSL *s, WPACKET *pkt, int *al)
{
#ifndef OPENSSL_NO_PSK
    int ret = 0;
    /*
     * The callback needs PSK_MAX_IDENTITY_LEN + 1 bytes to return a
     * \0-terminated identity. The last byte is for us for simulating
     * strnlen.
     */
    char identity[PSK_MAX_IDENTITY_LEN + 1];
    size_t identitylen = 0;
    unsigned char psk[PSK_MAX_PSK_LEN];
    unsigned char *tmppsk = NULL;
    char *tmpidentity = NULL;
    size_t psklen = 0;

    if (s->psk_client_callback == NULL) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_PSK_PREAMBLE, SSL_R_PSK_NO_CLIENT_CB);
        *al = SSL_AD_INTERNAL_ERROR;
        goto err;
    }

    memset(identity, 0, sizeof(identity));

    psklen = s->psk_client_callback(s, s->session->psk_identity_hint,
                                    identity, sizeof(identity) - 1,
                                    psk, sizeof(psk));

    if (psklen > PSK_MAX_PSK_LEN) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_PSK_PREAMBLE, ERR_R_INTERNAL_ERROR);
        *al = SSL_AD_HANDSHAKE_FAILURE;
        goto err;
    } else if (psklen == 0) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_PSK_PREAMBLE,
               SSL_R_PSK_IDENTITY_NOT_FOUND);
        *al = SSL_AD_HANDSHAKE_FAILURE;
        goto err;
    }

    identitylen = strlen(identity);
    if (identitylen > PSK_MAX_IDENTITY_LEN) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_PSK_PREAMBLE, ERR_R_INTERNAL_ERROR);
        *al = SSL_AD_HANDSHAKE_FAILURE;
        goto err;
    }

    tmppsk = OPENSSL_memdup(psk, psklen);
    tmpidentity = OPENSSL_strdup(identity);
    if (tmppsk == NULL || tmpidentity == NULL) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_PSK_PREAMBLE, ERR_R_MALLOC_FAILURE);
        *al = SSL_AD_INTERNAL_ERROR;
        goto err;
    }

    OPENSSL_free(s->s3->tmp.psk);
    s->s3->tmp.psk = tmppsk;
    s->s3->tmp.psklen = psklen;
    tmppsk = NULL;
    OPENSSL_free(s->session->psk_identity);
    s->session->psk_identity = tmpidentity;
    tmpidentity = NULL;

    if (!WPACKET_sub_memcpy_u16(pkt, identity, identitylen))  {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_PSK_PREAMBLE, ERR_R_INTERNAL_ERROR);
        *al = SSL_AD_INTERNAL_ERROR;
        goto err;
    }

    ret = 1;

 err:
    OPENSSL_cleanse(psk, psklen);
    OPENSSL_cleanse(identity, sizeof(identity));
    OPENSSL_clear_free(tmppsk, psklen);
    OPENSSL_clear_free(tmpidentity, identitylen);

    return ret;
#else
    SSLerr(SSL_F_TLS_CONSTRUCT_CKE_PSK_PREAMBLE, ERR_R_INTERNAL_ERROR);
    *al = SSL_AD_INTERNAL_ERROR;
    return 0;
#endif
}

static int tls_construct_cke_rsa(SSL *s, WPACKET *pkt, int *al)
{
#ifndef OPENSSL_NO_RSA
    unsigned char *encdata = NULL;
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *pctx = NULL;
    size_t enclen;
    unsigned char *pms = NULL;
    size_t pmslen = 0;

    if (s->session->peer == NULL) {
        /*
         * We should always have a server certificate with SSL_kRSA.
         */
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_RSA, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    pkey = X509_get0_pubkey(s->session->peer);
    if (EVP_PKEY_get0_RSA(pkey) == NULL) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_RSA, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    pmslen = SSL_MAX_MASTER_KEY_LENGTH;
    pms = OPENSSL_malloc(pmslen);
    if (pms == NULL) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_RSA, ERR_R_MALLOC_FAILURE);
        *al = SSL_AD_INTERNAL_ERROR;
        return 0;
    }

    pms[0] = s->client_version >> 8;
    pms[1] = s->client_version & 0xff;
    /* TODO(size_t): Convert this function */
    if (RAND_bytes(pms + 2, (int)(pmslen - 2)) <= 0) {
        goto err;
    }

    /* Fix buf for TLS and beyond */
    if (s->version > SSL3_VERSION && !WPACKET_start_sub_packet_u16(pkt)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_RSA, ERR_R_INTERNAL_ERROR);
        goto err;
    }
    pctx = EVP_PKEY_CTX_new(pkey, NULL);
    if (pctx == NULL || EVP_PKEY_encrypt_init(pctx) <= 0
        || EVP_PKEY_encrypt(pctx, NULL, &enclen, pms, pmslen) <= 0) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_RSA, ERR_R_EVP_LIB);
        goto err;
    }
    if (!WPACKET_allocate_bytes(pkt, enclen, &encdata)
            || EVP_PKEY_encrypt(pctx, encdata, &enclen, pms, pmslen) <= 0) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_RSA, SSL_R_BAD_RSA_ENCRYPT);
        goto err;
    }
    EVP_PKEY_CTX_free(pctx);
    pctx = NULL;
# ifdef PKCS1_CHECK
    if (s->options & SSL_OP_PKCS1_CHECK_1)
        (*p)[1]++;
    if (s->options & SSL_OP_PKCS1_CHECK_2)
        tmp_buf[0] = 0x70;
# endif

    /* Fix buf for TLS and beyond */
    if (s->version > SSL3_VERSION && !WPACKET_close(pkt)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_RSA, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    s->s3->tmp.pms = pms;
    s->s3->tmp.pmslen = pmslen;

    /* Log the premaster secret, if logging is enabled. */
    if (!ssl_log_rsa_client_key_exchange(s, encdata, enclen, pms, pmslen))
        goto err;

    return 1;
 err:
    OPENSSL_clear_free(pms, pmslen);
    EVP_PKEY_CTX_free(pctx);

    return 0;
#else
    SSLerr(SSL_F_TLS_CONSTRUCT_CKE_RSA, ERR_R_INTERNAL_ERROR);
    *al = SSL_AD_INTERNAL_ERROR;
    return 0;
#endif
}

static int tls_construct_cke_dhe(SSL *s, WPACKET *pkt, int *al)
{
#ifndef OPENSSL_NO_DH
    DH *dh_clnt = NULL;
    const BIGNUM *pub_key;
    EVP_PKEY *ckey = NULL, *skey = NULL;
    unsigned char *keybytes = NULL;

    skey = s->s3->peer_tmp;
    if (skey == NULL)
        goto err;

    ckey = ssl_generate_pkey(skey);
    if (ckey == NULL)
        goto err;

    dh_clnt = EVP_PKEY_get0_DH(ckey);

    if (dh_clnt == NULL || ssl_derive(s, ckey, skey, 0) == 0)
        goto err;

    /* send off the data */
    DH_get0_key(dh_clnt, &pub_key, NULL);
    if (!WPACKET_sub_allocate_bytes_u16(pkt, BN_num_bytes(pub_key), &keybytes))
        goto err;

    BN_bn2bin(pub_key, keybytes);
    EVP_PKEY_free(ckey);

    return 1;
 err:
    EVP_PKEY_free(ckey);
#endif
    SSLerr(SSL_F_TLS_CONSTRUCT_CKE_DHE, ERR_R_INTERNAL_ERROR);
    *al = SSL_AD_INTERNAL_ERROR;
    return 0;
}

static int tls_construct_cke_ecdhe(SSL *s, WPACKET *pkt, int *al)
{
#ifndef OPENSSL_NO_EC
    unsigned char *encodedPoint = NULL;
    size_t encoded_pt_len = 0;
    EVP_PKEY *ckey = NULL, *skey = NULL;
    int ret = 0;

    skey = s->s3->peer_tmp;
    if (skey == NULL) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_ECDHE, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    ckey = ssl_generate_pkey(skey);
    if (ckey == NULL) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_ECDHE, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    if (ssl_derive(s, ckey, skey, 0) == 0) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_ECDHE, ERR_R_EVP_LIB);
        goto err;
    }

    /* Generate encoding of client key */
    encoded_pt_len = EVP_PKEY_get1_tls_encodedpoint(ckey, &encodedPoint);

    if (encoded_pt_len == 0) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_ECDHE, ERR_R_EC_LIB);
        goto err;
    }

    if (!WPACKET_sub_memcpy_u8(pkt, encodedPoint, encoded_pt_len)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_ECDHE, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    ret = 1;
 err:
    OPENSSL_free(encodedPoint);
    EVP_PKEY_free(ckey);
    return ret;
#else
    SSLerr(SSL_F_TLS_CONSTRUCT_CKE_ECDHE, ERR_R_INTERNAL_ERROR);
    *al = SSL_AD_INTERNAL_ERROR;
    return 0;
#endif
}

static int tls_construct_cke_gost(SSL *s, WPACKET *pkt, int *al)
{
#ifndef OPENSSL_NO_GOST
    /* GOST key exchange message creation */
    EVP_PKEY_CTX *pkey_ctx = NULL;
    X509 *peer_cert;
    size_t msglen;
    unsigned int md_len;
    unsigned char shared_ukm[32], tmp[256];
    EVP_MD_CTX *ukm_hash = NULL;
    int dgst_nid = NID_id_GostR3411_94;
    unsigned char *pms = NULL;
    size_t pmslen = 0;

    if ((s->s3->tmp.new_cipher->algorithm_auth & SSL_aGOST12) != 0)
        dgst_nid = NID_id_GostR3411_2012_256;

    /*
     * Get server sertificate PKEY and create ctx from it
     */
    peer_cert = s->session->peer;
    if (!peer_cert) {
        *al = SSL_AD_HANDSHAKE_FAILURE;
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_GOST,
               SSL_R_NO_GOST_CERTIFICATE_SENT_BY_PEER);
        return 0;
    }

    pkey_ctx = EVP_PKEY_CTX_new(X509_get0_pubkey(peer_cert), NULL);
    if (pkey_ctx == NULL) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_GOST, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    /*
     * If we have send a certificate, and certificate key
     * parameters match those of server certificate, use
     * certificate key for key exchange
     */

    /* Otherwise, generate ephemeral key pair */
    pmslen = 32;
    pms = OPENSSL_malloc(pmslen);
    if (pms == NULL) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_GOST, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    if (EVP_PKEY_encrypt_init(pkey_ctx) <= 0
        /* Generate session key
         * TODO(size_t): Convert this function
         */
        || RAND_bytes(pms, (int)pmslen) <= 0) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_GOST, ERR_R_INTERNAL_ERROR);
        goto err;
    };
    /*
     * Compute shared IV and store it in algorithm-specific context
     * data
     */
    ukm_hash = EVP_MD_CTX_new();
    if (ukm_hash == NULL
        || EVP_DigestInit(ukm_hash, EVP_get_digestbynid(dgst_nid)) <= 0
        || EVP_DigestUpdate(ukm_hash, s->s3->client_random,
                            SSL3_RANDOM_SIZE) <= 0
        || EVP_DigestUpdate(ukm_hash, s->s3->server_random,
                            SSL3_RANDOM_SIZE) <= 0
        || EVP_DigestFinal_ex(ukm_hash, shared_ukm, &md_len) <= 0) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_GOST, ERR_R_INTERNAL_ERROR);
        goto err;
    }
    EVP_MD_CTX_free(ukm_hash);
    ukm_hash = NULL;
    if (EVP_PKEY_CTX_ctrl(pkey_ctx, -1, EVP_PKEY_OP_ENCRYPT,
                          EVP_PKEY_CTRL_SET_IV, 8, shared_ukm) < 0) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_GOST, SSL_R_LIBRARY_BUG);
        goto err;
    }
    /* Make GOST keytransport blob message */
    /*
     * Encapsulate it into sequence
     */
    msglen = 255;
    if (EVP_PKEY_encrypt(pkey_ctx, tmp, &msglen, pms, pmslen) <= 0) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_GOST, SSL_R_LIBRARY_BUG);
        goto err;
    }

    if (!WPACKET_put_bytes_u8(pkt, V_ASN1_SEQUENCE | V_ASN1_CONSTRUCTED)
            || (msglen >= 0x80 && !WPACKET_put_bytes_u8(pkt, 0x81))
            || !WPACKET_sub_memcpy_u8(pkt, tmp, msglen)) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_GOST, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    EVP_PKEY_CTX_free(pkey_ctx);
    s->s3->tmp.pms = pms;
    s->s3->tmp.pmslen = pmslen;

    return 1;
 err:
    EVP_PKEY_CTX_free(pkey_ctx);
    OPENSSL_clear_free(pms, pmslen);
    EVP_MD_CTX_free(ukm_hash);
    return 0;
#else
    SSLerr(SSL_F_TLS_CONSTRUCT_CKE_GOST, ERR_R_INTERNAL_ERROR);
    *al = SSL_AD_INTERNAL_ERROR;
    return 0;
#endif
}

static int tls_construct_cke_srp(SSL *s, WPACKET *pkt, int *al)
{
#ifndef OPENSSL_NO_SRP
    unsigned char *abytes = NULL;

    if (s->srp_ctx.A == NULL
            || !WPACKET_sub_allocate_bytes_u16(pkt, BN_num_bytes(s->srp_ctx.A),
                                               &abytes)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_SRP, ERR_R_INTERNAL_ERROR);
        return 0;
    }
    BN_bn2bin(s->srp_ctx.A, abytes);

    OPENSSL_free(s->session->srp_username);
    s->session->srp_username = OPENSSL_strdup(s->srp_ctx.login);
    if (s->session->srp_username == NULL) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CKE_SRP, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    return 1;
#else
    SSLerr(SSL_F_TLS_CONSTRUCT_CKE_SRP, ERR_R_INTERNAL_ERROR);
    *al = SSL_AD_INTERNAL_ERROR;
    return 0;
#endif
}

int tls_construct_client_key_exchange(SSL *s, WPACKET *pkt)
{
    unsigned long alg_k;
    int al = -1;

    alg_k = s->s3->tmp.new_cipher->algorithm_mkey;

    if ((alg_k & SSL_PSK)
        && !tls_construct_cke_psk_preamble(s, pkt, &al))
        goto err;

    if (alg_k & (SSL_kRSA | SSL_kRSAPSK)) {
        if (!tls_construct_cke_rsa(s, pkt, &al))
            goto err;
    } else if (alg_k & (SSL_kDHE | SSL_kDHEPSK)) {
        if (!tls_construct_cke_dhe(s, pkt, &al))
            goto err;
    } else if (alg_k & (SSL_kECDHE | SSL_kECDHEPSK)) {
        if (!tls_construct_cke_ecdhe(s, pkt, &al))
            goto err;
    } else if (alg_k & SSL_kGOST) {
        if (!tls_construct_cke_gost(s, pkt, &al))
            goto err;
    } else if (alg_k & SSL_kSRP) {
        if (!tls_construct_cke_srp(s, pkt, &al))
            goto err;
    } else if (!(alg_k & SSL_kPSK)) {
        ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_HANDSHAKE_FAILURE);
        SSLerr(SSL_F_TLS_CONSTRUCT_CLIENT_KEY_EXCHANGE, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    return 1;
 err:
    if (al != -1)
        ssl3_send_alert(s, SSL3_AL_FATAL, al);
    OPENSSL_clear_free(s->s3->tmp.pms, s->s3->tmp.pmslen);
    s->s3->tmp.pms = NULL;
#ifndef OPENSSL_NO_PSK
    OPENSSL_clear_free(s->s3->tmp.psk, s->s3->tmp.psklen);
    s->s3->tmp.psk = NULL;
#endif
    return 0;
}

int tls_client_key_exchange_post_work(SSL *s)
{
    unsigned char *pms = NULL;
    size_t pmslen = 0;

    pms = s->s3->tmp.pms;
    pmslen = s->s3->tmp.pmslen;

#ifndef OPENSSL_NO_SRP
    /* Check for SRP */
    if (s->s3->tmp.new_cipher->algorithm_mkey & SSL_kSRP) {
        if (!srp_generate_client_master_secret(s)) {
            SSLerr(SSL_F_TLS_CLIENT_KEY_EXCHANGE_POST_WORK,
                   ERR_R_INTERNAL_ERROR);
            goto err;
        }
        return 1;
    }
#endif

    if (pms == NULL && !(s->s3->tmp.new_cipher->algorithm_mkey & SSL_kPSK)) {
        ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_INTERNAL_ERROR);
        SSLerr(SSL_F_TLS_CLIENT_KEY_EXCHANGE_POST_WORK, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    if (!ssl_generate_master_secret(s, pms, pmslen, 1)) {
        ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_INTERNAL_ERROR);
        SSLerr(SSL_F_TLS_CLIENT_KEY_EXCHANGE_POST_WORK, ERR_R_INTERNAL_ERROR);
        /* ssl_generate_master_secret frees the pms even on error */
        pms = NULL;
        pmslen = 0;
        goto err;
    }
    pms = NULL;
    pmslen = 0;

#ifndef OPENSSL_NO_SCTP
    if (SSL_IS_DTLS(s)) {
        unsigned char sctpauthkey[64];
        char labelbuffer[sizeof(DTLS1_SCTP_AUTH_LABEL)];

        /*
         * Add new shared key for SCTP-Auth, will be ignored if no SCTP
         * used.
         */
        memcpy(labelbuffer, DTLS1_SCTP_AUTH_LABEL,
               sizeof(DTLS1_SCTP_AUTH_LABEL));

        if (SSL_export_keying_material(s, sctpauthkey,
                                       sizeof(sctpauthkey), labelbuffer,
                                       sizeof(labelbuffer), NULL, 0, 0) <= 0)
            goto err;

        BIO_ctrl(SSL_get_wbio(s), BIO_CTRL_DGRAM_SCTP_ADD_AUTH_KEY,
                 sizeof(sctpauthkey), sctpauthkey);
    }
#endif

    return 1;
 err:
    OPENSSL_clear_free(pms, pmslen);
    s->s3->tmp.pms = NULL;
    return 0;
}

/*
 * Check a certificate can be used for client authentication. Currently check
 * cert exists, if we have a suitable digest for TLS 1.2 if static DH client
 * certificates can be used and optionally checks suitability for Suite B.
 */
static int ssl3_check_client_certificate(SSL *s)
{
    if (!s->cert || !s->cert->key->x509 || !s->cert->key->privatekey)
        return 0;
    /* If no suitable signature algorithm can't use certificate */
    if (SSL_USE_SIGALGS(s) && !s->s3->tmp.md[s->cert->key - s->cert->pkeys])
        return 0;
    /*
     * If strict mode check suitability of chain before using it. This also
     * adjusts suite B digest if necessary.
     */
    if (s->cert->cert_flags & SSL_CERT_FLAGS_CHECK_TLS_STRICT &&
        !tls1_check_chain(s, NULL, NULL, NULL, -2))
        return 0;
    return 1;
}

WORK_STATE tls_prepare_client_certificate(SSL *s, WORK_STATE wst)
{
    X509 *x509 = NULL;
    EVP_PKEY *pkey = NULL;
    int i;

    if (wst == WORK_MORE_A) {
        /* Let cert callback update client certificates if required */
        if (s->cert->cert_cb) {
            i = s->cert->cert_cb(s, s->cert->cert_cb_arg);
            if (i < 0) {
                s->rwstate = SSL_X509_LOOKUP;
                return WORK_MORE_A;
            }
            if (i == 0) {
                ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_INTERNAL_ERROR);
                ossl_statem_set_error(s);
                return 0;
            }
            s->rwstate = SSL_NOTHING;
        }
        if (ssl3_check_client_certificate(s))
            return WORK_FINISHED_CONTINUE;

        /* Fall through to WORK_MORE_B */
        wst = WORK_MORE_B;
    }

    /* We need to get a client cert */
    if (wst == WORK_MORE_B) {
        /*
         * If we get an error, we need to ssl->rwstate=SSL_X509_LOOKUP;
         * return(-1); We then get retied later
         */
        i = ssl_do_client_cert_cb(s, &x509, &pkey);
        if (i < 0) {
            s->rwstate = SSL_X509_LOOKUP;
            return WORK_MORE_B;
        }
        s->rwstate = SSL_NOTHING;
        if ((i == 1) && (pkey != NULL) && (x509 != NULL)) {
            if (!SSL_use_certificate(s, x509) || !SSL_use_PrivateKey(s, pkey))
                i = 0;
        } else if (i == 1) {
            i = 0;
            SSLerr(SSL_F_TLS_PREPARE_CLIENT_CERTIFICATE,
                   SSL_R_BAD_DATA_RETURNED_BY_CALLBACK);
        }

        X509_free(x509);
        EVP_PKEY_free(pkey);
        if (i && !ssl3_check_client_certificate(s))
            i = 0;
        if (i == 0) {
            if (s->version == SSL3_VERSION) {
                s->s3->tmp.cert_req = 0;
                ssl3_send_alert(s, SSL3_AL_WARNING, SSL_AD_NO_CERTIFICATE);
                return WORK_FINISHED_CONTINUE;
            } else {
                s->s3->tmp.cert_req = 2;
                if (!ssl3_digest_cached_records(s, 0)) {
                    ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_INTERNAL_ERROR);
                    ossl_statem_set_error(s);
                    return 0;
                }
            }
        }

        return WORK_FINISHED_CONTINUE;
    }

    /* Shouldn't ever get here */
    return WORK_ERROR;
}

int tls_construct_client_certificate(SSL *s, WPACKET *pkt)
{
    int al = SSL_AD_INTERNAL_ERROR;

    /*
     * TODO(TLS1.3): For now we must put an empty context. Needs to be filled in
     * later
     */
    if ((SSL_IS_TLS13(s) && !WPACKET_put_bytes_u8(pkt, 0))
            || !ssl3_output_cert_chain(s, pkt,
                               (s->s3->tmp.cert_req == 2) ? NULL
                                                          : s->cert->key,
                                &al)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CLIENT_CERTIFICATE, ERR_R_INTERNAL_ERROR);
        ssl3_send_alert(s, SSL3_AL_FATAL, al);
        return 0;
    }

    return 1;
}

#define has_bits(i,m)   (((i)&(m)) == (m))

int ssl3_check_cert_and_algorithm(SSL *s)
{
    int i;
#ifndef OPENSSL_NO_EC
    int idx;
#endif
    long alg_k, alg_a;
    EVP_PKEY *pkey = NULL;
    int al = SSL_AD_HANDSHAKE_FAILURE;

    alg_k = s->s3->tmp.new_cipher->algorithm_mkey;
    alg_a = s->s3->tmp.new_cipher->algorithm_auth;

    /* we don't have a certificate */
    if ((alg_a & SSL_aNULL) || (alg_k & SSL_kPSK))
        return (1);

    /* This is the passed certificate */

#ifndef OPENSSL_NO_EC
    idx = s->session->peer_type;
    if (idx == SSL_PKEY_ECC) {
        if (ssl_check_srvr_ecc_cert_and_alg(s->session->peer, s) == 0) {
            /* check failed */
            SSLerr(SSL_F_SSL3_CHECK_CERT_AND_ALGORITHM, SSL_R_BAD_ECC_CERT);
            goto f_err;
        } else {
            return 1;
        }
    } else if (alg_a & SSL_aECDSA) {
        SSLerr(SSL_F_SSL3_CHECK_CERT_AND_ALGORITHM,
               SSL_R_MISSING_ECDSA_SIGNING_CERT);
        goto f_err;
    }
#endif
    pkey = X509_get0_pubkey(s->session->peer);
    i = X509_certificate_type(s->session->peer, pkey);

    /* Check that we have a certificate if we require one */
    if ((alg_a & SSL_aRSA) && !has_bits(i, EVP_PK_RSA | EVP_PKT_SIGN)) {
        SSLerr(SSL_F_SSL3_CHECK_CERT_AND_ALGORITHM,
               SSL_R_MISSING_RSA_SIGNING_CERT);
        goto f_err;
    }
#ifndef OPENSSL_NO_DSA
    else if ((alg_a & SSL_aDSS) && !has_bits(i, EVP_PK_DSA | EVP_PKT_SIGN)) {
        SSLerr(SSL_F_SSL3_CHECK_CERT_AND_ALGORITHM,
               SSL_R_MISSING_DSA_SIGNING_CERT);
        goto f_err;
    }
#endif
#ifndef OPENSSL_NO_RSA
    if (alg_k & (SSL_kRSA | SSL_kRSAPSK) &&
        !has_bits(i, EVP_PK_RSA | EVP_PKT_ENC)) {
        SSLerr(SSL_F_SSL3_CHECK_CERT_AND_ALGORITHM,
               SSL_R_MISSING_RSA_ENCRYPTING_CERT);
        goto f_err;
    }
#endif
#ifndef OPENSSL_NO_DH
    if ((alg_k & SSL_kDHE) && (s->s3->peer_tmp == NULL)) {
        al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_SSL3_CHECK_CERT_AND_ALGORITHM, ERR_R_INTERNAL_ERROR);
        goto f_err;
    }
#endif

    return (1);
 f_err:
    ssl3_send_alert(s, SSL3_AL_FATAL, al);
    return (0);
}

#ifndef OPENSSL_NO_NEXTPROTONEG
int tls_construct_next_proto(SSL *s, WPACKET *pkt)
{
    size_t len, padding_len;
    unsigned char *padding = NULL;

    len = s->ext.npn_len;
    padding_len = 32 - ((len + 2) % 32);

    if (!WPACKET_sub_memcpy_u8(pkt, s->ext.npn, len)
            || !WPACKET_sub_allocate_bytes_u8(pkt, padding_len, &padding)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_NEXT_PROTO, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    memset(padding, 0, padding_len);

    return 1;
 err:
    ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_INTERNAL_ERROR);
    return 0;
}
#endif

MSG_PROCESS_RETURN tls_process_hello_req(SSL *s, PACKET *pkt)
{
    if (PACKET_remaining(pkt) > 0) {
        /* should contain no data */
        SSLerr(SSL_F_TLS_PROCESS_HELLO_REQ, SSL_R_LENGTH_MISMATCH);
        ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_DECODE_ERROR);
        ossl_statem_set_error(s);
        return MSG_PROCESS_ERROR;
    }

    /*
     * This is a historical discrepancy (not in the RFC) maintained for
     * compatibility reasons. If a TLS client receives a HelloRequest it will
     * attempt an abbreviated handshake. However if a DTLS client receives a
     * HelloRequest it will do a full handshake. Either behaviour is reasonable
     * but doing one for TLS and another for DTLS is odd.
     */
    if (SSL_IS_DTLS(s))
        SSL_renegotiate(s);
    else
        SSL_renegotiate_abbreviated(s);

    return MSG_PROCESS_FINISHED_READING;
}

static MSG_PROCESS_RETURN tls_process_encrypted_extensions(SSL *s, PACKET *pkt)
{
    int al = SSL_AD_INTERNAL_ERROR;
    PACKET extensions;
    RAW_EXTENSION *rawexts = NULL;

    if (!PACKET_as_length_prefixed_2(pkt, &extensions)) {
        al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_ENCRYPTED_EXTENSIONS, SSL_R_LENGTH_MISMATCH);
        goto err;
    }

    if (!tls_collect_extensions(s, &extensions, EXT_TLS1_3_ENCRYPTED_EXTENSIONS,
                                &rawexts, &al)
            || !tls_parse_all_extensions(s, EXT_TLS1_3_ENCRYPTED_EXTENSIONS,
                                         rawexts, NULL, 0, &al))
        goto err;

    OPENSSL_free(rawexts);
    return MSG_PROCESS_CONTINUE_READING;

 err:
    ssl3_send_alert(s, SSL3_AL_FATAL, al);
    ossl_statem_set_error(s);
    OPENSSL_free(rawexts);
    return MSG_PROCESS_ERROR;
}

int ssl_do_client_cert_cb(SSL *s, X509 **px509, EVP_PKEY **ppkey)
{
    int i = 0;
#ifndef OPENSSL_NO_ENGINE
    if (s->ctx->client_cert_engine) {
        i = ENGINE_load_ssl_client_cert(s->ctx->client_cert_engine, s,
                                        SSL_get_client_CA_list(s),
                                        px509, ppkey, NULL, NULL, NULL);
        if (i != 0)
            return i;
    }
#endif
    if (s->ctx->client_cert_cb)
        i = s->ctx->client_cert_cb(s, px509, ppkey);
    return i;
}

int ssl_cipher_list_to_bytes(SSL *s, STACK_OF(SSL_CIPHER) *sk, WPACKET *pkt)
{
    int i;
    size_t totlen = 0, len, maxlen;
    int empty_reneg_info_scsv = !s->renegotiate;
    /* Set disabled masks for this session */
    ssl_set_client_disabled(s);

    if (sk == NULL)
        return (0);

#ifdef OPENSSL_MAX_TLS1_2_CIPHER_LENGTH
# if OPENSSL_MAX_TLS1_2_CIPHER_LENGTH < 6
#  error Max cipher length too short
# endif
    /*
     * Some servers hang if client hello > 256 bytes as hack workaround
     * chop number of supported ciphers to keep it well below this if we
     * use TLS v1.2
     */
    if (TLS1_get_version(s) >= TLS1_2_VERSION)
        maxlen = OPENSSL_MAX_TLS1_2_CIPHER_LENGTH & ~1;
    else
#endif
        /* Maximum length that can be stored in 2 bytes. Length must be even */
        maxlen = 0xfffe;

    if (empty_reneg_info_scsv)
        maxlen -= 2;
    if (s->mode & SSL_MODE_SEND_FALLBACK_SCSV)
        maxlen -= 2;

    for (i = 0; i < sk_SSL_CIPHER_num(sk) && totlen < maxlen; i++) {
        const SSL_CIPHER *c;

        c = sk_SSL_CIPHER_value(sk, i);
        /* Skip disabled ciphers */
        if (ssl_cipher_disabled(s, c, SSL_SECOP_CIPHER_SUPPORTED))
            continue;

        if (!s->method->put_cipher_by_char(c, pkt, &len)) {
            SSLerr(SSL_F_SSL_CIPHER_LIST_TO_BYTES, ERR_R_INTERNAL_ERROR);
            return 0;
        }

        totlen += len;
    }

    if (totlen == 0) {
        SSLerr(SSL_F_SSL_CIPHER_LIST_TO_BYTES, SSL_R_NO_CIPHERS_AVAILABLE);
        return 0;
    }

    if (totlen != 0) {
        if (empty_reneg_info_scsv) {
            static SSL_CIPHER scsv = {
                0, NULL, SSL3_CK_SCSV, 0, 0, 0, 0, 0, 0, 0, 0, 0
            };
            if (!s->method->put_cipher_by_char(&scsv, pkt, &len)) {
                SSLerr(SSL_F_SSL_CIPHER_LIST_TO_BYTES, ERR_R_INTERNAL_ERROR);
                return 0;
            }
        }
        if (s->mode & SSL_MODE_SEND_FALLBACK_SCSV) {
            static SSL_CIPHER scsv = {
                0, NULL, SSL3_CK_FALLBACK_SCSV, 0, 0, 0, 0, 0, 0, 0, 0, 0
            };
            if (!s->method->put_cipher_by_char(&scsv, pkt, &len)) {
                SSLerr(SSL_F_SSL_CIPHER_LIST_TO_BYTES, ERR_R_INTERNAL_ERROR);
                return 0;
            }
        }
    }

    return 1;
}
