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
#include "../ssl_locl.h"
#include "statem_locl.h"
#include "internal/constant_time_locl.h"
#include <openssl/buffer.h>
#include <openssl/rand.h>
#include <openssl/objects.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/x509.h>
#include <openssl/dh.h>
#include <openssl/bn.h>
#include <openssl/md5.h>

static int tls_construct_encrypted_extensions(SSL *s, WPACKET *pkt);
static STACK_OF(SSL_CIPHER) *ssl_bytes_to_cipher_list(SSL *s,
                                                      PACKET *cipher_suites,
                                                      STACK_OF(SSL_CIPHER)
                                                      **skp, int sslv2format,
                                                      int *al);

/*
 * ossl_statem_server13_read_transition() encapsulates the logic for the allowed
 * handshake state transitions when a TLSv1.3 server is reading messages from
 * the client. The message type that the client has sent is provided in |mt|.
 * The current state is in |s->statem.hand_state|.
 *
 * Return values are 1 for success (transition allowed) and  0 on error
 * (transition not allowed)
 */
static int ossl_statem_server13_read_transition(SSL *s, int mt)
{
    OSSL_STATEM *st = &s->statem;

    /*
     * TODO(TLS1.3): This is still based on the TLSv1.2 state machine. Over time
     * we will update this to look more like real TLSv1.3
     */

    /*
     * Note: There is no case for TLS_ST_BEFORE because at that stage we have
     * not negotiated TLSv1.3 yet, so that case is handled by
     * ossl_statem_server_read_transition()
     */
    switch (st->hand_state) {
    default:
        break;

    case TLS_ST_SW_FINISHED:
        if (s->s3->tmp.cert_request) {
            if (mt == SSL3_MT_CERTIFICATE) {
                st->hand_state = TLS_ST_SR_CERT;
                return 1;
            }
        } else {
            if (mt == SSL3_MT_FINISHED) {
                st->hand_state = TLS_ST_SR_FINISHED;
                return 1;
            }
        }
        break;

    case TLS_ST_SR_CERT:
        if (s->session->peer == NULL) {
            if (mt == SSL3_MT_FINISHED) {
                st->hand_state = TLS_ST_SR_FINISHED;
                return 1;
            }
        } else {
            if (mt == SSL3_MT_CERTIFICATE_VERIFY) {
                st->hand_state = TLS_ST_SR_CERT_VRFY;
                return 1;
            }
        }
        break;

    case TLS_ST_SR_CERT_VRFY:
        if (mt == SSL3_MT_FINISHED) {
            st->hand_state = TLS_ST_SR_FINISHED;
            return 1;
        }
        break;
    }

    /* No valid transition found */
    ssl3_send_alert(s, SSL3_AL_FATAL, SSL3_AD_UNEXPECTED_MESSAGE);
    SSLerr(SSL_F_OSSL_STATEM_SERVER13_READ_TRANSITION,
           SSL_R_UNEXPECTED_MESSAGE);
    return 0;
}

/*
 * ossl_statem_server_read_transition() encapsulates the logic for the allowed
 * handshake state transitions when the server is reading messages from the
 * client. The message type that the client has sent is provided in |mt|. The
 * current state is in |s->statem.hand_state|.
 *
 * Return values are 1 for success (transition allowed) and  0 on error
 * (transition not allowed)
 */
int ossl_statem_server_read_transition(SSL *s, int mt)
{
    OSSL_STATEM *st = &s->statem;

    if (SSL_IS_TLS13(s)) {
        if (!ossl_statem_server13_read_transition(s, mt))
            goto err;
        return 1;
    }

    switch (st->hand_state) {
    default:
        break;

    case TLS_ST_BEFORE:
    case TLS_ST_OK:
    case DTLS_ST_SW_HELLO_VERIFY_REQUEST:
        if (mt == SSL3_MT_CLIENT_HELLO) {
            st->hand_state = TLS_ST_SR_CLNT_HELLO;
            return 1;
        }
        break;

    case TLS_ST_SW_SRVR_DONE:
        /*
         * If we get a CKE message after a ServerDone then either
         * 1) We didn't request a Certificate
         * OR
         * 2) If we did request one then
         *      a) We allow no Certificate to be returned
         *      AND
         *      b) We are running SSL3 (in TLS1.0+ the client must return a 0
         *         list if we requested a certificate)
         */
        if (mt == SSL3_MT_CLIENT_KEY_EXCHANGE) {
            if (s->s3->tmp.cert_request) {
                if (s->version == SSL3_VERSION) {
                    if ((s->verify_mode & SSL_VERIFY_PEER)
                        && (s->verify_mode & SSL_VERIFY_FAIL_IF_NO_PEER_CERT)) {
                        /*
                         * This isn't an unexpected message as such - we're just
                         * not going to accept it because we require a client
                         * cert.
                         */
                        ssl3_send_alert(s, SSL3_AL_FATAL,
                                        SSL3_AD_HANDSHAKE_FAILURE);
                        SSLerr(SSL_F_OSSL_STATEM_SERVER_READ_TRANSITION,
                               SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE);
                        return 0;
                    }
                    st->hand_state = TLS_ST_SR_KEY_EXCH;
                    return 1;
                }
            } else {
                st->hand_state = TLS_ST_SR_KEY_EXCH;
                return 1;
            }
        } else if (s->s3->tmp.cert_request) {
            if (mt == SSL3_MT_CERTIFICATE) {
                st->hand_state = TLS_ST_SR_CERT;
                return 1;
            }
        }
        break;

    case TLS_ST_SR_CERT:
        if (mt == SSL3_MT_CLIENT_KEY_EXCHANGE) {
            st->hand_state = TLS_ST_SR_KEY_EXCH;
            return 1;
        }
        break;

    case TLS_ST_SR_KEY_EXCH:
        /*
         * We should only process a CertificateVerify message if we have
         * received a Certificate from the client. If so then |s->session->peer|
         * will be non NULL. In some instances a CertificateVerify message is
         * not required even if the peer has sent a Certificate (e.g. such as in
         * the case of static DH). In that case |st->no_cert_verify| should be
         * set.
         */
        if (s->session->peer == NULL || st->no_cert_verify) {
            if (mt == SSL3_MT_CHANGE_CIPHER_SPEC) {
                /*
                 * For the ECDH ciphersuites when the client sends its ECDH
                 * pub key in a certificate, the CertificateVerify message is
                 * not sent. Also for GOST ciphersuites when the client uses
                 * its key from the certificate for key exchange.
                 */
                st->hand_state = TLS_ST_SR_CHANGE;
                return 1;
            }
        } else {
            if (mt == SSL3_MT_CERTIFICATE_VERIFY) {
                st->hand_state = TLS_ST_SR_CERT_VRFY;
                return 1;
            }
        }
        break;

    case TLS_ST_SR_CERT_VRFY:
        if (mt == SSL3_MT_CHANGE_CIPHER_SPEC) {
            st->hand_state = TLS_ST_SR_CHANGE;
            return 1;
        }
        break;

    case TLS_ST_SR_CHANGE:
#ifndef OPENSSL_NO_NEXTPROTONEG
        if (s->s3->npn_seen) {
            if (mt == SSL3_MT_NEXT_PROTO) {
                st->hand_state = TLS_ST_SR_NEXT_PROTO;
                return 1;
            }
        } else {
#endif
            if (mt == SSL3_MT_FINISHED) {
                st->hand_state = TLS_ST_SR_FINISHED;
                return 1;
            }
#ifndef OPENSSL_NO_NEXTPROTONEG
        }
#endif
        break;

#ifndef OPENSSL_NO_NEXTPROTONEG
    case TLS_ST_SR_NEXT_PROTO:
        if (mt == SSL3_MT_FINISHED) {
            st->hand_state = TLS_ST_SR_FINISHED;
            return 1;
        }
        break;
#endif

    case TLS_ST_SW_FINISHED:
        if (mt == SSL3_MT_CHANGE_CIPHER_SPEC) {
            st->hand_state = TLS_ST_SR_CHANGE;
            return 1;
        }
        break;
    }

 err:
    /* No valid transition found */
    ssl3_send_alert(s, SSL3_AL_FATAL, SSL3_AD_UNEXPECTED_MESSAGE);
    SSLerr(SSL_F_OSSL_STATEM_SERVER_READ_TRANSITION, SSL_R_UNEXPECTED_MESSAGE);
    return 0;
}

/*
 * Should we send a ServerKeyExchange message?
 *
 * Valid return values are:
 *   1: Yes
 *   0: No
 */
static int send_server_key_exchange(SSL *s)
{
    unsigned long alg_k = s->s3->tmp.new_cipher->algorithm_mkey;

    /*
     * only send a ServerKeyExchange if DH or fortezza but we have a
     * sign only certificate PSK: may send PSK identity hints For
     * ECC ciphersuites, we send a serverKeyExchange message only if
     * the cipher suite is either ECDH-anon or ECDHE. In other cases,
     * the server certificate contains the server's public key for
     * key exchange.
     */
    if (alg_k & (SSL_kDHE | SSL_kECDHE)
        /*
         * PSK: send ServerKeyExchange if PSK identity hint if
         * provided
         */
#ifndef OPENSSL_NO_PSK
        /* Only send SKE if we have identity hint for plain PSK */
        || ((alg_k & (SSL_kPSK | SSL_kRSAPSK))
            && s->cert->psk_identity_hint)
        /* For other PSK always send SKE */
        || (alg_k & (SSL_PSK & (SSL_kDHEPSK | SSL_kECDHEPSK)))
#endif
#ifndef OPENSSL_NO_SRP
        /* SRP: send ServerKeyExchange */
        || (alg_k & SSL_kSRP)
#endif
        ) {
        return 1;
    }

    return 0;
}

/*
 * Should we send a CertificateRequest message?
 *
 * Valid return values are:
 *   1: Yes
 *   0: No
 */
static int send_certificate_request(SSL *s)
{
    if (
           /* don't request cert unless asked for it: */
           s->verify_mode & SSL_VERIFY_PEER
           /*
            * if SSL_VERIFY_CLIENT_ONCE is set, don't request cert
            * during re-negotiation:
            */
           && (s->s3->tmp.finish_md_len == 0 ||
               !(s->verify_mode & SSL_VERIFY_CLIENT_ONCE))
           /*
            * never request cert in anonymous ciphersuites (see
            * section "Certificate request" in SSL 3 drafts and in
            * RFC 2246):
            */
           && (!(s->s3->tmp.new_cipher->algorithm_auth & SSL_aNULL)
               /*
                * ... except when the application insists on
                * verification (against the specs, but statem_clnt.c accepts
                * this for SSL 3)
                */
               || (s->verify_mode & SSL_VERIFY_FAIL_IF_NO_PEER_CERT))
           /* don't request certificate for SRP auth */
           && !(s->s3->tmp.new_cipher->algorithm_auth & SSL_aSRP)
           /*
            * With normal PSK Certificates and Certificate Requests
            * are omitted
            */
           && !(s->s3->tmp.new_cipher->algorithm_auth & SSL_aPSK)) {
        return 1;
    }

    return 0;
}

/*
 * ossl_statem_server13_write_transition() works out what handshake state to
 * move to next when a TLSv1.3 server is writing messages to be sent to the
 * client.
 */
static WRITE_TRAN ossl_statem_server13_write_transition(SSL *s)
{
    OSSL_STATEM *st = &s->statem;

    /*
     * TODO(TLS1.3): This is still based on the TLSv1.2 state machine. Over time
     * we will update this to look more like real TLSv1.3
     */

    /*
     * No case for TLS_ST_BEFORE, because at that stage we have not negotiated
     * TLSv1.3 yet, so that is handled by ossl_statem_server_write_transition()
     */

    switch (st->hand_state) {
    default:
        /* Shouldn't happen */
        return WRITE_TRAN_ERROR;

    case TLS_ST_SR_CLNT_HELLO:
        st->hand_state = TLS_ST_SW_SRVR_HELLO;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_SW_SRVR_HELLO:
        st->hand_state = TLS_ST_SW_ENCRYPTED_EXTENSIONS;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_SW_ENCRYPTED_EXTENSIONS:
        if (s->hit)
            st->hand_state = TLS_ST_SW_FINISHED;
        else if (send_certificate_request(s))
            st->hand_state = TLS_ST_SW_CERT_REQ;
        else
            st->hand_state = TLS_ST_SW_CERT;

        return WRITE_TRAN_CONTINUE;

    case TLS_ST_SW_CERT_REQ:
        st->hand_state = TLS_ST_SW_CERT;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_SW_CERT:
        st->hand_state = TLS_ST_SW_CERT_VRFY;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_SW_CERT_VRFY:
        st->hand_state = TLS_ST_SW_FINISHED;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_SW_FINISHED:
        return WRITE_TRAN_FINISHED;

    case TLS_ST_SR_FINISHED:
        /*
         * Technically we have finished the handshake at this point, but we're
         * going to remain "in_init" for now and write out the session ticket
         * immediately.
         * TODO(TLS1.3): Perhaps we need to be able to control this behaviour
         * and give the application the opportunity to delay sending the
         * session ticket?
         */
        st->hand_state = TLS_ST_SW_SESSION_TICKET;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_SW_SESSION_TICKET:
        st->hand_state = TLS_ST_OK;
        ossl_statem_set_in_init(s, 0);
        return WRITE_TRAN_CONTINUE;
    }
}

/*
 * ossl_statem_server_write_transition() works out what handshake state to move
 * to next when the server is writing messages to be sent to the client.
 */
