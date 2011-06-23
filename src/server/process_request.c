/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/

#include <pbs_config.h>   /* the master config generated by configure */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <memory.h>
#include <time.h>
#include <pwd.h>
#include <sys/param.h>
#if HAVE_SYS_UCRED_H
#include <sys/ucred.h>
#endif
#if HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#include <pthread.h>

#include "libpbs.h"
#include "pbs_error.h"
#include "server_limits.h"
#include "pbs_nodes.h"
#include "list_link.h"
#include "attribute.h"
#include "pbs_job.h"
#include "server.h"
#include "credential.h"
#include "batch_request.h"
#include "net_connect.h"
#include "log.h"
#include "svrfunc.h"
#include "pbs_proto.h"
#include "csv.h"
#include "u_tree.h"
#include "threadpool.h"

#ifndef PBS_MOM
#include "array.h"
#endif

/*
 * process_request - this function gets, checks, and invokes the proper
 * function to deal with a batch request received over the network.
 *
 * All data encoding/decoding dependencies are moved to a lower level
 * routine.  That routine must convert
 * the data received into the internal server structures regardless of
 * the data structures used by the encode/decode routines.  This provides
 * the "protocol" and "protocol generation tool" freedom to the bulk
 * of the server.
 */

/* global data items */

tlist_head svr_requests;
#ifndef PBS_MOM
pthread_mutex_t *svr_requests_mutex = NULL;
#endif

extern struct    connection svr_conn[];

extern struct    credential conn_credent[PBS_NET_MAX_CONNECTIONS];

extern struct server server;
extern char      server_host[];
#ifndef PBS_MOM
struct all_jobs newjobs;
#else
extern tlist_head svr_newjobs;
#endif

extern time_t    time_now;
extern char  *msg_err_noqueue;
extern char  *msg_err_malloc;
extern char  *msg_request;
#ifndef PBS_MOM
extern char            server_name[];
#endif

extern AvlTree okclients;

extern int LOGLEVEL;

/* private functions local to this file */

#ifdef MUNGE_AUTH
static const int munge_on = 1;
#else
static const int munge_on = 0;
#endif 

static void close_client(int sfds);
static void freebr_manage(struct rq_manage *);
static void freebr_cpyfile(struct rq_cpyfile *);
static void close_quejob(int sfds);
#ifndef PBS_MOM
static void free_rescrq(struct rq_rescq *);
#endif /* PBS_MOM */

/* END private prototypes */

#ifndef PBS_MOM

extern struct pbsnode *PGetNodeFromAddr(pbs_net_t);
#endif

/* request processing prototypes */
#ifdef PBS_MOM
void req_quejob(struct batch_request *preq);
void req_jobcredential(struct batch_request *preq);
void req_jobscript(struct batch_request *preq);
void req_rdytocommit(struct batch_request *preq);
void req_commit(struct batch_request *preq);
void req_holdjob(struct batch_request *preq);
void req_deletejob(struct batch_request *preq);
void req_rerunjob(struct batch_request *preq);
void req_modifyjob(struct batch_request *preq);
#else
void *req_jobcredential(void *preq);
void *req_quejob(void *preq);
void *req_rdytocommit(void *preq);
void *req_jobscript(void *preq);
void *req_commit(void *preq);
void *req_holdjob(void *preq);
void *req_holdarray(void *preq);
void *req_stat_node(void *vp);
void *req_jobobit(void *preq);
void *req_stagein(void *preq);
void *req_deletearray(void *preq);
void *req_deletejob(void *preq);
void *req_orderjob(void *preq);
void *req_modifyarray(void *preq);
void *req_modifyjob(void *preq);

void req_track(struct batch_request *preq);
void req_rescreserve(struct batch_request *preq);
void req_rescfree(struct batch_request *preq);

#endif

void req_shutdown(struct batch_request *preq);
void req_signaljob(struct batch_request *preq);
void req_mvjobfile(struct batch_request *preq);
void req_checkpointjob(struct batch_request *preq);
void req_messagejob(struct batch_request *preq);
void req_gpuctrl(struct batch_request *preq);

/* END request processing prototypes */


