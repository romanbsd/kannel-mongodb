/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2014 Kannel Group  
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

#ifndef WBMP_H
#define WBMP_H

/* WBMP - Wireless Bitmap
 *
 * Kalle Marjola 1999 for WapIT Ltd.
 *
 * functions to store WBMPs and and create Octet strings from them
 */

#include "gwlib/gwlib.h"


/* extension header parameters... not implemented/supported in any WBMP
 * yet... but for future reference, although I'm quite sure there is
 * something wrong in these...
 */
typedef struct extparam {
    Octet	bitfield;	/* if bitfield additional data */
    /* or */
    char	param[9];	/* parameter */
    char	value[17];	/* and associated value */
} ExtParam;
    
/* WBMP - wireless bitmap format
 *
 * structure to define wireless bitmap - not complete!
 */
typedef struct wbmp {
    MultibyteInt     	type_field;
    Octet	    	fix_header_field;
    /* extension header is a bit more complicated thing that what is
     * represented here but the spesification is a bit obfuscated one
     * and they are not yet used to anything, so it is left undefined
     */
    ExtParam		*ext_header_field;
    int			exthdr_count;	/* total # of ext headers */
    MultibyteInt	width;
    MultibyteInt	height;
    Octet		*main_image;
    Octet		**animated_image;
    int			animimg_count;	/* total # of animated images */
} WBMP;

/* create a new empty WBMP struct. Return a pointer to it or NULL if
 * operation fails
 */
WBMP *wbmp_create_empty(void);


/* delete given WBMP, including freeing the pixmap */
void wbmp_delete(WBMP *pic);


#define NEGATIVE	1	/* source has white=0, black=1 */
#define REVERSE		2	/* source has righmost as most significant */

/* create a new bitmap
 *
 * type: 0 (B/W, Uncompressed bitmap) WBMP - the only type currently
 *  specificated.
 *
 * width and height are size of the bitmap,
 * data is the entire bitmap; from left-top corner to righ-bottom;
 * if the width is not dividable by 8, the rest of the row is padded with
 * zeros. bytes are ordered big-endian
 *
 * target: black=0, white=1, most significant leftmost
 *
 * You can generate raw bitmap in Linux (RH 6.0) with following line:
 * %> convert -monochrome input_file target.mono
 *
 * ..which then requires flags REVERSE and NEGATIVE
 *
 * return pointer to created WBMP, or NULL if fails
 */
WBMP *wbmp_create(int type, int width, int height, Octet *data, int flags);

/* create Octet stream out of given WBMP
 * return the length of stream, *stream is set to new stream which must
 * be freed by the caller
 */
int wbmp_create_stream(WBMP *pic, Octet **stream);


#endif