WRITE_TRAN ossl_statem_server_write_transition(SSL *s)
{
    OSSL_STATEM *st = &s->statem;

    /*
     * Note that before the ClientHello we don't know what version we are going
     * to negotiate yet, so we don't take this branch until later
     */

    if (SSL_IS_TLS13(s))
        return ossl_statem_server13_write_transition(s);

    switch (st->hand_state) {
    default:
        /* Shouldn't happen */
        return WRITE_TRAN_ERROR;

    case TLS_ST_OK:
        if (st->request_state == TLS_ST_SW_HELLO_REQ) {
            /* We must be trying to renegotiate */
            st->hand_state = TLS_ST_SW_HELLO_REQ;
            st->request_state = TLS_ST_BEFORE;
            return WRITE_TRAN_CONTINUE;
        }
        /* Must be an incoming ClientHello */
        if (!tls_setup_handshake(s)) {
            ossl_statem_set_error(s);
            return WRITE_TRAN_ERROR;
        }
        /* Fall through */

    case TLS_ST_BEFORE:
        /* Just go straight to trying to read from the client */
        return WRITE_TRAN_FINISHED;

    case TLS_ST_SW_HELLO_REQ:
        st->hand_state = TLS_ST_OK;
        ossl_statem_set_in_init(s, 0);
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_SR_CLNT_HELLO:
        if (SSL_IS_DTLS(s) && !s->d1->cookie_verified
            && (SSL_get_options(s) & SSL_OP_COOKIE_EXCHANGE))
            st->hand_state = DTLS_ST_SW_HELLO_VERIFY_REQUEST;
        else
            st->hand_state = TLS_ST_SW_SRVR_HELLO;
        return WRITE_TRAN_CONTINUE;

    case DTLS_ST_SW_HELLO_VERIFY_REQUEST:
        return WRITE_TRAN_FINISHED;

    case TLS_ST_SW_SRVR_HELLO:
        if (s->hit) {
            if (s->ext.ticket_expected)
                st->hand_state = TLS_ST_SW_SESSION_TICKET;
            else
                st->hand_state = TLS_ST_SW_CHANGE;
        } else {
            /* Check if it is anon DH or anon ECDH, */
            /* normal PSK or SRP */
            if (!(s->s3->tmp.new_cipher->algorithm_auth &
                  (SSL_aNULL | SSL_aSRP | SSL_aPSK))) {
                st->hand_state = TLS_ST_SW_CERT;
            } else if (send_server_key_exchange(s)) {
                st->hand_state = TLS_ST_SW_KEY_EXCH;
            } else if (send_certificate_request(s)) {
                st->hand_state = TLS_ST_SW_CERT_REQ;
            } else {
                st->hand_state = TLS_ST_SW_SRVR_DONE;
            }
        }
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_SW_CERT:
        if (s->ext.status_expected) {
            st->hand_state = TLS_ST_SW_CERT_STATUS;
            return WRITE_TRAN_CONTINUE;
        }
        /* Fall through */

    case TLS_ST_SW_CERT_STATUS:
        if (send_server_key_exchange(s)) {
            st->hand_state = TLS_ST_SW_KEY_EXCH;
            return WRITE_TRAN_CONTINUE;
        }
        /* Fall through */

    case TLS_ST_SW_KEY_EXCH:
        if (send_certificate_request(s)) {
            st->hand_state = TLS_ST_SW_CERT_REQ;
            return WRITE_TRAN_CONTINUE;
        }
        /* Fall through */

    case TLS_ST_SW_CERT_REQ:
        st->hand_state = TLS_ST_SW_SRVR_DONE;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_SW_SRVR_DONE:
        return WRITE_TRAN_FINISHED;

    case TLS_ST_SR_FINISHED:
        if (s->hit) {
            st->hand_state = TLS_ST_OK;
            ossl_statem_set_in_init(s, 0);
            return WRITE_TRAN_CONTINUE;
        } else if (s->ext.ticket_expected) {
            st->hand_state = TLS_ST_SW_SESSION_TICKET;
        } else {
            st->hand_state = TLS_ST_SW_CHANGE;
        }
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_SW_SESSION_TICKET:
        st->hand_state = TLS_ST_SW_CHANGE;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_SW_CHANGE:
        st->hand_state = TLS_ST_SW_FINISHED;
        return WRITE_TRAN_CONTINUE;

    case TLS_ST_SW_FINISHED:
        if (s->hit) {
            return WRITE_TRAN_FINISHED;
        }
        st->hand_state = TLS_ST_OK;
        ossl_statem_set_in_init(s, 0);
        return WRITE_TRAN_CONTINUE;
    }
}

/*
 * Perform any pre work that needs to be done prior to sending a message from
 * the server to the client.
 */
WORK_STATE ossl_statem_server_pre_work(SSL *s, WORK_STATE wst)
{
    OSSL_STATEM *st = &s->statem;

    switch (st->hand_state) {
    default:
        /* No pre work to be done */
        break;

    case TLS_ST_SW_HELLO_REQ:
        s->shutdown = 0;
        if (SSL_IS_DTLS(s))
            dtls1_clear_sent_buffer(s);
        break;

    case DTLS_ST_SW_HELLO_VERIFY_REQUEST:
        s->shutdown = 0;
        if (SSL_IS_DTLS(s)) {
            dtls1_clear_sent_buffer(s);
            /* We don't buffer this message so don't use the timer */
            st->use_timer = 0;
        }
        break;

    case TLS_ST_SW_SRVR_HELLO:
        if (SSL_IS_DTLS(s)) {
            /*
             * Messages we write from now on should be bufferred and
             * retransmitted if necessary, so we need to use the timer now
             */
            st->use_timer = 1;
        }
        break;

    case TLS_ST_SW_SRVR_DONE:
#ifndef OPENSSL_NO_SCTP
        if (SSL_IS_DTLS(s) && BIO_dgram_is_sctp(SSL_get_wbio(s)))
            return dtls_wait_for_dry(s);
#endif
        return WORK_FINISHED_CONTINUE;

    case TLS_ST_SW_SESSION_TICKET:
        if (SSL_IS_TLS13(s)) {
            /*
             * Actually this is the end of the handshake, but we're going
             * straight into writing the session ticket out. So we finish off
             * the handshake, but keep the various buffers active.
             */
            return tls_finish_handshake(s, wst, 0);
        } if (SSL_IS_DTLS(s)) {
            /*
             * We're into the last flight. We don't retransmit the last flight
             * unless we need to, so we don't use the timer
             */
            st->use_timer = 0;
        }
        break;

    case TLS_ST_SW_CHANGE:
        s->session->cipher = s->s3->tmp.new_cipher;
        if (!s->method->ssl3_enc->setup_key_block(s)) {
            ossl_statem_set_error(s);
            return WORK_ERROR;
        }
        if (SSL_IS_DTLS(s)) {
            /*
             * We're into the last flight. We don't retransmit the last flight
             * unless we need to, so we don't use the timer. This might have
             * already been set to 0 if we sent a NewSessionTicket message,
             * but we'll set it again here in case we didn't.
             */
            st->use_timer = 0;
        }
        return WORK_FINISHED_CONTINUE;

    case TLS_ST_OK:
        return tls_finish_handshake(s, wst, 1);
    }

    return WORK_FINISHED_CONTINUE;
}

/*
 * Perform any work that needs to be done after sending a message from the
 * server to the client.
 */
WORK_STATE ossl_statem_server_post_work(SSL *s, WORK_STATE wst)
{
    OSSL_STATEM *st = &s->statem;

    s->init_num = 0;

    switch (st->hand_state) {
    default:
        /* No post work to be done */
        break;

    case TLS_ST_SW_HELLO_REQ:
        if (statem_flush(s) != 1)
            return WORK_MORE_A;
        if (!ssl3_init_finished_mac(s)) {
            ossl_statem_set_error(s);
            return WORK_ERROR;
        }
        break;

    case DTLS_ST_SW_HELLO_VERIFY_REQUEST:
        if (statem_flush(s) != 1)
            return WORK_MORE_A;
        /* HelloVerifyRequest resets Finished MAC */
        if (s->version != DTLS1_BAD_VER && !ssl3_init_finished_mac(s)) {
            ossl_statem_set_error(s);
            return WORK_ERROR;
        }
        /*
         * The next message should be another ClientHello which we need to
         * treat like it was the first packet
         */
        s->first_packet = 1;
        break;

    case TLS_ST_SW_SRVR_HELLO:
#ifndef OPENSSL_NO_SCTP
        if (SSL_IS_DTLS(s) && s->hit) {
            unsigned char sctpauthkey[64];
            char labelbuffer[sizeof(DTLS1_SCTP_AUTH_LABEL)];

            /*
             * Add new shared key for SCTP-Auth, will be ignored if no
             * SCTP used.
             */
            memcpy(labelbuffer, DTLS1_SCTP_AUTH_LABEL,
                   sizeof(DTLS1_SCTP_AUTH_LABEL));

            if (SSL_export_keying_material(s, sctpauthkey,
                                           sizeof(sctpauthkey), labelbuffer,
                                           sizeof(labelbuffer), NULL, 0,
                                           0) <= 0) {
                ossl_statem_set_error(s);
                return WORK_ERROR;
            }

            BIO_ctrl(SSL_get_wbio(s), BIO_CTRL_DGRAM_SCTP_ADD_AUTH_KEY,
                     sizeof(sctpauthkey), sctpauthkey);
        }
#endif
        /*
         * TODO(TLS1.3): This actually causes a problem. We don't yet know
         * whether the next record we are going to receive is an unencrypted
         * alert, or an encrypted handshake message. We're going to need
         * something clever in the record layer for this.
         */
        if (SSL_IS_TLS13(s)) {
            if (!s->method->ssl3_enc->setup_key_block(s)
                || !s->method->ssl3_enc->change_cipher_state(s,
                        SSL3_CC_HANDSHAKE | SSL3_CHANGE_CIPHER_SERVER_WRITE)
                || !s->method->ssl3_enc->change_cipher_state(s,
                        SSL3_CC_HANDSHAKE |SSL3_CHANGE_CIPHER_SERVER_READ))
            return WORK_ERROR;
        }
        break;

    case TLS_ST_SW_CHANGE:
#ifndef OPENSSL_NO_SCTP
        if (SSL_IS_DTLS(s) && !s->hit) {
            /*
             * Change to new shared key of SCTP-Auth, will be ignored if
             * no SCTP used.
             */
            BIO_ctrl(SSL_get_wbio(s), BIO_CTRL_DGRAM_SCTP_NEXT_AUTH_KEY,
                     0, NULL);
        }
#endif
        if (!s->method->ssl3_enc->change_cipher_state(s,
                                                      SSL3_CHANGE_CIPHER_SERVER_WRITE))
        {
            ossl_statem_set_error(s);
            return WORK_ERROR;
        }

        if (SSL_IS_DTLS(s))
            dtls1_reset_seq_numbers(s, SSL3_CC_WRITE);
        break;

    case TLS_ST_SW_SRVR_DONE:
        if (statem_flush(s) != 1)
            return WORK_MORE_A;
        break;

    case TLS_ST_SW_FINISHED:
        if (statem_flush(s) != 1)
            return WORK_MORE_A;
#ifndef OPENSSL_NO_SCTP
        if (SSL_IS_DTLS(s) && s->hit) {
            /*
             * Change to new shared key of SCTP-Auth, will be ignored if
             * no SCTP used.
             */
            BIO_ctrl(SSL_get_wbio(s), BIO_CTRL_DGRAM_SCTP_NEXT_AUTH_KEY,
                     0, NULL);
        }
#endif
        if (SSL_IS_TLS13(s)) {
            if (!s->method->ssl3_enc->generate_master_secret(s,
                        s->master_secret, s->handshake_secret, 0,
                        &s->session->master_key_length)
                || !s->method->ssl3_enc->change_cipher_state(s,
                        SSL3_CC_APPLICATION | SSL3_CHANGE_CIPHER_SERVER_WRITE))
            return WORK_ERROR;
        }
        break;

    case TLS_ST_SW_SESSION_TICKET:
        if (SSL_IS_TLS13(s) && statem_flush(s) != 1)
            return WORK_MORE_A;
        break;
    }

    return WORK_FINISHED_CONTINUE;
}

/*
 * Get the message construction function and message type for sending from the
 * server
 *
 * Valid return values are:
 *   1: Success
 *   0: Error
 */
int ossl_statem_server_construct_message(SSL *s, WPACKET *pkt,
                                         confunc_f *confunc, int *mt)
{
    OSSL_STATEM *st = &s->statem;

    switch (st->hand_state) {
    default:
        /* Shouldn't happen */
        return 0;

    case TLS_ST_SW_CHANGE:
        if (SSL_IS_DTLS(s))
            *confunc = dtls_construct_change_cipher_spec;
        else
            *confunc = tls_construct_change_cipher_spec;
        *mt = SSL3_MT_CHANGE_CIPHER_SPEC;
        break;

    case DTLS_ST_SW_HELLO_VERIFY_REQUEST:
        *confunc = dtls_construct_hello_verify_request;
        *mt = DTLS1_MT_HELLO_VERIFY_REQUEST;
        break;

    case TLS_ST_SW_HELLO_REQ:
        /* No construction function needed */
        *confunc = NULL;
        *mt = SSL3_MT_HELLO_REQUEST;
        break;

    case TLS_ST_SW_SRVR_HELLO:
        *confunc = tls_construct_server_hello;
        *mt = SSL3_MT_SERVER_HELLO;
        break;

    case TLS_ST_SW_CERT:
        *confunc = tls_construct_server_certificate;
        *mt = SSL3_MT_CERTIFICATE;
        break;

    case TLS_ST_SW_CERT_VRFY:
        *confunc = tls_construct_cert_verify;
        *mt = SSL3_MT_CERTIFICATE_VERIFY;
        break;


    case TLS_ST_SW_KEY_EXCH:
        *confunc = tls_construct_server_key_exchange;
        *mt = SSL3_MT_SERVER_KEY_EXCHANGE;
        break;

    case TLS_ST_SW_CERT_REQ:
        *confunc = tls_construct_certificate_request;
        *mt = SSL3_MT_CERTIFICATE_REQUEST;
        break;

    case TLS_ST_SW_SRVR_DONE:
        *confunc = tls_construct_server_done;
        *mt = SSL3_MT_SERVER_DONE;
        break;

    case TLS_ST_SW_SESSION_TICKET:
        *confunc = tls_construct_new_session_ticket;
        *mt = SSL3_MT_NEWSESSION_TICKET;
        break;

    case TLS_ST_SW_CERT_STATUS:
        *confunc = tls_construct_cert_status;
        *mt = SSL3_MT_CERTIFICATE_STATUS;
        break;

    case TLS_ST_SW_FINISHED:
        *confunc = tls_construct_finished;
        *mt = SSL3_MT_FINISHED;
        break;

    case TLS_ST_SW_ENCRYPTED_EXTENSIONS:
        *confunc = tls_construct_encrypted_extensions;
        *mt = SSL3_MT_ENCRYPTED_EXTENSIONS;
        break;
    }

    return 1;
}

/*
 * Maximum size (excluding the Handshake header) of a ClientHello message,
 * calculated as follows:
 *
 *  2 + # client_version
 *  32 + # only valid length for random
 *  1 + # length of session_id
 *  32 + # maximum size for session_id
 *  2 + # length of cipher suites
 *  2^16-2 + # maximum length of cipher suites array
 *  1 + # length of compression_methods
 *  2^8-1 + # maximum length of compression methods
 *  2 + # length of extensions
 *  2^16-1 # maximum length of extensions
 */
#define CLIENT_HELLO_MAX_LENGTH         131396

#define CLIENT_KEY_EXCH_MAX_LENGTH      2048
#define NEXT_PROTO_MAX_LENGTH           514

/*
 * Returns the maximum allowed length for the current message that we are
 * reading. Excludes the message header.
 */
size_t ossl_statem_server_max_message_size(SSL *s)
{
    OSSL_STATEM *st = &s->statem;

    switch (st->hand_state) {
    default:
        /* Shouldn't happen */
        return 0;

    case TLS_ST_SR_CLNT_HELLO:
        return CLIENT_HELLO_MAX_LENGTH;

    case TLS_ST_SR_CERT:
        return s->max_cert_list;

    case TLS_ST_SR_KEY_EXCH:
        return CLIENT_KEY_EXCH_MAX_LENGTH;

    case TLS_ST_SR_CERT_VRFY:
        return SSL3_RT_MAX_PLAIN_LENGTH;

#ifndef OPENSSL_NO_NEXTPROTONEG
    case TLS_ST_SR_NEXT_PROTO:
        return NEXT_PROTO_MAX_LENGTH;
#endif

    case TLS_ST_SR_CHANGE:
        return CCS_MAX_LENGTH;

    case TLS_ST_SR_FINISHED:
        return FINISHED_MAX_LENGTH;
    }
}

/*
 * Process a message that the server has received from the client.
 */
MSG_PROCESS_RETURN ossl_statem_server_process_message(SSL *s, PACKET *pkt)
{
    OSSL_STATEM *st = &s->statem;

    switch (st->hand_state) {
    default:
        /* Shouldn't happen */
        return MSG_PROCESS_ERROR;

    case TLS_ST_SR_CLNT_HELLO:
        return tls_process_client_hello(s, pkt);

    case TLS_ST_SR_CERT:
        return tls_process_client_certificate(s, pkt);

    case TLS_ST_SR_KEY_EXCH:
        return tls_process_client_key_exchange(s, pkt);

    case TLS_ST_SR_CERT_VRFY:
        return tls_process_cert_verify(s, pkt);

#ifndef OPENSSL_NO_NEXTPROTONEG
    case TLS_ST_SR_NEXT_PROTO:
        return tls_process_next_proto(s, pkt);
#endif

    case TLS_ST_SR_CHANGE:
        return tls_process_change_cipher_spec(s, pkt);

    case TLS_ST_SR_FINISHED:
        return tls_process_finished(s, pkt);
    }
}