#ifdef ENABLE_UNIX_SOCKETS
#ifndef PBS_MOM
int get_creds(int sd, char *username, char *hostname)
  {
  int             nb/*, sync*/;
  char            ctrl[CMSG_SPACE(sizeof(struct ucred))];
  size_t          size;

  struct iovec    iov[1];

  struct msghdr   msg;

  struct cmsghdr  *cmptr;
  ucreds *credentials;

  struct passwd *cpwd;
  char dummy;

  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = iov;
  msg.msg_iovlen = 1;
  msg.msg_control = ctrl;
  msg.msg_controllen = sizeof(ctrl);
  msg.msg_flags = 0;

#ifdef LOCAL_CREDS
  nb = 1;

  if (setsockopt(sd, 0, LOCAL_CREDS, &nb, sizeof(nb)) == -1) return 0;

#else
#ifdef SO_PASSCRED
  nb = 1;

  if (setsockopt(sd, SOL_SOCKET, SO_PASSCRED, &nb, sizeof(nb)) == -1)
    return 0;

#endif
#endif

  dummy = '\0';

  do
    {
    msg.msg_iov->iov_base = (void *) & dummy;
    msg.msg_iov->iov_len  = sizeof(dummy);
    nb = recvmsg(sd, &msg, 0);
    }
  while (nb == -1 && (errno == EINTR || errno == EAGAIN));

  if (nb == -1) return 0;

  if ((unsigned)msg.msg_controllen < sizeof(struct cmsghdr)) return 0;

  cmptr = CMSG_FIRSTHDR(&msg);

#ifndef __NetBSD__
  size = sizeof(ucreds);

#else
  if (cmptr->cmsg_len < SOCKCREDSIZE(0)) return 0;

  size = SOCKCREDSIZE(((cred *)CMSG_DATA(cmptr))->sc_ngroups);

#endif
  if ((unsigned)cmptr->cmsg_len != CMSG_LEN(size)) return 0;

  if (cmptr->cmsg_level != SOL_SOCKET) return 0;

  if (cmptr->cmsg_type != SCM_CREDS) return 0;

  if (!(credentials = (ucreds *)malloc(size))) return 0;

  *credentials = *(ucreds *)CMSG_DATA(cmptr);

  cpwd = getpwuid(SPC_PEER_UID(credentials));

  if (cpwd)
    strcpy(username, cpwd->pw_name);

  strcpy(hostname, server_name);

  free(credentials);

  return 0;
  }

#endif
#endif /* END ENABLE_UNIX_SOCKETS */


/*
 * process_request - process an request from the network:
 *
 * - Call function to read in the request and decode it.
 * - Validate requesting host and user.
 * - Call function to process request based on type.
 *    That function MUST free the request by calling free_br()
 *
 * NOTE: the caller functions hold the mutex for the connection
 */

