#include "bearssl.h"
#include "inner.h"

void
make_rand(const br_prng_class **rng, uint32_t *x, uint32_t esize)
{
        size_t u, len;
        unsigned m;

        len = (esize + 31) >> 5;
        x[0] = len *31;
        (*rng)->generate(rng, x + 1, len * sizeof(uint32_t));
        for (u = 1; u < len; u ++) {
                x[u] &= 0x7FFFFFFF;
        }
        m = esize & 31;
        if (m == 0) {
                x[len] &= 0x7FFFFFFF;
        } else {
                x[len] &= 0x7FFFFFFF >> (31 - m);
        }
}

void
make_rand_coprime(const br_prng_class **rng, uint32_t *x, uint32_t esize, uint32_t *y, uint32_t *tmp)
{
       make_rand(rng, x, esize);
       x[1] |= 1;
       uint32_t t1[(BR_RSA_RAND_FACTOR + 63) >> 5];
       uint32_t y0i = br_i31_ninv31(y[1]);
       memcpy(t1 + 1, x + 1, (x[0] + 7) >> 3);
       t1[0] = x[0];
       while(br_i31_moddiv(x, x, y, y0i, tmp) == 0){
                make_rand(rng, x, esize);
                x[1] |= 1;
                memcpy(t1 + 1, x + 1, (x[0] + 7) >> 3);
                t1[0] = x[0];
       }
       memcpy(x + 1, t1 + 1, (t1[0] + 7) >> 3);
       x[0] = t1[0];

}



size_t blind_exponent( const br_prng_class ** rng, unsigned char * x, const unsigned char* d, const size_t size, uint32_t * m, uint32_t * t1){

        uint32_t r[(BR_RSA_RAND_FACTOR + 63) >> 5];
        make_rand(rng, r, BR_RSA_RAND_FACTOR);
        r[0] = br_i31_bit_length(r + 1, (BR_RSA_RAND_FACTOR + 31) >> 5);
        
        br_i31_zero(t1, m[0]);
        br_i31_decode(t1, d, size);
        t1[0] = m[0];
                
        size_t xlen = (m[0] + 7) >> 3; 
        // store in t1 = d + r * phi(m)
        br_i31_mulacc(t1, m, r);
        t1[0] = br_i31_bit_length(t1 + 1, (t1[0] + 31) >> 5);
        xlen = (t1[0] + 7) >> 3;
        
        br_i31_encode(x, xlen, t1);

        return xlen;
}


static void reblind(uint32_t * dest, uint32_t * src, uint32_t* mod, uint32_t * new_mask, uint32_t* tmp_buf){
    
        br_i31_zero(tmp_buf, 2*mod[0]);
        tmp_buf[0] = new_mask[0];
        br_i31_mulacc(tmp_buf, new_mask, src);
        br_i31_zero(dest, mod[0]);
        br_i31_reduce(dest, tmp_buf, mod);
        dest[0] = br_i31_bit_length(dest + 1, (dest[0] + 31) >> 5);
}

static void inverse(uint32_t * dest, uint32_t * src, uint32_t * mod, uint32_t * tmp){
        br_i31_zero(dest, mod[0]);
        src[0] = mod[0];
        dest[1] = 1;
        br_i31_moddiv(dest, src, mod, br_i31_ninv31(mod[1]), tmp);
        dest[0] = br_i31_bit_length(dest + 1, (dest[0] + 31) >> 5);
}

static void create_mask(uint32_t * dest, uint32_t * m,uint32_t *op1,uint32_t * op2, uint32_t * tmp_buf){
        br_i31_zero(tmp_buf, op1[0]);
        tmp_buf[0] = op1[0];
        br_i31_mulacc(tmp_buf, op1, op2);
        br_i31_reduce(dest, tmp_buf, m);
        dest[0] = br_i31_bit_length(dest + 1, (dest[0] + 31) >> 5);
}


