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

/* wtls_statesupport.h
 *
 * Nikos Balkanas, Inaccess Networks (2009)
 */
#ifndef WTLS_STATESUPPORT_H
#define WTLS_STATESUPPORT_H

#include "gwlib/gwlib.h"
#include "wtls_pdu.h"
#include "wtls.h"
#define KEYSIZE_MAX 2048
#define NOT_EXPORTABLE 0
#define EXPORTABLE 1
#define BLOCK 0
#define STREAM 1
#define ALG EVP_rc5_32_12_16_cbc()

/* These enums and tables are pulled straight from the WTLS appendices.
   Go and have a look at them if these aren't particularly clear. Obviously, since NULL
   is a builtin, and since RSA/MD5/SHA are all macros referenced by the openssl
   libraries, the names have had to be slightly altered to protect the innocent :->
*/

enum key_exchange_suites {
        NULL_keyxchg,
        SHARED_SECRET,
        DH_anon,
        DH_anon_512,
        RSA_anon,
        RSA_anon_512,
        RSA_anon_768,
        RSA_NOLIMIT,
        RSA_512,
        RSA_768,
        ECDH_anon,
        ECDH_anon_113,
        ECDH_anon_131,
        ECDH_ECDSA_NOLIMIT
};

enum bulk_algorithms {
        NULL_bulk,
        RC5_CBC_40,
        RC5_CBC_56,
        RC5_CBC,
        DES_CBC_40,
        DES_CBC,
        TRIPLE_DES_CBC_EDE,
        IDEA_CBC_40,
        IDEA_CBC_56,
        IDEA_CBC
};

enum keyed_macs {
        SHA_0,
        SHA_40,
        SHA_80,
        SHA_NOLIMIT,
        SHA_XOR_40,
        MD5_40,
        MD5_80,
        MD5_NOLIMIT
};

typedef struct {
   const char *title;
        int key_size_limit;
} keyxchg_table_t;

typedef struct {
   const char *title;
        int is_exportable;
        int block_or_stream;
        int key_material;
        int expanded_key_material;
        int effective_key_bits;
        int iv_size;
        int block_size;
} bulk_table_t;

typedef struct {
   const char *title;
        int key_size;
        int mac_size;
} hash_table_t;

Octstr *wtls_calculate_prf(Octstr * secret, Octstr * label,
            Octstr * seed, int byteLength,
            WTLSMachine * wtls_machine);
RSAPublicKey *wtls_get_rsapublickey(void);
Random *wtls_get_random(void);
Octstr *wtls_decrypt(wtls_Payload * payload, WTLSMachine * wtls_machine);
Octstr *wtls_encrypt(Octstr * buffer, WTLSMachine * wtls_machine,
           int recordType);
Octstr *wtls_decrypt_key(int type, Octstr * encryptedData);
void wtls_decrypt_pdu_list(WTLSMachine * wtls_machine, List * pdu_list);
Octstr *wtls_hash(Octstr * inputData, WTLSMachine * wtls_machine);

/* The wtls_choose* functions implement the decision making process behind the
   protocol negotiations in wtls. */
CipherSuite *wtls_choose_ciphersuite(List * ciphersuites);
int wtls_choose_clientkeyid(List * clientKeyIDs, int *algo);
int wtls_choose_snmode(int snmode);
int wtls_choose_krefresh(int krefresh);

/* The *_are_identical functions all return 1 if the packets match the condition as
 * expressed in the function name. As each packet can contain a "list" of pdus, we
 * need to search that list and return whether or not they contain identical pdus as listed.
 * On failure, they will return non-zero
 */
int clienthellos_are_identical(List * pdu_list, List * last_received_packet);
int certifcateverifys_are_identical(List * pdu_list, List 
   *last_received_packet);
int certificates_are_identical(List * pdu_list, List * last_received_packet);
int clientkeyexchanges_are_identical(List * pdu_list, List 
   *last_received_packet);
int changecipherspecs_are_identical(List * pdu_list, List 
   *last_received_packet);
int finishes_are_indentical(List * pdu_list, List * last_received_packet);

/* the packet_contains_* functions all return 1 if the packet contains a pdu of the type
 * expressed in the function name.
 */
int packet_contains_changecipherspec(List * pdu_list);
int packet_contains_finished(List * pdu_list);
int packet_contains_optional_stuff(List * pdu_list);
int packet_contains_userdata(List * pdu_list);
int packet_contains_clienthello(List * pdu_list);

/* the is_type functions return 1 if all pdus in the list are of said type.
 * Else return 0.
 */
int packet_is_application_data(List * pdu_list);

/* the is_*_alert functions return 1 if the packet is a pdu of the type expressed in the
 * function name.
 */
int is_critical_alert(List * pdu_list, WTLSMachine * wtls_machine);
int is_warning_alert(List * pdu_list, WTLSMachine * wtls_machine);

void calculate_client_key_block(WTLSMachine * wtls_machine);
void calculate_server_key_block(WTLSMachine * wtls_machine);

/* Printing naming functions. Free result from calling program. */

void cipherName(char *name, int cipher);
void keyName(char *name, int key);
void macName(char *name, int mac);
void alertName(char *name, int alert);
void pduName(char *name, int pdu);
void hsName(char *name, int handshake);

#endif /* WTLS_STATESUPPORT_H */