/*
 * Perform any further processing required following the receipt of a message
 * from the client
 */
WORK_STATE ossl_statem_server_post_process_message(SSL *s, WORK_STATE wst)
{
    OSSL_STATEM *st = &s->statem;

    switch (st->hand_state) {
    default:
        /* Shouldn't happen */
        return WORK_ERROR;

    case TLS_ST_SR_CLNT_HELLO:
        return tls_post_process_client_hello(s, wst);

    case TLS_ST_SR_KEY_EXCH:
        return tls_post_process_client_key_exchange(s, wst);

    case TLS_ST_SR_CERT_VRFY:
#ifndef OPENSSL_NO_SCTP
        if (                    /* Is this SCTP? */
               BIO_dgram_is_sctp(SSL_get_wbio(s))
               /* Are we renegotiating? */
               && s->renegotiate && BIO_dgram_sctp_msg_waiting(SSL_get_rbio(s))) {
            s->s3->in_read_app_data = 2;
            s->rwstate = SSL_READING;
            BIO_clear_retry_flags(SSL_get_rbio(s));
            BIO_set_retry_read(SSL_get_rbio(s));
            ossl_statem_set_sctp_read_sock(s, 1);
            return WORK_MORE_A;
        } else {
            ossl_statem_set_sctp_read_sock(s, 0);
        }
#endif
        return WORK_FINISHED_CONTINUE;
    }
    return WORK_FINISHED_CONTINUE;
}

#ifndef OPENSSL_NO_SRP
static int ssl_check_srp_ext_ClientHello(SSL *s, int *al)
{
    int ret = SSL_ERROR_NONE;

    *al = SSL_AD_UNRECOGNIZED_NAME;

    if ((s->s3->tmp.new_cipher->algorithm_mkey & SSL_kSRP) &&
        (s->srp_ctx.TLS_ext_srp_username_callback != NULL)) {
        if (s->srp_ctx.login == NULL) {
            /*
             * RFC 5054 says SHOULD reject, we do so if There is no srp
             * login name
             */
            ret = SSL3_AL_FATAL;
            *al = SSL_AD_UNKNOWN_PSK_IDENTITY;
        } else {
            ret = SSL_srp_server_param_with_username(s, al);
        }
    }
    return ret;
}
#endif

int dtls_raw_hello_verify_request(WPACKET *pkt, unsigned char *cookie,
                                  size_t cookie_len)
{
    /* Always use DTLS 1.0 version: see RFC 6347 */
    if (!WPACKET_put_bytes_u16(pkt, DTLS1_VERSION)
            || !WPACKET_sub_memcpy_u8(pkt, cookie, cookie_len))
        return 0;

    return 1;
}

int dtls_construct_hello_verify_request(SSL *s, WPACKET *pkt)
{
    unsigned int cookie_leni;
    if (s->ctx->app_gen_cookie_cb == NULL ||
        s->ctx->app_gen_cookie_cb(s, s->d1->cookie,
                                  &cookie_leni) == 0 ||
        cookie_leni > 255) {
        SSLerr(SSL_F_DTLS_CONSTRUCT_HELLO_VERIFY_REQUEST,
               SSL_R_COOKIE_GEN_CALLBACK_FAILURE);
        return 0;
    }
    s->d1->cookie_len = cookie_leni;

    if (!dtls_raw_hello_verify_request(pkt, s->d1->cookie,
                                              s->d1->cookie_len)) {
        SSLerr(SSL_F_DTLS_CONSTRUCT_HELLO_VERIFY_REQUEST, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    return 1;
}

#ifndef OPENSSL_NO_EC
/*-
 * ssl_check_for_safari attempts to fingerprint Safari using OS X
 * SecureTransport using the TLS extension block in |hello|.
 * Safari, since 10.6, sends exactly these extensions, in this order:
 *   SNI,
 *   elliptic_curves
 *   ec_point_formats
 *
 * We wish to fingerprint Safari because they broke ECDHE-ECDSA support in 10.8,
 * but they advertise support. So enabling ECDHE-ECDSA ciphers breaks them.
 * Sadly we cannot differentiate 10.6, 10.7 and 10.8.4 (which work), from
 * 10.8..10.8.3 (which don't work).
 */
static void ssl_check_for_safari(SSL *s, const CLIENTHELLO_MSG *hello)
{
    static const unsigned char kSafariExtensionsBlock[] = {
        0x00, 0x0a,             /* elliptic_curves extension */
        0x00, 0x08,             /* 8 bytes */
        0x00, 0x06,             /* 6 bytes of curve ids */
        0x00, 0x17,             /* P-256 */
        0x00, 0x18,             /* P-384 */
        0x00, 0x19,             /* P-521 */

        0x00, 0x0b,             /* ec_point_formats */
        0x00, 0x02,             /* 2 bytes */
        0x01,                   /* 1 point format */
        0x00,                   /* uncompressed */
        /* The following is only present in TLS 1.2 */
        0x00, 0x0d,             /* signature_algorithms */
        0x00, 0x0c,             /* 12 bytes */
        0x00, 0x0a,             /* 10 bytes */
        0x05, 0x01,             /* SHA-384/RSA */
        0x04, 0x01,             /* SHA-256/RSA */
        0x02, 0x01,             /* SHA-1/RSA */
        0x04, 0x03,             /* SHA-256/ECDSA */
        0x02, 0x03,             /* SHA-1/ECDSA */
    };
    /* Length of the common prefix (first two extensions). */
    static const size_t kSafariCommonExtensionsLength = 18;
    unsigned int type;
    PACKET sni, tmppkt;
    size_t ext_len;

    tmppkt = hello->extensions;

    if (!PACKET_forward(&tmppkt, 2)
        || !PACKET_get_net_2(&tmppkt, &type)
        || !PACKET_get_length_prefixed_2(&tmppkt, &sni)) {
        return;
    }

    if (type != TLSEXT_TYPE_server_name)
        return;

    ext_len = TLS1_get_client_version(s) >= TLS1_2_VERSION ?
        sizeof(kSafariExtensionsBlock) : kSafariCommonExtensionsLength;

    s->s3->is_probably_safari = PACKET_equal(&tmppkt, kSafariExtensionsBlock,
                                             ext_len);
}
#endif                          /* !OPENSSL_NO_EC */

MSG_PROCESS_RETURN tls_process_client_hello(SSL *s, PACKET *pkt)
{
    int i, al = SSL_AD_INTERNAL_ERROR;
    unsigned int j;
    size_t loop;
    unsigned long id;
    const SSL_CIPHER *c;
#ifndef OPENSSL_NO_COMP
    SSL_COMP *comp = NULL;
#endif
    STACK_OF(SSL_CIPHER) *ciphers = NULL;
    int protverr;
    /* |cookie| will only be initialized for DTLS. */
    PACKET session_id, compression, extensions, cookie;
    static const unsigned char null_compression = 0;
    CLIENTHELLO_MSG clienthello;

    /* Check if this is actually an unexpected renegotiation ClientHello */
    if (s->renegotiate == 0 && !SSL_IS_FIRST_HANDSHAKE(s)) {
        s->renegotiate = 1;
        s->new_session = 1;
    }

    /* This is a real handshake so make sure we clean it up at the end */
    s->statem.cleanuphand = 1;

    /*
     * First, parse the raw ClientHello data into the CLIENTHELLO_MSG structure.
     */
    memset(&clienthello, 0, sizeof(clienthello));
    clienthello.isv2 = RECORD_LAYER_is_sslv2_record(&s->rlayer);
    PACKET_null_init(&cookie);

    if (clienthello.isv2) {
        unsigned int mt;

        /*-
         * An SSLv3/TLSv1 backwards-compatible CLIENT-HELLO in an SSLv2
         * header is sent directly on the wire, not wrapped as a TLS
         * record. Our record layer just processes the message length and passes
         * the rest right through. Its format is:
         * Byte  Content
         * 0-1   msg_length - decoded by the record layer
         * 2     msg_type - s->init_msg points here
         * 3-4   version
         * 5-6   cipher_spec_length
         * 7-8   session_id_length
         * 9-10  challenge_length
         * ...   ...
         */

        if (!PACKET_get_1(pkt, &mt)
            || mt != SSL2_MT_CLIENT_HELLO) {
            /*
             * Should never happen. We should have tested this in the record
             * layer in order to have determined that this is a SSLv2 record
             * in the first place
             */
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, ERR_R_INTERNAL_ERROR);
            goto err;
        }
    }

    if (!PACKET_get_net_2(pkt, &clienthello.legacy_version)) {
        al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_LENGTH_TOO_SHORT);
        goto err;
    }

    /* Parse the message and load client random. */
    if (clienthello.isv2) {
        /*
         * Handle an SSLv2 backwards compatible ClientHello
         * Note, this is only for SSLv3+ using the backward compatible format.
         * Real SSLv2 is not supported, and is rejected below.
         */
        unsigned int ciphersuite_len, session_id_len, challenge_len;
        PACKET challenge;

        if (!PACKET_get_net_2(pkt, &ciphersuite_len)
            || !PACKET_get_net_2(pkt, &session_id_len)
            || !PACKET_get_net_2(pkt, &challenge_len)) {
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO,
                   SSL_R_RECORD_LENGTH_MISMATCH);
            al = SSL_AD_DECODE_ERROR;
            goto f_err;
        }

        if (session_id_len > SSL_MAX_SSL_SESSION_ID_LENGTH) {
            al = SSL_AD_DECODE_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_LENGTH_MISMATCH);
            goto f_err;
        }

        if (!PACKET_get_sub_packet(pkt, &clienthello.ciphersuites,
                                   ciphersuite_len)
            || !PACKET_copy_bytes(pkt, clienthello.session_id, session_id_len)
            || !PACKET_get_sub_packet(pkt, &challenge, challenge_len)
            /* No extensions. */
            || PACKET_remaining(pkt) != 0) {
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO,
                   SSL_R_RECORD_LENGTH_MISMATCH);
            al = SSL_AD_DECODE_ERROR;
            goto f_err;
        }
        clienthello.session_id_len = session_id_len;

        /* Load the client random and compression list. We use SSL3_RANDOM_SIZE
         * here rather than sizeof(clienthello.random) because that is the limit
         * for SSLv3 and it is fixed. It won't change even if
         * sizeof(clienthello.random) does.
         */
        challenge_len = challenge_len > SSL3_RANDOM_SIZE
                        ? SSL3_RANDOM_SIZE : challenge_len;
        memset(clienthello.random, 0, SSL3_RANDOM_SIZE);
        if (!PACKET_copy_bytes(&challenge,
                               clienthello.random + SSL3_RANDOM_SIZE -
                               challenge_len, challenge_len)
            /* Advertise only null compression. */
            || !PACKET_buf_init(&compression, &null_compression, 1)) {
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, ERR_R_INTERNAL_ERROR);
            al = SSL_AD_INTERNAL_ERROR;
            goto f_err;
        }

        PACKET_null_init(&clienthello.extensions);
    } else {
        /* Regular ClientHello. */
        if (!PACKET_copy_bytes(pkt, clienthello.random, SSL3_RANDOM_SIZE)
            || !PACKET_get_length_prefixed_1(pkt, &session_id)
            || !PACKET_copy_all(&session_id, clienthello.session_id,
                    SSL_MAX_SSL_SESSION_ID_LENGTH,
                    &clienthello.session_id_len)) {
            al = SSL_AD_DECODE_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_LENGTH_MISMATCH);
            goto f_err;
        }

        if (SSL_IS_DTLS(s)) {
            if (!PACKET_get_length_prefixed_1(pkt, &cookie)) {
                al = SSL_AD_DECODE_ERROR;
                SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_LENGTH_MISMATCH);
                goto f_err;
            }
            if (!PACKET_copy_all(&cookie, clienthello.dtls_cookie,
                                 DTLS1_COOKIE_LENGTH,
                                 &clienthello.dtls_cookie_len)) {
                al = SSL_AD_DECODE_ERROR;
                SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_LENGTH_MISMATCH);
                goto f_err;
            }
            /*
             * If we require cookies and this ClientHello doesn't contain one,
             * just return since we do not want to allocate any memory yet.
             * So check cookie length...
             */
            if (SSL_get_options(s) & SSL_OP_COOKIE_EXCHANGE) {
                if (clienthello.dtls_cookie_len == 0)
                    return 1;
            }
        }

        if (!PACKET_get_length_prefixed_2(pkt, &clienthello.ciphersuites)) {
            al = SSL_AD_DECODE_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_LENGTH_MISMATCH);
            goto f_err;
        }

        if (!PACKET_get_length_prefixed_1(pkt, &compression)) {
            al = SSL_AD_DECODE_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_LENGTH_MISMATCH);
            goto f_err;
        }

        /* Could be empty. */
        if (PACKET_remaining(pkt) == 0) {
            PACKET_null_init(&clienthello.extensions);
        } else {
            if (!PACKET_get_length_prefixed_2(pkt, &clienthello.extensions)) {
                al = SSL_AD_DECODE_ERROR;
                SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_LENGTH_MISMATCH);
                goto f_err;
            }
        }
    }

    if (!PACKET_copy_all(&compression, clienthello.compressions,
                         MAX_COMPRESSIONS_SIZE,
                         &clienthello.compressions_len)) {
        al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_LENGTH_MISMATCH);
        goto f_err;
    }

    /* Preserve the raw extensions PACKET for later use */
    extensions = clienthello.extensions;
    if (!tls_collect_extensions(s, &extensions, EXT_CLIENT_HELLO,
                                &clienthello.pre_proc_exts, &al)) {
        /* SSLerr already been called */
        goto f_err;
    }

    /* Finished parsing the ClientHello, now we can start processing it */

    /* Set up the client_random */
    memcpy(s->s3->client_random, clienthello.random, SSL3_RANDOM_SIZE);

    /* Choose the version */

    if (clienthello.isv2) {
        if (clienthello.legacy_version == SSL2_VERSION
                || (clienthello.legacy_version & 0xff00)
                   != (SSL3_VERSION_MAJOR << 8)) {
            /*
             * This is real SSLv2 or something complete unknown. We don't
             * support it.
             */
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_UNKNOWN_PROTOCOL);
            goto err;
        }
        /* SSLv3/TLS */
        s->client_version = clienthello.legacy_version;
    }
    /*
     * Do SSL/TLS version negotiation if applicable. For DTLS we just check
     * versions are potentially compatible. Version negotiation comes later.
     */
    if (!SSL_IS_DTLS(s)) {
        protverr = ssl_choose_server_version(s, &clienthello);
    } else if (s->method->version != DTLS_ANY_VERSION &&
               DTLS_VERSION_LT((int)clienthello.legacy_version, s->version)) {
        protverr = SSL_R_VERSION_TOO_LOW;
    } else {
        protverr = 0;
    }

    if (protverr) {
        SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, protverr);
        if ((!s->enc_write_ctx && !s->write_hash)) {
            /* like ssl3_get_record, send alert using remote version number */
            s->version = s->client_version = clienthello.legacy_version;
        }
        al = SSL_AD_PROTOCOL_VERSION;
        goto f_err;
    }

    if (SSL_IS_DTLS(s)) {
        /* Empty cookie was already handled above by returning early. */
        if (SSL_get_options(s) & SSL_OP_COOKIE_EXCHANGE) {
            if (s->ctx->app_verify_cookie_cb != NULL) {
                if (s->ctx->app_verify_cookie_cb(s, clienthello.dtls_cookie,
                        clienthello.dtls_cookie_len) == 0) {
                    al = SSL_AD_HANDSHAKE_FAILURE;
                    SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO,
                           SSL_R_COOKIE_MISMATCH);
                    goto f_err;
                    /* else cookie verification succeeded */
                }
                /* default verification */
            } else if (s->d1->cookie_len != clienthello.dtls_cookie_len
                    || memcmp(clienthello.dtls_cookie, s->d1->cookie,
                              s->d1->cookie_len) != 0) {
                al = SSL_AD_HANDSHAKE_FAILURE;
                SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_COOKIE_MISMATCH);
                goto f_err;
            }
            s->d1->cookie_verified = 1;
        }
        if (s->method->version == DTLS_ANY_VERSION) {
            protverr = ssl_choose_server_version(s, &clienthello);
            if (protverr != 0) {
                SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, protverr);
                s->version = s->client_version;
                al = SSL_AD_PROTOCOL_VERSION;
                goto f_err;
            }
        }
    }

    s->hit = 0;

    /* We need to do this before getting the session */
    if (!tls_parse_extension(s, TLSEXT_IDX_extended_master_secret,
                             EXT_CLIENT_HELLO,
                             clienthello.pre_proc_exts, NULL, 0, &al)) {
        SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_CLIENTHELLO_TLSEXT);
        goto f_err;
    }

    /*
     * We don't allow resumption in a backwards compatible ClientHello.
     * TODO(openssl-team): in TLS1.1+, session_id MUST be empty.
     *
     * Versions before 0.9.7 always allow clients to resume sessions in
     * renegotiation. 0.9.7 and later allow this by default, but optionally
     * ignore resumption requests with flag
     * SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION (it's a new flag rather
     * than a change to default behavior so that applications relying on
     * this for security won't even compile against older library versions).
     * 1.0.1 and later also have a function SSL_renegotiate_abbreviated() to
     * request renegotiation but not a new session (s->new_session remains
     * unset): for servers, this essentially just means that the
     * SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION setting will be
     * ignored.
     */
    if (clienthello.isv2 ||
        (s->new_session &&
         (s->options & SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION))) {
        if (!ssl_get_new_session(s, 1))
            goto err;
    } else {
        i = ssl_get_prev_session(s, &clienthello, &al);
        if (i == 1) {
            /* previous session */
            s->hit = 1;
        } else if (i == -1) {
            goto f_err;
        } else {
            /* i == 0 */
            if (!ssl_get_new_session(s, 1))
                goto err;
        }
    }

    if (ssl_bytes_to_cipher_list(s, &clienthello.ciphersuites, &ciphers,
                                 clienthello.isv2, &al) == NULL) {
        goto f_err;
    }

    /* If it is a hit, check that the cipher is in the list */
    if (s->hit) {
        j = 0;
        id = s->session->cipher->id;

#ifdef CIPHER_DEBUG
        fprintf(stderr, "client sent %d ciphers\n", sk_SSL_CIPHER_num(ciphers));
#endif
        for (i = 0; i < sk_SSL_CIPHER_num(ciphers); i++) {
            c = sk_SSL_CIPHER_value(ciphers, i);
#ifdef CIPHER_DEBUG
            fprintf(stderr, "client [%2d of %2d]:%s\n",
                    i, sk_SSL_CIPHER_num(ciphers), SSL_CIPHER_get_name(c));
#endif
            if (c->id == id) {
                j = 1;
                break;
            }
        }
        if (j == 0) {
            /*
             * we need to have the cipher in the cipher list if we are asked
             * to reuse it
             */
            al = SSL_AD_ILLEGAL_PARAMETER;
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO,
                   SSL_R_REQUIRED_CIPHER_MISSING);
            goto f_err;
        }
    }

    for (loop = 0; loop < clienthello.compressions_len; loop++) {
        if (clienthello.compressions[loop] == 0)
            break;
    }

    if (loop >= clienthello.compressions_len) {
        /* no compress */
        al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_NO_COMPRESSION_SPECIFIED);
        goto f_err;
    }