void init_key( const br_prng_class ** rng, const br_rsa_private_key *sk, br_rsa_private_key *new_sk, uint32_t *tmp, uint32_t fwlen){

        // copy public modulus
        memcpy(new_sk->n, sk->n, (sk->n_bitlen +7) >> 3);
        new_sk->n_bitlen = sk->n_bitlen;
        
        // create random mask r1
        make_rand(rng, new_sk->r1, BR_RSA_RAND_FACTOR);
        new_sk->r1[1] |= 1;
        new_sk->r1[0] = br_i31_bit_length(new_sk->r1 + 1, (BR_RSA_RAND_FACTOR + 31) >> 5);
        
        // create random mask r2
        make_rand(rng, new_sk->r2, BR_RSA_RAND_FACTOR);

        new_sk->r2[1] |= 1;
        new_sk->r2[0] = br_i31_bit_length(new_sk->r2 + 1, (BR_RSA_RAND_FACTOR + 31) >> 5);

        uint32_t * t1 = tmp + fwlen;
        

        br_i31_decode(tmp, sk->p, sk->plen);
        
        // blind phi(p)
        tmp[1] ^= 1;
        br_i31_zero(new_sk->phi_p, tmp[0]);
        br_i31_mulacc(new_sk->phi_p, tmp, new_sk->r1);
        tmp[1] ^= 1;

        // blind p
        br_i31_zero(t1, tmp[0]);
        br_i31_mulacc(t1, tmp, new_sk->r1);
        t1[0] = br_i31_bit_length(t1 + 1, (t1[0] + 31) >> 5);
        br_i31_encode(new_sk->p, (t1[0] + 7) >> 3, t1);
        new_sk->plen = (t1[0] + 7) >> 3;
        
        br_i31_decode(tmp, sk->q, sk->qlen);
        
        // blind phi(q)
        tmp[1] ^= 1;
        br_i31_zero(new_sk->phi_q, tmp[0]);
        br_i31_mulacc(new_sk->phi_q, tmp, new_sk->r2);
        tmp[1] ^= 1;
        
        // blind q
        br_i31_zero(t1, tmp[0]);
        br_i31_mulacc(t1, tmp, new_sk->r2);
        t1[0] = br_i31_bit_length(t1 + 1, (t1[0] + 31) >> 5);
        br_i31_encode(new_sk->q, (t1[0] + 7) >> 3, t1);
        new_sk->qlen = (t1[0] + 7) >> 3;
        

        br_i31_decode(tmp, new_sk->n, (new_sk->n_bitlen + 7) >> 3);
        t1 = tmp + 2 * fwlen;
        uint32_t *t2 = t1 + 2 * fwlen;

        // inverse of random factor r_2 mod n
        br_i31_zero(t1, tmp[0]);
        memcpy(t1 + 1, new_sk->r2 + 1, (new_sk->r2[0] + 7) >> 3);
        inverse(t2, t1, tmp, t2 + 2*fwlen);

        // blind qinv
        br_i31_decode(tmp, new_sk->p, new_sk->plen);
        br_i31_reduce(t1, t2, tmp);
    
        uint32_t * t3 = t2 + 2 * fwlen;
        
        br_i31_decode_reduce(t2, sk->iq, sk->iqlen,tmp);
        reblind(t2, t1, tmp, t2, t3);
        br_i31_encode(new_sk->iq, (t2[0] + 7) >> 3, t2);
        new_sk->iqlen = (t2[0] + 7) >> 3;

   
        // blind dp 
        br_i31_decode(t1, sk->dp, sk->dplen);
        br_i31_zero(t2, t1[0]);
        t2[1] = 1;
        br_i31_mulacc(t1, t2, new_sk->phi_p);
        t1[0] = br_i31_bit_length(t1 + 1, (t1[0] + 31) >> 5);
        br_i31_encode(new_sk->dp, (t1[0] + 7) >> 3, t1);
        new_sk->dplen = (t1[0] + 7) >> 3;

        // blind dq 
        br_i31_decode(t1, sk->dq, sk->dqlen);
        br_i31_zero(t2, t1[0]);
        t2[1] = 1;
        br_i31_mulacc(t1, t2, new_sk->phi_q);
        t1[0] = br_i31_bit_length(t1 + 1, (t1[0] + 31) >> 5);
        br_i31_encode(new_sk->dq, (t1[0] + 7) >> 3, t1);
        new_sk->dqlen = (t1[0] + 7) >> 3;
        
        memcpy(new_sk->e, sk->e, sk->elen);
        new_sk->elen = sk->elen;
}




