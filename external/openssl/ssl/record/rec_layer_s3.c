/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <errno.h>
#define USE_SOCKETS
#include "../ssl_locl.h"
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <openssl/rand.h>
#include "record_locl.h"

#if     defined(OPENSSL_SMALL_FOOTPRINT) || \
        !(      defined(AES_ASM) &&     ( \
                defined(__x86_64)       || defined(__x86_64__)  || \
                defined(_M_AMD64)       || defined(_M_X64)      ) \
        )
# undef EVP_CIPH_FLAG_TLS1_1_MULTIBLOCK
# define EVP_CIPH_FLAG_TLS1_1_MULTIBLOCK 0
#endif

void RECORD_LAYER_init(RECORD_LAYER *rl, SSL *s)
{
    rl->s = s;
    RECORD_LAYER_set_first_record(&s->rlayer);
    SSL3_RECORD_clear(rl->rrec, SSL_MAX_PIPELINES);
}

void RECORD_LAYER_clear(RECORD_LAYER *rl)
{
    rl->rstate = SSL_ST_READ_HEADER;

    /*
     * Do I need to clear read_ahead? As far as I can tell read_ahead did not
     * previously get reset by SSL_clear...so I'll keep it that way..but is
     * that right?
     */

    rl->packet = NULL;
    rl->packet_length = 0;
    rl->wnum = 0;
    memset(rl->alert_fragment, 0, sizeof(rl->alert_fragment));
    rl->alert_fragment_len = 0;
    memset(rl->handshake_fragment, 0, sizeof(rl->handshake_fragment));
    rl->handshake_fragment_len = 0;
    rl->wpend_tot = 0;
    rl->wpend_type = 0;
    rl->wpend_ret = 0;
    rl->wpend_buf = NULL;

    SSL3_BUFFER_clear(&rl->rbuf);
    ssl3_release_write_buffer(rl->s);
    rl->numrpipes = 0;
    SSL3_RECORD_clear(rl->rrec, SSL_MAX_PIPELINES);

    RECORD_LAYER_reset_read_sequence(rl);
    RECORD_LAYER_reset_write_sequence(rl);

    if (rl->d)
        DTLS_RECORD_LAYER_clear(rl);
}

void RECORD_LAYER_release(RECORD_LAYER *rl)
{
    if (SSL3_BUFFER_is_initialised(&rl->rbuf))
        ssl3_release_read_buffer(rl->s);
    if (rl->numwpipes > 0)
        ssl3_release_write_buffer(rl->s);
    SSL3_RECORD_release(rl->rrec, SSL_MAX_PIPELINES);
}

int RECORD_LAYER_read_pending(const RECORD_LAYER *rl)
{
    return SSL3_BUFFER_get_left(&rl->rbuf) != 0;
}

int RECORD_LAYER_write_pending(const RECORD_LAYER *rl)
{
    return (rl->numwpipes > 0)
        && SSL3_BUFFER_get_left(&rl->wbuf[rl->numwpipes - 1]) != 0;
}

int RECORD_LAYER_set_data(RECORD_LAYER *rl, const unsigned char *buf,
                          size_t len)
{
    rl->packet_length = len;
    if (len != 0) {
        rl->rstate = SSL_ST_READ_HEADER;
        if (!SSL3_BUFFER_is_initialised(&rl->rbuf))
            if (!ssl3_setup_read_buffer(rl->s))
                return 0;
    }

    rl->packet = SSL3_BUFFER_get_buf(&rl->rbuf);
    SSL3_BUFFER_set_data(&rl->rbuf, buf, len);

    return 1;
}

void RECORD_LAYER_reset_read_sequence(RECORD_LAYER *rl)
{
    memset(rl->read_sequence, 0, sizeof(rl->read_sequence));
}

void RECORD_LAYER_reset_write_sequence(RECORD_LAYER *rl)
{
    memset(rl->write_sequence, 0, sizeof(rl->write_sequence));
}

size_t ssl3_pending(const SSL *s)
{
    size_t i, num = 0;

    if (s->rlayer.rstate == SSL_ST_READ_BODY)
        return 0;

    for (i = 0; i < RECORD_LAYER_get_numrpipes(&s->rlayer); i++) {
        if (SSL3_RECORD_get_type(&s->rlayer.rrec[i])
            != SSL3_RT_APPLICATION_DATA)
            return 0;
        num += SSL3_RECORD_get_length(&s->rlayer.rrec[i]);
    }

    return num;
}

void SSL_CTX_set_default_read_buffer_len(SSL_CTX *ctx, size_t len)
{
    ctx->default_read_buf_len = len;
}

void SSL_set_default_read_buffer_len(SSL *s, size_t len)
{
    SSL3_BUFFER_set_default_len(RECORD_LAYER_get_rbuf(&s->rlayer), len);
}

const char *SSL_rstate_string_long(const SSL *s)
{
    switch (s->rlayer.rstate) {
    case SSL_ST_READ_HEADER:
        return "read header";
    case SSL_ST_READ_BODY:
        return "read body";
    case SSL_ST_READ_DONE:
        return "read done";
    default:
        return "unknown";
    }
}

const char *SSL_rstate_string(const SSL *s)
{
    switch (s->rlayer.rstate) {
    case SSL_ST_READ_HEADER:
        return "RH";
    case SSL_ST_READ_BODY:
        return "RB";
    case SSL_ST_READ_DONE:
        return "RD";
    default:
        return "unknown";
    }
}

/*
 * Return values are as per SSL_read()
 */
