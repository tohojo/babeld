#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <openssl/ripemd.h>

#include "hmactrailer.h"
#include "babeld.h"
#include "util.h"
#include "net.h"
#include "interface.h"
#include "source.h"
#include "neighbour.h"
#include "route.h"
#include "kernel.h"
#include "xroute.h"
#include "resend.h"
#include "message.h"
#include "configuration.h"

unsigned char *key = (unsigned char *)"Ala ma kota";
struct pseudo_header head = {0,(unsigned char *)"src", (unsigned char *)"dest",
			     0,0};

struct pseudo_header
set_head()
{
    return head;
}

int
compute_hmac(unsigned char *packet_header, unsigned char *hmac,
	     const unsigned char *body, int bodylen, int hash_type)
{
    SHA_CTX inner_ctx;
    SHA_CTX outer_ctx;
    SHA_CTX key_ctx;
    unsigned char inner_hash[SHA_DIGEST_LENGTH];
    unsigned char key_hash[SHA_DIGEST_LENGTH];
    uint8_t inner_key_pad[SHA1_BLOCK_SIZE];
    uint8_t outer_key_pad[SHA1_BLOCK_SIZE];
    int i;
    int keylen;


    switch(hash_type) {
        case 0:
	  keylen = sizeof(key);
	  memcpy(key_hash, key, keylen);
	  if(keylen > SHA1_BLOCK_SIZE) {
	      SHA1_Init(&key_ctx);
	      SHA1_Update(&key_ctx, key, keylen);
	      SHA1_Final(key_hash, &key_ctx);
	      keylen = SHA_DIGEST_LENGTH;
	  }
	  for(i = 0; i < keylen; i++) {
	    inner_key_pad[i] = key_hash[i]^0x36;
            }
            for(i = keylen; i < SHA1_BLOCK_SIZE; i++) {
                inner_key_pad[i] = 0x36;
            }
            SHA1_Init(&inner_ctx);
            SHA1_Update(&inner_ctx, inner_key_pad, SHA1_BLOCK_SIZE);
            SHA1_Update(&inner_ctx, head.addr_dest, sizeof(head.addr_dest));
            SHA1_Update(&inner_ctx, head.addr_src, sizeof(head.addr_src));
            SHA1_Update(&inner_ctx, packet_header, 4);
            SHA1_Update(&inner_ctx, body, bodylen);
            SHA1_Final(inner_hash, &inner_ctx);

            for(i = 0; i < keylen; i++) {
	      outer_key_pad[i] = key_hash[i]^0x5c;
            }
            for(i = keylen; i < SHA1_BLOCK_SIZE; i++) {
                outer_key_pad[i] = 0x5c;
            }
            SHA1_Init(&outer_ctx);
            SHA1_Update(&outer_ctx, outer_key_pad, SHA1_BLOCK_SIZE);
            SHA1_Update(&outer_ctx, inner_hash, SHA_DIGEST_LENGTH);
            SHA1_Final(hmac, &outer_ctx);
            return SHA_DIGEST_LENGTH;
        case 1:
            RIPEMD160(body, bodylen, hmac);
            return RIPEMD160_DIGEST_LENGTH;
        default:
            return -1;
    }
}

int
add_hmac(unsigned char *packet_header, char *buf, int buf_len,
	 int nb_hmac)
{
    int i = buf_len;
    int hmaclen;
    int hmac_space = 0;
    printf("add_hmac\n");
    while (nb_hmac > 0){
        buf[i] = HMAC_TYPE;
	buf[i+1] = DIGEST_LEN;
	hmaclen = compute_hmac(packet_header, (unsigned char *)buf + i + 2,
			       (unsigned char *)buf, buf_len, 0);
	if(hmaclen < 0){
	    return -1;
	}
	i += hmaclen + 2;
	hmac_space += hmaclen + 2;
	nb_hmac --;
    }
    return hmac_space;
}


int
hmac_compare(const unsigned char *packet, int bodylen,
	     const unsigned char *hmac, int hmaclen)
{
    int j;
    unsigned char true_hmac[DIGEST_LEN];
    unsigned char packet_header[4] = {packet[0], packet[1], packet[2],
				      packet[3]};
    int true_hmaclen = compute_hmac(packet_header, true_hmac,
				    packet + 4, bodylen, 0);
    printf("hmac_compare: %d.", hmaclen);
    for(j = 0; j < hmaclen; j++) {
      printf("%02x", hmac[j]);
    }
    printf(" %d.", true_hmaclen);
    for(j = 0; j < true_hmaclen; j++) {
      printf("%02x", true_hmac[j]);
    }
    printf("\n");
    if(true_hmaclen != hmaclen) {
       fprintf(stderr, "Length inconsistency of two hmacs.\n");
		return -1;
    }
    if(memcmp(true_hmac, hmac, hmaclen)==0)
	return 1;
    return 0;
}

int
check_hmac(const unsigned char *packet, int packetlen, int bodylen)
{
    int i = bodylen + 4;
    int hmaclen;
    while(i < packetlen){
        hmaclen = packet [i+1];
        if(packet[i] == HMAC_TYPE){
	    if(hmaclen + i > packetlen){
	        fprintf(stderr, "Received truncated hmac.\n");
		return -1;
	    }
	    if(hmac_compare(packet, bodylen, packet + i + 2 , hmaclen)){
		printf("accept hmac\n");
		return 1;
	    }
	}
	i += hmaclen + 2;
	/* on suppose qu'on a que des TLV dans le trailer*/
    }
    return 0;
}