#ifndef OPENSSL_NO_EC
    if (s->options & SSL_OP_SAFARI_ECDHE_ECDSA_BUG)
        ssl_check_for_safari(s, &clienthello);
#endif                          /* !OPENSSL_NO_EC */

    /* TLS extensions */
    if (!tls_parse_all_extensions(s, EXT_CLIENT_HELLO,
                                  clienthello.pre_proc_exts, NULL, 0, &al)) {
        SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_PARSE_TLSEXT);
        goto f_err;
    }

    /*
     * Check if we want to use external pre-shared secret for this handshake
     * for not reused session only. We need to generate server_random before
     * calling tls_session_secret_cb in order to allow SessionTicket
     * processing to use it in key derivation.
     */
    {
        unsigned char *pos;
        pos = s->s3->server_random;
        if (ssl_fill_hello_random(s, 1, pos, SSL3_RANDOM_SIZE) <= 0) {
            goto f_err;
        }
    }

    if (!s->hit && s->version >= TLS1_VERSION && s->ext.session_secret_cb) {
        const SSL_CIPHER *pref_cipher = NULL;
        /*
         * s->session->master_key_length is a size_t, but this is an int for
         * backwards compat reasons
         */
        int master_key_length;

        master_key_length = sizeof(s->session->master_key);
        if (s->ext.session_secret_cb(s, s->session->master_key,
                                     &master_key_length, ciphers,
                                     &pref_cipher,
                                     s->ext.session_secret_cb_arg)
                && master_key_length > 0) {
            s->session->master_key_length = master_key_length;
            s->hit = 1;
            s->session->ciphers = ciphers;
            s->session->verify_result = X509_V_OK;

            ciphers = NULL;

            /* check if some cipher was preferred by call back */
            pref_cipher =
                pref_cipher ? pref_cipher : ssl3_choose_cipher(s,
                                                               s->
                                                               session->ciphers,
                                                               SSL_get_ciphers
                                                               (s));
            if (pref_cipher == NULL) {
                al = SSL_AD_HANDSHAKE_FAILURE;
                SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_NO_SHARED_CIPHER);
                goto f_err;
            }

            s->session->cipher = pref_cipher;
            sk_SSL_CIPHER_free(s->cipher_list);
            s->cipher_list = sk_SSL_CIPHER_dup(s->session->ciphers);
            sk_SSL_CIPHER_free(s->cipher_list_by_id);
            s->cipher_list_by_id = sk_SSL_CIPHER_dup(s->session->ciphers);
        }
    }

    /*
     * Worst case, we will use the NULL compression, but if we have other
     * options, we will now look for them.  We have complen-1 compression
     * algorithms from the client, starting at q.
     */
    s->s3->tmp.new_compression = NULL;
#ifndef OPENSSL_NO_COMP
    /* This only happens if we have a cache hit */
    if (s->session->compress_meth != 0) {
        int m, comp_id = s->session->compress_meth;
        unsigned int k;
        /* Perform sanity checks on resumed compression algorithm */
        /* Can't disable compression */
        if (!ssl_allow_compression(s)) {
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO,
                   SSL_R_INCONSISTENT_COMPRESSION);
            goto f_err;
        }
        /* Look for resumed compression method */
        for (m = 0; m < sk_SSL_COMP_num(s->ctx->comp_methods); m++) {
            comp = sk_SSL_COMP_value(s->ctx->comp_methods, m);
            if (comp_id == comp->id) {
                s->s3->tmp.new_compression = comp;
                break;
            }
        }
        if (s->s3->tmp.new_compression == NULL) {
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO,
                   SSL_R_INVALID_COMPRESSION_ALGORITHM);
            goto f_err;
        }
        /* Look for resumed method in compression list */
        for (k = 0; k < clienthello.compressions_len; k++) {
            if (clienthello.compressions[k] == comp_id)
                break;
        }
        if (k >= clienthello.compressions_len) {
            al = SSL_AD_ILLEGAL_PARAMETER;
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO,
                   SSL_R_REQUIRED_COMPRESSION_ALGORITHM_MISSING);
            goto f_err;
        }
    } else if (s->hit)
        comp = NULL;
    else if (ssl_allow_compression(s) && s->ctx->comp_methods) {
        /* See if we have a match */
        int m, nn, v, done = 0;
        unsigned int o;

        nn = sk_SSL_COMP_num(s->ctx->comp_methods);
        for (m = 0; m < nn; m++) {
            comp = sk_SSL_COMP_value(s->ctx->comp_methods, m);
            v = comp->id;
            for (o = 0; o < clienthello.compressions_len; o++) {
                if (v == clienthello.compressions[o]) {
                    done = 1;
                    break;
                }
            }
            if (done)
                break;
        }
        if (done)
            s->s3->tmp.new_compression = comp;
        else
            comp = NULL;
    }
#else
    /*
     * If compression is disabled we'd better not try to resume a session
     * using compression.
     */
    if (s->session->compress_meth != 0) {
        SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_INCONSISTENT_COMPRESSION);
        goto f_err;
    }
#endif

    /*
     * Given s->session->ciphers and SSL_get_ciphers, we must pick a cipher
     */

    if (!s->hit) {
#ifdef OPENSSL_NO_COMP
        s->session->compress_meth = 0;
#else
        s->session->compress_meth = (comp == NULL) ? 0 : comp->id;
#endif
        sk_SSL_CIPHER_free(s->session->ciphers);
        s->session->ciphers = ciphers;
        if (ciphers == NULL) {
            al = SSL_AD_INTERNAL_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, ERR_R_INTERNAL_ERROR);
            goto f_err;
        }
        ciphers = NULL;
        if (!tls1_set_server_sigalgs(s)) {
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_HELLO, SSL_R_CLIENTHELLO_TLSEXT);
            goto err;
        }
    }

    sk_SSL_CIPHER_free(ciphers);
    OPENSSL_free(clienthello.pre_proc_exts);
    return MSG_PROCESS_CONTINUE_PROCESSING;
 f_err:
    ssl3_send_alert(s, SSL3_AL_FATAL, al);
 err:
    ossl_statem_set_error(s);

    sk_SSL_CIPHER_free(ciphers);
    OPENSSL_free(clienthello.pre_proc_exts);

    return MSG_PROCESS_ERROR;
}

/*
 * Call the status request callback if needed. Upon success, returns 1.
 * Upon failure, returns 0 and sets |*al| to the appropriate fatal alert.
 */
static int tls_handle_status_request(SSL *s, int *al)
{
    s->ext.status_expected = 0;

    /*
     * If status request then ask callback what to do. Note: this must be
     * called after servername callbacks in case the certificate has changed,
     * and must be called after the cipher has been chosen because this may
     * influence which certificate is sent
     */
    if (s->ext.status_type != TLSEXT_STATUSTYPE_nothing && s->ctx != NULL
            && s->ctx->ext.status_cb != NULL) {
        int ret;
        CERT_PKEY *certpkey = ssl_get_server_send_pkey(s);

        /* If no certificate can't return certificate status */
        if (certpkey != NULL) {
            /*
             * Set current certificate to one we will use so SSL_get_certificate
             * et al can pick it up.
             */
            s->cert->key = certpkey;
            ret = s->ctx->ext.status_cb(s, s->ctx->ext.status_arg);
            switch (ret) {
                /* We don't want to send a status request response */
            case SSL_TLSEXT_ERR_NOACK:
                s->ext.status_expected = 0;
                break;
                /* status request response should be sent */
            case SSL_TLSEXT_ERR_OK:
                if (s->ext.ocsp.resp)
                    s->ext.status_expected = 1;
                break;
                /* something bad happened */
            case SSL_TLSEXT_ERR_ALERT_FATAL:
            default:
                *al = SSL_AD_INTERNAL_ERROR;
                return 0;
            }
        }
    }

    return 1;
}

WORK_STATE tls_post_process_client_hello(SSL *s, WORK_STATE wst)
{
    int al = SSL_AD_HANDSHAKE_FAILURE;
    const SSL_CIPHER *cipher;

    if (wst == WORK_MORE_A) {
        if (!s->hit) {
            /* Let cert callback update server certificates if required */
            if (s->cert->cert_cb) {
                int rv = s->cert->cert_cb(s, s->cert->cert_cb_arg);
                if (rv == 0) {
                    al = SSL_AD_INTERNAL_ERROR;
                    SSLerr(SSL_F_TLS_POST_PROCESS_CLIENT_HELLO,
                           SSL_R_CERT_CB_ERROR);
                    goto f_err;
                }
                if (rv < 0) {
                    s->rwstate = SSL_X509_LOOKUP;
                    return WORK_MORE_A;
                }
                s->rwstate = SSL_NOTHING;
            }
            cipher =
                ssl3_choose_cipher(s, s->session->ciphers, SSL_get_ciphers(s));

            if (cipher == NULL) {
                SSLerr(SSL_F_TLS_POST_PROCESS_CLIENT_HELLO,
                       SSL_R_NO_SHARED_CIPHER);
                goto f_err;
            }
            s->s3->tmp.new_cipher = cipher;
            /* check whether we should disable session resumption */
            if (s->not_resumable_session_cb != NULL)
                s->session->not_resumable =
                    s->not_resumable_session_cb(s, ((cipher->algorithm_mkey
                                                    & (SSL_kDHE | SSL_kECDHE))
                                                   != 0));
            if (s->session->not_resumable)
                /* do not send a session ticket */
                s->ext.ticket_expected = 0;
        } else {
            /* Session-id reuse */
            s->s3->tmp.new_cipher = s->session->cipher;
        }

        if (!(s->verify_mode & SSL_VERIFY_PEER)) {
            if (!ssl3_digest_cached_records(s, 0)) {
                al = SSL_AD_INTERNAL_ERROR;
                goto f_err;
            }
        }

        /*-
         * we now have the following setup.
         * client_random
         * cipher_list          - our preferred list of ciphers
         * ciphers              - the clients preferred list of ciphers
         * compression          - basically ignored right now
         * ssl version is set   - sslv3
         * s->session           - The ssl session has been setup.
         * s->hit               - session reuse flag
         * s->s3->tmp.new_cipher- the new cipher to use.
         */

        /*
         * Call status_request callback if needed. Has to be done after the
         * certificate callbacks etc above.
         */
        if (!tls_handle_status_request(s, &al)) {
            SSLerr(SSL_F_TLS_POST_PROCESS_CLIENT_HELLO,
                   SSL_R_CLIENTHELLO_TLSEXT);
            goto f_err;
        }

        wst = WORK_MORE_B;
    }
#ifndef OPENSSL_NO_SRP
    if (wst == WORK_MORE_B) {
        int ret;
        if ((ret = ssl_check_srp_ext_ClientHello(s, &al)) < 0) {
            /*
             * callback indicates further work to be done
             */
            s->rwstate = SSL_X509_LOOKUP;
            return WORK_MORE_B;
        }
        if (ret != SSL_ERROR_NONE) {
            /*
             * This is not really an error but the only means to for
             * a client to detect whether srp is supported.
             */
            if (al != TLS1_AD_UNKNOWN_PSK_IDENTITY)
                SSLerr(SSL_F_TLS_POST_PROCESS_CLIENT_HELLO,
                       SSL_R_CLIENTHELLO_TLSEXT);
            else
                SSLerr(SSL_F_TLS_POST_PROCESS_CLIENT_HELLO,
                       SSL_R_PSK_IDENTITY_NOT_FOUND);
            goto f_err;
        }
    }
#endif

    return WORK_FINISHED_STOP;
 f_err:
    ssl3_send_alert(s, SSL3_AL_FATAL, al);
    ossl_statem_set_error(s);
    return WORK_ERROR;
}