int ssl3_read_n(SSL *s, size_t n, size_t max, int extend, int clearold,
                size_t *readbytes)
{
    /*
     * If extend == 0, obtain new n-byte packet; if extend == 1, increase
     * packet by another n bytes. The packet will be in the sub-array of
     * s->s3->rbuf.buf specified by s->packet and s->packet_length. (If
     * s->rlayer.read_ahead is set, 'max' bytes may be stored in rbuf [plus
     * s->packet_length bytes if extend == 1].)
     * if clearold == 1, move the packet to the start of the buffer; if
     * clearold == 0 then leave any old packets where they were
     */
    size_t len, left, align = 0;
    unsigned char *pkt;
    SSL3_BUFFER *rb;

    if (n == 0)
        return 0;

    rb = &s->rlayer.rbuf;
    if (rb->buf == NULL)
        if (!ssl3_setup_read_buffer(s))
            return -1;

    left = rb->left;
#if defined(SSL3_ALIGN_PAYLOAD) && SSL3_ALIGN_PAYLOAD!=0
    align = (size_t)rb->buf + SSL3_RT_HEADER_LENGTH;
    align = SSL3_ALIGN_PAYLOAD - 1 - ((align - 1) % SSL3_ALIGN_PAYLOAD);
#endif

    if (!extend) {
        /* start with empty packet ... */
        if (left == 0)
            rb->offset = align;
        else if (align != 0 && left >= SSL3_RT_HEADER_LENGTH) {
            /*
             * check if next packet length is large enough to justify payload
             * alignment...
             */
            pkt = rb->buf + rb->offset;
            if (pkt[0] == SSL3_RT_APPLICATION_DATA
                && (pkt[3] << 8 | pkt[4]) >= 128) {
                /*
                 * Note that even if packet is corrupted and its length field
                 * is insane, we can only be led to wrong decision about
                 * whether memmove will occur or not. Header values has no
                 * effect on memmove arguments and therefore no buffer
                 * overrun can be triggered.
                 */
                memmove(rb->buf + align, pkt, left);
                rb->offset = align;
            }
        }
        s->rlayer.packet = rb->buf + rb->offset;
        s->rlayer.packet_length = 0;
        /* ... now we can act as if 'extend' was set */
    }

    len = s->rlayer.packet_length;
    pkt = rb->buf + align;
    /*
     * Move any available bytes to front of buffer: 'len' bytes already
     * pointed to by 'packet', 'left' extra ones at the end
     */
    if (s->rlayer.packet != pkt && clearold == 1) {
        memmove(pkt, s->rlayer.packet, len + left);
        s->rlayer.packet = pkt;
        rb->offset = len + align;
    }

    /*
     * For DTLS/UDP reads should not span multiple packets because the read
     * operation returns the whole packet at once (as long as it fits into
     * the buffer).
     */
    if (SSL_IS_DTLS(s)) {
        if (left == 0 && extend)
            return 0;
        if (left > 0 && n > left)
            n = left;
    }

    /* if there is enough in the buffer from a previous read, take some */
    if (left >= n) {
        s->rlayer.packet_length += n;
        rb->left = left - n;
        rb->offset += n;
        *readbytes = n;
        return 1;
    }

    /* else we need to read more data */

    if (n > rb->len - rb->offset) { /* does not happen */
        SSLerr(SSL_F_SSL3_READ_N, ERR_R_INTERNAL_ERROR);
        return -1;
    }

    /* We always act like read_ahead is set for DTLS */
    if (!s->rlayer.read_ahead && !SSL_IS_DTLS(s))
        /* ignore max parameter */
        max = n;
    else {
        if (max < n)
            max = n;
        if (max > rb->len - rb->offset)
            max = rb->len - rb->offset;
    }

    while (left < n) {
        size_t bioread = 0;
        int ret;

        /*
         * Now we have len+left bytes at the front of s->s3->rbuf.buf and
         * need to read in more until we have len+n (up to len+max if
         * possible)
         */

        clear_sys_error();
        if (s->rbio != NULL) {
            s->rwstate = SSL_READING;
            /* TODO(size_t): Convert this function */
            ret = BIO_read(s->rbio, pkt + len + left, max - left);
            if (ret >= 0)
                bioread = ret;
        } else {
            SSLerr(SSL_F_SSL3_READ_N, SSL_R_READ_BIO_NOT_SET);
            ret = -1;
        }

        if (ret <= 0) {
            rb->left = left;
            if (s->mode & SSL_MODE_RELEASE_BUFFERS && !SSL_IS_DTLS(s))
                if (len + left == 0)
                    ssl3_release_read_buffer(s);
            return ret;
        }
        left += bioread;
        /*
         * reads should *never* span multiple packets for DTLS because the
         * underlying transport protocol is message oriented as opposed to
         * byte oriented as in the TLS case.
         */
        if (SSL_IS_DTLS(s)) {
            if (n > left)
                n = left;       /* makes the while condition false */
        }
    }

    /* done reading, now the book-keeping */
    rb->offset += n;
    rb->left = left - n;
    s->rlayer.packet_length += n;
    s->rwstate = SSL_NOTHING;
    *readbytes = n;
    return 1;
}

/*
 * Call this to write data in records of type 'type' It will return <= 0 if
 * not all data has been sent or non-blocking IO.
 */