void process_request(

  int sfds) /* file descriptor (socket) to get request */

  {
#ifdef PBS_MOM
  char *id = "process_request";
#endif

  int                   rc;

  struct batch_request *request = NULL;

  time_now = time(NULL);

  request = alloc_br(0);

  request->rq_conn = sfds;

  /*
   * Read in the request and decode it to the internal request structure.
   */

#ifndef PBS_MOM
  pthread_mutex_lock(svr_conn[sfds].cn_mutex);

  if (svr_conn[sfds].cn_active == FromClientDIS)
    {
#ifdef ENABLE_UNIX_SOCKETS

    if ((svr_conn[sfds].cn_socktype & PBS_SOCK_UNIX) &&
        (svr_conn[sfds].cn_authen != PBS_NET_CONN_AUTHENTICATED))
      {
      get_creds(sfds, conn_credent[sfds].username, conn_credent[sfds].hostname);
      }

#endif /* END ENABLE_UNIX_SOCKETS */
    rc = dis_request_read(sfds, request);
    }
  else
    {
    LOG_EVENT(
      PBSEVENT_SYSTEM,
      PBS_EVENTCLASS_REQUEST,
      "process_req",
      "request on invalid type of connection");

    close_conn(sfds);

    pthread_mutex_unlock(svr_conn[sfds].cn_mutex);

    free_br(request);
    
    return;
    }

#else /* PBS_MOM */
  rc = dis_request_read(sfds, request);

#endif /* PBS_MOM */

  if (rc == -1)
    {
    /* FAILURE */

    /* premature end of file */

    close_client(sfds);

#ifndef PBS_MOM
    pthread_mutex_unlock(svr_conn[sfds].cn_mutex);
#endif

    free_br(request);

    return;
    }

  if ((rc == PBSE_SYSTEM) || (rc == PBSE_INTERNAL))
    {
    /* FAILURE */

    /* read error, likely cannot send reply so just disconnect */

    /* ??? not sure about this ??? */

    close_client(sfds);

#ifndef PBS_MOM
    pthread_mutex_unlock(svr_conn[sfds].cn_mutex);
#endif

    free_br(request);

    return;
    }

  if (rc > 0)
    {
    /* FAILURE */

    /*
     * request didn't decode, either garbage or unknown
     * request type, in either case, return reject-reply
     */

    req_reject(rc, 0, request, NULL, "cannot decode message");

    close_client(sfds);

#ifndef PBS_MOM
    pthread_mutex_unlock(svr_conn[sfds].cn_mutex);
#endif

    return;
    }

  if (get_connecthost(sfds, request->rq_host, PBS_MAXHOSTNAME) != 0)
    {
    char tmpLine[1024];

    sprintf(log_buffer, "%s: %lu",
            pbse_to_txt(PBSE_BADHOST),
            get_connectaddr(sfds,FALSE));

    LOG_EVENT(PBSEVENT_DEBUG, PBS_EVENTCLASS_REQUEST, "", log_buffer);

    snprintf(tmpLine, sizeof(tmpLine), "cannot determine hostname for connection from %lu",
             get_connectaddr(sfds,FALSE));

    req_reject(PBSE_BADHOST, 0, request, NULL, tmpLine);

#ifndef PBS_MOM
    pthread_mutex_unlock(svr_conn[sfds].cn_mutex);
#endif

    return;
    }

  if (LOGLEVEL >= 1)
    {
    sprintf(
      log_buffer,
      msg_request,
      reqtype_to_txt(request->rq_type),
      request->rq_user,
      request->rq_host,
      sfds);

    LOG_EVENT(PBSEVENT_DEBUG2, PBS_EVENTCLASS_REQUEST, "", log_buffer);
    }

  /* is the request from a host acceptable to the server */

#ifndef PBS_MOM

  if (svr_conn[sfds].cn_socktype & PBS_SOCK_UNIX)
    {
    strcpy(request->rq_host, server_name);
    }

  if (server.sv_attr[SRV_ATR_acl_host_enable].at_val.at_long)
    {
    /* acl enabled, check it; always allow myself and nodes */

    struct pbsnode *isanode;

    isanode = PGetNodeFromAddr(get_connectaddr(sfds,FALSE));

    if ((isanode == NULL) &&
        (strcmp(server_host, request->rq_host) != 0) &&
        (acl_check(
           &server.sv_attr[SRV_ATR_acl_hosts],
           request->rq_host,
           ACL_Host) == 0))
      {
      char tmpLine[1024];

      snprintf(tmpLine, sizeof(tmpLine), "request not authorized from host %s",
               request->rq_host);

      req_reject(PBSE_BADHOST, 0, request, NULL, tmpLine);

      close_client(sfds);

      pthread_mutex_unlock(svr_conn[sfds].cn_mutex);

      return;
      }

    pthread_mutex_unlock(isanode->nd_mutex);
    }

  /*
   * determine source (user client or another server) of request.
   * set the permissions granted to the client
   */

  if (svr_conn[sfds].cn_authen == PBS_NET_CONN_FROM_PRIVIL)
    {
    /* request came from another server */

    request->rq_fromsvr = 1;

    request->rq_perm =
      ATR_DFLAG_USRD | ATR_DFLAG_USWR |
      ATR_DFLAG_OPRD | ATR_DFLAG_OPWR |
      ATR_DFLAG_MGRD | ATR_DFLAG_MGWR |
      ATR_DFLAG_SvWR;
    }
  else
    {
    /* request not from another server */

    request->rq_fromsvr = 0;

    /*
     * Client must be authenticated by an Authenticate User Request, if not,
     * reject request and close connection.  -- The following is retained for
     * compat with old cmds -- The exception to this is of course the Connect
     * Request which cannot have been authenticated, because it contains the
     * needed ticket; so trap it here.  Of course, there is no prior
     * authentication on the Authenticate User request either, but it comes
     * over a reserved port and appears from another server, hence is
     * automatically granted authentication.
     *
     * The above is only true with inet sockets.  With unix domain sockets, the
     * user creds were read before the first dis_request_read call above.
     * We automatically granted authentication because we can trust the socket
     * creds.  Authorization is still granted in svr_get_privilege below
     */

    if (request->rq_type == PBS_BATCH_Connect)
      {
      req_connect(request);

      if (svr_conn[sfds].cn_socktype == PBS_SOCK_INET)
        {
        pthread_mutex_unlock(svr_conn[sfds].cn_mutex);

        return;
        }

      }

    if (svr_conn[sfds].cn_socktype & PBS_SOCK_UNIX)
      {
      conn_credent[sfds].timestamp = time_now;
      svr_conn[sfds].cn_authen = PBS_NET_CONN_AUTHENTICATED;
      }


    if (ENABLE_TRUSTED_AUTH == TRUE )
      rc = 0;  /* bypass the authentication of the user--trust the client completely */
    else if (munge_on)
      {
      /* If munge_on is true we will validate the connection later */
      conn_credent[sfds].timestamp = time_now;
      svr_conn[sfds].cn_authen = PBS_NET_CONN_AUTHENTICATED;
      rc = 0;
      }
    else if (svr_conn[sfds].cn_authen != PBS_NET_CONN_AUTHENTICATED)
      rc = PBSE_BADCRED;
    else
      rc = authenticate_user(request, &conn_credent[sfds]);

    if (rc != 0)
      {
      req_reject(rc, 0, request, NULL, NULL);

      close_client(sfds);

      pthread_mutex_unlock(svr_conn[sfds].cn_mutex);

      return;
      }

    /*
     * pbs_mom and checkpoint restart scripts both need the authority to do
     * alters and releases on checkpointable jobs.  Allow manager permission
     * for root on the jobs execution node.
     */
     
    if (((request->rq_type == PBS_BATCH_ModifyJob) ||
        (request->rq_type == PBS_BATCH_ReleaseJob)) &&
        (strcmp(request->rq_user, PBS_DEFAULT_ADMIN) == 0))
      {
      job *pjob;
      char *dptr;
      int skip = FALSE;
      char short_host[PBS_MAXHOSTNAME+1];

      /* make short host name */

      strcpy(short_host, request->rq_host);
      if ((dptr = strchr(short_host, '.')) != NULL)
        {
        *dptr = '\0';
        }
      
      if (((pjob = find_job(request->rq_ind.rq_modify.rq_objname)) != (job *)0) &&
          (pjob->ji_qs.ji_state == JOB_STATE_RUNNING))
        {

        if ((pjob->ji_wattr[JOB_ATR_checkpoint].at_flags & ATR_VFLAG_SET) &&
          ((csv_find_string(pjob->ji_wattr[JOB_ATR_checkpoint].at_val.at_str, "s") != NULL) ||
          (csv_find_string(pjob->ji_wattr[JOB_ATR_checkpoint].at_val.at_str, "c") != NULL) ||
          (csv_find_string(pjob->ji_wattr[JOB_ATR_checkpoint].at_val.at_str, "enabled") != NULL)) &&
          (strstr(pjob->ji_wattr[JOB_ATR_exec_host].at_val.at_str, short_host) != NULL))
          {

          request->rq_perm = svr_get_privilege(request->rq_user, server_host);
          skip = TRUE;

          }
        
        }
      if (pjob != NULL)
        {
        pthread_mutex_unlock(pjob->ji_mutex);
        }
      if (!skip)
        {
        request->rq_perm = svr_get_privilege(request->rq_user, request->rq_host);
        }
      }
    else
      {
      request->rq_perm = svr_get_privilege(request->rq_user, request->rq_host);
      }
    }  /* END else (svr_conn[sfds].cn_authen == PBS_NET_CONN_FROM_PRIVIL) */

  /* if server shutting down, disallow new jobs and new running */

  if (server.sv_attr[SRV_ATR_State].at_val.at_long > SV_STATE_RUN)
    {
    switch (request->rq_type)
      {
      case PBS_BATCH_AsyrunJob:
      case PBS_BATCH_JobCred:
      case PBS_BATCH_MoveJob:
      case PBS_BATCH_QueueJob:
      case PBS_BATCH_RunJob:
      case PBS_BATCH_StageIn:
      case PBS_BATCH_jobscript:

        pthread_mutex_unlock(svr_conn[sfds].cn_mutex);

        req_reject(PBSE_SVRDOWN, 0, request, NULL, NULL);

        return;

        /*NOTREACHED*/

        break;
      }
    }

  pthread_mutex_unlock(svr_conn[sfds].cn_mutex);

#else /* THIS CODE FOR MOM ONLY */

    {
    /*extern tree *okclients; */

    extern void mom_server_update_receive_time_by_ip(u_long ipaddr, const char *cmd);

    /* check connecting host against allowed list of ok clients */

    if (LOGLEVEL >= 6)
      {
      sprintf(log_buffer, "request type %s from host %s received",
        reqtype_to_txt(request->rq_type),
        request->rq_host);

      log_record(
        PBSEVENT_JOB,
        PBS_EVENTCLASS_JOB,
        id,
        log_buffer);
      }

/*    if (!tfind(svr_conn[sfds].cn_addr, &okclients)) */
    pthread_mutex_lock(svr_conn[sfds].cn_mutex);

    if (!AVL_is_in_tree(svr_conn[sfds].cn_addr, 0, okclients))
      {
      sprintf(log_buffer, "request type %s from host %s rejected (host not authorized)",
        reqtype_to_txt(request->rq_type),
        request->rq_host);

      log_record(
        PBSEVENT_JOB,
        PBS_EVENTCLASS_JOB,
        id,
        log_buffer);

      req_reject(PBSE_BADHOST, 0, request, NULL, "request not authorized");

      close_client(sfds);

      pthread_mutex_unlock(svr_conn[sfds].cn_mutex);

      return;
      }

    if (LOGLEVEL >= 3)
      {
      sprintf(log_buffer, "request type %s from host %s allowed",
        reqtype_to_txt(request->rq_type),
        request->rq_host);

      log_record(
        PBSEVENT_JOB,
        PBS_EVENTCLASS_JOB,
        id,
        log_buffer);
      }

    mom_server_update_receive_time_by_ip(svr_conn[sfds].cn_addr, reqtype_to_txt(request->rq_type));

    pthread_mutex_unlock(svr_conn[sfds].cn_mutex);
    }    /* END BLOCK */

  request->rq_fromsvr = 1;

  request->rq_perm =
    ATR_DFLAG_USRD | ATR_DFLAG_USWR |
    ATR_DFLAG_OPRD | ATR_DFLAG_OPWR |
    ATR_DFLAG_MGRD | ATR_DFLAG_MGWR |
    ATR_DFLAG_SvWR | ATR_DFLAG_MOM;

#endif /* END else !PBS_MOM */

  /*
   * dispatch the request to the correct processing function.
   * The processing function must call reply_send() to free
   * the request struture.
   */

  dispatch_request(sfds, request);

  return;
  }  /* END process_request() */


