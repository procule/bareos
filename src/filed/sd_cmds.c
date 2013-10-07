/*
   BAREOS® - Backup Archiving REcovery Open Sourced

   Copyright (C) 2013-2013 Bareos GmbH & Co. KG

   This program is Free Software; you can redistribute it and/or
   modify it under the terms of version three of the GNU Affero General Public
   License as published by the Free Software Foundation and included
   in the file LICENSE.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.
*/
/**
 * This file handles accepting Storage Daemon Commands
 *
 * Marco van Wieringen, March 2013
 */

#include "bareos.h"
#include "filed.h"

static int dbglvl = 100;

void *handle_stored_connection(BSOCK *sd)
{
   JCR *jcr;
   char job_name[MAX_NAME_LENGTH];

   /*
    * Do a sanity check on the message received
    */
   if (sd->msglen < 25 || sd->msglen > 256) {
      Dmsg1(000, "<filed: %s", sd->msg);
      Emsg2(M_ERROR, 0, _("Invalid connection from %s. Len=%d\n"), sd->who(), sd->msglen);
      bmicrosleep(5, 0);   /* make user wait 5 seconds */
      sd->close();
      return NULL;
   }

   if (sscanf(sd->msg, "Hello Storage calling Start Job %127s", job_name) != 1) {
      char addr[64];
      char *who = bnet_get_peer(sd, addr, sizeof(addr)) ? sd->who() : addr;

      sd->msg[100] = 0;
      Dmsg2(dbglvl, "Bad Hello command from Director at %s: %s\n", sd->who(), sd->msg);
      Jmsg2(NULL, M_FATAL, 0, _("Bad Hello command from Director at %s: %s\n"), who, sd->msg);
      sd->close();
      return NULL;
   }

   if (!(jcr = get_jcr_by_full_name(job_name))) {
      Jmsg1(NULL, M_FATAL, 0, _("SD connect failed: Job name not found: %s\n"), job_name);
      Dmsg1(3, "**** Job \"%s\" not found.\n", job_name);
      sd->close();
      return NULL;
   }

   Dmsg1(50, "Found Job %s\n", job_name);

   jcr->store_bsock = sd;
   jcr->store_bsock->set_jcr(jcr);

   /*
    * Authenticate the Storage Daemon.
    */
   if (!authenticate_storagedaemon(jcr)) {
      Dmsg1(50, "Authentication failed Job %s\n", jcr->Job);
      Jmsg(jcr, M_FATAL, 0, _("Unable to authenticate File daemon\n"));
      jcr->setJobStatus(JS_ErrorTerminated);
   } else {
      Dmsg2(50, "OK Authentication jid=%u Job %s\n", (uint32_t)jcr->JobId, jcr->Job);
   }

   if (!jcr->max_bandwidth) {
      if (jcr->director->max_bandwidth_per_job) {
         jcr->max_bandwidth = jcr->director->max_bandwidth_per_job;
      } else if (me->max_bandwidth_per_job) {
         jcr->max_bandwidth = me->max_bandwidth_per_job;
      }
   }

   sd->set_bwlimit(jcr->max_bandwidth);
   if (me->allow_bw_bursting) {
      sd->set_bwlimit_bursting();
   }

   free_jcr(jcr);

   return NULL;
}