void update_key( const br_prng_class ** rng, br_rsa_private_key *new_sk, uint32_t *tmp, uint32_t fwlen ){

        uint32_t * r1_inv = tmp;
        uint32_t * r2_inv = tmp + 2 * fwlen;
        uint32_t * t1 = r2_inv + 2 * fwlen;
        uint32_t * mod = t1 + 2 * fwlen;
        uint32_t * t3 = mod + 2 * fwlen;
        uint32_t * t4 = t3 + 2 * fwlen;


        br_i31_decode(mod, new_sk->n, (new_sk->n_bitlen + 7) >> 3);


        // calculating multiplicative inverse of r_1
        br_i31_zero(t1, mod[0]);
        memcpy(t1 + 1, new_sk->r1 + 1, (new_sk->r1[0] + 7) >> 3);
        inverse(r1_inv, t1, mod, t3);
        

        // generating new value for r_1
        make_rand(rng, new_sk->r1, BR_RSA_RAND_FACTOR);
        new_sk->r1[1] |= 1;
        new_sk->r1[0] = br_i31_bit_length(new_sk->r1 + 1, (BR_RSA_RAND_FACTOR + 31) >> 5);
        
        // storing old random factor, later used in mask for qinv       
        uint32_t temp_r2[(BR_RSA_RAND_FACTOR + 63) >> 5];
        br_i31_zero(temp_r2, new_sk->r2[0]);
        memcpy(temp_r2 + 1, new_sk->r2 + 1, (new_sk->r2[0] + 7) >> 3);
        temp_r2[0] = new_sk->r2[0];

        // generating new value for r_2
        make_rand(rng, new_sk->r2, BR_RSA_RAND_FACTOR);
        new_sk->r2[1] |= 1;
        new_sk->r2[0] = br_i31_bit_length(new_sk->r2 + 1, (BR_RSA_RAND_FACTOR + 31) >> 5);
        
        
        // re-blinding p
        br_i31_decode(t1, new_sk->p, new_sk->plen);
        create_mask(t3, mod, r1_inv, new_sk->r1, t4);
        reblind(t3, t1, mod, t3, t4);
        br_i31_encode(new_sk->p, (t3[0] + 7) >> 3, t3);
        new_sk->plen = (t3[0] + 7) >> 3;

        // copy old phi(p)
        br_i31_zero(t3, mod[0]);
        memcpy(t3 + 1, new_sk->phi_p + 1, (new_sk->phi_p[0] + 7) >> 3);
        t3[0] = new_sk->phi_p[0];

        // re-blinding phi(p)
        create_mask(t1, mod, r1_inv, new_sk->r1, t4);
        reblind(t1, new_sk->phi_p, mod, t1, t4);
        br_i31_zero(new_sk->phi_p, t1[0]);
        memcpy(new_sk->phi_p + 1, t1 + 1, (t1[0] + 7) >> 3);
        new_sk->phi_p[0] = t1[0];

        // re-blinding dp
        br_i31_sub(t1, t3, 1);
        br_i31_zero(t3, mod[0]);
        br_i31_decode(t3, new_sk->dp, new_sk->dplen);
        t3[0] = t1[0];
        br_i31_add(t3, t1, 1);
        t3[0] = br_i31_bit_length(t3 + 1, (t3[0] + 31) >> 5);
        br_i31_encode(new_sk->dp, (t3[0] + 7) >> 3, t3);
        new_sk->dplen = (t3[0] + 7) >> 3;
        
        // calculating multiplicative inverse of old r_2
        br_i31_zero(t1, mod[0]);
        memcpy(t1 + 1, temp_r2 + 1, (temp_r2[0] + 7) >> 3);
        inverse(r2_inv, t1, mod, t3);
        

        // copy old phi(q)
        br_i31_zero(t3, mod[0]);
        memcpy(t3 + 1, new_sk->phi_q + 1, (new_sk->phi_q[0] + 7) >> 3);
        t3[0] = new_sk->phi_q[0];
        
        // re-blinding phi(q)
        create_mask(t1, mod, r2_inv, new_sk->r2, t4);
        reblind(t1, new_sk->phi_q, mod, t1, t4);
        br_i31_zero(new_sk->phi_q, t1[0]);
        memcpy(new_sk->phi_q + 1, t1 + 1, (t1[0] + 7) >> 3);
        new_sk->phi_q[0] = t1[0];

        // re-blinding dq
        br_i31_sub(t1, t3, 1);
        br_i31_zero(t3, mod[0]);
        br_i31_decode(t3, new_sk->dq, new_sk->dqlen);
        t3[0] = t1[0];
        br_i31_add(t3, t1, 1);
        t3[0] = br_i31_bit_length(t3 + 1, (t3[0] + 31) >> 5);
        br_i31_encode(new_sk->dq, (t3[0] + 7) >> 3, t3);
        new_sk->dqlen = (t3[0] + 7) >> 3;
        
        // re-blinding q
        br_i31_decode(t1, new_sk->q, new_sk->qlen);
        create_mask(t3, mod, r2_inv, new_sk->r2, t4);
        reblind(t3, t1, mod, t3, t4);
        br_i31_encode(new_sk->q, (t3[0] + 7) >> 3, t3);
        new_sk->qlen = (t3[0] + 7) >> 3;
        
        // calculating multiplicative inverse of new r_2
        br_i31_zero(t1, mod[0]);
        br_i31_zero(r2_inv, mod[0]);
        memcpy(t1 + 1, new_sk->r2 + 1, (new_sk->r2[0] + 7) >> 3);
        inverse(r2_inv, t1, mod, t3);
                
        // blinding qinv
        br_i31_zero(t4, mod[0]);
        br_i31_zero(mod, mod[0]);
        br_i31_decode(mod, new_sk->p, new_sk->plen);

        br_i31_decode(t1, new_sk->iq, new_sk->iqlen);
        create_mask(t3, mod, r2_inv, temp_r2, t4);
        reblind(t3, t1, mod, t3, t4);

        br_i31_encode(new_sk->iq, (t3[0] + 7) >> 3, t3);
        new_sk->iqlen = (t3[0] + 7) >> 3;
    
}