int tls_construct_server_hello(SSL *s, WPACKET *pkt)
{
    int compm, al = SSL_AD_INTERNAL_ERROR;
    size_t sl, len;
    int version;

    /* TODO(TLS1.3): Remove the DRAFT conditional before release */
    version = SSL_IS_TLS13(s) ? TLS1_3_VERSION_DRAFT : s->version;
    if (!WPACKET_put_bytes_u16(pkt, version)
               /*
                * Random stuff. Filling of the server_random takes place in
                * tls_process_client_hello()
                */
            || !WPACKET_memcpy(pkt, s->s3->server_random, SSL3_RANDOM_SIZE)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_HELLO, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    /*-
     * There are several cases for the session ID to send
     * back in the server hello:
     * - For session reuse from the session cache,
     *   we send back the old session ID.
     * - If stateless session reuse (using a session ticket)
     *   is successful, we send back the client's "session ID"
     *   (which doesn't actually identify the session).
     * - If it is a new session, we send back the new
     *   session ID.
     * - However, if we want the new session to be single-use,
     *   we send back a 0-length session ID.
     * s->hit is non-zero in either case of session reuse,
     * so the following won't overwrite an ID that we're supposed
     * to send back.
     */
    if (s->session->not_resumable ||
        (!(s->ctx->session_cache_mode & SSL_SESS_CACHE_SERVER)
         && !s->hit))
        s->session->session_id_length = 0;

    sl = s->session->session_id_length;
    if (sl > sizeof(s->session->session_id)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_HELLO, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    /* set up the compression method */
#ifdef OPENSSL_NO_COMP
    compm = 0;
#else
    if (s->s3->tmp.new_compression == NULL)
        compm = 0;
    else
        compm = s->s3->tmp.new_compression->id;
#endif

    if ((!SSL_IS_TLS13(s)
                && !WPACKET_sub_memcpy_u8(pkt, s->session->session_id, sl))
            || !s->method->put_cipher_by_char(s->s3->tmp.new_cipher, pkt, &len)
            || (!SSL_IS_TLS13(s)
                && !WPACKET_put_bytes_u8(pkt, compm))
            || !tls_construct_extensions(s, pkt,
                                         SSL_IS_TLS13(s)
                                            ? EXT_TLS1_3_SERVER_HELLO
                                            : EXT_TLS1_2_SERVER_HELLO,
                                         NULL, 0, &al)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_HELLO, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    return 1;
 err:
    ssl3_send_alert(s, SSL3_AL_FATAL, al);
    return 0;
}

int tls_construct_server_done(SSL *s, WPACKET *pkt)
{
    if (!s->s3->tmp.cert_request) {
        if (!ssl3_digest_cached_records(s, 0)) {
            ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_INTERNAL_ERROR);
            return 0;
        }
    }
    return 1;
}

int tls_construct_server_key_exchange(SSL *s, WPACKET *pkt)
{
#ifndef OPENSSL_NO_DH
    EVP_PKEY *pkdh = NULL;
#endif
#ifndef OPENSSL_NO_EC
    unsigned char *encodedPoint = NULL;
    size_t encodedlen = 0;
    int curve_id = 0;
#endif
    EVP_PKEY *pkey;
    const EVP_MD *md = NULL;
    int al = SSL_AD_INTERNAL_ERROR, i;
    unsigned long type;
    const BIGNUM *r[4];
    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    EVP_PKEY_CTX *pctx = NULL;
    size_t paramlen, paramoffset;

    if (!WPACKET_get_total_written(pkt, &paramoffset)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE, ERR_R_INTERNAL_ERROR);
        goto f_err;
    }

    if (md_ctx == NULL) {
        SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE, ERR_R_MALLOC_FAILURE);
        goto f_err;
    }

    type = s->s3->tmp.new_cipher->algorithm_mkey;

    r[0] = r[1] = r[2] = r[3] = NULL;
#ifndef OPENSSL_NO_PSK
    /* Plain PSK or RSAPSK nothing to do */
    if (type & (SSL_kPSK | SSL_kRSAPSK)) {
    } else
#endif                          /* !OPENSSL_NO_PSK */
#ifndef OPENSSL_NO_DH
    if (type & (SSL_kDHE | SSL_kDHEPSK)) {
        CERT *cert = s->cert;

        EVP_PKEY *pkdhp = NULL;
        DH *dh;

        if (s->cert->dh_tmp_auto) {
            DH *dhp = ssl_get_auto_dh(s);
            pkdh = EVP_PKEY_new();
            if (pkdh == NULL || dhp == NULL) {
                DH_free(dhp);
                SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                       ERR_R_INTERNAL_ERROR);
                goto f_err;
            }
            EVP_PKEY_assign_DH(pkdh, dhp);
            pkdhp = pkdh;
        } else {
            pkdhp = cert->dh_tmp;
        }
        if ((pkdhp == NULL) && (s->cert->dh_tmp_cb != NULL)) {
            DH *dhp = s->cert->dh_tmp_cb(s, 0, 1024);
            pkdh = ssl_dh_to_pkey(dhp);
            if (pkdh == NULL) {
                SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                       ERR_R_INTERNAL_ERROR);
                goto f_err;
            }
            pkdhp = pkdh;
        }
        if (pkdhp == NULL) {
            al = SSL_AD_HANDSHAKE_FAILURE;
            SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                   SSL_R_MISSING_TMP_DH_KEY);
            goto f_err;
        }
        if (!ssl_security(s, SSL_SECOP_TMP_DH,
                          EVP_PKEY_security_bits(pkdhp), 0, pkdhp)) {
            al = SSL_AD_HANDSHAKE_FAILURE;
            SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                   SSL_R_DH_KEY_TOO_SMALL);
            goto f_err;
        }
        if (s->s3->tmp.pkey != NULL) {
            SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                   ERR_R_INTERNAL_ERROR);
            goto err;
        }

        s->s3->tmp.pkey = ssl_generate_pkey(pkdhp);

        if (s->s3->tmp.pkey == NULL) {
            SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE, ERR_R_EVP_LIB);
            goto err;
        }

        dh = EVP_PKEY_get0_DH(s->s3->tmp.pkey);

        EVP_PKEY_free(pkdh);
        pkdh = NULL;

        DH_get0_pqg(dh, &r[0], NULL, &r[1]);
        DH_get0_key(dh, &r[2], NULL);
    } else
#endif
#ifndef OPENSSL_NO_EC
    if (type & (SSL_kECDHE | SSL_kECDHEPSK)) {
        int nid;

        if (s->s3->tmp.pkey != NULL) {
            SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                   ERR_R_INTERNAL_ERROR);
            goto err;
        }

        /* Get NID of appropriate shared curve */
        nid = tls1_shared_group(s, -2);
        curve_id = tls1_ec_nid2curve_id(nid);
        if (curve_id == 0) {
            SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                   SSL_R_UNSUPPORTED_ELLIPTIC_CURVE);
            goto err;
        }
        s->s3->tmp.pkey = ssl_generate_pkey_curve(curve_id);
        /* Generate a new key for this curve */
        if (s->s3->tmp.pkey == NULL) {
            SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE, ERR_R_EVP_LIB);
            goto f_err;
        }

        /* Encode the public key. */
        encodedlen = EVP_PKEY_get1_tls_encodedpoint(s->s3->tmp.pkey,
                                                    &encodedPoint);
        if (encodedlen == 0) {
            SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE, ERR_R_EC_LIB);
            goto err;
        }

        /*
         * We'll generate the serverKeyExchange message explicitly so we
         * can set these to NULLs
         */
        r[0] = NULL;
        r[1] = NULL;
        r[2] = NULL;
        r[3] = NULL;
    } else
#endif                          /* !OPENSSL_NO_EC */
#ifndef OPENSSL_NO_SRP
    if (type & SSL_kSRP) {
        if ((s->srp_ctx.N == NULL) ||
            (s->srp_ctx.g == NULL) ||
            (s->srp_ctx.s == NULL) || (s->srp_ctx.B == NULL)) {
            SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                   SSL_R_MISSING_SRP_PARAM);
            goto err;
        }
        r[0] = s->srp_ctx.N;
        r[1] = s->srp_ctx.g;
        r[2] = s->srp_ctx.s;
        r[3] = s->srp_ctx.B;
    } else
#endif
    {
        al = SSL_AD_HANDSHAKE_FAILURE;
        SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
               SSL_R_UNKNOWN_KEY_EXCHANGE_TYPE);
        goto f_err;
    }

    if (!(s->s3->tmp.new_cipher->algorithm_auth & (SSL_aNULL | SSL_aSRP))
        && !(s->s3->tmp.new_cipher->algorithm_mkey & SSL_PSK)) {
        if ((pkey = ssl_get_sign_pkey(s, s->s3->tmp.new_cipher, &md))
            == NULL) {
            al = SSL_AD_DECODE_ERROR;
            goto f_err;
        }
    } else {
        pkey = NULL;
    }

#ifndef OPENSSL_NO_PSK
    if (type & SSL_PSK) {
        size_t len = (s->cert->psk_identity_hint == NULL)
                        ? 0 : strlen(s->cert->psk_identity_hint);

        /*
         * It should not happen that len > PSK_MAX_IDENTITY_LEN - we already
         * checked this when we set the identity hint - but just in case
         */
        if (len > PSK_MAX_IDENTITY_LEN
                || !WPACKET_sub_memcpy_u16(pkt, s->cert->psk_identity_hint,
                                           len)) {
            SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                   ERR_R_INTERNAL_ERROR);
            goto f_err;
        }
    }
#endif

    for (i = 0; i < 4 && r[i] != NULL; i++) {
        unsigned char *binval;
        int res;

#ifndef OPENSSL_NO_SRP
        if ((i == 2) && (type & SSL_kSRP)) {
            res = WPACKET_start_sub_packet_u8(pkt);
        } else
#endif
            res = WPACKET_start_sub_packet_u16(pkt);

        if (!res) {
            SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                   ERR_R_INTERNAL_ERROR);
            goto f_err;
        }

#ifndef OPENSSL_NO_DH
        /*-
         * for interoperability with some versions of the Microsoft TLS
         * stack, we need to zero pad the DHE pub key to the same length
         * as the prime
         */
        if ((i == 2) && (type & (SSL_kDHE | SSL_kDHEPSK))) {
            size_t len = BN_num_bytes(r[0]) - BN_num_bytes(r[2]);

            if (len > 0) {
                if (!WPACKET_allocate_bytes(pkt, len, &binval)) {
                    SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                           ERR_R_INTERNAL_ERROR);
                    goto f_err;
                }
                memset(binval, 0, len);
            }
        }
#endif
        if (!WPACKET_allocate_bytes(pkt, BN_num_bytes(r[i]), &binval)
                || !WPACKET_close(pkt)) {
            SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                   ERR_R_INTERNAL_ERROR);
            goto f_err;
        }

        BN_bn2bin(r[i], binval);
    }

#ifndef OPENSSL_NO_EC
    if (type & (SSL_kECDHE | SSL_kECDHEPSK)) {
        /*
         * We only support named (not generic) curves. In this situation, the
         * ServerKeyExchange message has: [1 byte CurveType], [2 byte CurveName]
         * [1 byte length of encoded point], followed by the actual encoded
         * point itself
         */
        if (!WPACKET_put_bytes_u8(pkt, NAMED_CURVE_TYPE)
                || !WPACKET_put_bytes_u8(pkt, 0)
                || !WPACKET_put_bytes_u8(pkt, curve_id)
                || !WPACKET_sub_memcpy_u8(pkt, encodedPoint, encodedlen)) {
            SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                   ERR_R_INTERNAL_ERROR);
            goto f_err;
        }
        OPENSSL_free(encodedPoint);
        encodedPoint = NULL;
    }
#endif

    /* not anonymous */
    if (pkey != NULL) {
        /*
         * n is the length of the params, they start at &(d[4]) and p
         * points to the space at the end.
         */
        if (md) {
            unsigned char *sigbytes1, *sigbytes2;
            size_t siglen;
            int ispss = 0;

            /* Get length of the parameters we have written above */
            if (!WPACKET_get_length(pkt, &paramlen)) {
                SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                       ERR_R_INTERNAL_ERROR);
                goto f_err;
            }
            /* send signature algorithm */
            if (SSL_USE_SIGALGS(s)) {
                if (!tls12_get_sigandhash(s, pkt, pkey, md, &ispss)) {
                    /* Should never happen */
                    SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                           ERR_R_INTERNAL_ERROR);
                    goto f_err;
                }
            }
#ifdef SSL_DEBUG
            fprintf(stderr, "Using hash %s\n", EVP_MD_name(md));
#endif
            /*
             * Create the signature. We don't know the actual length of the sig
             * until after we've created it, so we reserve enough bytes for it
             * up front, and then properly allocate them in the WPACKET
             * afterwards.
             */
            siglen = EVP_PKEY_size(pkey);
            if (!WPACKET_sub_reserve_bytes_u16(pkt, siglen, &sigbytes1)
                    || EVP_DigestSignInit(md_ctx, &pctx, md, NULL, pkey) <= 0) {
                SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                       ERR_R_INTERNAL_ERROR);
                goto f_err;
            }
            if (ispss) {
                if (EVP_PKEY_CTX_set_rsa_padding(pctx,
                                                 RSA_PKCS1_PSS_PADDING) <= 0
                    || EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, RSA_PSS_SALTLEN_DIGEST) <= 0) {
                    SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                           ERR_R_EVP_LIB);
                    goto f_err;
                }
            }
            if (EVP_DigestSignUpdate(md_ctx, &(s->s3->client_random[0]),
                                     SSL3_RANDOM_SIZE) <= 0
                    || EVP_DigestSignUpdate(md_ctx, &(s->s3->server_random[0]),
                                            SSL3_RANDOM_SIZE) <= 0
                    || EVP_DigestSignUpdate(md_ctx,
                                            s->init_buf->data + paramoffset,
                                            paramlen) <= 0
                    || EVP_DigestSignFinal(md_ctx, sigbytes1, &siglen) <= 0
                    || !WPACKET_sub_allocate_bytes_u16(pkt, siglen, &sigbytes2)
                    || sigbytes1 != sigbytes2) {
                SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                       ERR_R_INTERNAL_ERROR);
                goto f_err;
            }
        } else {
            /* Is this error check actually needed? */
            al = SSL_AD_HANDSHAKE_FAILURE;
            SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_KEY_EXCHANGE,
                   SSL_R_UNKNOWN_PKEY_TYPE);
            goto f_err;
        }
    }

    EVP_MD_CTX_free(md_ctx);
    return 1;
 f_err:
    ssl3_send_alert(s, SSL3_AL_FATAL, al);
 err:
#ifndef OPENSSL_NO_DH
    EVP_PKEY_free(pkdh);
#endif
#ifndef OPENSSL_NO_EC
    OPENSSL_free(encodedPoint);
#endif
    EVP_MD_CTX_free(md_ctx);
    return 0;
}

