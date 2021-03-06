/**********************************************************************
 *                                                                    *
 * Created by Adam Brockett                                           *
 * Modified by Joel Pelaez Jorge                                      *
 *                                                                    *
 * Copyright (c) 2010 Adam Brockett                                   *
 *                                                                    *
 * Redistribution and use in source and binary forms, with or without *
 * modification is allowed.                                           *
 *                                                                    *
 * But if you let me know you're using my code, that would be freaking*
 * sweet.                                                             *
 *                                                                    *
 **********************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <gmp.h>

#define MODULUS_SIZE 1024                   /* This is the number of bits we want in the modulus */
#define PBLOCK_SIZE ((MODULUS_SIZE/8)-1)    /* This is the max size of a block that gets encrypted */
#define EBLOCK_SIZE (MODULUS_SIZE/8)        /* This is the size of a block that gets decrypted */
#define BLOCK_SIZE  EBLOCK_SIZE             /* This is the defautl size used in text en/decrypt functions */
#define BUFFER_SIZE ((MODULUS_SIZE/8) / 2)  /* This is the number of bytes in n and p */

typedef struct {
    mpz_t n; /* Modulus */
    mpz_t e; /* Public Exponent */
} public_key;

typedef struct {
    mpz_t n;  /* Modulus */
    mpz_t e;  /* Public Exponent */
    mpz_t d;  /* Private Exponent */
    mpz_t p;  /* Starting prime p */
    mpz_t q;  /* Starting prime q */
    mpz_t dp; /* Private exponent mod p */
    mpz_t dq; /* Private exponent mod q */
    mpz_t qi; /* Inverse of q */
} private_key;

void print_hex(char* arr, int len)
{
    int i;
    for(i = 0; i < len; i++)
        printf("%02x", (unsigned char) arr[i]); 
}

/* NOTE: Assumes mpz_t's are initted in ku and kp */
void generate_keys(private_key* ku, public_key* kp)
{
    char buf[BUFFER_SIZE];
    int i;
    mpz_t phi; mpz_init(phi);
    mpz_t tmp1; mpz_init(tmp1);
    mpz_t tmp2; mpz_init(tmp2);

    srand(time(NULL));

    /* Insetead of selecting e st. gcd(phi, e) = 1; 1 < e < phi, lets choose e
     * first then pick p,q st. gcd(e, p-1) = gcd(e, q-1) = 1 */
    // We'll set e globally.  I've seen suggestions to use primes like 3, 17 or 
    // 65537, as they make coming calculations faster.  Lets use 3.
    mpz_set_ui(ku->e, 65537); 

    /* Select p and q */
    /* Start with p */
    // Set the bits of tmp randomly
    for(i = 0; i < BUFFER_SIZE; i++)
        buf[i] = rand() % 0xFF; 
    // Set the top two bits to 1 to ensure int(tmp) is relatively large
    buf[0] |= 0xC0;
    // Set the bottom bit to 1 to ensure int(tmp) is odd (better for finding primes)
    buf[BUFFER_SIZE - 1] |= 0x01;
    // Interpret this char buffer as an int
    mpz_import(tmp1, BUFFER_SIZE, 1, sizeof(buf[0]), 0, 0, buf);
    // Pick the next prime starting from that random number
    mpz_nextprime(ku->p, tmp1);
    /* Make sure this is a good choice*/
    mpz_mod(tmp2, ku->p, ku->e);        /* If p mod e == 1, gcd(phi, e) != 1 */
    while(!mpz_cmp_ui(tmp2, 1))         
    {
        mpz_nextprime(ku->p, ku->p);    /* so choose the next prime */
        mpz_mod(tmp2, ku->p, ku->e);
    }

    /* Now select q */
    do {
        for(i = 0; i < BUFFER_SIZE; i++)
            buf[i] = rand() % 0xFF; 
        // Set the top two bits to 1 to ensure int(tmp) is relatively large
        buf[0] |= 0xC0;
        // Set the bottom bit to 1 to ensure int(tmp) is odd
        buf[BUFFER_SIZE - 1] |= 0x01;
        // Interpret this char buffer as an int
        mpz_import(tmp1, (BUFFER_SIZE), 1, sizeof(buf[0]), 0, 0, buf);
        // Pick the next prime starting from that random number
        mpz_nextprime(ku->q, tmp1);
        mpz_mod(tmp2, ku->q, ku->e);
        while(!mpz_cmp_ui(tmp2, 1))
        {
            mpz_nextprime(ku->q, ku->q);
            mpz_mod(tmp2, ku->q, ku->e);
        }
    } while(mpz_cmp(ku->p, ku->q) == 0); /* If we have identical primes (unlikely), try again */

    /* Calculate n = p x q */
    mpz_mul(ku->n, ku->p, ku->q);

    /* Compute phi(n) = (p-1)(q-1) */
    mpz_sub_ui(tmp1, ku->p, 1);
    mpz_sub_ui(tmp2, ku->q, 1);
    mpz_mul(phi, tmp1, tmp2);

    /* Calculate d (multiplicative inverse of e mod phi) */
    if(mpz_invert(ku->d, ku->e, phi) == 0)
    {
        mpz_gcd(tmp1, ku->e, phi);
        printf("gcd(e, phi) = [%s]\n", mpz_get_str(NULL, 16, tmp1));
        printf("Invert failed\n");
    }

    /* Calculate values for Chinese remainder algorithm */
    mpz_sub_ui(tmp1, ku->p, 1);
    mpz_sub_ui(tmp2, ku->q, 1);
    mpz_mod(ku->dp, ku->d, tmp1);
    mpz_mod(ku->dq, ku->d, tmp2);
    mpz_invert(ku->qi, ku->q, ku->p);

    /* Set public key */
    mpz_set(kp->e, ku->e);
    mpz_set(kp->n, ku->n);

    return;
}