void br_i31_init_key( const br_prng_class ** rng, const br_rsa_private_key *sk, br_rsa_private_key *new_sk, uint32_t *tmp, uint32_t fwlen){

        // copy public modulus
        memcpy(new_sk->n, sk->n, (sk->n_bitlen +7) >> 3);
        new_sk->n_bitlen = sk->n_bitlen;
        
        // create random mask r1
        make_rand(rng, new_sk->r1, BR_RSA_RAND_FACTOR);
        new_sk->r1[1] |= 1;
        new_sk->r1[0] = br_i31_bit_length(new_sk->r1 + 1, (BR_RSA_RAND_FACTOR + 31) >> 5);
        
        uint32_t * t1 = tmp + fwlen;

        // create random mask r2
        make_rand_coprime(rng, new_sk->r2, BR_RSA_RAND_FACTOR, new_sk->r1, t1);

        new_sk->r2[1] |= 1;
        new_sk->r2[0] = br_i31_bit_length(new_sk->r2 + 1, (BR_RSA_RAND_FACTOR + 31) >> 5);

        
        

        br_i31_decode(tmp, sk->p, sk->plen);
        
        // blind phi(p)
        tmp[1] ^= 1;
        br_i31_zero(new_sk->phi_p, tmp[0]);
        br_i31_mulacc(new_sk->phi_p, tmp, new_sk->r1);
        tmp[1] ^= 1;

        // blind p
        br_i31_zero(t1, tmp[0]);
        br_i31_mulacc(t1, tmp, new_sk->r1);
        t1[0] = br_i31_bit_length(t1 + 1, (t1[0] + 31) >> 5);
        br_i31_encode(new_sk->p, (t1[0] + 7) >> 3, t1);
        new_sk->plen = (t1[0] + 7) >> 3;
        
        br_i31_decode(tmp, sk->q, sk->qlen);
        
        // blind phi(q)
        tmp[1] ^= 1;
        br_i31_zero(new_sk->phi_q, tmp[0]);
        br_i31_mulacc(new_sk->phi_q, tmp, new_sk->r2);
        tmp[1] ^= 1;
        
        // blind q
        br_i31_zero(t1, tmp[0]);
        br_i31_mulacc(t1, tmp, new_sk->r2);
        t1[0] = br_i31_bit_length(t1 + 1, (t1[0] + 31) >> 5);
        br_i31_encode(new_sk->q, (t1[0] + 7) >> 3, t1);
        new_sk->qlen = (t1[0] + 7) >> 3;
        

        br_i31_decode(tmp, new_sk->n, (new_sk->n_bitlen + 7) >> 3);
        t1 = tmp + 2 * fwlen;
        uint32_t *t2 = t1 + 2 * fwlen;

 
        // blind qinv
        br_i31_decode(tmp, new_sk->p, new_sk->plen);
        br_i31_decode(t2, new_sk->q, new_sk->qlen);
        br_i31_reduce(t1, t2, tmp);
    
       // uint32_t * t3 = t2 + 2 * fwlen;
        
        br_i31_zero(t2, tmp[0]);
        t2[1] = 1;
        br_i31_moddiv(t2, t1, tmp, br_i31_ninv31(tmp[1]), t2 + 2*fwlen);
        t2[0] = br_i31_bit_length(t2 + 1, (t2[0] + 31) >> 5);
        br_i31_encode(new_sk->iq, (t2[0] + 7) >> 3, t2);
        new_sk->iqlen = (t2[0] + 7) >> 3;

        // blind dp 
        br_i31_decode(t1, sk->dp, sk->dplen);
        br_i31_zero(t2, t1[0]);
        t2[1] = 1;
        br_i31_mulacc(t1, t2, new_sk->phi_p);
        t1[0] = br_i31_bit_length(t1 + 1, (t1[0] + 31) >> 5);
        br_i31_encode(new_sk->dp, (t1[0] + 7) >> 3, t1);
        new_sk->dplen = (t1[0] + 7) >> 3;

        // blind dq 
        br_i31_decode(t1, sk->dq, sk->dqlen);
        br_i31_zero(t2, t1[0]);
        t2[1] = 1;
        br_i31_mulacc(t1, t2, new_sk->phi_q);
        t1[0] = br_i31_bit_length(t1 + 1, (t1[0] + 31) >> 5);
        br_i31_encode(new_sk->dq, (t1[0] + 7) >> 3, t1);
        new_sk->dqlen = (t1[0] + 7) >> 3;
        
        memcpy(new_sk->e, sk->e, sk->elen);
        new_sk->elen = sk->elen;
}




