/*****************************************************************************
 *  __________________    _________  _____            _____  .__         ._.
 *  \______   \______ \  /   _____/ /     \          /  _  \ |__| ____   | |
 *   |    |  _/|    |  \ \_____  \ /  \ /  \        /  /_\  \|  _/ __ \  | |
 *   |    |   \|    `   \/        /    Y    \      /    |    |  \  ___/   \|
 *   |______  /_______  /_______  \____|__  / /\   \____|__  |__|\___ |   __
 *          \/        \/        \/        \/  )/           \/        \/   \/
 *
 * This file is part of liBDSM. Copyright © 2014-2015 VideoLabs SAS
 *
 * Author: Julien 'Lta' BALLET <contact@lta.io>
 *
 * liBDSM is released under LGPLv2.1 (or later) and is also available
 * under a commercial license.
 *****************************************************************************
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include "../xcode/config.h"
#include "bdsm_debug.h"
#include "smb_message.h"
#include "smb_session.h"
#include "smb_session_msg.h"
#include "smb_utils.h"
#include "smb_stat.h"

/*
 * Receive trans2 management
 */

static smb_message *smb_tr2_recv(smb_session *s)
{
    smb_message           recv, *res;
    smb_trans2_resp       *tr2;
    size_t                growth;
    int                   remaining;

    if (!smb_session_recv_msg(s, &recv))
        return NULL;
    tr2         = (smb_trans2_resp *)recv.packet->payload;
    growth      = tr2->total_data_count - tr2->data_count;
    res         = smb_message_grow(&recv, growth);
    if (!res)
        return NULL;
    res->cursor = recv.payload_size;
    remaining   = (int)tr2->total_data_count -
                  (tr2->data_displacement + tr2->data_count);

    while (remaining > 0)
    {
        remaining = smb_session_recv_msg(s, &recv);
        if (remaining)
        {
            tr2   = (smb_trans2_resp *)recv.packet->payload;
            /*
             * XXX: Why does padding was necessary and is not anymore, i.e.
             * find a reproductible setup where it is
             */
            smb_message_append(res, tr2->payload /* + 2 pad */, tr2->data_count);
            remaining = (int)tr2->total_data_count -
                        (tr2->data_displacement + tr2->data_count);
        }
    }

    return res;
}

/*
 * Find management
 */

static void smb_tr2_find2_parse_entries(smb_file **files_p, smb_tr2_find2_entry *iter, size_t count, uint8_t *eod)
{
    smb_file *tmp = NULL;
    size_t   i;

    for (i = 0; i < count && (uint8_t *)iter < eod; i++)
    {
        // Create a smb_file and fill it
        tmp = calloc(1, sizeof(smb_file));
        if (!tmp)
            return;

        tmp->name_len = smb_from_utf16((const char *)iter->name, iter->name_len,
                                       &tmp->name);
        if (tmp->name_len == 0)
        {
            free(tmp);
            return;
        }
        tmp->name[tmp->name_len] = 0;

        tmp->created    = iter->created;
        tmp->accessed   = iter->accessed;
        tmp->written    = iter->written;
        tmp->changed    = iter->changed;
        tmp->size       = iter->size;
        tmp->alloc_size = iter->alloc_size;
        tmp->attr       = iter->attr;
        tmp->is_dir     = tmp->attr & SMB_ATTR_DIR;

        tmp->next = *files_p;
        *files_p  = tmp;

        iter = (smb_tr2_find2_entry *)(((char *)iter) + iter->next_entry);
    }

    return;
}

static void smb_find_first_parse(smb_message *msg, smb_file **files_p)
{
    smb_trans2_resp       *tr2;
    smb_tr2_findfirst2_params  *params;
    smb_tr2_find2_entry   *iter;
    uint8_t               *eod;
    size_t                count;

    bdsm_assert(msg != NULL);
    
    if(msg != NULL){
        
        // Let's parse the answer we got from server
        tr2     = (smb_trans2_resp *)msg->packet->payload;

        if(tr2->wct>0){
            params  = (smb_tr2_findfirst2_params *)tr2->payload;
            iter    = (smb_tr2_find2_entry *)(tr2->payload + sizeof(smb_tr2_findfirst2_params));
            eod     = msg->packet->payload + msg->payload_size;
            count   = params->count;
            smb_tr2_find2_parse_entries(files_p, iter, count, eod);
        }
        
    }
}