/*
 * dispatch_request - Determine the request type and invoke the corresponding
 * function.  The function will perform the request action and return the
 * reply.  The function MUST also reply and free the request by calling
 * reply_send().
 *
 * @see process_request() - parent
 */

void dispatch_request(

  int          sfds,    /* I */
  struct batch_request *request) /* I */

  {
  char *id = "dispatch_request";

  if (LOGLEVEL >= 5)
    {
    sprintf(log_buffer,"dispatching request %s on sd=%d",
      reqtype_to_txt(request->rq_type),
      sfds);

    log_record(
      PBSEVENT_JOB,
      PBS_EVENTCLASS_JOB,
      id,
      log_buffer);
    }

  switch (request->rq_type)
    {
    case PBS_BATCH_QueueJob:

      net_add_close_func(sfds, close_quejob);

#ifdef PBS_MOM
      req_quejob(request);
#else
      enqueue_threadpool_request(req_quejob,request);
#endif
      break;

    case PBS_BATCH_JobCred:

#ifdef PBS_MOM
      req_jobcredential(request);
#else
      enqueue_threadpool_request(req_jobcredential,request);
#endif

      break;

    case PBS_BATCH_jobscript:

#ifdef PBS_MOM
      req_jobscript(request);
#else
      enqueue_threadpool_request(req_jobscript,request);
#endif
      break;

    case PBS_BATCH_RdytoCommit:

#ifdef PBS_MOM
      req_rdytocommit(request);
#else
      enqueue_threadpool_request(req_rdytocommit,request);
#endif
      break;

    case PBS_BATCH_Commit:

#ifdef PBS_MOM
      req_commit(request);
#else
      enqueue_threadpool_request(req_commit,request);
#endif

      net_add_close_func(sfds, (void (*)())0);

      break;

    case PBS_BATCH_DeleteJob:

#ifdef PBS_MOM
      req_deletejob(request);
#else
      /* if this is a server size job delete request, then the request could also be
      * for an entire array.  we check to see if the request object name is an array id.
      * if so we hand off the the req_deletearray() function.  If not we pass along to the
      * normal req_deltejob() function.
      */

      if (is_array(request->rq_ind.rq_delete.rq_objname))
        enqueue_threadpool_request(req_deletearray,request);
      else
        enqueue_threadpool_request(req_deletejob,request);

#endif
      break;

    case PBS_BATCH_HoldJob:
#ifdef PBS_MOM
      req_holdjob(request);

#else
      if (is_array(request->rq_ind.rq_hold.rq_orig.rq_objname))
        enqueue_threadpool_request(req_holdarray,request);
      else
        enqueue_threadpool_request(req_holdjob,request);

#endif
      break;

    case PBS_BATCH_CheckpointJob:

      req_checkpointjob(request);

      break;

#ifndef PBS_MOM

    case PBS_BATCH_LocateJob:

      enqueue_threadpool_request(req_locatejob,request);

      break;

    case PBS_BATCH_Manager:

      enqueue_threadpool_request(req_manager,request);

      break;

#endif  /* END !PBS_MOM */

    case PBS_BATCH_MessJob:

      req_messagejob(request);

      break;

    case PBS_BATCH_AsyModifyJob:

    case PBS_BATCH_ModifyJob:
#ifndef PBS_MOM
      if (is_array(request->rq_ind.rq_delete.rq_objname))
        enqueue_threadpool_request(req_modifyarray,request);
      else
        enqueue_threadpool_request(req_modifyjob,request);

#else /* END ifndef PBS_MOM */
      req_modifyjob(request);
#endif /* PBS_MOM */

      break;

    case PBS_BATCH_Rerun:

      req_rerunjob(request);

      break;

#ifndef PBS_MOM

    case PBS_BATCH_MoveJob:

      enqueue_threadpool_request(req_movejob,request);

      break;

    case PBS_BATCH_OrderJob:

      enqueue_threadpool_request(req_orderjob,request);

      break;

    case PBS_BATCH_Rescq:

      req_rescq(request);

      break;

    case PBS_BATCH_ReserveResc:

      req_rescreserve(request);

      break;

    case PBS_BATCH_ReleaseResc:

      req_rescfree(request);

      break;

    case PBS_BATCH_ReleaseJob:

      if (is_array(request->rq_ind.rq_delete.rq_objname))
        enqueue_threadpool_request(req_releasearray,request);
      else
        enqueue_threadpool_request(req_releasejob,request);

      break;

    case PBS_BATCH_RunJob:

    case PBS_BATCH_AsyrunJob:

      req_runjob(request);

      break;

    case PBS_BATCH_SelectJobs:

    case PBS_BATCH_SelStat:

      /* handle special 'truncated' keyword */

      if (!strncasecmp(request->rq_ind.rq_status.rq_id, "truncated", strlen("truncated")))
        enqueue_threadpool_request(req_stat_job,request);
      else
        req_selectjobs(request);

      break;

#endif  /* !PBS_MOM */

    case PBS_BATCH_Shutdown:

      req_shutdown(request);

      break;

    case PBS_BATCH_SignalJob:

    case PBS_BATCH_AsySignalJob:

      req_signaljob(request);

      break;

    case PBS_BATCH_GpuCtrl:

      req_gpuctrl(request);

      break;

    case PBS_BATCH_MvJobFile:

      req_mvjobfile(request);

      break;

#ifndef PBS_MOM  /* server only functions */

    case PBS_BATCH_StatusQue:

      req_stat_que(request);

      break;

    case PBS_BATCH_StatusNode:
      enqueue_threadpool_request(req_stat_node,request);

      break;

    case PBS_BATCH_StatusSvr:

      req_stat_svr(request);

      break;

      /* DIAGTODO: handle PBS_BATCH_StatusDiag and define req_stat_diag() */

    case PBS_BATCH_TrackJob:

      req_track(request);

      break;

    case PBS_BATCH_RegistDep:

      if (is_array(request->rq_ind.rq_register.rq_parent))
        {
        req_registerarray(request);
        }
      else
        {
        req_register(request);
        }

      break;

    case PBS_BATCH_AuthenUser:

      /* determine if user is valid */

      req_authenuser(request);

      break;

    case PBS_BATCH_AltAuthenUser:
      /* Use given authentication method to determine
         if user is valid */
       req_altauthenuser(request); 

      break;

    case PBS_BATCH_JobObit:

      enqueue_threadpool_request(req_jobobit,request);

      break;

    case PBS_BATCH_StageIn:

      enqueue_threadpool_request(req_stagein,request);

      break;

    case PBS_BATCH_StatusJob:

      enqueue_threadpool_request(req_stat_job,request);
      
      break;

#else /* MOM only functions */

    case PBS_BATCH_StatusJob:

      req_stat_job(request);

      break;

    case PBS_BATCH_ReturnFiles:

      req_returnfiles(request);

      break;

    case PBS_BATCH_CopyFiles:

      req_cpyfile(request);

      break;

    case PBS_BATCH_DelFiles:

      req_delfile(request);

      break;

#endif /* !PBS_MOM */

    default:

      req_reject(PBSE_UNKREQ, 0, request, NULL, NULL);

      close_client(sfds);

      break;
    }  /* END switch (request->rq_type) */

  return;
  }  /* END dispatch_request() */





