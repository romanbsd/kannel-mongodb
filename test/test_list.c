/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2013 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

/*
 * test_list.c - test List objects
 *
 * Stipe Tolj <stolj@kannel.org>
 */


#include "gwlib/gwlib.h"

#define HUGE_SIZE 20


static int my_sort_cmp(const void *a, const void *b)
{
    const Octstr *fa = a;
    const Octstr *fb = b;
    
    return octstr_compare(fa, fb);
}


int main(void)
{
    List *list;
    char id[UUID_STR_LEN + 1];
    int i;
     
    gwlib_init();

    debug("",0,"List performance test.");
    list = gwlist_create();
        
    /* generate UUIDs and populate the list */
    debug("", 0, "Creating %d UUIDs for the list.", HUGE_SIZE);
    for (i = 0; i < HUGE_SIZE; i++) {
        Octstr *os;
        uuid_t uid;
        uuid_generate(uid);
        uuid_unparse(uid, id);
        os = octstr_create(id);
        gwlist_append(list, os);
        uuid_clear(uid);
    }
    debug("",0,"Objects in the list: %ld", gwlist_len(list));

    /* try to sort */
    debug("",0,"Sorting.");
    gwlist_sort(list, my_sort_cmp);
    debug("",0,"Sorting done.");
    for (i = 0; i < HUGE_SIZE; i++) {
        Octstr *os = gwlist_get(list, i);
        debug("",0,"After sort: %s %d", octstr_get_cstr(os), i);
    }       
    
    gwlist_destroy(list, octstr_destroy_item);

    gwlib_shutdown();
    return 0;
}