static void smb_find_next_parse(smb_message *msg, smb_file **files_p)
{
    smb_trans2_resp       *tr2;
    smb_tr2_findnext2_params  *params;
    smb_tr2_find2_entry   *iter;
    uint8_t               *eod;
    size_t                count;

    bdsm_assert(msg != NULL);
    
    if(msg != NULL){

        // Let's parse the answer we got from server
        tr2     = (smb_trans2_resp *)msg->packet->payload;

        if(tr2->wct>0){
            params  = (smb_tr2_findnext2_params *)tr2->payload;
            iter    = (smb_tr2_find2_entry *)(tr2->payload + sizeof(smb_tr2_findnext2_params));
            eod     = msg->packet->payload + msg->payload_size;
            count   = params->count;
            smb_tr2_find2_parse_entries(files_p, iter, count, eod);
        }
        
    }
}

static smb_message  *smb_trans2_find_first (smb_session *s, smb_tid tid, const char *pattern)
{
    smb_message           *msg;
    smb_trans2_req        tr2;
    smb_tr2_findfirst2    find;
    size_t                utf_pattern_len, tr2_bct, tr2_param_count;
    char                  *utf_pattern;
    int                   res;
    unsigned int          padding = 0;

    bdsm_assert(s != NULL && pattern != NULL);
    
    if(s != NULL && pattern != NULL){

        utf_pattern_len = smb_to_utf16(pattern, strlen(pattern) + 1, &utf_pattern);
        if (utf_pattern_len == 0)
            return NULL;

        tr2_bct = sizeof(smb_tr2_findfirst2) + utf_pattern_len;
        tr2_param_count = tr2_bct;
        tr2_bct += 3;
        // Adds padding at the end if necessary.
        while ((tr2_bct % 4) != 3)
        {
            padding++;
            tr2_bct++;
        }

        msg = smb_message_new(SMB_CMD_TRANS2);
        if (!msg) {
            free(utf_pattern);
            return NULL;
        }
        msg->packet->header.tid = tid;

        SMB_MSG_INIT_PKT(tr2);
        tr2.wct                = 15;
        tr2.max_param_count    = 10; // ?? Why not the same or 12 ?
        tr2.max_data_count     = 0xffff;;
        tr2.param_offset       = 68; // Offset of find_first_params in packet;
        tr2.data_count         = 0;
        tr2.data_offset        = 88; // Offset of pattern in packet
        tr2.setup_count        = 1;
        tr2.cmd                = SMB_TR2_FIND_FIRST;
        tr2.total_param_count = tr2_param_count;
        tr2.param_count       = tr2_param_count;
        tr2.bct = tr2_bct; //3 == padding
        SMB_MSG_PUT_PKT(msg, tr2);

        SMB_MSG_INIT_PKT(find);
        find.attrs     = SMB_FIND2_ATTR_DEFAULT;
        find.count     = 1366;     // ??
        find.flags     = SMB_FIND2_FLAG_CLOSE_EOS | SMB_FIND2_FLAG_RESUME;
        find.interest  = SMB_FIND2_INTEREST_BOTH_DIRECTORY_INFO;
        SMB_MSG_PUT_PKT(msg, find);
        smb_message_append(msg, utf_pattern, utf_pattern_len);
        while (padding--)
            smb_message_put8(msg, 0);

        res = smb_session_send_msg(s, msg);
        smb_message_destroy(msg);
        free(utf_pattern);

        if (!res)
        {
            BDSM_dbg("Unable to query pattern: %s\n", pattern);
            return NULL;
        }

        msg = smb_tr2_recv(s);
        return msg;
    }
    
     return NULL;
}