int tls_construct_certificate_request(SSL *s, WPACKET *pkt)
{
    int i;
    STACK_OF(X509_NAME) *sk = NULL;

    /* get the list of acceptable cert types */
    if (!WPACKET_start_sub_packet_u8(pkt)
            || !ssl3_get_req_cert_type(s, pkt)
            || !WPACKET_close(pkt)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CERTIFICATE_REQUEST, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    if (SSL_USE_SIGALGS(s)) {
        const uint16_t *psigs;
        size_t nl = tls12_get_psigalgs(s, 1, &psigs);

        if (!WPACKET_start_sub_packet_u16(pkt)
                || !tls12_copy_sigalgs(s, pkt, psigs, nl)
                || !WPACKET_close(pkt)) {
            SSLerr(SSL_F_TLS_CONSTRUCT_CERTIFICATE_REQUEST,
                   ERR_R_INTERNAL_ERROR);
            goto err;
        }
    }

    /* Start sub-packet for client CA list */
    if (!WPACKET_start_sub_packet_u16(pkt)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CERTIFICATE_REQUEST, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    sk = SSL_get_client_CA_list(s);
    if (sk != NULL) {
        for (i = 0; i < sk_X509_NAME_num(sk); i++) {
            unsigned char *namebytes;
            X509_NAME *name = sk_X509_NAME_value(sk, i);
            int namelen;

            if (name == NULL
                    || (namelen = i2d_X509_NAME(name, NULL)) < 0
                    || !WPACKET_sub_allocate_bytes_u16(pkt, namelen,
                                                       &namebytes)
                    || i2d_X509_NAME(name, &namebytes) != namelen) {
                SSLerr(SSL_F_TLS_CONSTRUCT_CERTIFICATE_REQUEST,
                       ERR_R_INTERNAL_ERROR);
                goto err;
            }
        }
    }
    /* else no CA names */

    if (!WPACKET_close(pkt)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CERTIFICATE_REQUEST, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    s->s3->tmp.cert_request = 1;

    return 1;
 err:
    ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_INTERNAL_ERROR);
    return 0;
}

static int tls_process_cke_psk_preamble(SSL *s, PACKET *pkt, int *al)
{
#ifndef OPENSSL_NO_PSK
    unsigned char psk[PSK_MAX_PSK_LEN];
    size_t psklen;
    PACKET psk_identity;

    if (!PACKET_get_length_prefixed_2(pkt, &psk_identity)) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_PSK_PREAMBLE, SSL_R_LENGTH_MISMATCH);
        return 0;
    }
    if (PACKET_remaining(&psk_identity) > PSK_MAX_IDENTITY_LEN) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_PSK_PREAMBLE, SSL_R_DATA_LENGTH_TOO_LONG);
        return 0;
    }
    if (s->psk_server_callback == NULL) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_PSK_PREAMBLE, SSL_R_PSK_NO_SERVER_CB);
        return 0;
    }

    if (!PACKET_strndup(&psk_identity, &s->session->psk_identity)) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_PSK_PREAMBLE, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    psklen = s->psk_server_callback(s, s->session->psk_identity,
                                    psk, sizeof(psk));

    if (psklen > PSK_MAX_PSK_LEN) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_PSK_PREAMBLE, ERR_R_INTERNAL_ERROR);
        return 0;
    } else if (psklen == 0) {
        /*
         * PSK related to the given identity not found
         */
        *al = SSL_AD_UNKNOWN_PSK_IDENTITY;
        SSLerr(SSL_F_TLS_PROCESS_CKE_PSK_PREAMBLE,
               SSL_R_PSK_IDENTITY_NOT_FOUND);
        return 0;
    }

    OPENSSL_free(s->s3->tmp.psk);
    s->s3->tmp.psk = OPENSSL_memdup(psk, psklen);
    OPENSSL_cleanse(psk, psklen);

    if (s->s3->tmp.psk == NULL) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_PSK_PREAMBLE, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    s->s3->tmp.psklen = psklen;

    return 1;
#else
    /* Should never happen */
    *al = SSL_AD_INTERNAL_ERROR;
    SSLerr(SSL_F_TLS_PROCESS_CKE_PSK_PREAMBLE, ERR_R_INTERNAL_ERROR);
    return 0;
#endif
}

static int tls_process_cke_rsa(SSL *s, PACKET *pkt, int *al)
{
#ifndef OPENSSL_NO_RSA
    unsigned char rand_premaster_secret[SSL_MAX_MASTER_KEY_LENGTH];
    int decrypt_len;
    unsigned char decrypt_good, version_good;
    size_t j, padding_len;
    PACKET enc_premaster;
    RSA *rsa = NULL;
    unsigned char *rsa_decrypt = NULL;
    int ret = 0;

    rsa = EVP_PKEY_get0_RSA(s->cert->pkeys[SSL_PKEY_RSA_ENC].privatekey);
    if (rsa == NULL) {
        *al = SSL_AD_HANDSHAKE_FAILURE;
        SSLerr(SSL_F_TLS_PROCESS_CKE_RSA, SSL_R_MISSING_RSA_CERTIFICATE);
        return 0;
    }

    /* SSLv3 and pre-standard DTLS omit the length bytes. */
    if (s->version == SSL3_VERSION || s->version == DTLS1_BAD_VER) {
        enc_premaster = *pkt;
    } else {
        if (!PACKET_get_length_prefixed_2(pkt, &enc_premaster)
            || PACKET_remaining(pkt) != 0) {
            *al = SSL_AD_DECODE_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_CKE_RSA, SSL_R_LENGTH_MISMATCH);
            return 0;
        }
    }

    /*
     * We want to be sure that the plaintext buffer size makes it safe to
     * iterate over the entire size of a premaster secret
     * (SSL_MAX_MASTER_KEY_LENGTH). Reject overly short RSA keys because
     * their ciphertext cannot accommodate a premaster secret anyway.
     */
    if (RSA_size(rsa) < SSL_MAX_MASTER_KEY_LENGTH) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_RSA, RSA_R_KEY_SIZE_TOO_SMALL);
        return 0;
    }

    rsa_decrypt = OPENSSL_malloc(RSA_size(rsa));
    if (rsa_decrypt == NULL) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_RSA, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    /*
     * We must not leak whether a decryption failure occurs because of
     * Bleichenbacher's attack on PKCS #1 v1.5 RSA padding (see RFC 2246,
     * section 7.4.7.1). The code follows that advice of the TLS RFC and
     * generates a random premaster secret for the case that the decrypt
     * fails. See https://tools.ietf.org/html/rfc5246#section-7.4.7.1
     */

    if (RAND_bytes(rand_premaster_secret, sizeof(rand_premaster_secret)) <= 0)
        goto err;

    /*
     * Decrypt with no padding. PKCS#1 padding will be removed as part of
     * the timing-sensitive code below.
     */
     /* TODO(size_t): Convert this function */
    decrypt_len = (int)RSA_private_decrypt((int)PACKET_remaining(&enc_premaster),
                                           PACKET_data(&enc_premaster),
                                           rsa_decrypt, rsa, RSA_NO_PADDING);
    if (decrypt_len < 0)
        goto err;

    /* Check the padding. See RFC 3447, section 7.2.2. */

    /*
     * The smallest padded premaster is 11 bytes of overhead. Small keys
     * are publicly invalid, so this may return immediately. This ensures
     * PS is at least 8 bytes.
     */
    if (decrypt_len < 11 + SSL_MAX_MASTER_KEY_LENGTH) {
        *al = SSL_AD_DECRYPT_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_RSA, SSL_R_DECRYPTION_FAILED);
        goto err;
    }

    padding_len = decrypt_len - SSL_MAX_MASTER_KEY_LENGTH;
    decrypt_good = constant_time_eq_int_8(rsa_decrypt[0], 0) &
        constant_time_eq_int_8(rsa_decrypt[1], 2);
    for (j = 2; j < padding_len - 1; j++) {
        decrypt_good &= ~constant_time_is_zero_8(rsa_decrypt[j]);
    }
    decrypt_good &= constant_time_is_zero_8(rsa_decrypt[padding_len - 1]);

    /*
     * If the version in the decrypted pre-master secret is correct then
     * version_good will be 0xff, otherwise it'll be zero. The
     * Klima-Pokorny-Rosa extension of Bleichenbacher's attack
     * (http://eprint.iacr.org/2003/052/) exploits the version number
     * check as a "bad version oracle". Thus version checks are done in
     * constant time and are treated like any other decryption error.
     */
    version_good =
        constant_time_eq_8(rsa_decrypt[padding_len],
                           (unsigned)(s->client_version >> 8));
    version_good &=
        constant_time_eq_8(rsa_decrypt[padding_len + 1],
                           (unsigned)(s->client_version & 0xff));

    /*
     * The premaster secret must contain the same version number as the
     * ClientHello to detect version rollback attacks (strangely, the
     * protocol does not offer such protection for DH ciphersuites).
     * However, buggy clients exist that send the negotiated protocol
     * version instead if the server does not support the requested
     * protocol version. If SSL_OP_TLS_ROLLBACK_BUG is set, tolerate such
     * clients.
     */
    if (s->options & SSL_OP_TLS_ROLLBACK_BUG) {
        unsigned char workaround_good;
        workaround_good = constant_time_eq_8(rsa_decrypt[padding_len],
                                             (unsigned)(s->version >> 8));
        workaround_good &=
            constant_time_eq_8(rsa_decrypt[padding_len + 1],
                               (unsigned)(s->version & 0xff));
        version_good |= workaround_good;
    }

    /*
     * Both decryption and version must be good for decrypt_good to
     * remain non-zero (0xff).
     */
    decrypt_good &= version_good;

    /*
     * Now copy rand_premaster_secret over from p using
     * decrypt_good_mask. If decryption failed, then p does not
     * contain valid plaintext, however, a check above guarantees
     * it is still sufficiently large to read from.
     */
    for (j = 0; j < sizeof(rand_premaster_secret); j++) {
        rsa_decrypt[padding_len + j] =
            constant_time_select_8(decrypt_good,
                                   rsa_decrypt[padding_len + j],
                                   rand_premaster_secret[j]);
    }

    if (!ssl_generate_master_secret(s, rsa_decrypt + padding_len,
                                    sizeof(rand_premaster_secret), 0)) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_RSA, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    ret = 1;
 err:
    OPENSSL_free(rsa_decrypt);
    return ret;
#else
    /* Should never happen */
    *al = SSL_AD_INTERNAL_ERROR;
    SSLerr(SSL_F_TLS_PROCESS_CKE_RSA, ERR_R_INTERNAL_ERROR);
    return 0;
#endif
}

static int tls_process_cke_dhe(SSL *s, PACKET *pkt, int *al)
{
#ifndef OPENSSL_NO_DH
    EVP_PKEY *skey = NULL;
    DH *cdh;
    unsigned int i;
    BIGNUM *pub_key;
    const unsigned char *data;
    EVP_PKEY *ckey = NULL;
    int ret = 0;

    if (!PACKET_get_net_2(pkt, &i) || PACKET_remaining(pkt) != i) {
        *al = SSL_AD_HANDSHAKE_FAILURE;
        SSLerr(SSL_F_TLS_PROCESS_CKE_DHE,
               SSL_R_DH_PUBLIC_VALUE_LENGTH_IS_WRONG);
        goto err;
    }
    skey = s->s3->tmp.pkey;
    if (skey == NULL) {
        *al = SSL_AD_HANDSHAKE_FAILURE;
        SSLerr(SSL_F_TLS_PROCESS_CKE_DHE, SSL_R_MISSING_TMP_DH_KEY);
        goto err;
    }

    if (PACKET_remaining(pkt) == 0L) {
        *al = SSL_AD_HANDSHAKE_FAILURE;
        SSLerr(SSL_F_TLS_PROCESS_CKE_DHE, SSL_R_MISSING_TMP_DH_KEY);
        goto err;
    }
    if (!PACKET_get_bytes(pkt, &data, i)) {
        /* We already checked we have enough data */
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_DHE, ERR_R_INTERNAL_ERROR);
        goto err;
    }
    ckey = EVP_PKEY_new();
    if (ckey == NULL || EVP_PKEY_copy_parameters(ckey, skey) == 0) {
        SSLerr(SSL_F_TLS_PROCESS_CKE_DHE, SSL_R_BN_LIB);
        goto err;
    }
    cdh = EVP_PKEY_get0_DH(ckey);
    pub_key = BN_bin2bn(data, i, NULL);

    if (pub_key == NULL || !DH_set0_key(cdh, pub_key, NULL)) {
        SSLerr(SSL_F_TLS_PROCESS_CKE_DHE, ERR_R_INTERNAL_ERROR);
        if (pub_key != NULL)
            BN_free(pub_key);
        goto err;
    }

    if (ssl_derive(s, skey, ckey, 1) == 0) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_DHE, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    ret = 1;
    EVP_PKEY_free(s->s3->tmp.pkey);
    s->s3->tmp.pkey = NULL;
 err:
    EVP_PKEY_free(ckey);
    return ret;
#else
    /* Should never happen */
    *al = SSL_AD_INTERNAL_ERROR;
    SSLerr(SSL_F_TLS_PROCESS_CKE_DHE, ERR_R_INTERNAL_ERROR);
    return 0;
#endif
}

static int tls_process_cke_ecdhe(SSL *s, PACKET *pkt, int *al)
{
#ifndef OPENSSL_NO_EC
    EVP_PKEY *skey = s->s3->tmp.pkey;
    EVP_PKEY *ckey = NULL;
    int ret = 0;

    if (PACKET_remaining(pkt) == 0L) {
        /* We don't support ECDH client auth */
        *al = SSL_AD_HANDSHAKE_FAILURE;
        SSLerr(SSL_F_TLS_PROCESS_CKE_ECDHE, SSL_R_MISSING_TMP_ECDH_KEY);
        goto err;
    } else {
        unsigned int i;
        const unsigned char *data;

        /*
         * Get client's public key from encoded point in the
         * ClientKeyExchange message.
         */

        /* Get encoded point length */
        if (!PACKET_get_1(pkt, &i) || !PACKET_get_bytes(pkt, &data, i)
            || PACKET_remaining(pkt) != 0) {
            *al = SSL_AD_DECODE_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_CKE_ECDHE, SSL_R_LENGTH_MISMATCH);
            goto err;
        }
        ckey = EVP_PKEY_new();
        if (ckey == NULL || EVP_PKEY_copy_parameters(ckey, skey) <= 0) {
            SSLerr(SSL_F_TLS_PROCESS_CKE_ECDHE, ERR_R_EVP_LIB);
            goto err;
        }
        if (EVP_PKEY_set1_tls_encodedpoint(ckey, data, i) == 0) {
            *al = SSL_AD_HANDSHAKE_FAILURE;
            SSLerr(SSL_F_TLS_PROCESS_CKE_ECDHE, ERR_R_EC_LIB);
            goto err;
        }
    }

    if (ssl_derive(s, skey, ckey, 1) == 0) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_ECDHE, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    ret = 1;
    EVP_PKEY_free(s->s3->tmp.pkey);
    s->s3->tmp.pkey = NULL;
 err:
    EVP_PKEY_free(ckey);

    return ret;
#else
    /* Should never happen */
    *al = SSL_AD_INTERNAL_ERROR;
    SSLerr(SSL_F_TLS_PROCESS_CKE_ECDHE, ERR_R_INTERNAL_ERROR);
    return 0;
#endif
}

