
/*
 * (c) 2009 Cray Inc.  All Rights Reserved.  Unpublished Proprietary
 * Information.  This unpublished work is protected to trade secret,
 * copyright and other laws.  Except as permitted by contract or
 * express written permission of Cray Inc., no part of this work or
 * its content may be used, reproduced or disclosed in any form.
 */

/* libalpslli.h:
 *
 * This describes the low-level interface between ALPS and applications.
 *
 * As with libdmapp, unless otherwise stated, all of these routines
 * return zero for success or -1 and set errno for failure.
 */

#ifndef _LIBALPSLLI_H
#define _LIBALPSLLI_H

#ident "$Id: libalpslli.h 6022 2009-05-19 15:12:29Z ben $"

#include <unistd.h>
#include <stdint.h>

/*
 * The interface is implemented on pipes.  The initialization call
 * creates two pipes, one for sending requests or information from the
 * app to ALPS, and the other for sending responses from ALPS to the app.
 *
 * A request from the app up to ALPS consists of a 32-bit integer, one
 * of the ALPS_APP_LLI_ALPS_REQ_* manifest constants, perhaps followed
 * by other information, the amount and format of which is specific to
 * that request type.  If any information is sent back from ALPS to the
 * app, that also is formatted as specified by the request type.  The
 * interface interface library neither specifies nor depends upon the
 * format of the information it transports.
 *
 * Each pair of pipes is private to the apinit shepherd and the app
 * PEs on a single node.  There is always just one shepherd on a node,
 * but there may be more than one app PE.  Access to these pipes must
 * be single threaded among such multiple PEs.  The interface provides
 * locking to achieve this.
 */

 /*
  * Communications protocol between alps and the application.
  * Lengths are specified in bytes.
  *
  * Application request message. App writes on fd LLI_FD_APPWRITE:
  *      request code (ALPS_APP_LLI_ALPS_* value)
  *      sending pid
  *      data length
  *      data...
  */
typedef struct {
    int    request;
    pid_t  pid;
    size_t len;         /* length of associated data */
} alpsLliReq_t;

/*
 *
 * Alps reply messages are defined by the request code. The library
 * adds the status word to the reply.
 *
 *      reply length
 *      reply data...
 */
typedef struct {
    int    status;
    size_t len;         /* length of associated data */
} alpsLliRep_t;

/*
 * These are the request codes.  They identify the kind of request an
 * app is making to ALPS.
 * Request codes must be less than 4096.
 *
 * SIGNAL:
 *   Arrange to send a signal to some other PEs.  The signal and PE
 *   numbers follow the request code in the request pipe.  Both are
 *   native 32-bit ints.  The signal number is one of the SIG* manifest
 *   constants defined by <signal.h>.  The PE number is -1 to send the
 *   signal to every PE in the job, or a number in [0, npes) to send it
 *   to a specific PE.  However, note that any given ALPS implementation
 *   may not be able to reliably send a signal to just one PE.  If it
 *   cannot, it will send the signal to every PE on the specified PE's
 *   node.  It is the app's responsibility to handle this eventuality
 *   and figure out which PE on that node is the intended recipient.
 *
 *   No information flows back from ALPS to the app in response to this
 *   request.
 */
#define ALPS_APP_LLI_ALPS_REQ_SIGNAL            1

/*
 * EXITING:
 *   Tell ALPS that the app is exiting normally, and no SIGABRTs
 *   should be sent due to PE exits on this node.
 *
 *   ALPS must respond. The response allows the app to wait to exit until
 *   it knows ALPS understands that a normal exit is in progress.
  */
#define ALPS_APP_LLI_ALPS_REQ_EXITING           2

/*
 * APID:
 *   From the app, ask ALPS for the assigned apid.  An apid is a uint64_t
 *   value.
 *
 *   ALPS must respond with either the apid or indicate an error in the
 *   status code.
 */
#define ALPS_APP_LLI_ALPS_REQ_APID		3

/*
 * AFFINITY MASK:
 *   From the app, ask ALPS for the combined CPU affinity mask for all
 *   PEs on this node. The affinity mask is a type cpu_set_t variable
 *   described in the sched_setaffinity(2) man page and in sched.h.
 *   Affinity masks are changed using the system call sched_setaffinity().
 *   It is the responsibility of the app to distribute the affinity
 *   mask properly to all processes of the app on this node.
 *
 *   ALPS must respond with either the affinity mask or indicate an error
 *   in the status code.
 */
#define ALPS_APP_LLI_ALPS_REQ_CPUMASK		4

/*
 * START BARRIER:
 *   From the app, ask ALPS to respond when the startup barrier is
 *   released.
 *
 *   ALPS must respond when the application can proceed out of the
 *   startup barrier or indicate an error in the status code.
 */
#define ALPS_APP_LLI_ALPS_REQ_START		5

/*
 * CPU AFFINITY REBIND:
 *    From the app, ask ALPS to ask the kernel to reset any cpu binding
 *    values.
 *
 *    ALPS must respond with ALPS_APP_LLI_ALPS_STAT_OK if the kernel
 *    call completed successfully, otherwise with either
 *    ALPS_APP_LLI_ALPS_STAT_UNAVAIL if no cpu binding info available,
 *    ALPS_APP_LLI_ALPS_STAT_FAIL for any error return from the kernel.
 *    ALPS_APP_LLI_ALPS_STAT_FORM if the caller pid is not provided
 */
#define ALPS_APP_LLI_ALPS_REQ_REBIND		6   