static smb_message  *smb_trans2_find_next (smb_session *s, smb_tid tid, uint16_t resume_key, uint16_t sid, const char *pattern)
{
    smb_message           *msg_find_next2 = NULL;
    smb_trans2_req        tr2_find_next2;
    smb_tr2_findnext2     find_next2;
    size_t                utf_pattern_len, tr2_bct, tr2_param_count;
    char                  *utf_pattern;
    int                   res;
    unsigned int          padding = 0;

    bdsm_assert(s != NULL && pattern != NULL);
    
    if(s != NULL && pattern != NULL){

        utf_pattern_len = smb_to_utf16(pattern, strlen(pattern) + 1, &utf_pattern);
        if (utf_pattern_len == 0)
            return NULL;

        tr2_bct = sizeof(smb_tr2_findnext2) + utf_pattern_len;
        tr2_param_count = tr2_bct;
        tr2_bct += 3;
        // Adds padding at the end if necessary.
        while ((tr2_bct % 4) != 3)
        {
            padding++;
            tr2_bct++;
        }

        msg_find_next2 = smb_message_new(SMB_CMD_TRANS2);
        if (!msg_find_next2)
        {
            free(utf_pattern);
            return NULL;
        }
        msg_find_next2->packet->header.tid = tid;

        SMB_MSG_INIT_PKT(tr2_find_next2);
        tr2_find_next2.wct                = 0x0f;
        tr2_find_next2.total_param_count  = tr2_param_count;
        tr2_find_next2.total_data_count   = 0x0000;
        tr2_find_next2.max_param_count    = 10; // ?? Why not the same or 12 ?
        tr2_find_next2.max_data_count     = 0xffff;
        //max_setup_count
        //reserved
        //flags
        //timeout
        //reserve2
        tr2_find_next2.param_count        = tr2_param_count;
        tr2_find_next2.param_offset       = 68; // Offset of find_next_params in packet;
        tr2_find_next2.data_count         = 0;
        tr2_find_next2.data_offset        = 88; // Offset of pattern in packet
        tr2_find_next2.setup_count        = 1;
        //reserve3
        tr2_find_next2.cmd                = SMB_TR2_FIND_NEXT;
        tr2_find_next2.bct = tr2_bct; //3 == padding
        SMB_MSG_PUT_PKT(msg_find_next2, tr2_find_next2);

        SMB_MSG_INIT_PKT(find_next2);
        find_next2.sid        = sid;
        find_next2.count      = 255;
        find_next2.interest   = SMB_FIND2_INTEREST_BOTH_DIRECTORY_INFO;
        find_next2.flags      = SMB_FIND2_FLAG_CLOSE_EOS|SMB_FIND2_FLAG_CONTINUE;
        find_next2.resume_key = resume_key;
        SMB_MSG_PUT_PKT(msg_find_next2, find_next2);
        smb_message_append(msg_find_next2, utf_pattern, utf_pattern_len);
        while (padding--)
            smb_message_put8(msg_find_next2, 0);

        res = smb_session_send_msg(s, msg_find_next2);
        smb_message_destroy(msg_find_next2);
        free(utf_pattern);

        if (!res)
        {
            BDSM_dbg("Unable to query pattern: %s\n", pattern);
            return NULL;
        }

        msg_find_next2 = smb_tr2_recv(s);
        return msg_find_next2;
    }
    return NULL;
}