static int tls_process_cke_srp(SSL *s, PACKET *pkt, int *al)
{
#ifndef OPENSSL_NO_SRP
    unsigned int i;
    const unsigned char *data;

    if (!PACKET_get_net_2(pkt, &i)
        || !PACKET_get_bytes(pkt, &data, i)) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_SRP, SSL_R_BAD_SRP_A_LENGTH);
        return 0;
    }
    if ((s->srp_ctx.A = BN_bin2bn(data, i, NULL)) == NULL) {
        SSLerr(SSL_F_TLS_PROCESS_CKE_SRP, ERR_R_BN_LIB);
        return 0;
    }
    if (BN_ucmp(s->srp_ctx.A, s->srp_ctx.N) >= 0 || BN_is_zero(s->srp_ctx.A)) {
        *al = SSL_AD_ILLEGAL_PARAMETER;
        SSLerr(SSL_F_TLS_PROCESS_CKE_SRP, SSL_R_BAD_SRP_PARAMETERS);
        return 0;
    }
    OPENSSL_free(s->session->srp_username);
    s->session->srp_username = OPENSSL_strdup(s->srp_ctx.login);
    if (s->session->srp_username == NULL) {
        SSLerr(SSL_F_TLS_PROCESS_CKE_SRP, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    if (!srp_generate_server_master_secret(s)) {
        SSLerr(SSL_F_TLS_PROCESS_CKE_SRP, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    return 1;
#else
    /* Should never happen */
    *al = SSL_AD_INTERNAL_ERROR;
    SSLerr(SSL_F_TLS_PROCESS_CKE_SRP, ERR_R_INTERNAL_ERROR);
    return 0;
#endif
}

static int tls_process_cke_gost(SSL *s, PACKET *pkt, int *al)
{
#ifndef OPENSSL_NO_GOST
    EVP_PKEY_CTX *pkey_ctx;
    EVP_PKEY *client_pub_pkey = NULL, *pk = NULL;
    unsigned char premaster_secret[32];
    const unsigned char *start;
    size_t outlen = 32, inlen;
    unsigned long alg_a;
    int Ttag, Tclass;
    long Tlen;
    size_t sess_key_len;
    const unsigned char *data;
    int ret = 0;

    /* Get our certificate private key */
    alg_a = s->s3->tmp.new_cipher->algorithm_auth;
    if (alg_a & SSL_aGOST12) {
        /*
         * New GOST ciphersuites have SSL_aGOST01 bit too
         */
        pk = s->cert->pkeys[SSL_PKEY_GOST12_512].privatekey;
        if (pk == NULL) {
            pk = s->cert->pkeys[SSL_PKEY_GOST12_256].privatekey;
        }
        if (pk == NULL) {
            pk = s->cert->pkeys[SSL_PKEY_GOST01].privatekey;
        }
    } else if (alg_a & SSL_aGOST01) {
        pk = s->cert->pkeys[SSL_PKEY_GOST01].privatekey;
    }

    pkey_ctx = EVP_PKEY_CTX_new(pk, NULL);
    if (pkey_ctx == NULL) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_GOST, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    if (EVP_PKEY_decrypt_init(pkey_ctx) <= 0) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_GOST, ERR_R_INTERNAL_ERROR);
        return 0;
    }
    /*
     * If client certificate is present and is of the same type, maybe
     * use it for key exchange.  Don't mind errors from
     * EVP_PKEY_derive_set_peer, because it is completely valid to use a
     * client certificate for authorization only.
     */
    client_pub_pkey = X509_get0_pubkey(s->session->peer);
    if (client_pub_pkey) {
        if (EVP_PKEY_derive_set_peer(pkey_ctx, client_pub_pkey) <= 0)
            ERR_clear_error();
    }
    /* Decrypt session key */
    sess_key_len = PACKET_remaining(pkt);
    if (!PACKET_get_bytes(pkt, &data, sess_key_len)) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_GOST, ERR_R_INTERNAL_ERROR);
        goto err;
    }
    /* TODO(size_t): Convert this function */
    if (ASN1_get_object((const unsigned char **)&data, &Tlen, &Ttag,
                        &Tclass, (long)sess_key_len) != V_ASN1_CONSTRUCTED
        || Ttag != V_ASN1_SEQUENCE || Tclass != V_ASN1_UNIVERSAL) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_GOST, SSL_R_DECRYPTION_FAILED);
        goto err;
    }
    start = data;
    inlen = Tlen;
    if (EVP_PKEY_decrypt
        (pkey_ctx, premaster_secret, &outlen, start, inlen) <= 0) {
        *al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_GOST, SSL_R_DECRYPTION_FAILED);
        goto err;
    }
    /* Generate master secret */
    if (!ssl_generate_master_secret(s, premaster_secret,
                                    sizeof(premaster_secret), 0)) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CKE_GOST, ERR_R_INTERNAL_ERROR);
        goto err;
    }
    /* Check if pubkey from client certificate was used */
    if (EVP_PKEY_CTX_ctrl
        (pkey_ctx, -1, -1, EVP_PKEY_CTRL_PEER_KEY, 2, NULL) > 0)
        s->statem.no_cert_verify = 1;

    ret = 1;
 err:
    EVP_PKEY_CTX_free(pkey_ctx);
    return ret;
#else
    /* Should never happen */
    *al = SSL_AD_INTERNAL_ERROR;
    SSLerr(SSL_F_TLS_PROCESS_CKE_GOST, ERR_R_INTERNAL_ERROR);
    return 0;
#endif
}

MSG_PROCESS_RETURN tls_process_client_key_exchange(SSL *s, PACKET *pkt)
{
    int al = -1;
    unsigned long alg_k;

    alg_k = s->s3->tmp.new_cipher->algorithm_mkey;

    /* For PSK parse and retrieve identity, obtain PSK key */
    if ((alg_k & SSL_PSK) && !tls_process_cke_psk_preamble(s, pkt, &al))
        goto err;

    if (alg_k & SSL_kPSK) {
        /* Identity extracted earlier: should be nothing left */
        if (PACKET_remaining(pkt) != 0) {
            al = SSL_AD_HANDSHAKE_FAILURE;
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_KEY_EXCHANGE,
                   SSL_R_LENGTH_MISMATCH);
            goto err;
        }
        /* PSK handled by ssl_generate_master_secret */
        if (!ssl_generate_master_secret(s, NULL, 0, 0)) {
            al = SSL_AD_INTERNAL_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_KEY_EXCHANGE, ERR_R_INTERNAL_ERROR);
            goto err;
        }
    } else if (alg_k & (SSL_kRSA | SSL_kRSAPSK)) {
        if (!tls_process_cke_rsa(s, pkt, &al))
            goto err;
    } else if (alg_k & (SSL_kDHE | SSL_kDHEPSK)) {
        if (!tls_process_cke_dhe(s, pkt, &al))
            goto err;
    } else if (alg_k & (SSL_kECDHE | SSL_kECDHEPSK)) {
        if (!tls_process_cke_ecdhe(s, pkt, &al))
            goto err;
    } else if (alg_k & SSL_kSRP) {
        if (!tls_process_cke_srp(s, pkt, &al))
            goto err;
    } else if (alg_k & SSL_kGOST) {
        if (!tls_process_cke_gost(s, pkt, &al))
            goto err;
    } else {
        al = SSL_AD_HANDSHAKE_FAILURE;
        SSLerr(SSL_F_TLS_PROCESS_CLIENT_KEY_EXCHANGE,
               SSL_R_UNKNOWN_CIPHER_TYPE);
        goto err;
    }

    return MSG_PROCESS_CONTINUE_PROCESSING;
 err:
    if (al != -1)
        ssl3_send_alert(s, SSL3_AL_FATAL, al);
#ifndef OPENSSL_NO_PSK
    OPENSSL_clear_free(s->s3->tmp.psk, s->s3->tmp.psklen);
    s->s3->tmp.psk = NULL;
#endif
    ossl_statem_set_error(s);
    return MSG_PROCESS_ERROR;
}

WORK_STATE tls_post_process_client_key_exchange(SSL *s, WORK_STATE wst)
{
#ifndef OPENSSL_NO_SCTP
    if (wst == WORK_MORE_A) {
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
                                           sizeof(labelbuffer), NULL, 0,
                                           0) <= 0) {
                ossl_statem_set_error(s);
                return WORK_ERROR;
            }

            BIO_ctrl(SSL_get_wbio(s), BIO_CTRL_DGRAM_SCTP_ADD_AUTH_KEY,
                     sizeof(sctpauthkey), sctpauthkey);
        }
        wst = WORK_MORE_B;
    }

    if ((wst == WORK_MORE_B)
        /* Is this SCTP? */
        && BIO_dgram_is_sctp(SSL_get_wbio(s))
        /* Are we renegotiating? */
        && s->renegotiate
        /* Are we going to skip the CertificateVerify? */
        && (s->session->peer == NULL || s->statem.no_cert_verify)
        && BIO_dgram_sctp_msg_waiting(SSL_get_rbio(s))) {
        s->s3->in_read_app_data = 2;
        s->rwstate = SSL_READING;
        BIO_clear_retry_flags(SSL_get_rbio(s));
        BIO_set_retry_read(SSL_get_rbio(s));
        ossl_statem_set_sctp_read_sock(s, 1);
        return WORK_MORE_B;
    } else {
        ossl_statem_set_sctp_read_sock(s, 0);
    }
#endif

    if (s->statem.no_cert_verify || !s->session->peer) {
        /*
         * No certificate verify or no peer certificate so we no longer need
         * the handshake_buffer
         */
        if (!ssl3_digest_cached_records(s, 0)) {
            ossl_statem_set_error(s);
            return WORK_ERROR;
        }
        return WORK_FINISHED_CONTINUE;
    } else {
        if (!s->s3->handshake_buffer) {
            SSLerr(SSL_F_TLS_POST_PROCESS_CLIENT_KEY_EXCHANGE,
                   ERR_R_INTERNAL_ERROR);
            ossl_statem_set_error(s);
            return WORK_ERROR;
        }
        /*
         * For sigalgs freeze the handshake buffer. If we support
         * extms we've done this already so this is a no-op
         */
        if (!ssl3_digest_cached_records(s, 1)) {
            ossl_statem_set_error(s);
            return WORK_ERROR;
        }
    }

    return WORK_FINISHED_CONTINUE;
}

MSG_PROCESS_RETURN tls_process_client_certificate(SSL *s, PACKET *pkt)
{
    int i, al = SSL_AD_INTERNAL_ERROR, ret = MSG_PROCESS_ERROR;
    X509 *x = NULL;
    unsigned long l, llen;
    const unsigned char *certstart, *certbytes;
    STACK_OF(X509) *sk = NULL;
    PACKET spkt, context;
    size_t chainidx;

    if ((sk = sk_X509_new_null()) == NULL) {
        SSLerr(SSL_F_TLS_PROCESS_CLIENT_CERTIFICATE, ERR_R_MALLOC_FAILURE);
        goto f_err;
    }

    /* TODO(TLS1.3): For now we ignore the context. We need to verify this */
    if ((SSL_IS_TLS13(s) && !PACKET_get_length_prefixed_1(pkt, &context))
            || !PACKET_get_net_3(pkt, &llen)
            || !PACKET_get_sub_packet(pkt, &spkt, llen)
            || PACKET_remaining(pkt) != 0) {
        al = SSL_AD_DECODE_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CLIENT_CERTIFICATE, SSL_R_LENGTH_MISMATCH);
        goto f_err;
    }

    for (chainidx = 0; PACKET_remaining(&spkt) > 0; chainidx++) {
        if (!PACKET_get_net_3(&spkt, &l)
            || !PACKET_get_bytes(&spkt, &certbytes, l)) {
            al = SSL_AD_DECODE_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_CERTIFICATE,
                   SSL_R_CERT_LENGTH_MISMATCH);
            goto f_err;
        }

        certstart = certbytes;
        x = d2i_X509(NULL, (const unsigned char **)&certbytes, l);
        if (x == NULL) {
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_CERTIFICATE, ERR_R_ASN1_LIB);
            goto f_err;
        }
        if (certbytes != (certstart + l)) {
            al = SSL_AD_DECODE_ERROR;
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_CERTIFICATE,
                   SSL_R_CERT_LENGTH_MISMATCH);
            goto f_err;
        }

        if (SSL_IS_TLS13(s)) {
            RAW_EXTENSION *rawexts = NULL;
            PACKET extensions;

            if (!PACKET_get_length_prefixed_2(&spkt, &extensions)) {
                al = SSL_AD_DECODE_ERROR;
                SSLerr(SSL_F_TLS_PROCESS_CLIENT_CERTIFICATE, SSL_R_BAD_LENGTH);
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
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_CERTIFICATE, ERR_R_MALLOC_FAILURE);
            goto f_err;
        }
        x = NULL;
    }

    if (sk_X509_num(sk) <= 0) {
        /* TLS does not mind 0 certs returned */
        if (s->version == SSL3_VERSION) {
            al = SSL_AD_HANDSHAKE_FAILURE;
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_CERTIFICATE,
                   SSL_R_NO_CERTIFICATES_RETURNED);
            goto f_err;
        }
        /* Fail for TLS only if we required a certificate */
        else if ((s->verify_mode & SSL_VERIFY_PEER) &&
                 (s->verify_mode & SSL_VERIFY_FAIL_IF_NO_PEER_CERT)) {
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_CERTIFICATE,
                   SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE);
            al = SSL_AD_HANDSHAKE_FAILURE;
            goto f_err;
        }
        /* No client certificate so digest cached records */
        if (s->s3->handshake_buffer && !ssl3_digest_cached_records(s, 0)) {
            goto f_err;
        }
    } else {
        EVP_PKEY *pkey;
        i = ssl_verify_cert_chain(s, sk);
        if (i <= 0) {
            al = ssl_verify_alarm_type(s->verify_result);
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_CERTIFICATE,
                   SSL_R_CERTIFICATE_VERIFY_FAILED);
            goto f_err;
        }
        if (i > 1) {
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_CERTIFICATE, i);
            al = SSL_AD_HANDSHAKE_FAILURE;
            goto f_err;
        }
        pkey = X509_get0_pubkey(sk_X509_value(sk, 0));
        if (pkey == NULL) {
            al = SSL3_AD_HANDSHAKE_FAILURE;
            SSLerr(SSL_F_TLS_PROCESS_CLIENT_CERTIFICATE,
                   SSL_R_UNKNOWN_CERTIFICATE_TYPE);
            goto f_err;
        }
    }

    X509_free(s->session->peer);
    s->session->peer = sk_X509_shift(sk);
    s->session->verify_result = s->verify_result;

    sk_X509_pop_free(s->session->peer_chain, X509_free);
    s->session->peer_chain = sk;

    /*
     * Freeze the handshake buffer. For <TLS1.3 we do this after the CKE
     * message
     */
    if (SSL_IS_TLS13(s) && !ssl3_digest_cached_records(s, 1)) {
        al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CLIENT_CERTIFICATE, ERR_R_INTERNAL_ERROR);
        goto f_err;
    }

    /*
     * Inconsistency alert: cert_chain does *not* include the peer's own
     * certificate, while we do include it in statem_clnt.c
     */
    sk = NULL;

    /* Save the current hash state for when we receive the CertificateVerify */
    if (SSL_IS_TLS13(s)
            && !ssl_handshake_hash(s, s->cert_verify_hash,
                                   sizeof(s->cert_verify_hash),
                                   &s->cert_verify_hash_len)) {
        al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_TLS_PROCESS_CLIENT_CERTIFICATE, ERR_R_INTERNAL_ERROR);
        goto f_err;
    }

    ret = MSG_PROCESS_CONTINUE_READING;
    goto done;

 f_err:
    ssl3_send_alert(s, SSL3_AL_FATAL, al);
    ossl_statem_set_error(s);
 done:
    X509_free(x);
    sk_X509_pop_free(sk, X509_free);
    return ret;
}

int tls_construct_server_certificate(SSL *s, WPACKET *pkt)
{
    CERT_PKEY *cpk;
    int al = SSL_AD_INTERNAL_ERROR;

    cpk = ssl_get_server_send_pkey(s);
    if (cpk == NULL) {
        SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_CERTIFICATE, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    /*
     * In TLSv1.3 the certificate chain is always preceded by a 0 length context
     * for the server Certificate message
     */
    if ((SSL_IS_TLS13(s) && !WPACKET_put_bytes_u8(pkt, 0))
            || !ssl3_output_cert_chain(s, pkt, cpk, &al)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_SERVER_CERTIFICATE, ERR_R_INTERNAL_ERROR);
        ssl3_send_alert(s, SSL3_AL_FATAL, al);
        return 0;
    }

    return 1;
}