/*
 * ALPS_APP_LLI_ALPS_REQ_GNI			
 *    From the app, ask ALPS to provide the app specific Gemini device
 *    configuration set by ALPS on behalf of the app.  The local_addr field
 *    value is either the local node entry index into an expanded NTT or
 *    is the local NIC address for this device when an NTT is not being used.
 *
 *    The information will be provided per local NIC interface within the
 *    structures defined here.
 *
 *    ALPS sets the return status code to:
 *        ALPS_APP_LLI_ALPS_STAT_OK for success
 *        ALPS_APP_LLI_ALPS_STAT_FAIL for failures in providing the data
 *        ALPS_APP_LLI_ALPS_STAT_FORM invalid caller pid in request
 */
#define ALPS_APP_LLI_ALPS_REQ_GNI		7

typedef struct {
    uint32_t  device_id;	/* ghal index for this interface */
    int32_t   local_addr;	/* expanded NTT index or local NIC address */
    uint32_t  cookie;		/* app cookie */
    uint32_t  ptag;		/* app ptag */
} alpsAppGni_t;

typedef struct {
    int         count;		/* Number of alpsAppGni_t entries */
    union {
	int64_t align;
	char    buf[1];		/* Start location of alpsAppGni_t entries */
    } u;
} alpsAppLLIGni_t;


/*
 * These are the status codes.
 */
#define ALPS_APP_LLI_ALPS_STAT_OK		0
#define ALPS_APP_LLI_ALPS_STAT_REQ		1 /* request unknown */
#define ALPS_APP_LLI_ALPS_STAT_FORM		2 /* request format is bad */
#define ALPS_APP_LLI_ALPS_STAT_READ		3 /* response read error */
#define ALPS_APP_LLI_ALPS_STAT_UNAVAIL		4 /* data unavailable */
#define ALPS_APP_LLI_ALPS_STAT_FAIL		5 /* request failure */

__BEGIN_DECLS
/*====================
 *
 * This is the ALPS side of the interface.
 *
 */

/*
 * Create the pipes.
 *
 * Returns -1 if an error occurs. Errno will contain the error code
 * from the failing system call.
 */
extern int alps_app_lli_init(void);

/*
 * Return the pipe file descriptors.  Both arguments are arrays of two
 * entries, the same as the argument to pipe(2).
 * App_alps_filedes is the app-->ALPS pipe, and alps_app_filedes is the
 * ALPS-->app pipe.  Note that in both cases, as with pipe(2), the first
 * element of the two-element array is the file descriptor for reading,
 * and the second is the file descriptor for writing.
 *
 * (This call is primarily designed to allow ALPS to acquire the "read"
 * file descriptor for the request pipe, so that it can include it in
 * its polling for activity during app execution.  The interface is more
 * general than needed for this, to allow for possible future needs.)
 */
extern int alps_app_lli_pipes(int app_alps_filedes[2],
			      int alps_app_filedes[2]);

/*
 * Receive a request.  The return value is -1 on failure and
 * a request structure may not be delivered.
 */
extern int alps_app_lli_get_request(alpsLliReq_t *req);

/*
 * Receive additional bytes for a request.  Buf will be filled in with
 * exactly count bytes of additional info associated with a request.
 */
extern int alps_app_lli_get_request_bytes(void *buf,
					  size_t count);

/*
 * Send a response, putting count bytes from buf into the response pipe.
 */
extern int alps_app_lli_put_response(const void *buf,
				     size_t count,
				     int status);

/*====================
 *
 * This is the application side of the interface.
 *
 */

/*
 * Communication with Implicit Locking.
 */

/*
 * Send a simple request which will not deliver any response.  Buf
 * may be passed as NULL, if count is 0 (zero).  This routine locks
 * the pipes itself.
 *
 * This request blocks until alps responds.
 *
 * One could use this routine to send SIGUSR2 to all PEs in the job
 * using something like the following.
 *
 *   int32_t buf[2] = { SIGUSR2, -1 };
 *   ...
 *   err_code = alps_app_lli_put_simple_request(ALPS_APP_LLI_ALPS_REQ_SIGNAL,
 *                                              buf, sizeof(buf));
 *   ...
 */
extern int alps_app_lli_put_simple_request(int32_t req_code,
					   const void *buf,
					   size_t count);

/*
 * Communication with Explicit Locking.
 */

/*
 * Lock and unlock the pipes.
 */
extern int alps_app_lli_lock(void);
extern int alps_app_lli_unlock(void);

/*
 * Send a request.  Buf may be passed as NULL, if count is 0 (zero).
 */
extern int alps_app_lli_put_request(int32_t req_code,
				    const void *buf,
				    size_t count);

/*
 * Send more bytes associated with a request.
 */
extern int alps_app_lli_put_request_bytes(const void *buf,
					  size_t count);

/*
 * Receive a response header.
 * This function will block until alps returns the response header.
 *
 * *status will contain an ALPS_APP_LLI_ALPS_STAT_* status code
 * *count will contain the length of the data alps will return. This
 * will be zero if no data returns from the request.
 */
extern int alps_app_lli_get_response(int    *status,
				     size_t *count);

/*
 * Receive a response body.
 * This function will block until alps returns the response body.
 *
 * Exactly count bytes will be returned unless a -1 return value
 * occurs.
 */
extern int alps_app_lli_get_response_bytes(void *buf,
					   size_t count);

__END_DECLS
#endif /* _LIBALPSLLI_H */		      