/*
 * close_client - close a connection to a client, also "inactivate"
 *    any outstanding batch requests on that connection.
 */

static void close_client(

  int sfds)  /* connection socket */

  {

  struct batch_request *preq;

  close_conn(sfds); /* close the connection */

#ifndef PBS_MOM
  pthread_mutex_lock(svr_requests_mutex);
#endif

  preq = (struct batch_request *)GET_NEXT(svr_requests);

  while (preq != NULL)
    {
    /* list of outstanding requests */

    if (preq->rq_conn == sfds)
      preq->rq_conn = -1;

    if (preq->rq_orgconn == sfds)
      preq->rq_orgconn = -1;

    preq = (struct batch_request *)GET_NEXT(preq->rq_link);
    }

#ifndef PBS_MOM
  pthread_mutex_unlock(svr_requests_mutex);
#endif

  return;
  }  /* END close_client() */




/*
 * alloc_br - allocate and clear a batch_request structure
 */

struct batch_request *alloc_br(

        int type)

  {

  struct batch_request *req = NULL;

  req = (struct batch_request *)malloc(sizeof(struct batch_request));

  if (req == NULL)
    {
    log_err(errno, "alloc_br", msg_err_malloc);

    return(NULL);
    }

  memset((void *)req, (int)0, sizeof(struct batch_request));

  req->rq_type = type;

  req->rq_conn = -1;  /* indicate not connected */
  req->rq_orgconn = -1;  /* indicate not connected */
  req->rq_time = time_now;
  req->rq_reply.brp_choice = BATCH_REPLY_CHOICE_NULL;
  req->rq_noreply = FALSE;  /* indicate reply is needed */

#ifndef PBS_MOM
  pthread_mutex_lock(svr_requests_mutex);
#endif

  CLEAR_LINK(req->rq_link);

  append_link(&svr_requests, &req->rq_link, req);

#ifndef PBS_MOM
  pthread_mutex_unlock(svr_requests_mutex);
#endif

  return(req);
  }