smb_file  *smb_find(smb_session *s, smb_tid tid, const char *pattern)
{
    smb_file                  *files = NULL;
    smb_message               *msg;
    smb_trans2_resp           *tr2_resp;
    smb_tr2_findfirst2_params *findfirst2_params;
    smb_tr2_findnext2_params  *findnext2_params;
    bool                      end_of_search;
    uint16_t                  sid;
    uint16_t                  resume_key;
    uint16_t                  error_offset;

    bdsm_assert(s != NULL && pattern != NULL);
    
    if(s != NULL && pattern != NULL){

        // Send FIND_FIRST request
        msg = smb_trans2_find_first(s,tid,pattern);
        
        if (msg)
        {
            smb_find_first_parse(msg,&files);
            if (files)
            {
                // Check if we shall send a FIND_NEXT request
                tr2_resp          = (smb_trans2_resp *)msg->packet->payload;
                findfirst2_params = (smb_tr2_findfirst2_params *)tr2_resp->payload;

                sid               = findfirst2_params->id;
                end_of_search     = findfirst2_params->eos;
                resume_key        = findfirst2_params->last_name_offset;
                error_offset      = findfirst2_params->ea_error_offset;

                smb_message_destroy(msg);

                // Send FIND_NEXT queries until the find is finished
                // or until an error occurs
                while ((!end_of_search) && (error_offset == 0))
                {
                    msg = smb_trans2_find_next(s, tid, resume_key, sid, pattern);

                    if (msg)
                    {
                        // Update info for next FIND_NEXT query
                        tr2_resp         = (smb_trans2_resp *)msg->packet->payload;
                        findnext2_params = (smb_tr2_findnext2_params *)tr2_resp->payload;
                        end_of_search    = findnext2_params->eos;
                        resume_key       = findnext2_params->last_name_offset;
                        error_offset     = findnext2_params->ea_error_offset;

                        // parse the result for files
                        smb_find_next_parse(msg, &files);
                        smb_message_destroy(msg);

                        if (!files)
                        {
                            BDSM_dbg("Error during FIND_NEXT answer parsing\n");
                            end_of_search = true;
                        }
                    }
                    else
                    {
                        BDSM_dbg("Error during FIND_NEXT request\n");
                        smb_stat_list_destroy(files);
                        return NULL;
                    }
                }
            }
            else
            {
                BDSM_dbg("Error during FIND_FIRST answer parsing\n");
                smb_message_destroy(msg);
            }
        }
        else
        {
            BDSM_dbg("Error during FIND_FIRST request\n");
            smb_stat_list_destroy(files);
            smb_message_destroy(msg);
            return NULL;
        }

        return files;
    }
    return NULL;
}

/*
 * Query management
 */
    