int ssl3_write_bytes(SSL *s, int type, const void *buf_, size_t len,
                     size_t *written)
{
    const unsigned char *buf = buf_;
    size_t tot;
    size_t n, split_send_fragment, maxpipes;
#if !defined(OPENSSL_NO_MULTIBLOCK) && EVP_CIPH_FLAG_TLS1_1_MULTIBLOCK
    size_t max_send_fragment, nw;
#endif
    SSL3_BUFFER *wb = &s->rlayer.wbuf[0];
    int i;
    size_t tmpwrit;

    s->rwstate = SSL_NOTHING;
    tot = s->rlayer.wnum;
    /*
     * ensure that if we end up with a smaller value of data to write out
     * than the the original len from a write which didn't complete for
     * non-blocking I/O and also somehow ended up avoiding the check for
     * this in ssl3_write_pending/SSL_R_BAD_WRITE_RETRY as it must never be
     * possible to end up with (len-tot) as a large number that will then
     * promptly send beyond the end of the users buffer ... so we trap and
     * report the error in a way the user will notice
     */
    if (len < s->rlayer.wnum) {
        SSLerr(SSL_F_SSL3_WRITE_BYTES, SSL_R_BAD_LENGTH);
        return -1;
    }

    s->rlayer.wnum = 0;

    if (SSL_in_init(s) && !ossl_statem_get_in_handshake(s)) {
        i = s->handshake_func(s);
        if (i < 0)
            return i;
        if (i == 0) {
            SSLerr(SSL_F_SSL3_WRITE_BYTES, SSL_R_SSL_HANDSHAKE_FAILURE);
            return -1;
        }
    }

    /*
     * first check if there is a SSL3_BUFFER still being written out.  This
     * will happen with non blocking IO
     */
    if (wb->left != 0) {
        i = ssl3_write_pending(s, type, &buf[tot], s->rlayer.wpend_tot,
                               &tmpwrit);
        if (i <= 0) {
            /* XXX should we ssl3_release_write_buffer if i<0? */
            s->rlayer.wnum = tot;
            return i;
        }
        tot += tmpwrit;               /* this might be last fragment */
    }
#if !defined(OPENSSL_NO_MULTIBLOCK) && EVP_CIPH_FLAG_TLS1_1_MULTIBLOCK
    /*
     * Depending on platform multi-block can deliver several *times*
     * better performance. Downside is that it has to allocate
     * jumbo buffer to accommodate up to 8 records, but the
     * compromise is considered worthy.
     */
    if (type == SSL3_RT_APPLICATION_DATA &&
        len >= 4 * (max_send_fragment = s->max_send_fragment) &&
        s->compress == NULL && s->msg_callback == NULL &&
        !SSL_USE_ETM(s) && SSL_USE_EXPLICIT_IV(s) &&
        EVP_CIPHER_flags(EVP_CIPHER_CTX_cipher(s->enc_write_ctx)) &
        EVP_CIPH_FLAG_TLS1_1_MULTIBLOCK) {
        unsigned char aad[13];
        EVP_CTRL_TLS1_1_MULTIBLOCK_PARAM mb_param;
        size_t packlen;
        int packleni;

        /* minimize address aliasing conflicts */
        if ((max_send_fragment & 0xfff) == 0)
            max_send_fragment -= 512;

        if (tot == 0 || wb->buf == NULL) { /* allocate jumbo buffer */
            ssl3_release_write_buffer(s);

            packlen = EVP_CIPHER_CTX_ctrl(s->enc_write_ctx,
                                          EVP_CTRL_TLS1_1_MULTIBLOCK_MAX_BUFSIZE,
                                          (int)max_send_fragment, NULL);

            if (len >= 8 * max_send_fragment)
                packlen *= 8;
            else
                packlen *= 4;

            if (!ssl3_setup_write_buffer(s, 1, packlen)) {
                SSLerr(SSL_F_SSL3_WRITE_BYTES, ERR_R_MALLOC_FAILURE);
                return -1;
            }
        } else if (tot == len) { /* done? */
            /* free jumbo buffer */
            ssl3_release_write_buffer(s);
            *written = tot;
            return 1;
        }

        n = (len - tot);
        for (;;) {
            if (n < 4 * max_send_fragment) {
                /* free jumbo buffer */
                ssl3_release_write_buffer(s);
                break;
            }

            if (s->s3->alert_dispatch) {
                i = s->method->ssl_dispatch_alert(s);
                if (i <= 0) {
                    s->rlayer.wnum = tot;
                    return i;
                }
            }

            if (n >= 8 * max_send_fragment)
                nw = max_send_fragment * (mb_param.interleave = 8);
            else
                nw = max_send_fragment * (mb_param.interleave = 4);

            memcpy(aad, s->rlayer.write_sequence, 8);
            aad[8] = type;
            aad[9] = (unsigned char)(s->version >> 8);
            aad[10] = (unsigned char)(s->version);
            aad[11] = 0;
            aad[12] = 0;
            mb_param.out = NULL;
            mb_param.inp = aad;
            mb_param.len = nw;

            packleni = EVP_CIPHER_CTX_ctrl(s->enc_write_ctx,
                                          EVP_CTRL_TLS1_1_MULTIBLOCK_AAD,
                                          sizeof(mb_param), &mb_param);
            packlen = (size_t)packleni;
            if (packleni <= 0 || packlen > wb->len) { /* never happens */
                /* free jumbo buffer */
                ssl3_release_write_buffer(s);
                break;
            }

            mb_param.out = wb->buf;
            mb_param.inp = &buf[tot];
            mb_param.len = nw;

            if (EVP_CIPHER_CTX_ctrl(s->enc_write_ctx,
                                    EVP_CTRL_TLS1_1_MULTIBLOCK_ENCRYPT,
                                    sizeof(mb_param), &mb_param) <= 0)
                return -1;

            s->rlayer.write_sequence[7] += mb_param.interleave;
            if (s->rlayer.write_sequence[7] < mb_param.interleave) {
                int j = 6;
                while (j >= 0 && (++s->rlayer.write_sequence[j--]) == 0) ;
            }

            wb->offset = 0;
            wb->left = packlen;

            s->rlayer.wpend_tot = nw;
            s->rlayer.wpend_buf = &buf[tot];
            s->rlayer.wpend_type = type;
            s->rlayer.wpend_ret = nw;

            i = ssl3_write_pending(s, type, &buf[tot], nw, &tmpwrit);
            if (i <= 0) {
                if (i < 0 && (!s->wbio || !BIO_should_retry(s->wbio))) {
                    /* free jumbo buffer */
                    ssl3_release_write_buffer(s);
                }
                s->rlayer.wnum = tot;
                return i;
            }
            if (tmpwrit == n) {
                /* free jumbo buffer */
                ssl3_release_write_buffer(s);
                *written = tot + tmpwrit;
                return 1;
            }
            n -= tmpwrit;
            tot += tmpwrit;
        }
    } else
#endif
    if (tot == len) {           /* done? */
        if (s->mode & SSL_MODE_RELEASE_BUFFERS && !SSL_IS_DTLS(s))
            ssl3_release_write_buffer(s);

        *written = tot;
        return 1;
    }

    n = (len - tot);

    split_send_fragment = s->split_send_fragment;
    /*
     * If max_pipelines is 0 then this means "undefined" and we default to
     * 1 pipeline. Similarly if the cipher does not support pipelined
     * processing then we also only use 1 pipeline, or if we're not using
     * explicit IVs
     */
    maxpipes = s->max_pipelines;
    if (maxpipes > SSL_MAX_PIPELINES) {
        /*
         * We should have prevented this when we set max_pipelines so we
         * shouldn't get here
         */
        SSLerr(SSL_F_SSL3_WRITE_BYTES, ERR_R_INTERNAL_ERROR);
        return -1;
    }
    if (maxpipes == 0
        || s->enc_write_ctx == NULL
        || !(EVP_CIPHER_flags(EVP_CIPHER_CTX_cipher(s->enc_write_ctx))
             & EVP_CIPH_FLAG_PIPELINE)
        || !SSL_USE_EXPLICIT_IV(s))
        maxpipes = 1;
    if (s->max_send_fragment == 0 || split_send_fragment > s->max_send_fragment
        || split_send_fragment == 0) {
        /*
         * We should have prevented this when we set the split and max send
         * fragments so we shouldn't get here
         */
        SSLerr(SSL_F_SSL3_WRITE_BYTES, ERR_R_INTERNAL_ERROR);
        return -1;
    }

    for (;;) {
        size_t pipelens[SSL_MAX_PIPELINES], tmppipelen, remain;
        size_t numpipes, j;

        if (n == 0)
            numpipes = 1;
        else
            numpipes = ((n - 1) / split_send_fragment) + 1;
        if (numpipes > maxpipes)
            numpipes = maxpipes;

        if (n / numpipes >= s->max_send_fragment) {
            /*
             * We have enough data to completely fill all available
             * pipelines
             */
            for (j = 0; j < numpipes; j++) {
                pipelens[j] = s->max_send_fragment;
            }
        } else {
            /* We can partially fill all available pipelines */
            tmppipelen = n / numpipes;
            remain = n % numpipes;
            for (j = 0; j < numpipes; j++) {
                pipelens[j] = tmppipelen;
                if (j < remain)
                    pipelens[j]++;
            }
        }

        i = do_ssl3_write(s, type, &(buf[tot]), pipelens, numpipes, 0,
                          &tmpwrit);
        if (i <= 0) {
            /* XXX should we ssl3_release_write_buffer if i<0? */
            s->rlayer.wnum = tot;
            return i;
        }

        if (tmpwrit == n ||
            (type == SSL3_RT_APPLICATION_DATA &&
             (s->mode & SSL_MODE_ENABLE_PARTIAL_WRITE))) {
            /*
             * next chunk of data should get another prepended empty fragment
             * in ciphersuites with known-IV weakness:
             */
            s->s3->empty_fragment_done = 0;

            if ((i == (int)n) && s->mode & SSL_MODE_RELEASE_BUFFERS &&
                !SSL_IS_DTLS(s))
                ssl3_release_write_buffer(s);

            *written = tot + tmpwrit;
            return 1;
        }

        n -= tmpwrit;
        tot += tmpwrit;
    }
}