int tls_construct_new_session_ticket(SSL *s, WPACKET *pkt)
{
    unsigned char *senc = NULL;
    EVP_CIPHER_CTX *ctx = NULL;
    HMAC_CTX *hctx = NULL;
    unsigned char *p, *encdata1, *encdata2, *macdata1, *macdata2;
    const unsigned char *const_p;
    int len, slen_full, slen, lenfinal;
    SSL_SESSION *sess;
    unsigned int hlen;
    SSL_CTX *tctx = s->session_ctx;
    unsigned char iv[EVP_MAX_IV_LENGTH];
    unsigned char key_name[TLSEXT_KEYNAME_LENGTH];
    int iv_len, al = SSL_AD_INTERNAL_ERROR;
    size_t macoffset, macendoffset;
    union {
        unsigned char age_add_c[sizeof(uint32_t)];
        uint32_t age_add;
    } age_add_u;

    if (SSL_IS_TLS13(s)) {
        if (RAND_bytes(age_add_u.age_add_c, sizeof(age_add_u)) <= 0)
            goto err;
        s->session->ext.tick_age_add = age_add_u.age_add;
    }

    /* get session encoding length */
    slen_full = i2d_SSL_SESSION(s->session, NULL);
    /*
     * Some length values are 16 bits, so forget it if session is too
     * long
     */
    if (slen_full == 0 || slen_full > 0xFF00) {
        ossl_statem_set_error(s);
        return 0;
    }
    senc = OPENSSL_malloc(slen_full);
    if (senc == NULL) {
        ossl_statem_set_error(s);
        return 0;
    }

    ctx = EVP_CIPHER_CTX_new();
    hctx = HMAC_CTX_new();
    if (ctx == NULL || hctx == NULL) {
        SSLerr(SSL_F_TLS_CONSTRUCT_NEW_SESSION_TICKET, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    p = senc;
    if (!i2d_SSL_SESSION(s->session, &p))
        goto err;

    /*
     * create a fresh copy (not shared with other threads) to clean up
     */
    const_p = senc;
    sess = d2i_SSL_SESSION(NULL, &const_p, slen_full);
    if (sess == NULL)
        goto err;
    sess->session_id_length = 0; /* ID is irrelevant for the ticket */

    slen = i2d_SSL_SESSION(sess, NULL);
    if (slen == 0 || slen > slen_full) { /* shouldn't ever happen */
        SSL_SESSION_free(sess);
        goto err;
    }
    p = senc;
    if (!i2d_SSL_SESSION(sess, &p)) {
        SSL_SESSION_free(sess);
        goto err;
    }
    SSL_SESSION_free(sess);

    /*
     * Initialize HMAC and cipher contexts. If callback present it does
     * all the work otherwise use generated values from parent ctx.
     */
    if (tctx->ext.ticket_key_cb) {
        /* if 0 is returned, write an empty ticket */
        int ret = tctx->ext.ticket_key_cb(s, key_name, iv, ctx,
                                             hctx, 1);

        if (ret == 0) {

            /* Put timeout and length */
            if (!WPACKET_put_bytes_u32(pkt, 0)
                    || !WPACKET_put_bytes_u16(pkt, 0)) {
                SSLerr(SSL_F_TLS_CONSTRUCT_NEW_SESSION_TICKET,
                       ERR_R_INTERNAL_ERROR);
                goto err;
            }
            OPENSSL_free(senc);
            EVP_CIPHER_CTX_free(ctx);
            HMAC_CTX_free(hctx);
            return 1;
        }
        if (ret < 0)
            goto err;
        iv_len = EVP_CIPHER_CTX_iv_length(ctx);
    } else {
        const EVP_CIPHER *cipher = EVP_aes_256_cbc();

        iv_len = EVP_CIPHER_iv_length(cipher);
        if (RAND_bytes(iv, iv_len) <= 0)
            goto err;
        if (!EVP_EncryptInit_ex(ctx, cipher, NULL,
                                tctx->ext.tick_aes_key, iv))
            goto err;
        if (!HMAC_Init_ex(hctx, tctx->ext.tick_hmac_key,
                          sizeof(tctx->ext.tick_hmac_key),
                          EVP_sha256(), NULL))
            goto err;
        memcpy(key_name, tctx->ext.tick_key_name,
               sizeof(tctx->ext.tick_key_name));
    }

    /*
     * Ticket lifetime hint (advisory only): We leave this unspecified
     * for resumed session (for simplicity), and guess that tickets for
     * new sessions will live as long as their sessions.
     */
    if (!WPACKET_put_bytes_u32(pkt, s->hit ? 0 : s->session->timeout)
            || (SSL_IS_TLS13(s)
                && !WPACKET_put_bytes_u32(pkt, age_add_u.age_add))
               /* Now the actual ticket data */
            || !WPACKET_start_sub_packet_u16(pkt)
            || !WPACKET_get_total_written(pkt, &macoffset)
               /* Output key name */
            || !WPACKET_memcpy(pkt, key_name, sizeof(key_name))
               /* output IV */
            || !WPACKET_memcpy(pkt, iv, iv_len)
            || !WPACKET_reserve_bytes(pkt, slen + EVP_MAX_BLOCK_LENGTH,
                                      &encdata1)
               /* Encrypt session data */
            || !EVP_EncryptUpdate(ctx, encdata1, &len, senc, slen)
            || !WPACKET_allocate_bytes(pkt, len, &encdata2)
            || encdata1 != encdata2
            || !EVP_EncryptFinal(ctx, encdata1 + len, &lenfinal)
            || !WPACKET_allocate_bytes(pkt, lenfinal, &encdata2)
            || encdata1 + len != encdata2
            || len + lenfinal > slen + EVP_MAX_BLOCK_LENGTH
            || !WPACKET_get_total_written(pkt, &macendoffset)
            || !HMAC_Update(hctx,
                            (unsigned char *)s->init_buf->data + macoffset,
                            macendoffset - macoffset)
            || !WPACKET_reserve_bytes(pkt, EVP_MAX_MD_SIZE, &macdata1)
            || !HMAC_Final(hctx, macdata1, &hlen)
            || hlen > EVP_MAX_MD_SIZE
            || !WPACKET_allocate_bytes(pkt, hlen, &macdata2)
            || macdata1 != macdata2
            || !WPACKET_close(pkt)
            || (SSL_IS_TLS13(s)
                && !tls_construct_extensions(s, pkt,
                                             EXT_TLS1_3_NEW_SESSION_TICKET,
                                             NULL, 0, &al))) {
        SSLerr(SSL_F_TLS_CONSTRUCT_NEW_SESSION_TICKET, ERR_R_INTERNAL_ERROR);
        goto err;
    }
    EVP_CIPHER_CTX_free(ctx);
    HMAC_CTX_free(hctx);
    OPENSSL_free(senc);

    return 1;
 err:
    OPENSSL_free(senc);
    EVP_CIPHER_CTX_free(ctx);
    HMAC_CTX_free(hctx);
    ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_INTERNAL_ERROR);
    return 0;
}

/*
 * In TLSv1.3 this is called from the extensions code, otherwise it is used to
 * create a separate message. Returns 1 on success or 0 on failure.
 */
int tls_construct_cert_status_body(SSL *s, WPACKET *pkt)
{
    if (!WPACKET_put_bytes_u8(pkt, s->ext.status_type)
            || !WPACKET_sub_memcpy_u24(pkt, s->ext.ocsp.resp,
                                       s->ext.ocsp.resp_len)) {
        SSLerr(SSL_F_TLS_CONSTRUCT_CERT_STATUS_BODY, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    return 1;
}

int tls_construct_cert_status(SSL *s, WPACKET *pkt)
{
    if (!tls_construct_cert_status_body(s, pkt)) {
        ssl3_send_alert(s, SSL3_AL_FATAL, SSL_AD_INTERNAL_ERROR);
        return 0;
    }

    return 1;
}

#ifndef OPENSSL_NO_NEXTPROTONEG
/*
 * tls_process_next_proto reads a Next Protocol Negotiation handshake message.
 * It sets the next_proto member in s if found
 */
MSG_PROCESS_RETURN tls_process_next_proto(SSL *s, PACKET *pkt)
{
    PACKET next_proto, padding;
    size_t next_proto_len;

    /*-
     * The payload looks like:
     *   uint8 proto_len;
     *   uint8 proto[proto_len];
     *   uint8 padding_len;
     *   uint8 padding[padding_len];
     */
    if (!PACKET_get_length_prefixed_1(pkt, &next_proto)
        || !PACKET_get_length_prefixed_1(pkt, &padding)
        || PACKET_remaining(pkt) > 0) {
        SSLerr(SSL_F_TLS_PROCESS_NEXT_PROTO, SSL_R_LENGTH_MISMATCH);
        goto err;
    }

    if (!PACKET_memdup(&next_proto, &s->ext.npn, &next_proto_len)) {
        s->ext.npn_len = 0;
        goto err;
    }

    s->ext.npn_len = (unsigned char)next_proto_len;

    return MSG_PROCESS_CONTINUE_READING;
 err:
    ossl_statem_set_error(s);
    return MSG_PROCESS_ERROR;
}
#endif

static int tls_construct_encrypted_extensions(SSL *s, WPACKET *pkt)
{
    int al;

    if (!tls_construct_extensions(s, pkt, EXT_TLS1_3_ENCRYPTED_EXTENSIONS,
                                  NULL, 0, &al)) {
        ssl3_send_alert(s, SSL3_AL_FATAL, al);
        SSLerr(SSL_F_TLS_CONSTRUCT_ENCRYPTED_EXTENSIONS, ERR_R_INTERNAL_ERROR);
        ssl3_send_alert(s, SSL3_AL_FATAL, al);
        return 0;
    }

    return 1;
}

#define SSLV2_CIPHER_LEN    3

STACK_OF(SSL_CIPHER) *ssl_bytes_to_cipher_list(SSL *s,
                                               PACKET *cipher_suites,
                                               STACK_OF(SSL_CIPHER) **skp,
                                               int sslv2format, int *al)
{
    const SSL_CIPHER *c;
    STACK_OF(SSL_CIPHER) *sk;
    int n;
    /* 3 = SSLV2_CIPHER_LEN > TLS_CIPHER_LEN = 2. */
    unsigned char cipher[SSLV2_CIPHER_LEN];

    s->s3->send_connection_binding = 0;

    n = sslv2format ? SSLV2_CIPHER_LEN : TLS_CIPHER_LEN;

    if (PACKET_remaining(cipher_suites) == 0) {
        SSLerr(SSL_F_SSL_BYTES_TO_CIPHER_LIST, SSL_R_NO_CIPHERS_SPECIFIED);
        *al = SSL_AD_ILLEGAL_PARAMETER;
        return NULL;
    }

    if (PACKET_remaining(cipher_suites) % n != 0) {
        SSLerr(SSL_F_SSL_BYTES_TO_CIPHER_LIST,
               SSL_R_ERROR_IN_RECEIVED_CIPHER_LIST);
        *al = SSL_AD_DECODE_ERROR;
        return NULL;
    }

    sk = sk_SSL_CIPHER_new_null();
    if (sk == NULL) {
        SSLerr(SSL_F_SSL_BYTES_TO_CIPHER_LIST, ERR_R_MALLOC_FAILURE);
        *al = SSL_AD_INTERNAL_ERROR;
        return NULL;
    }

    if (sslv2format) {
        size_t numciphers = PACKET_remaining(cipher_suites) / n;
        PACKET sslv2ciphers = *cipher_suites;
        unsigned int leadbyte;
        unsigned char *raw;

        /*
         * We store the raw ciphers list in SSLv3+ format so we need to do some
         * preprocessing to convert the list first. If there are any SSLv2 only
         * ciphersuites with a non-zero leading byte then we are going to
         * slightly over allocate because we won't store those. But that isn't a
         * problem.
         */
        raw = OPENSSL_malloc(numciphers * TLS_CIPHER_LEN);
        s->s3->tmp.ciphers_raw = raw;
        if (raw == NULL) {
            *al = SSL_AD_INTERNAL_ERROR;
            goto err;
        }
        for (s->s3->tmp.ciphers_rawlen = 0;
             PACKET_remaining(&sslv2ciphers) > 0;
             raw += TLS_CIPHER_LEN) {
            if (!PACKET_get_1(&sslv2ciphers, &leadbyte)
                    || (leadbyte == 0
                        && !PACKET_copy_bytes(&sslv2ciphers, raw,
                                              TLS_CIPHER_LEN))
                    || (leadbyte != 0
                        && !PACKET_forward(&sslv2ciphers, TLS_CIPHER_LEN))) {
                *al = SSL_AD_INTERNAL_ERROR;
                OPENSSL_free(s->s3->tmp.ciphers_raw);
                s->s3->tmp.ciphers_raw = NULL;
                s->s3->tmp.ciphers_rawlen = 0;
                goto err;
            }
            if (leadbyte == 0)
                s->s3->tmp.ciphers_rawlen += TLS_CIPHER_LEN;
        }
    } else if (!PACKET_memdup(cipher_suites, &s->s3->tmp.ciphers_raw,
                           &s->s3->tmp.ciphers_rawlen)) {
        *al = SSL_AD_INTERNAL_ERROR;
        goto err;
    }

    while (PACKET_copy_bytes(cipher_suites, cipher, n)) {
        /*
         * SSLv3 ciphers wrapped in an SSLv2-compatible ClientHello have the
         * first byte set to zero, while true SSLv2 ciphers have a non-zero
         * first byte. We don't support any true SSLv2 ciphers, so skip them.
         */
        if (sslv2format && cipher[0] != '\0')
            continue;

        /* Check for TLS_EMPTY_RENEGOTIATION_INFO_SCSV */
        if ((cipher[n - 2] == ((SSL3_CK_SCSV >> 8) & 0xff)) &&
            (cipher[n - 1] == (SSL3_CK_SCSV & 0xff))) {
            /* SCSV fatal if renegotiating */
            if (s->renegotiate) {
                SSLerr(SSL_F_SSL_BYTES_TO_CIPHER_LIST,
                       SSL_R_SCSV_RECEIVED_WHEN_RENEGOTIATING);
                *al = SSL_AD_HANDSHAKE_FAILURE;
                goto err;
            }
            s->s3->send_connection_binding = 1;
            continue;
        }

        /* Check for TLS_FALLBACK_SCSV */
        if ((cipher[n - 2] == ((SSL3_CK_FALLBACK_SCSV >> 8) & 0xff)) &&
            (cipher[n - 1] == (SSL3_CK_FALLBACK_SCSV & 0xff))) {
            /*
             * The SCSV indicates that the client previously tried a higher
             * version. Fail if the current version is an unexpected
             * downgrade.
             */
            if (!ssl_check_version_downgrade(s)) {
                SSLerr(SSL_F_SSL_BYTES_TO_CIPHER_LIST,
                       SSL_R_INAPPROPRIATE_FALLBACK);
                *al = SSL_AD_INAPPROPRIATE_FALLBACK;
                goto err;
            }
            continue;
        }

        /* For SSLv2-compat, ignore leading 0-byte. */
        c = ssl_get_cipher_by_char(s, sslv2format ? &cipher[1] : cipher);
        if (c != NULL) {
            if (!sk_SSL_CIPHER_push(sk, c)) {
                SSLerr(SSL_F_SSL_BYTES_TO_CIPHER_LIST, ERR_R_MALLOC_FAILURE);
                *al = SSL_AD_INTERNAL_ERROR;
                goto err;
            }
        }
    }
    if (PACKET_remaining(cipher_suites) > 0) {
        *al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_SSL_BYTES_TO_CIPHER_LIST, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    *skp = sk;
    return sk;
 err:
    sk_SSL_CIPHER_free(sk);
    return NULL;
}