smb_file  *smb_fstat_interest(smb_session *s, smb_tid tid, uint16_t interest, const char *path)
{
    smb_message           *msg, reply;
    smb_trans2_query_path_info_req tr2;
    smb_trans2_resp       *tr2_resp;
    smb_tr2_query         query;
    smb_tr2_basic_path_info     *info_basic;
    smb_tr2_standard_path_info  *info_standard;
    smb_file              *file;
    size_t                utf_path_len, msg_len;
    char                  *utf_path;
    int                   res, padding = 0;

    bdsm_assert(s != NULL && path != NULL);
    
    bdsm_assert(interest == SMB_FIND2_QUERY_FILE_BASIC_INFO || interest == SMB_FIND2_QUERY_FILE_STANDARD_INFO);

    bdsm_assert(smb_session_supports(s, SMB_SESSION_NTSMB));
    if (smb_session_supports(s, SMB_SESSION_NTSMB) == false)
    {
        return NULL;
    }

    if(s != NULL && path != NULL){

        utf_path_len = smb_to_utf16(path, strlen(path) + 1, &utf_path);
        if (utf_path_len == 0)
            return 0;

        msg_len   = sizeof(smb_trans2_req) + sizeof(smb_tr2_query);
        msg_len  += utf_path_len;
        if (msg_len %4)
            padding = 4 - msg_len % 4;

        msg = smb_message_new(SMB_CMD_TRANS2);
        if (!msg) {
            free(utf_path);
            return 0;
        }
        msg->packet->header.tid = tid;

        SMB_MSG_INIT_PKT(tr2);
        tr2.wct                = 15;
        tr2.total_param_count  = utf_path_len + sizeof(smb_tr2_query);
        tr2.param_count        = tr2.total_param_count;
        tr2.max_param_count    = 2; // ?? Why not the same or 12 ?
        tr2.max_data_count     = 40;
        tr2.param_offset       = 66; // Offset of find_first_params in packet;
        tr2.data_count         = 0;
        tr2.data_offset        = 0; // Offset of pattern in packet
        tr2.setup_count        = 1;
        tr2.cmd                = SMB_TR2_QUERY_PATH;
        tr2.bct                = sizeof(smb_tr2_query) + utf_path_len + padding + 1; // 1 - reserved
        SMB_MSG_PUT_PKT(msg, tr2);
        
        SMB_MSG_INIT_PKT(query);
        query.interest   = interest;
        SMB_MSG_PUT_PKT(msg, query);

        smb_message_append(msg, utf_path, utf_path_len);
        free(utf_path);

        // Adds padding at the end if necessary.
        while (padding--)
            smb_message_put8(msg, 0);

        res = smb_session_send_msg(s, msg);
        smb_message_destroy(msg);
        if (!res)
        {
            BDSM_dbg("Unable to query pattern: %s\n", path);
            return NULL;
        }

        if (!smb_session_recv_msg(s, &reply) ||
            !smb_session_check_nt_status(s, &reply))
        {
            BDSM_dbg("Unable to recv msg or failure for %s\n", path);
            return NULL;
        }
        
        file      = calloc(1, sizeof(smb_file));
        if (!file) {
            BDSM_dbg("Unable to create file for %s\n", path);
            return NULL;
        }
        
        bool isBasicFileInfo = (query.interest == SMB_FIND2_QUERY_FILE_BASIC_INFO);
        bool isStandardFileInfo = (query.interest == SMB_FIND2_QUERY_FILE_STANDARD_INFO);

        if (isBasicFileInfo && reply.payload_size < sizeof(smb_tr2_basic_path_info))
        {
            BDSM_dbg("[smb_fstat]Malformed message %s\n", path);
            return NULL;
        }
        
        if (isStandardFileInfo && reply.payload_size < sizeof(smb_tr2_standard_path_info))
        {
            BDSM_dbg("[smb_fstat]Malformed message %s\n", path);
            return NULL;
        }
        
        tr2_resp  = (smb_trans2_resp *)reply.packet->payload;
        
        if (isBasicFileInfo) {
            info_basic = (smb_tr2_basic_path_info *)(tr2_resp->payload + 4); //+4 is padding
            
            file->created     = info_basic->created;
            file->accessed    = info_basic->accessed;
            file->written     = info_basic->written;
            file->changed     = info_basic->changed;
            file->attr        = info_basic->attr;
            file->is_dir      = info_basic->attr & SMB_ATTR_DIR;
        }
        else if (isStandardFileInfo) {
            info_standard = (smb_tr2_standard_path_info *)(tr2_resp->payload + 4); //+4 is padding
            
            file->alloc_size     = info_standard->alloc_size;
            file->size           = info_standard->size;
            //file->link_count     = info_standard->link_count;
            //file->rm_pending     = info_standard->rm_pending;
            file->is_dir         = info_standard->is_dir;
        }
        else{
            BDSM_dbg("[smb_fstat]Unknown file info %s\n", path);
        }

        return file;
    }
    return NULL;
}

smb_file  *smb_fstat_basic(smb_session *s, smb_tid tid, const char *path)
{
    return smb_fstat_interest(s, tid, SMB_FIND2_QUERY_FILE_BASIC_INFO, path);
}

smb_file  *smb_fstat_standard(smb_session *s, smb_tid tid, const char *path)
{
    return smb_fstat_interest(s, tid, SMB_FIND2_QUERY_FILE_STANDARD_INFO, path);
}

