/*
 * Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <gst/gst.h>
#include <glib.h>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <sstream>
#include "gstnvdsmeta.h"
#include "nvds_analytics_meta.h"
#include "analytics.h"
extern "C" void
analytics_custom_parse_direction_obj_data (NvDsObjectMeta *obj_meta, AnalyticsUserMeta *data)
{           std::stringstream out_string;
            data->lcc_cnt_entry = 0;
	        data->lcc_cnt_exit = 0;
	        data->lccum_cnt = 0;
            for (NvDsMetaList *l_user_meta = obj_meta->obj_user_meta_list; l_user_meta != NULL;
                    l_user_meta = l_user_meta->next) {
                NvDsUserMeta *user_meta = (NvDsUserMeta *) (l_user_meta->data);
                 if(user_meta->base_meta.meta_type == NVDS_USER_OBJ_META_NVDSANALYTICS)
                {
                    NvDsAnalyticsObjInfo * user_meta_data =
                        (NvDsAnalyticsObjInfo *)user_meta->user_meta_data;
                    if (user_meta_data->dirStatus.length()){
                        data->direction = user_meta_data->dirStatus.c_str();
                        out_string << " object " << obj_meta->object_id <<
                            " is moving in " <<  user_meta_data->dirStatus;
                            
                    }
                }
            }
            //out_string << " object ";
            if (out_string.str().size()){
            g_print (    out_string.str().c_str());}
			
        
               

}