void block_encrypt(mpz_t C, mpz_t M, public_key kp)
{
    /* C = M^e mod n */
    mpz_powm(C, M, kp.e, kp.n);
    return;
}

int encrypt(char cipher[], char message[], int length, public_key kp)
{
    /* Its probably overkill, but I implemented PKCS#1v1.5 paging
     * Encoded message block is of the form:
     * EMB = 00 || 02 || PS || 00 || D
     * Where || is concatenation, D is the message, and PS is a string of
     * (block_size-|D|-3) non-zero, randomly generated bytes
     * |D| must be less than block_size - 11, which means we have at least 8
     * bytes of PS
     */
    int block_count = 0;
    int prog = length;
    char mess_block[BLOCK_SIZE];
    mpz_t m; mpz_init(m);
    mpz_t c; mpz_init(c);

    while(prog > 0)
    {
        int i = 0;
        int d_len = (prog >= (BLOCK_SIZE - 11)) ? BLOCK_SIZE - 11 : prog;

        /* Construct the header */
        mess_block[i++] = 0x00;
        mess_block[i++] = 0x02;
        while(i < (BLOCK_SIZE - d_len - 1))
            mess_block[i++] = (rand() % (0xFF - 1)) + 1; 
        mess_block[i++] = 0x00;

        /* Copy in the message */
        memcpy(mess_block + i, message + (length - prog), d_len);
        
        // Convert bytestream to integer 
        mpz_import(m, BLOCK_SIZE, 1, sizeof(mess_block[0]), 0, 0, mess_block);
        // Perform encryption on that block
        block_encrypt(c, m, kp);

        // Calculate cipher write offset to take into account that we want to
        // pad with zeros in the front if the number we get back has fewer bits
        // than BLOCK_SIZE
        int off = block_count * BLOCK_SIZE;         // Base offset to start of this block
        off += (BLOCK_SIZE - (mpz_sizeinbase(c, 2) + 8 - 1)/8); // See manual for mpz_export
        
        // Pull out bytestream of ciphertext
        mpz_export(cipher + off, NULL, 1, sizeof(char), 0, 0, c);

        block_count++;
        prog -= d_len;
    }
    return block_count * BLOCK_SIZE;
} 

void block_decrypt(mpz_t M, mpz_t C, private_key ku)
{
    /* mpz_powm(M, C, ku.d, ku.n); */
    /* use Chinese remainder algorithm */
    mpz_t m1;
    mpz_t m2;
    mpz_t h;
    mpz_t tmp1;
    mpz_t tmp2;
    mpz_init(m1);
    mpz_init(m2);
    mpz_init(h);
    mpz_init(tmp1);
    mpz_init(tmp2);
    /* m1 = M^dp mod p */
    mpz_powm(m1, C, ku.dp, ku.p);
    /* m2 = M^dq mod q */
    mpz_powm(m2, C, ku.dq, ku.q);

    /* h = qi (m1 - m2) mod p */
    mpz_sub (tmp1, m1, m2);
    mpz_mul (tmp2, ku.qi, tmp1);
    mpz_mod (h, tmp2, ku.p);

    /* m = m2 + hq */
    mpz_mul (tmp1, h, ku.q);
    mpz_add (M, m2, tmp1);
    return;
}

int decrypt(char* message, char* cipher, int length, private_key ku)
{
    int msg_idx = 0;
    char buf[BLOCK_SIZE];
    *(long long*)buf = 0ll;
    mpz_t c; mpz_init(c);
    mpz_t m; mpz_init(m);

    int i;
    for(i = 0; i < (length / BLOCK_SIZE); i++)
    {
        // Pull block into mpz_t
        mpz_import(c, BLOCK_SIZE, 1, sizeof(char), 0, 0, cipher + i * BLOCK_SIZE);
        // Decrypt block
        block_decrypt(m, c, ku);

        // Calculate message write offset to take into account that we want to
        // pad with zeros in the front if the number we get back has fewer bits
        // than BLOCK_SIZE
        int off = (BLOCK_SIZE - (mpz_sizeinbase(m, 2) + 8 - 1)/8); // See manual for mpz_export
        // Convert back to bitstream
        mpz_export(buf + off, NULL, 1, sizeof(char), 0, 0, m);

        // Now we just need to lop off top padding before memcpy-ing to message
        // We know the first 2 bytes are 0x00 and 0x02, so manually skip those
        // After that, increment forward till we see a zero byte
        int j;
        for(j = 2; ((buf[j] != 0) && (j < BLOCK_SIZE)); j++);
        j++;        // Skip the 00 byte

        /* Copy over the message part of the plaintext to the message return var */
        memcpy(message + msg_idx, buf + j, BLOCK_SIZE - j);

        msg_idx += BLOCK_SIZE - j; 
    } 
    return msg_idx;
}