int do_ssl3_write(SSL *s, int type, const unsigned char *buf,
                  size_t *pipelens, size_t numpipes,
                  int create_empty_fragment, size_t *written)
{
    WPACKET pkt[SSL_MAX_PIPELINES];
    SSL3_RECORD wr[SSL_MAX_PIPELINES];
    WPACKET *thispkt;
    SSL3_RECORD *thiswr;
    unsigned char *recordstart;
    int i, mac_size, clear = 0;
    size_t prefix_len = 0;
    int eivlen = 0;
    size_t align = 0;
    SSL3_BUFFER *wb;
    SSL_SESSION *sess;
    size_t totlen = 0, len, wpinited = 0;
    size_t j;

    for (j = 0; j < numpipes; j++)
        totlen += pipelens[j];
    /*
     * first check if there is a SSL3_BUFFER still being written out.  This
     * will happen with non blocking IO
     */
    if (RECORD_LAYER_write_pending(&s->rlayer))
        return ssl3_write_pending(s, type, buf, totlen, written);

    /* If we have an alert to send, lets send it */
    if (s->s3->alert_dispatch) {
        i = s->method->ssl_dispatch_alert(s);
        if (i <= 0)
            return (i);
        /* if it went, fall through and send more stuff */
    }

    if (s->rlayer.numwpipes < numpipes)
        if (!ssl3_setup_write_buffer(s, numpipes, 0))
            return -1;

    if (totlen == 0 && !create_empty_fragment)
        return 0;

    sess = s->session;

    if ((sess == NULL) ||
        (s->enc_write_ctx == NULL) || (EVP_MD_CTX_md(s->write_hash) == NULL)) {
        clear = s->enc_write_ctx ? 0 : 1; /* must be AEAD cipher */
        mac_size = 0;
    } else {
        /* TODO(siz_t): Convert me */
        mac_size = EVP_MD_CTX_size(s->write_hash);
        if (mac_size < 0)
            goto err;
    }

    /*
     * 'create_empty_fragment' is true only when this function calls itself
     */
    if (!clear && !create_empty_fragment && !s->s3->empty_fragment_done) {
        /*
         * countermeasure against known-IV weakness in CBC ciphersuites (see
         * http://www.openssl.org/~bodo/tls-cbc.txt)
         */

        if (s->s3->need_empty_fragments && type == SSL3_RT_APPLICATION_DATA) {
            /*
             * recursive function call with 'create_empty_fragment' set; this
             * prepares and buffers the data for an empty fragment (these
             * 'prefix_len' bytes are sent out later together with the actual
             * payload)
             */
            size_t tmppipelen = 0;
            int ret;

            ret = do_ssl3_write(s, type, buf, &tmppipelen, 1, 1, &prefix_len);
            if (ret <= 0)
                goto err;

            if (prefix_len >
                (SSL3_RT_HEADER_LENGTH + SSL3_RT_SEND_MAX_ENCRYPTED_OVERHEAD)) {
                /* insufficient space */
                SSLerr(SSL_F_DO_SSL3_WRITE, ERR_R_INTERNAL_ERROR);
                goto err;
            }
        }

        s->s3->empty_fragment_done = 1;
    }

    if (create_empty_fragment) {
        wb = &s->rlayer.wbuf[0];
#if defined(SSL3_ALIGN_PAYLOAD) && SSL3_ALIGN_PAYLOAD!=0
        /*
         * extra fragment would be couple of cipher blocks, which would be
         * multiple of SSL3_ALIGN_PAYLOAD, so if we want to align the real
         * payload, then we can just pretend we simply have two headers.
         */
        align = (size_t)SSL3_BUFFER_get_buf(wb) + 2 * SSL3_RT_HEADER_LENGTH;
        align = SSL3_ALIGN_PAYLOAD - 1 - ((align - 1) % SSL3_ALIGN_PAYLOAD);
#endif
        SSL3_BUFFER_set_offset(wb, align);
        if (!WPACKET_init_static_len(&pkt[0], SSL3_BUFFER_get_buf(wb),
                                     SSL3_BUFFER_get_len(wb), 0)
                || !WPACKET_allocate_bytes(&pkt[0], align, NULL)) {
            SSLerr(SSL_F_DO_SSL3_WRITE, ERR_R_INTERNAL_ERROR);
            goto err;
        }
        wpinited = 1;
    } else if (prefix_len) {
        wb = &s->rlayer.wbuf[0];
        if (!WPACKET_init_static_len(&pkt[0],
                                     SSL3_BUFFER_get_buf(wb),
                                     SSL3_BUFFER_get_len(wb), 0)
                || !WPACKET_allocate_bytes(&pkt[0], SSL3_BUFFER_get_offset(wb)
                                                    + prefix_len, NULL)) {
            SSLerr(SSL_F_DO_SSL3_WRITE, ERR_R_INTERNAL_ERROR);
            goto err;
        }
        wpinited = 1;
    } else {
        for (j = 0; j < numpipes; j++) {
            thispkt = &pkt[j];

            wb = &s->rlayer.wbuf[j];
#if defined(SSL3_ALIGN_PAYLOAD) && SSL3_ALIGN_PAYLOAD != 0
            align = (size_t)SSL3_BUFFER_get_buf(wb) + SSL3_RT_HEADER_LENGTH;
            align = SSL3_ALIGN_PAYLOAD - 1 - ((align - 1) % SSL3_ALIGN_PAYLOAD);
#endif
            SSL3_BUFFER_set_offset(wb, align);
            if (!WPACKET_init_static_len(thispkt, SSL3_BUFFER_get_buf(wb),
                                         SSL3_BUFFER_get_len(wb), 0)
                    || !WPACKET_allocate_bytes(thispkt, align, NULL)) {
                SSLerr(SSL_F_DO_SSL3_WRITE, ERR_R_INTERNAL_ERROR);
                goto err;
            }
            wpinited++;
        }
    }

    /* Explicit IV length, block ciphers appropriate version flag */
    if (s->enc_write_ctx && SSL_USE_EXPLICIT_IV(s)) {
        int mode = EVP_CIPHER_CTX_mode(s->enc_write_ctx);
        if (mode == EVP_CIPH_CBC_MODE) {
            /* TODO(size_t): Convert me */
            eivlen = EVP_CIPHER_CTX_iv_length(s->enc_write_ctx);
            if (eivlen <= 1)
                eivlen = 0;
        } else if (mode == EVP_CIPH_GCM_MODE) {
            /* Need explicit part of IV for GCM mode */
            eivlen = EVP_GCM_TLS_EXPLICIT_IV_LEN;
        } else if (mode == EVP_CIPH_CCM_MODE) {
            eivlen = EVP_CCM_TLS_EXPLICIT_IV_LEN;
        }
    }

    totlen = 0;
    /* Clear our SSL3_RECORD structures */
    memset(wr, 0, sizeof wr);
    for (j = 0; j < numpipes; j++) {
        unsigned int version = SSL_IS_TLS13(s) ? TLS1_VERSION : s->version;
        unsigned char *compressdata = NULL;
        size_t maxcomplen;
        unsigned int rectype;

        thispkt = &pkt[j];
        thiswr = &wr[j];

        SSL3_RECORD_set_type(thiswr, type);
        /*
         * In TLSv1.3, once encrypting, we always use application data for the
         * record type
         */
        if (SSL_IS_TLS13(s) && s->enc_write_ctx != NULL)
            rectype = SSL3_RT_APPLICATION_DATA;
        else
            rectype = type;
        /*
         * Some servers hang if initial client hello is larger than 256 bytes
         * and record version number > TLS 1.0
         */
        if (SSL_get_state(s) == TLS_ST_CW_CLNT_HELLO
            && !s->renegotiate && TLS1_get_version(s) > TLS1_VERSION)
            version = TLS1_VERSION;

        maxcomplen = pipelens[j];
        if (s->compress != NULL)
            pipelens[j] += SSL3_RT_MAX_COMPRESSED_OVERHEAD;

        /* write the header */
        if (!WPACKET_put_bytes_u8(thispkt, rectype)
                || !WPACKET_put_bytes_u16(thispkt, version)
                || !WPACKET_start_sub_packet_u16(thispkt)
                || (eivlen > 0
                    && !WPACKET_allocate_bytes(thispkt, eivlen, NULL))
                || (maxcomplen > 0
                    && !WPACKET_reserve_bytes(thispkt, maxcomplen,
                                              &compressdata))) {
            SSLerr(SSL_F_DO_SSL3_WRITE, ERR_R_INTERNAL_ERROR);
            goto err;
        }

        /* lets setup the record stuff. */
        SSL3_RECORD_set_data(thiswr, compressdata);
        SSL3_RECORD_set_length(thiswr, pipelens[j]);
        SSL3_RECORD_set_input(thiswr, (unsigned char *)&buf[totlen]);
        totlen += pipelens[j];

        /*
         * we now 'read' from thiswr->input, thiswr->length bytes into
         * thiswr->data
         */

        /* first we compress */
        if (s->compress != NULL) {
            /*
             * TODO(TLS1.3): Make sure we prevent compression!!!
             */
            if (!ssl3_do_compress(s, thiswr)
                    || !WPACKET_allocate_bytes(thispkt, thiswr->length, NULL)) {
                SSLerr(SSL_F_DO_SSL3_WRITE, SSL_R_COMPRESSION_FAILURE);
                goto err;
            }
        } else {
            if (!WPACKET_memcpy(thispkt, thiswr->input, thiswr->length)) {
                SSLerr(SSL_F_DO_SSL3_WRITE, ERR_R_INTERNAL_ERROR);
                goto err;
            }
            SSL3_RECORD_reset_input(&wr[j]);
        }

        if (SSL_IS_TLS13(s) && s->enc_write_ctx != NULL) {
            if (!WPACKET_put_bytes_u8(thispkt, type)) {
                SSLerr(SSL_F_DO_SSL3_WRITE, ERR_R_INTERNAL_ERROR);
                goto err;
            }
            SSL3_RECORD_add_length(thiswr, 1);
            /*
             * TODO(TLS1.3): Padding goes here. Do we need an API to add this?
             * For now, use no padding
             */
        }

        /*
         * we should still have the output to thiswr->data and the input from
         * wr->input. Length should be thiswr->length. thiswr->data still points
         * in the wb->buf
         */

        if (!SSL_USE_ETM(s) && mac_size != 0) {
            unsigned char *mac;

            if (!WPACKET_allocate_bytes(thispkt, mac_size, &mac)
                    || !s->method->ssl3_enc->mac(s, thiswr, mac, 1)) {
                SSLerr(SSL_F_DO_SSL3_WRITE, ERR_R_INTERNAL_ERROR);
                goto err;
            }
        }

        /*
         * Reserve some bytes for any growth that may occur during encryption.
         * This will be at most one cipher block or the tag length if using
         * AEAD. SSL_RT_MAX_CIPHER_BLOCK_SIZE covers either case.
         */
        if(!WPACKET_reserve_bytes(thispkt, SSL_RT_MAX_CIPHER_BLOCK_SIZE,
                                  NULL)
                   /*
                    * We also need next the amount of bytes written to this
                    * sub-packet
                    */
                || !WPACKET_get_length(thispkt, &len)) {
            SSLerr(SSL_F_DO_SSL3_WRITE, ERR_R_INTERNAL_ERROR);
            goto err;
        }

        /* Get a pointer to the start of this record excluding header */
        recordstart = WPACKET_get_curr(thispkt) - len;

        SSL3_RECORD_set_data(thiswr, recordstart);
        SSL3_RECORD_reset_input(thiswr);
        SSL3_RECORD_set_length(thiswr, len);
    }

    if (s->method->ssl3_enc->enc(s, wr, numpipes, 1) < 1)
        goto err;

    for (j = 0; j < numpipes; j++) {
        size_t origlen;

        thispkt = &pkt[j];
        thiswr = &wr[j];

        /* Allocate bytes for the encryption overhead */
        if (!WPACKET_get_length(thispkt, &origlen)
                   /* Encryption should never shrink the data! */
                || origlen > thiswr->length
                || (thiswr->length > origlen
                    && !WPACKET_allocate_bytes(thispkt,
                                               thiswr->length - origlen, NULL))) {
            SSLerr(SSL_F_DO_SSL3_WRITE, ERR_R_INTERNAL_ERROR);
            goto err;
        }
        if (SSL_USE_ETM(s) && mac_size != 0) {
            unsigned char *mac;

            if (!WPACKET_allocate_bytes(thispkt, mac_size, &mac)
                    || !s->method->ssl3_enc->mac(s, thiswr, mac, 1)) {
                SSLerr(SSL_F_DO_SSL3_WRITE, ERR_R_INTERNAL_ERROR);
                goto err;
            }
            SSL3_RECORD_add_length(thiswr, mac_size);
        }

        if (!WPACKET_get_length(thispkt, &len)
                || !WPACKET_close(thispkt)) {
            SSLerr(SSL_F_DO_SSL3_WRITE, ERR_R_INTERNAL_ERROR);
            goto err;
        }

        if (s->msg_callback) {
            recordstart = WPACKET_get_curr(thispkt) - len
                          - SSL3_RT_HEADER_LENGTH;
            s->msg_callback(1, 0, SSL3_RT_HEADER, recordstart,
                            SSL3_RT_HEADER_LENGTH, s,
                            s->msg_callback_arg);
        }

        if (!WPACKET_finish(thispkt)) {
            SSLerr(SSL_F_DO_SSL3_WRITE, ERR_R_INTERNAL_ERROR);
            goto err;
        }

        /*
         * we should now have thiswr->data pointing to the encrypted data, which
         * is thiswr->length long
         */
        SSL3_RECORD_set_type(thiswr, type); /* not needed but helps for
                                             * debugging */
        SSL3_RECORD_add_length(thiswr, SSL3_RT_HEADER_LENGTH);

        if (create_empty_fragment) {
            /*
             * we are in a recursive call; just return the length, don't write
             * out anything here
             */
            if (j > 0) {
                /* We should never be pipelining an empty fragment!! */
                SSLerr(SSL_F_DO_SSL3_WRITE, ERR_R_INTERNAL_ERROR);
                goto err;
            }
            *written = SSL3_RECORD_get_length(thiswr);
            return 1;
        }

        /* now let's set up wb */
        SSL3_BUFFER_set_left(&s->rlayer.wbuf[j],
                             prefix_len + SSL3_RECORD_get_length(thiswr));
    }

    /*
     * memorize arguments so that ssl3_write_pending can detect bad write
     * retries later
     */
    s->rlayer.wpend_tot = totlen;
    s->rlayer.wpend_buf = buf;
    s->rlayer.wpend_type = type;
    s->rlayer.wpend_ret = totlen;

    /* we now just need to write the buffer */
    return ssl3_write_pending(s, type, buf, totlen, written);
 err:
    for (j = 0; j < wpinited; j++)
        WPACKET_cleanup(&pkt[j]);
    return -1;
}