/*
 * close_quejob - locate and deal with the new job that was being recevied
 *    when the net connection closed.
 */

static void close_quejob(

  int sfds)

  {
  job *pjob;

#ifdef PBS_MOM
  /* version for PBS_MOM */
  job *npjob;

  pjob = (job *)GET_NEXT(svr_newjobs);

  while (pjob != NULL)
    {
    npjob = GET_NEXT(pjob->ji_alljobs);

    if (pjob->ji_qs.ji_un.ji_newt.ji_fromsock == sfds)
      {
      if (pjob->ji_qs.ji_substate != JOB_SUBSTATE_TRANSICM)
        {
        /* delete the job */
        delete_link(&pjob->ji_alljobs);

        job_purge(pjob);

        pjob = NULL;
        }
      
      break;
      }  /* END if (..) */

    pjob = npjob;
    }

#else
  /* version for PBS_SERVER */
  int iter = -1;

  while ((pjob = next_job(&newjobs,&iter)) != NULL)
    {
    if (pjob->ji_qs.ji_un.ji_newt.ji_fromsock == sfds)
      {
      if (pjob->ji_qs.ji_substate == JOB_SUBSTATE_TRANSICM)
        {
        if (pjob->ji_qs.ji_svrflags & JOB_SVFLG_HERE)
          {
          /*
           * the job was being created here for the first time
           * go ahead and enqueue it as QUEUED; otherwise, hold
           * it here as TRANSICM until we hear from the sending
           * server again to commit.
           */

          remove_job(&newjobs,pjob);

          pjob->ji_qs.ji_state = JOB_STATE_QUEUED;
          pjob->ji_qs.ji_substate = JOB_SUBSTATE_QUEUED;

          if (svr_enquejob(pjob))
            job_abt(&pjob, msg_err_noqueue);
          }
        }
      else
        {
        /* else delete the job */
        remove_job(&newjobs,pjob);

        job_purge(pjob);

        pjob = NULL;
        }

      break;
      }  /* END if (..) */

    pthread_mutex_unlock(pjob->ji_mutex);
    }

  if (pjob != NULL)
    pthread_mutex_unlock(pjob->ji_mutex);
#endif /* PBS_MOM or not */

  return;
  }  /* END close_quejob() */