int main(int argc, char** argv)
{
    int i, j, k;
    char* message = "Hello RSA";
    char dmessage[BLOCK_SIZE];  memset(dmessage, 0, sizeof(dmessage));
    char emessage[BLOCK_SIZE];  memset(emessage, 0, sizeof(emessage));
    mpz_t M;  mpz_init(M);
    mpz_t C;  mpz_init(C);
    mpz_t DC;  mpz_init(DC);
    private_key ku;
    public_key kp;

    // Use custom message
    if ((argc >= 2) && (strlen(argv[1]) <= (BLOCK_SIZE - 11)))
       message = argv[1];

    // Initialize public key
    mpz_init(kp.n);
    mpz_init(kp.e); 
    // Initialize private key
    mpz_init(ku.n); 
    mpz_init(ku.e); 
    mpz_init(ku.d); 
    mpz_init(ku.p); 
    mpz_init(ku.q); 
    mpz_init(ku.dp);
    mpz_init(ku.dq);
    mpz_init(ku.qi);

    generate_keys(&ku, &kp);
    printf("---------------Private Key-----------------\n");
    printf("kp.n is\t\t[%s]\n", mpz_get_str(NULL, 16, kp.n));
    printf("kp.e is\t\t[%s]\n", mpz_get_str(NULL, 16, kp.e));
    printf("---------------Public Key------------------\n");
    printf("ku.n is\t\t[%s]\n", mpz_get_str(NULL, 16, ku.n));
    printf("ku.e is\t\t[%s]\n", mpz_get_str(NULL, 16, ku.e));
    printf("ku.d is\t\t[%s]\n", mpz_get_str(NULL, 16, ku.d));
    printf("ku.p is\t\t[%s]\n", mpz_get_str(NULL, 16, ku.p));
    printf("ku.q is\t\t[%s]\n", mpz_get_str(NULL, 16, ku.q));

    // Encrypt a block data with size in bytes (BLOCK_SIZE - 1) and decrypt it
    char buf[PBLOCK_SIZE]; 
    for(i = 0; i < PBLOCK_SIZE; i++)
        buf[i] = rand() % 0xFF;

    mpz_import(M, (PBLOCK_SIZE), 1, sizeof(buf[0]), 0, 0, buf);
    printf("original is\t[%s]\n", mpz_get_str(NULL, 16, M)); 
    block_encrypt(C, M, kp);
    printf("encrypted is\t[%s]\n", mpz_get_str(NULL, 16, C));
    block_decrypt(DC, C, ku);
    printf("decrypted is\t[%s]\n", mpz_get_str(NULL, 16, DC));

    // Check 128 other integers that such (MODULUS_SIZE - 1), encrypt and decrypt.
    printf("Checking 128 other encrypts\n");
    for (j = 0; j < 128; j++)
    {
        k = 0; // Check if any encrypt fail.

        for(i = 0; i < BLOCK_SIZE; i++)
            buf[i] = rand() % 0xFF;
        buf[0] &= 0x7F; // Set last bit to zero to such (bits N - 1)

        mpz_import(M, (BLOCK_SIZE), 1, sizeof(buf[0]), 0, 0, buf);
        block_encrypt(C, M, kp);
        block_decrypt(DC, C, ku);
        if (mpz_cmp (M, DC)) {
            printf ("Error in test: %d\n", j);
            k = 1;
        }
    }

    if (k)
        printf("Encrypt 128 blocks test: fail\n");
    else
        printf("Encrypt 128 blocks test: OK\n");

    // Encrypt a text string, check block size and decrypt it.
    printf("original message:\t\t%s\n", message);

    k = encrypt(emessage, message, strlen(message), kp);

    printf("encrypted message length:\t%d\n", k);

    decrypt(dmessage, emessage, BLOCK_SIZE, ku);

    printf("decrypted message:\t\t%s\n", dmessage);

    if (!strcmp(message,dmessage))
        printf("Message en/decyprted successful\n");
    else
        printf("Message en/decyprted failed\n");

    printf("Modulus size: %d bits - %d bytes\n", mpz_sizeinbase(kp.n, 2), mpz_sizeinbase(kp.n, 256));

    return 0;
}