/* if s->s3->wbuf.left != 0, we need to call this
 *
 * Return values are as per SSL_write()
 */
int ssl3_write_pending(SSL *s, int type, const unsigned char *buf, size_t len,
                       size_t *written)
{
    int i;
    SSL3_BUFFER *wb = s->rlayer.wbuf;
    size_t currbuf = 0;
    size_t tmpwrit = 0;

    if ((s->rlayer.wpend_tot > len)
        || ((s->rlayer.wpend_buf != buf) &&
            !(s->mode & SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER))
        || (s->rlayer.wpend_type != type)) {
        SSLerr(SSL_F_SSL3_WRITE_PENDING, SSL_R_BAD_WRITE_RETRY);
        return -1;
    }

    for (;;) {
        /* Loop until we find a buffer we haven't written out yet */
        if (SSL3_BUFFER_get_left(&wb[currbuf]) == 0
            && currbuf < s->rlayer.numwpipes - 1) {
            currbuf++;
            continue;
        }
        clear_sys_error();
        if (s->wbio != NULL) {
            s->rwstate = SSL_WRITING;
            /* TODO(size_t): Convert this call */
            i = BIO_write(s->wbio, (char *)
                          &(SSL3_BUFFER_get_buf(&wb[currbuf])
                            [SSL3_BUFFER_get_offset(&wb[currbuf])]),
                          (unsigned int)SSL3_BUFFER_get_left(&wb[currbuf]));
            if (i >= 0)
                tmpwrit = i;
        } else {
            SSLerr(SSL_F_SSL3_WRITE_PENDING, SSL_R_BIO_NOT_SET);
            i = -1;
        }
        if (i > 0 && tmpwrit == SSL3_BUFFER_get_left(&wb[currbuf])) {
            SSL3_BUFFER_set_left(&wb[currbuf], 0);
            SSL3_BUFFER_add_offset(&wb[currbuf], tmpwrit);
            if (currbuf + 1 < s->rlayer.numwpipes)
                continue;
            s->rwstate = SSL_NOTHING;
            *written = s->rlayer.wpend_ret;
            return 1;
        } else if (i <= 0) {
            if (SSL_IS_DTLS(s)) {
                /*
                 * For DTLS, just drop it. That's kind of the whole point in
                 * using a datagram service
                 */
                SSL3_BUFFER_set_left(&wb[currbuf], 0);
            }
            return (i);
        }
        SSL3_BUFFER_add_offset(&wb[currbuf], tmpwrit);
        SSL3_BUFFER_sub_left(&wb[currbuf], tmpwrit);
    }
}