//https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-cifs/847573c9-cbe6-4dcb-a0db-9b5af815759b
smb_file  *smb_fstat_query_info(smb_session *s, smb_tid tid, const char *path)
{
    smb_message           *req_msg, resp_msg;
    smb_query_path_info_req  req;
    smb_query_path_info_resp *resp;
    size_t                utf_pattern_len;
    char                  *utf_pattern;
    smb_file              *file;

    bdsm_assert(s != NULL && path != NULL);

    if(s != NULL && path != NULL){
        
        utf_pattern_len = smb_to_utf16(path, strlen(path) + 1, &utf_pattern);
        if (utf_pattern_len == 0)
            return NULL;

        req_msg = smb_message_new(SMB_CMD_QUERY_INFO);
        if (!req_msg)
        {
            free(utf_pattern);
            return NULL;
        }

        req_msg->packet->header.tid = (uint16_t)tid;

        SMB_MSG_INIT_PKT(req);
        req.wct              = 0x00; // Must be 0
        req.bct              = (uint16_t)(utf_pattern_len + 1);
        req.buffer_format    = 0x04; // Must be 4
        SMB_MSG_PUT_PKT(req_msg, req);
        smb_message_append(req_msg, utf_pattern, utf_pattern_len);

        smb_session_send_msg(s, req_msg);
        smb_message_destroy(req_msg);

        free(utf_pattern);

        if (!smb_session_recv_msg(s, &resp_msg)){
            BDSM_dbg("Unable to recv msg or failure for %s\n", path);
            return NULL;
        }

        if (!smb_session_check_nt_status(s, &resp_msg)){
            BDSM_dbg("Unable to recv msg or failure for %s\n", path);
            return NULL;
        }
        
        if (resp_msg.payload_size < sizeof(smb_query_path_info_resp))
        {
            BDSM_dbg("[smb_query_path_info]Malformed message %s\n", path);
            return NULL;
        }

        resp = (smb_query_path_info_resp *)resp_msg.packet->payload;
        if ((resp->wct) == 0) {
            BDSM_dbg("[smb_query_path_info]Malformed message wct == 0 %s\n", path);
            return NULL;
        }

        file = calloc(1, sizeof(smb_file));
        if (!file) {
            BDSM_dbg("Unable to create file for %s\n", path);
            return NULL;
        }
        
        uint64_t time_zone = smb_session_server_time_zone(s);
        
        file->written_dep = resp->written + time_zone;
        file->attr        = resp->attr;
        file->is_dir      = resp->attr & SMB_ATTR_DIR;
        file->size        = resp->size;
        file->alloc_size  = resp->size;
        
        return file;
    }
    return NULL;
}


smb_file  *smb_fstat(smb_session *s, smb_tid tid, const char *path)
{
   
    if (smb_session_supports(s, SMB_SESSION_NTSMB) == false)
    {
        //return deprecated query_info (needed for old NAS)
        return smb_fstat_query_info(s,tid,path);
    }

    smb_file *file      = calloc(1, sizeof(smb_file));
    if (!file) {
        BDSM_dbg("Unable to create file for %s\n", path);
        return NULL;
    }
    
    smb_stat statBasic = smb_fstat_basic(s, tid, path);
    
    if (statBasic != NULL) {
        file->created     = statBasic->created;
        file->accessed    = statBasic->accessed;
        file->written     = statBasic->written;
        file->changed     = statBasic->changed;
        file->attr        = statBasic->attr;
        file->is_dir      = statBasic->is_dir;
        
        smb_stat_destroy(statBasic);
    }
    else {
        BDSM_dbg("Unable to recv basic info for file %s\n", path);
        smb_stat_destroy(file);
        
        return NULL;
    }
    
    smb_stat statStandard = smb_fstat_standard(s, tid, path);
    
    if (statStandard != NULL) {
        file->alloc_size     = statStandard->alloc_size;
        file->size           = statStandard->size;

        smb_stat_destroy(statStandard);
    }
    
//#ifdef DEBUG
//    smb_stat statDeprecated =  smb_fstat_query_info(s,tid,path);
//    if (statDeprecated != NULL) {
//        bdsm_assert(statDeprecated->attr == file->attr);
//        bdsm_assert(statDeprecated->is_dir == file->is_dir);
//        bdsm_assert(statDeprecated->alloc_size == file->alloc_size);
//        smb_stat_destroy(statDeprecated);
//    }
//#endif
    
    return file;
}


