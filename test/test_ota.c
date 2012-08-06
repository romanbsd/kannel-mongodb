/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2012 Kannel Group  
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
 * test_ota.c: A simple program to test ota tokenizer
 *
 * By Aarno Syv�nen for Wiral Ltd
 */

#include <stdio.h>

#include "gwlib/gwlib.h"
#include "gw/ota_compiler.h"

Octstr *charset = NULL;
Octstr *file_name = NULL;

static void help (void)
{
    info(0, "Usage test_ota [options] ota_source");
    info(0, "where options are");
    info(0, "-h - print this text");
    info(0, "-f <file> - output binary to file");
    info(0, "-c <charset> - charset given by http");
    info(0, "-v <level> - set log level for stderr logging");
}

int main(int argc, char **argv)
{
    int opt, file, have_charset, ret;
    FILE *fp;
    Octstr *ota_doc, *ota_binary;

    file = 0;
    have_charset = 0;
    fp = NULL;

    gwlib_init();

    while ((opt = getopt(argc, argv, "hf:c:v:")) != EOF) {
        switch (opt) {
            case 'h':
                help();
                exit(1);
                break;

            case 'f':
                file = 1;
                file_name = octstr_create(optarg);
                fp = fopen(optarg, "a");
                if (fp == NULL)
                    panic(0, "Cannot open output file");
                break;

            case 'c':
                have_charset = 1;
                charset = octstr_create(optarg);
                break;

            case 'v':
                log_set_output_level(atoi(optarg));
                break;

            case '?':
            default:
                error(0, "Invalid option %c", opt);
                help();
                panic(0, "Stopping");
                break;
        }
    }

    if (optind >= argc) {
        error(0, "Missing arguments");
        help();
        panic(0, "Stopping");
    }

    ota_doc = octstr_read_file(argv[optind]);
    if (ota_doc == NULL)
        panic(0, "Cannot read the ota document");

    if (!have_charset)
        charset = NULL;
    
    /* run compiler */
    ret = ota_compile(ota_doc, charset, &ota_binary);
    debug("test.ota", 0, "ota compiler returned %d", ret);

    if (ret == 0) {
        if (fp == NULL)
            fp = stdout;

        if (file) {
            octstr_print(fp, ota_binary);
        } else {
            debug("test.ota", 0, "ota binary was");
            octstr_dump(ota_binary, 0);
        }
    }

    if (have_charset)
        octstr_destroy(charset);
    if (file) {
        fclose(fp);
        octstr_destroy(file_name);
    }
    
    octstr_destroy(ota_doc);
    if (ret == 0)
        octstr_destroy(ota_binary);

    gwlib_shutdown();
    return 0;
}