/*-
 * Return up to 'len' payload bytes received in 'type' records.
 * 'type' is one of the following:
 *
 *   -  SSL3_RT_HANDSHAKE (when ssl3_get_message calls us)
 *   -  SSL3_RT_APPLICATION_DATA (when ssl3_read calls us)
 *   -  0 (during a shutdown, no data has to be returned)
 *
 * If we don't have stored data to work from, read a SSL/TLS record first
 * (possibly multiple records if we still don't have anything to return).
 *
 * This function must handle any surprises the peer may have for us, such as
 * Alert records (e.g. close_notify) or renegotiation requests. ChangeCipherSpec
 * messages are treated as if they were handshake messages *if* the |recd_type|
 * argument is non NULL.
 * Also if record payloads contain fragments too small to process, we store
 * them until there is enough for the respective protocol (the record protocol
 * may use arbitrary fragmentation and even interleaving):
 *     Change cipher spec protocol
 *             just 1 byte needed, no need for keeping anything stored
 *     Alert protocol
 *             2 bytes needed (AlertLevel, AlertDescription)
 *     Handshake protocol
 *             4 bytes needed (HandshakeType, uint24 length) -- we just have
 *             to detect unexpected Client Hello and Hello Request messages
 *             here, anything else is handled by higher layers
 *     Application data protocol
 *             none of our business
 */