/*
 * free_br - free space allocated to a batch_request structure
 * including any sub-structures
 */

void free_br(

  struct batch_request *preq)

  {
#ifndef PBS_MOM
  pthread_mutex_lock(svr_requests_mutex);
#endif

  delete_link(&preq->rq_link);

#ifndef PBS_MOM
  pthread_mutex_unlock(svr_requests_mutex);
#endif

  reply_free(&preq->rq_reply);

  if (preq->rq_extend) 
      free(preq->rq_extend);

  switch (preq->rq_type)
    {
    case PBS_BATCH_QueueJob:

      free_attrlist(&preq->rq_ind.rq_queuejob.rq_attr);

      break;

    case PBS_BATCH_JobCred:

      if (preq->rq_ind.rq_jobcred.rq_data)
        free(preq->rq_ind.rq_jobcred.rq_data);

      break;

    case PBS_BATCH_MvJobFile:

    case PBS_BATCH_jobscript:

      if (preq->rq_ind.rq_jobfile.rq_data)
        free(preq->rq_ind.rq_jobfile.rq_data);

      break;

    case PBS_BATCH_HoldJob:

      freebr_manage(&preq->rq_ind.rq_hold.rq_orig);

      break;

    case PBS_BATCH_CheckpointJob:

      freebr_manage(&preq->rq_ind.rq_manager);

      break;

    case PBS_BATCH_MessJob:

      if (preq->rq_ind.rq_message.rq_text)
        free(preq->rq_ind.rq_message.rq_text);

      break;

    case PBS_BATCH_ModifyJob:

    case PBS_BATCH_AsyModifyJob:

      freebr_manage(&preq->rq_ind.rq_modify);

      break;

    case PBS_BATCH_StatusJob:

    case PBS_BATCH_StatusQue:

    case PBS_BATCH_StatusNode:

    case PBS_BATCH_StatusSvr:
      /* DIAGTODO: handle PBS_BATCH_StatusDiag */

      free_attrlist(&preq->rq_ind.rq_status.rq_attr);

      break;

    case PBS_BATCH_JobObit:

      free_attrlist(&preq->rq_ind.rq_jobobit.rq_attr);

      break;

    case PBS_BATCH_CopyFiles:

    case PBS_BATCH_DelFiles:

      freebr_cpyfile(&preq->rq_ind.rq_cpyfile);

      break;

#ifndef PBS_MOM /* Server Only */

    case PBS_BATCH_Manager:

      freebr_manage(&preq->rq_ind.rq_manager);

      break;

    case PBS_BATCH_ReleaseJob:

      freebr_manage(&preq->rq_ind.rq_release);

      break;

    case PBS_BATCH_Rescq:

      free_rescrq(&preq->rq_ind.rq_rescq);

      break;

    case PBS_BATCH_SelectJobs:

    case PBS_BATCH_SelStat:

      free_attrlist(&preq->rq_ind.rq_select);

      break;

    case PBS_BATCH_RunJob:

    case PBS_BATCH_AsyrunJob:

      if (preq->rq_ind.rq_run.rq_destin)
        free(preq->rq_ind.rq_run.rq_destin);

      break;

#endif /* !PBS_MOM */

    default:

      /* NO-OP */

      break;
    }  /* END switch (preq->rq_type) */

  free(preq);

  return;
  }  /* END free_br() */





static void freebr_manage(

  struct rq_manage *pmgr)

  {
  free_attrlist(&pmgr->rq_attr);

  return;
  }  /* END freebr_manage() */




static void freebr_cpyfile(

  struct rq_cpyfile *pcf)

  {

  struct rqfpair *ppair;

  while ((ppair = (struct rqfpair *)GET_NEXT(pcf->rq_pair)) != NULL)
    {
    delete_link(&ppair->fp_link);

    if (ppair->fp_local != NULL)
      free(ppair->fp_local);

    if (ppair->fp_rmt != NULL)
      free(ppair->fp_rmt);

    free(ppair);
    }

  return;
  }  /* END freebr_cpyfile() */





#ifndef PBS_MOM
static void free_rescrq(

  struct rq_rescq *pq)

  {
  int i;

  i = pq->rq_num;

  while (i--)
    {
    if (*(pq->rq_list + i) != NULL)
      free(*(pq->rq_list + i));
    }

  if (pq->rq_list != NULL)
    free(pq->rq_list);

  return;
  }  /* END free_rescrq() */

#endif /* PBS_MOM */

/* END process_requests.c */