void br_i31_update_key( const br_prng_class ** rng, br_rsa_private_key *new_sk, uint32_t *tmp, uint32_t fwlen ){

        uint32_t * r1_inv = tmp;
        uint32_t * r2_inv = tmp + 2 * fwlen;
        uint32_t * t1 = r2_inv + 2 * fwlen;
        uint32_t * mod = t1 + 2 * fwlen;
        uint32_t * t3 = mod + 2 * fwlen;
        uint32_t * t4 = t3 + 2 * fwlen;


        br_i31_decode(mod, new_sk->n, (new_sk->n_bitlen + 7) >> 3);


        // calculating multiplicative inverse of r_1
        br_i31_zero(t1, mod[0]);
        memcpy(t1 + 1, new_sk->r1 + 1, (new_sk->r1[0] + 7) >> 3);
        inverse(r1_inv, t1, mod, t3);
        

        // generating new value for r_1
        make_rand(rng, new_sk->r1, BR_RSA_RAND_FACTOR);
        new_sk->r1[1] |= 1;
        new_sk->r1[0] = br_i31_bit_length(new_sk->r1 + 1, (BR_RSA_RAND_FACTOR + 31) >> 5);
        
        // storing old random factor, later used in mask for qinv       
        uint32_t temp_r2[(BR_RSA_RAND_FACTOR + 63) >> 5];
        br_i31_zero(temp_r2, new_sk->r2[0]);
        memcpy(temp_r2 + 1, new_sk->r2 + 1, (new_sk->r2[0] + 7) >> 3);
        temp_r2[0] = new_sk->r2[0];

        // generating new value for r_2
        make_rand_coprime(rng, new_sk->r2, BR_RSA_RAND_FACTOR, new_sk->r1, t3);
        new_sk->r2[1] |= 1;
        new_sk->r2[0] = br_i31_bit_length(new_sk->r2 + 1, (BR_RSA_RAND_FACTOR + 31) >> 5);
        
        
        // re-blinding p
        br_i31_decode(t1, new_sk->p, new_sk->plen);
        create_mask(t3, mod, r1_inv, new_sk->r1, t4);
        reblind(t3, t1, mod, t3, t4);
        br_i31_encode(new_sk->p, (t3[0] + 7) >> 3, t3);
        new_sk->plen = (t3[0] + 7) >> 3;

        // copy old phi(p)
        br_i31_zero(t3, mod[0]);
        memcpy(t3 + 1, new_sk->phi_p + 1, (new_sk->phi_p[0] + 7) >> 3);
        t3[0] = new_sk->phi_p[0];

        // re-blinding phi(p)
        create_mask(t1, mod, r1_inv, new_sk->r1, t4);
        reblind(t1, new_sk->phi_p, mod, t1, t4);
        br_i31_zero(new_sk->phi_p, t1[0]);
        memcpy(new_sk->phi_p + 1, t1 + 1, (t1[0] + 7) >> 3);
        new_sk->phi_p[0] = t1[0];

        // re-blinding dp
        br_i31_sub(t1, t3, 1);
        br_i31_zero(t3, mod[0]);
        br_i31_decode(t3, new_sk->dp, new_sk->dplen);
        t3[0] = t1[0];
        br_i31_add(t3, t1, 1);
        t3[0] = br_i31_bit_length(t3 + 1, (t3[0] + 31) >> 5);
        br_i31_encode(new_sk->dp, (t3[0] + 7) >> 3, t3);
        new_sk->dplen = (t3[0] + 7) >> 3;
        
        //calculating multiplicative inverse of old r_2
        br_i31_zero(t1, mod[0]);
        memcpy(t1 + 1, temp_r2 + 1, (temp_r2[0] + 7) >> 3);
        inverse(r2_inv, t1, mod, t3);
        

        // copy old phi(q)
        br_i31_zero(t3, mod[0]);
        memcpy(t3 + 1, new_sk->phi_q + 1, (new_sk->phi_q[0] + 7) >> 3);
        t3[0] = new_sk->phi_q[0];
        
        // re-blinding phi(q)
        create_mask(t1, mod, r2_inv, new_sk->r2, t4);
        reblind(t1, new_sk->phi_q, mod, t1, t4);
        br_i31_zero(new_sk->phi_q, t1[0]);
        memcpy(new_sk->phi_q + 1, t1 + 1, (t1[0] + 7) >> 3);
        new_sk->phi_q[0] = t1[0];

        // re-blinding dq
        br_i31_sub(t1, t3, 1);
        br_i31_zero(t3, mod[0]);
        br_i31_decode(t3, new_sk->dq, new_sk->dqlen);
        t3[0] = t1[0];
        br_i31_add(t3, t1, 1);
        t3[0] = br_i31_bit_length(t3 + 1, (t3[0] + 31) >> 5);
        br_i31_encode(new_sk->dq, (t3[0] + 7) >> 3, t3);
        new_sk->dqlen = (t3[0] + 7) >> 3;
        
        // re-blinding q
        br_i31_decode(t1, new_sk->q, new_sk->qlen);
        create_mask(t3, mod, r2_inv, new_sk->r2, t4);
        reblind(t3, t1, mod, t3, t4);
        br_i31_encode(new_sk->q, (t3[0] + 7) >> 3, t3);
        new_sk->qlen = (t3[0] + 7) >> 3;
        
                
        // blinding qinv
        br_i31_decode(mod, new_sk->p, new_sk->plen);
        br_i31_decode(t3, new_sk->q, new_sk->qlen);
        br_i31_reduce(t1, t3, mod);
    
        br_i31_zero(t3, mod[0]);
        t3[1] = 1;
        br_i31_moddiv(t3, t1, mod, br_i31_ninv31(mod[1]), t4);
        t3[0] = br_i31_bit_length(t3 + 1, (t3[0] + 31) >> 5);
        br_i31_encode(new_sk->iq, (t3[0] + 7) >> 3, t3);
        new_sk->iqlen = (t3[0] + 7) >> 3;


        
    
}