int ssl3_read_bytes(SSL *s, int type, int *recvd_type, unsigned char *buf,
                    size_t len, int peek, size_t *readbytes)
{
    int al, i, j, ret;
    size_t n, curr_rec, num_recs, totalbytes;
    SSL3_RECORD *rr;
    SSL3_BUFFER *rbuf;
    void (*cb) (const SSL *ssl, int type2, int val) = NULL;

    rbuf = &s->rlayer.rbuf;

    if (!SSL3_BUFFER_is_initialised(rbuf)) {
        /* Not initialized yet */
        if (!ssl3_setup_read_buffer(s))
            return -1;
    }

    if ((type && (type != SSL3_RT_APPLICATION_DATA)
         && (type != SSL3_RT_HANDSHAKE)) || (peek
                                             && (type !=
                                                 SSL3_RT_APPLICATION_DATA))) {
        SSLerr(SSL_F_SSL3_READ_BYTES, ERR_R_INTERNAL_ERROR);
        return -1;
    }

    if ((type == SSL3_RT_HANDSHAKE) && (s->rlayer.handshake_fragment_len > 0))
        /* (partially) satisfy request from storage */
    {
        unsigned char *src = s->rlayer.handshake_fragment;
        unsigned char *dst = buf;
        unsigned int k;

        /* peek == 0 */
        n = 0;
        while ((len > 0) && (s->rlayer.handshake_fragment_len > 0)) {
            *dst++ = *src++;
            len--;
            s->rlayer.handshake_fragment_len--;
            n++;
        }
        /* move any remaining fragment bytes: */
        for (k = 0; k < s->rlayer.handshake_fragment_len; k++)
            s->rlayer.handshake_fragment[k] = *src++;

        if (recvd_type != NULL)
            *recvd_type = SSL3_RT_HANDSHAKE;

        *readbytes = n;
        return 1;
    }

    /*
     * Now s->rlayer.handshake_fragment_len == 0 if type == SSL3_RT_HANDSHAKE.
     */

    if (!ossl_statem_get_in_handshake(s) && SSL_in_init(s)) {
        /* type == SSL3_RT_APPLICATION_DATA */
        i = s->handshake_func(s);
        if (i < 0)
            return i;
        if (i == 0) {
            SSLerr(SSL_F_SSL3_READ_BYTES, SSL_R_SSL_HANDSHAKE_FAILURE);
            return -1;
        }
    }
 start:
    s->rwstate = SSL_NOTHING;

    /*-
     * For each record 'i' up to |num_recs]
     * rr[i].type     - is the type of record
     * rr[i].data,    - data
     * rr[i].off,     - offset into 'data' for next read
     * rr[i].length,  - number of bytes.
     */
    rr = s->rlayer.rrec;
    num_recs = RECORD_LAYER_get_numrpipes(&s->rlayer);

    do {
        /* get new records if necessary */
        if (num_recs == 0) {
            ret = ssl3_get_record(s);
            if (ret <= 0)
                return ret;
            num_recs = RECORD_LAYER_get_numrpipes(&s->rlayer);
            if (num_recs == 0) {
                /* Shouldn't happen */
                al = SSL_AD_INTERNAL_ERROR;
                SSLerr(SSL_F_SSL3_READ_BYTES, ERR_R_INTERNAL_ERROR);
                goto f_err;
            }
        }
        /* Skip over any records we have already read */
        for (curr_rec = 0;
             curr_rec < num_recs && SSL3_RECORD_is_read(&rr[curr_rec]);
             curr_rec++) ;
        if (curr_rec == num_recs) {
            RECORD_LAYER_set_numrpipes(&s->rlayer, 0);
            num_recs = 0;
            curr_rec = 0;
        }
    } while (num_recs == 0);
    rr = &rr[curr_rec];

    /*
     * Reset the count of consecutive warning alerts if we've got a non-empty
     * record that isn't an alert.
     */
    if (SSL3_RECORD_get_type(rr) != SSL3_RT_ALERT
            && SSL3_RECORD_get_length(rr) != 0)
        s->rlayer.alert_count = 0;

    /* we now have a packet which can be read and processed */

    if (s->s3->change_cipher_spec /* set when we receive ChangeCipherSpec,
                                   * reset by ssl3_get_finished */
        && (SSL3_RECORD_get_type(rr) != SSL3_RT_HANDSHAKE)) {
        al = SSL_AD_UNEXPECTED_MESSAGE;
        SSLerr(SSL_F_SSL3_READ_BYTES, SSL_R_DATA_BETWEEN_CCS_AND_FINISHED);
        goto f_err;
    }

    /*
     * If the other end has shut down, throw anything we read away (even in
     * 'peek' mode)
     */
    if (s->shutdown & SSL_RECEIVED_SHUTDOWN) {
        SSL3_RECORD_set_length(rr, 0);
        s->rwstate = SSL_NOTHING;
        return 0;
    }

    if (type == SSL3_RECORD_get_type(rr)
        || (SSL3_RECORD_get_type(rr) == SSL3_RT_CHANGE_CIPHER_SPEC
            && type == SSL3_RT_HANDSHAKE && recvd_type != NULL
            && !SSL_IS_TLS13(s))) {
        /*
         * SSL3_RT_APPLICATION_DATA or
         * SSL3_RT_HANDSHAKE or
         * SSL3_RT_CHANGE_CIPHER_SPEC
         */
        /*
         * make sure that we are not getting application data when we are
         * doing a handshake for the first time
         */
        if (SSL_in_init(s) && (type == SSL3_RT_APPLICATION_DATA) &&
            (s->enc_read_ctx == NULL)) {
            al = SSL_AD_UNEXPECTED_MESSAGE;
            SSLerr(SSL_F_SSL3_READ_BYTES, SSL_R_APP_DATA_IN_HANDSHAKE);
            goto f_err;
        }

        if (type == SSL3_RT_HANDSHAKE
            && SSL3_RECORD_get_type(rr) == SSL3_RT_CHANGE_CIPHER_SPEC
            && s->rlayer.handshake_fragment_len > 0) {
            al = SSL_AD_UNEXPECTED_MESSAGE;
            SSLerr(SSL_F_SSL3_READ_BYTES, SSL_R_CCS_RECEIVED_EARLY);
            goto f_err;
        }

        if (recvd_type != NULL)
            *recvd_type = SSL3_RECORD_get_type(rr);

        if (len == 0)
            return 0;

        totalbytes = 0;
        do {
            if (len - totalbytes > SSL3_RECORD_get_length(rr))
                n = SSL3_RECORD_get_length(rr);
            else
                n = len - totalbytes;

            memcpy(buf, &(rr->data[rr->off]), n);
            buf += n;
            if (peek) {
                /* Mark any zero length record as consumed CVE-2016-6305 */
                if (SSL3_RECORD_get_length(rr) == 0)
                    SSL3_RECORD_set_read(rr);
            } else {
                SSL3_RECORD_sub_length(rr, n);
                SSL3_RECORD_add_off(rr, n);
                if (SSL3_RECORD_get_length(rr) == 0) {
                    s->rlayer.rstate = SSL_ST_READ_HEADER;
                    SSL3_RECORD_set_off(rr, 0);
                    SSL3_RECORD_set_read(rr);
                }
            }
            if (SSL3_RECORD_get_length(rr) == 0
                || (peek && n == SSL3_RECORD_get_length(rr))) {
                curr_rec++;
                rr++;
            }
            totalbytes += n;
        } while (type == SSL3_RT_APPLICATION_DATA && curr_rec < num_recs
                 && totalbytes < len);
        if (totalbytes == 0) {
            /* We must have read empty records. Get more data */
            goto start;
        }
        if (!peek && curr_rec == num_recs
            && (s->mode & SSL_MODE_RELEASE_BUFFERS)
            && SSL3_BUFFER_get_left(rbuf) == 0)
            ssl3_release_read_buffer(s);
        *readbytes = totalbytes;
        return 1;
    }

    /*
     * If we get here, then type != rr->type; if we have a handshake message,
     * then it was unexpected (Hello Request or Client Hello) or invalid (we
     * were actually expecting a CCS).
     */

    /*
     * Lets just double check that we've not got an SSLv2 record
     */
    if (rr->rec_version == SSL2_VERSION) {
        /*
         * Should never happen. ssl3_get_record() should only give us an SSLv2
         * record back if this is the first packet and we are looking for an
         * initial ClientHello. Therefore |type| should always be equal to
         * |rr->type|. If not then something has gone horribly wrong
         */
        al = SSL_AD_INTERNAL_ERROR;
        SSLerr(SSL_F_SSL3_READ_BYTES, ERR_R_INTERNAL_ERROR);
        goto f_err;
    }

    if (s->method->version == TLS_ANY_VERSION
        && (s->server || rr->type != SSL3_RT_ALERT)) {
        /*
         * If we've got this far and still haven't decided on what version
         * we're using then this must be a client side alert we're dealing with
         * (we don't allow heartbeats yet). We shouldn't be receiving anything
         * other than a ClientHello if we are a server.
         */
        s->version = rr->rec_version;
        al = SSL_AD_UNEXPECTED_MESSAGE;
        SSLerr(SSL_F_SSL3_READ_BYTES, SSL_R_UNEXPECTED_MESSAGE);
        goto f_err;
    }

    /*
     * In case of record types for which we have 'fragment' storage, fill
     * that so that we can process the data at a fixed place.
     */
    {
        size_t dest_maxlen = 0;
        unsigned char *dest = NULL;
        size_t *dest_len = NULL;

        if (SSL3_RECORD_get_type(rr) == SSL3_RT_HANDSHAKE) {
            dest_maxlen = sizeof s->rlayer.handshake_fragment;
            dest = s->rlayer.handshake_fragment;
            dest_len = &s->rlayer.handshake_fragment_len;
        } else if (SSL3_RECORD_get_type(rr) == SSL3_RT_ALERT) {
            dest_maxlen = sizeof s->rlayer.alert_fragment;
            dest = s->rlayer.alert_fragment;
            dest_len = &s->rlayer.alert_fragment_len;
        }

        if (dest_maxlen > 0) {
            n = dest_maxlen - *dest_len; /* available space in 'dest' */
            if (SSL3_RECORD_get_length(rr) < n)
                n = SSL3_RECORD_get_length(rr); /* available bytes */

            /* now move 'n' bytes: */
            while (n-- > 0) {
                dest[(*dest_len)++] =
                    SSL3_RECORD_get_data(rr)[SSL3_RECORD_get_off(rr)];
                SSL3_RECORD_add_off(rr, 1);
                SSL3_RECORD_add_length(rr, -1);
            }

            if (*dest_len < dest_maxlen) {
                SSL3_RECORD_set_read(rr);
                goto start;     /* fragment was too small */
            }
        }
    }

    /*-
     * s->rlayer.handshake_fragment_len == 4  iff  rr->type == SSL3_RT_HANDSHAKE;
     * s->rlayer.alert_fragment_len == 2      iff  rr->type == SSL3_RT_ALERT.
     * (Possibly rr is 'empty' now, i.e. rr->length may be 0.)
     */

    /*
     * If we are a server and get a client hello when renegotiation isn't
     * allowed send back a no renegotiation alert and carry on. WARNING:
     * experimental code, needs reviewing (steve)
     */
    if (s->server &&
        SSL_is_init_finished(s) &&
        !s->s3->send_connection_binding &&
        (s->version > SSL3_VERSION) &&
        !SSL_IS_TLS13(s) &&
        (s->rlayer.handshake_fragment_len >= 4) &&
        (s->rlayer.handshake_fragment[0] == SSL3_MT_CLIENT_HELLO) &&
        (s->session != NULL) && (s->session->cipher != NULL) &&
        !(s->ctx->options & SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION)) {
        SSL3_RECORD_set_length(rr, 0);
        SSL3_RECORD_set_read(rr);
        ssl3_send_alert(s, SSL3_AL_WARNING, SSL_AD_NO_RENEGOTIATION);
        goto start;
    }
    if (s->rlayer.alert_fragment_len >= 2) {
        int alert_level = s->rlayer.alert_fragment[0];
        int alert_descr = s->rlayer.alert_fragment[1];

        s->rlayer.alert_fragment_len = 0;

        if (s->msg_callback)
            s->msg_callback(0, s->version, SSL3_RT_ALERT,
                            s->rlayer.alert_fragment, 2, s,
                            s->msg_callback_arg);

        if (s->info_callback != NULL)
            cb = s->info_callback;
        else if (s->ctx->info_callback != NULL)
            cb = s->ctx->info_callback;

        if (cb != NULL) {
            j = (alert_level << 8) | alert_descr;
            cb(s, SSL_CB_READ_ALERT, j);
        }

        if (alert_level == SSL3_AL_WARNING) {
            s->s3->warn_alert = alert_descr;
            SSL3_RECORD_set_read(rr);

            s->rlayer.alert_count++;
            if (s->rlayer.alert_count == MAX_WARN_ALERT_COUNT) {
                al = SSL_AD_UNEXPECTED_MESSAGE;
                SSLerr(SSL_F_SSL3_READ_BYTES, SSL_R_TOO_MANY_WARN_ALERTS);
                goto f_err;
            }

            if (alert_descr == SSL_AD_CLOSE_NOTIFY) {
                s->shutdown |= SSL_RECEIVED_SHUTDOWN;
                return 0;
            }
            /*
             * This is a warning but we receive it if we requested
             * renegotiation and the peer denied it. Terminate with a fatal
             * alert because if application tried to renegotiate it
             * presumably had a good reason and expects it to succeed. In
             * future we might have a renegotiation where we don't care if
             * the peer refused it where we carry on.
             */
            else if (alert_descr == SSL_AD_NO_RENEGOTIATION) {
                al = SSL_AD_HANDSHAKE_FAILURE;
                SSLerr(SSL_F_SSL3_READ_BYTES, SSL_R_NO_RENEGOTIATION);
                goto f_err;
            }
#ifdef SSL_AD_MISSING_SRP_USERNAME
            else if (alert_descr == SSL_AD_MISSING_SRP_USERNAME)
                return (0);
#endif
        } else if (alert_level == SSL3_AL_FATAL) {
            char tmp[16];

            s->rwstate = SSL_NOTHING;
            s->s3->fatal_alert = alert_descr;
            SSLerr(SSL_F_SSL3_READ_BYTES, SSL_AD_REASON_OFFSET + alert_descr);
            BIO_snprintf(tmp, sizeof tmp, "%d", alert_descr);
            ERR_add_error_data(2, "SSL alert number ", tmp);
            s->shutdown |= SSL_RECEIVED_SHUTDOWN;
            SSL3_RECORD_set_read(rr);
            SSL_CTX_remove_session(s->session_ctx, s->session);
            return 0;
        } else {
            al = SSL_AD_ILLEGAL_PARAMETER;
            SSLerr(SSL_F_SSL3_READ_BYTES, SSL_R_UNKNOWN_ALERT_TYPE);
            goto f_err;
        }

        goto start;
    }

    if (s->shutdown & SSL_SENT_SHUTDOWN) { /* but we have not received a
                                            * shutdown */
        s->rwstate = SSL_NOTHING;
        SSL3_RECORD_set_length(rr, 0);
        SSL3_RECORD_set_read(rr);
        return 0;
    }

    if (SSL3_RECORD_get_type(rr) == SSL3_RT_CHANGE_CIPHER_SPEC) {
        al = SSL_AD_UNEXPECTED_MESSAGE;
        SSLerr(SSL_F_SSL3_READ_BYTES, SSL_R_CCS_RECEIVED_EARLY);
        goto f_err;
    }

    /*
     * Unexpected handshake message (ClientHello, NewSessionTicket (TLS1.3) or
     * protocol violation)
     */
    if ((s->rlayer.handshake_fragment_len >= 4)
            && !ossl_statem_get_in_handshake(s)) {
        /*
         * To get here we must be trying to read app data but found handshake
         * data. But if we're trying to read app data, and we're not in init
         * (which is tested for at the top of this function) then init must be
         * finished
         */
        assert(SSL_is_init_finished(s));
        if (!SSL_is_init_finished(s)) {
            al = SSL_AD_INTERNAL_ERROR;
            SSLerr(SSL_F_SSL3_READ_BYTES, ERR_R_INTERNAL_ERROR);
            goto f_err;
        }

        /* We found handshake data, so we're going back into init */
        ossl_statem_set_in_init(s, 1);

        i = s->handshake_func(s);
        if (i < 0)
            return i;
        if (i == 0) {
            SSLerr(SSL_F_SSL3_READ_BYTES, SSL_R_SSL_HANDSHAKE_FAILURE);
            return -1;
        }

        if (!(s->mode & SSL_MODE_AUTO_RETRY)) {
            if (SSL3_BUFFER_get_left(rbuf) == 0) {
                /* no read-ahead left? */
                BIO *bio;
                /*
                 * In the case where we try to read application data, but we
                 * trigger an SSL handshake, we return -1 with the retry
                 * option set.  Otherwise renegotiation may cause nasty
                 * problems in the blocking world
                 */
                s->rwstate = SSL_READING;
                bio = SSL_get_rbio(s);
                BIO_clear_retry_flags(bio);
                BIO_set_retry_read(bio);
                return -1;
            }
        }
        goto start;
    }

    switch (SSL3_RECORD_get_type(rr)) {
    default:
        /*
         * TLS 1.0 and 1.1 say you SHOULD ignore unrecognised record types, but
         * TLS 1.2 says you MUST send an unexpected message alert. We use the
         * TLS 1.2 behaviour for all protocol versions to prevent issues where
         * no progress is being made and the peer continually sends unrecognised
         * record types, using up resources processing them.
         */
        al = SSL_AD_UNEXPECTED_MESSAGE;
        SSLerr(SSL_F_SSL3_READ_BYTES, SSL_R_UNEXPECTED_RECORD);
        goto f_err;
    case SSL3_RT_CHANGE_CIPHER_SPEC:
    case SSL3_RT_ALERT:
    case SSL3_RT_HANDSHAKE:
        /*
         * we already handled all of these, with the possible exception of
         * SSL3_RT_HANDSHAKE when ossl_statem_get_in_handshake(s) is true, but
         * that should not happen when type != rr->type
         */
        al = SSL_AD_UNEXPECTED_MESSAGE;
        SSLerr(SSL_F_SSL3_READ_BYTES, ERR_R_INTERNAL_ERROR);
        goto f_err;
    case SSL3_RT_APPLICATION_DATA:
        /*
         * At this point, we were expecting handshake data, but have
         * application data.  If the library was running inside ssl3_read()
         * (i.e. in_read_app_data is set) and it makes sense to read
         * application data at this point (session renegotiation not yet
         * started), we will indulge it.
         */
        if (ossl_statem_app_data_allowed(s)) {
            s->s3->in_read_app_data = 2;
            return -1;
        } else {
            al = SSL_AD_UNEXPECTED_MESSAGE;
            SSLerr(SSL_F_SSL3_READ_BYTES, SSL_R_UNEXPECTED_RECORD);
            goto f_err;
        }
    }
    /* not reached */

 f_err:
    ssl3_send_alert(s, SSL3_AL_FATAL, al);
    return -1;
}

void ssl3_record_sequence_update(unsigned char *seq)
{
    int i;

    for (i = 7; i >= 0; i--) {
        ++seq[i];
        if (seq[i] != 0)
            break;
    }
}

/*
 * Returns true if the current rrec was sent in SSLv2 backwards compatible
 * format and false otherwise.
 */
int RECORD_LAYER_is_sslv2_record(RECORD_LAYER *rl)
{
    return SSL3_RECORD_is_sslv2_record(&rl->rrec[0]);
}

/*
 * Returns the length in bytes of the current rrec
 */
size_t RECORD_LAYER_get_rrec_length(RECORD_LAYER *rl)
{
    return SSL3_RECORD_get_length(&rl->rrec[0]);
}
