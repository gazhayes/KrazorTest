// Copyright (c) 2016, Monero Research Labs
//
// Author: Shen Noether <shen.noether@gmx.com>
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "misc_log_ex.h"
#include "rctSigs.h"
using namespace crypto;
using namespace std;

namespace rct {
    
    //Schnorr Non-linkable
    //Gen Gives a signature (L1, s1, s2) proving that the sender knows "x" such that xG = one of P1 or P2
    //Ver Verifies that signer knows an "x" such that xG = one of P1 or P2
    //These are called in the below ASNL sig generation    
    
    void GenSchnorrNonLinkable(key & L1, key & s1, key & s2, const key & x, const key & P1, const key & P2, int index) {
        key c1, c2, L2;
        key a = skGen();
        if (index == 0) {
            scalarmultBase(L1, a);
            hash_to_scalar(c2, L1);
            skGen(s2);
            addKeys2(L2, s2, c2, P2);
            hash_to_scalar(c1, L2);
            //s1 = a - x * c1
            sc_mulsub(s1.bytes, x.bytes, c1.bytes, a.bytes);
        }
        else if (index == 1) {
            scalarmultBase(L2, a);
            hash_to_scalar(c1, L2);
            skGen(s1);
            addKeys2(L1, s1, c1, P1);
            hash_to_scalar(c2, L1);
            sc_mulsub(s2.bytes, x.bytes, c2.bytes, a.bytes);
        }
        else {
          throw std::runtime_error("GenSchnorrNonLinkable: invalid index (should be 0 or 1)");
        }
    }

    //Schnorr Non-linkable
    //Gen Gives a signature (L1, s1, s2) proving that the sender knows "x" such that xG = one of P1 or P2
    //Ver Verifies that signer knows an "x" such that xG = one of P1 or P2
    //These are called in the below ASNL sig generation        
    bool VerSchnorrNonLinkable(const key & P1, const key & P2, const key & L1, const key & s1, const key & s2) {
        key c2, L2, c1, L1p;
        hash_to_scalar(c2, L1);
        addKeys2(L2, s2, c2, P2);
        hash_to_scalar(c1, L2);
        addKeys2(L1p, s1, c1, P1);
        
        return equalKeys(L1, L1p);
    }
    
    //Aggregate Schnorr Non-linkable Ring Signature (ASNL)
    // c.f. http://eprint.iacr.org/2015/1098 section 5. 
    // These are used in range proofs (alternatively Borromean could be used)
    // Gen gives a signature which proves the signer knows, for each i, 
    //   an x[i] such that x[i]G = one of P1[i] or P2[i]
    // Ver Verifies the signer knows a key for one of P1[i], P2[i] at each i
    asnlSig GenASNL(key64 x, key64 P1, key64 P2, bits indices) {
        DP("Generating Aggregate Schnorr Non-linkable Ring Signature\n");
        key64 s1;
        int j = 0;
        asnlSig rv;
        rv.s = zero();
        for (j = 0; j < ATOMS; j++) {
            GenSchnorrNonLinkable(rv.L1[j], s1[j], rv.s2[j], x[j], P1[j], P2[j], (int)indices[j]);
            sc_add(rv.s.bytes, rv.s.bytes, s1[j].bytes);
        }
        return rv;
    }

    //Aggregate Schnorr Non-linkable Ring Signature (ASNL)
    // c.f. http://eprint.iacr.org/2015/1098 section 5. 
    // These are used in range proofs (alternatively Borromean could be used)
    // Gen gives a signature which proves the signer knows, for each i, 
    //   an x[i] such that x[i]G = one of P1[i] or P2[i]
    // Ver Verifies the signer knows a key for one of P1[i], P2[i] at each i    
    bool VerASNL(const key64 P1, const key64 P2, const asnlSig &as) {
        DP("Verifying Aggregate Schnorr Non-linkable Ring Signature\n");
        key LHS = identity();
        key RHS = scalarmultBase(as.s);
        key c2, L2, c1;
        int j = 0;
        for (j = 0; j < ATOMS; j++) {
            hash_to_scalar(c2, as.L1[j]);
            addKeys2(L2, as.s2[j], c2, P2[j]);
            addKeys(LHS, LHS, as.L1[j]);
            hash_to_scalar(c1, L2);
            addKeys(RHS, RHS, scalarmultKey(P1[j], c1));
        }
        key cc;
        sc_sub(cc.bytes, LHS.bytes, RHS.bytes);
        return sc_isnonzero(cc.bytes) == 0;
    }
    
    //Multilayered Spontaneous Anonymous Group Signatures (MLSAG signatures)
    //These are aka MG signatutes in earlier drafts of the ring ct paper
    // c.f. http://eprint.iacr.org/2015/1098 section 2. 
    // keyImageV just does I[i] = xx[i] * Hash(xx[i] * G) for each i
    // Gen creates a signature which proves that for some column in the keymatrix "pk"
    //   the signer knows a secret key for each row in that column
    // Ver verifies that the MG sig was created correctly
    keyV keyImageV(const keyV &xx) {
        keyV II(xx.size());
        size_t i = 0;
        for (i = 0; i < xx.size(); i++) {
            II[i] = scalarmultKey(hashToPoint(scalarmultBase(xx[i])), xx[i]);
        }
        return II;
    }
    
    
    //Multilayered Spontaneous Anonymous Group Signatures (MLSAG signatures)
    //This is a just slghtly more efficient version than the ones described below
    //(will be explained in more detail in Ring Multisig paper
    //These are aka MG signatutes in earlier drafts of the ring ct paper
    // c.f. http://eprint.iacr.org/2015/1098 section 2. 
    // keyImageV just does I[i] = xx[i] * Hash(xx[i] * G) for each i
    // Gen creates a signature which proves that for some column in the keymatrix "pk"
    //   the signer knows a secret key for each row in that column
    // Ver verifies that the MG sig was created correctly        
    mgSig MLSAG_Gen(key message, const keyM & pk, const keyV & xx, const unsigned int index) {
        mgSig rv;
        size_t cols = pk.size();
        CHECK_AND_ASSERT_THROW_MES(cols >= 2, "Error! What is c if cols = 1!");
        CHECK_AND_ASSERT_THROW_MES(index < cols, "Index out of range");
        size_t rows = pk[0].size();
        CHECK_AND_ASSERT_THROW_MES(rows >= 1, "Empty pk");
        for (size_t i = 1; i < cols; ++i) {
          CHECK_AND_ASSERT_THROW_MES(pk[i].size() == rows, "pk is not rectangular");
        }
        CHECK_AND_ASSERT_THROW_MES(xx.size() == rows, "Bad xx size");

        size_t i = 0, j = 0;
        key c, c_old, L, R, Hi;
        sc_0(c_old.bytes);
        vector<geDsmp> Ip(rows);
        rv.II = keyV(rows);
        rv.ss = keyM(cols, rv.II);
        keyV alpha(rows);
        keyV aG(rows);
        keyV aHP(rows);
        key m2hash;
        unsigned char m2[128]; 
        memcpy(m2, message.bytes, 32);
        DP("here1");
        for (i = 0; i < rows; i++) {
            skpkGen(alpha[i], aG[i]); //need to save alphas for later..
            Hi = hashToPoint(pk[index][i]);
            aHP[i] = scalarmultKey(Hi, alpha[i]);
            memcpy(m2+32, pk[index][i].bytes, 32);
            memcpy(m2 + 64, aG[i].bytes, 32);
            memcpy(m2 + 96, aHP[i].bytes, 32);
            rv.II[i] = scalarmultKey(Hi, xx[i]);
            precomp(Ip[i].k, rv.II[i]);
            m2hash = hash_to_scalar128(m2);
            sc_add(c_old.bytes, c_old.bytes, m2hash.bytes);
        }
        
        i = (index + 1) % cols;
        if (i == 0) {
            copy(rv.cc, c_old);
        }
        while (i != index) {

            rv.ss[i] = skvGen(rows);            
            sc_0(c.bytes);
            for (j = 0; j < rows; j++) {
                addKeys2(L, rv.ss[i][j], c_old, pk[i][j]);
                hashToPoint(Hi, pk[i][j]);
                addKeys3(R, rv.ss[i][j], Hi, c_old, Ip[j].k);
                memcpy(m2+32, pk[i][j].bytes, 32);
                memcpy(m2 + 64, L.bytes, 32);
                memcpy(m2 + 96, R.bytes, 32);      
                m2hash = hash_to_scalar128(m2);
                sc_add(c.bytes, c.bytes, m2hash.bytes);
            }
            copy(c_old, c);
            i = (i + 1) % cols;
            
            if (i == 0) { 
                copy(rv.cc, c_old);
            }   
        }
        for (j = 0; j < rows; j++) {
            sc_mulsub(rv.ss[index][j].bytes, c.bytes, xx[j].bytes, alpha[j].bytes);
        }        
        return rv;
    }
    
    //Multilayered Spontaneous Anonymous Group Signatures (MLSAG signatures)
    //This is a just slghtly more efficient version than the ones described below
    //(will be explained in more detail in Ring Multisig paper
    //These are aka MG signatutes in earlier drafts of the ring ct paper
    // c.f. http://eprint.iacr.org/2015/1098 section 2. 
    // keyImageV just does I[i] = xx[i] * Hash(xx[i] * G) for each i
    // Gen creates a signature which proves that for some column in the keymatrix "pk"
    //   the signer knows a secret key for each row in that column
    // Ver verifies that the MG sig was created correctly            
    bool MLSAG_Ver(key message, const keyM & pk, const mgSig & rv) {

        size_t cols = pk.size();
        CHECK_AND_ASSERT_THROW_MES(cols >= 2, "Error! What is c if cols = 1!");
        size_t rows = pk[0].size();
        CHECK_AND_ASSERT_THROW_MES(rows >= 1, "Empty pk");
        for (size_t i = 1; i < cols; ++i) {
          CHECK_AND_ASSERT_THROW_MES(pk[i].size() == rows, "pk is not rectangular");
        }
        CHECK_AND_ASSERT_THROW_MES(rv.II.size() == rows, "Bad rv.II size");
        CHECK_AND_ASSERT_THROW_MES(rv.ss.size() == cols, "Bad rv.ss size");
        for (size_t i = 0; i < cols; ++i) {
          CHECK_AND_ASSERT_THROW_MES(rv.ss[i].size() == rows, "rv.ss is not rectangular");
        }

        size_t i = 0, j = 0;
        key c,  L, R, Hi;
        key c_old = copy(rv.cc);
        vector<geDsmp> Ip(rows);
        for (i= 0 ; i< rows ; i++) {
            precomp(Ip[i].k, rv.II[i]);
        }
        unsigned char m2[128]; 
        memcpy(m2, message.bytes, 32);
        
        key m2hash;
        i = 0;
        while (i < cols) {
            sc_0(c.bytes);
            for (j = 0; j < rows; j++) {
                addKeys2(L, rv.ss[i][j], c_old, pk[i][j]);
                hashToPoint(Hi, pk[i][j]);
                addKeys3(R, rv.ss[i][j], Hi, c_old, Ip[j].k);
                memcpy(m2 + 32, pk[i][j].bytes, 32);
                memcpy(m2 + 64, L.bytes, 32);
                memcpy(m2 + 96, R.bytes, 32);      
                m2hash = hash_to_scalar128(m2);
                sc_add(c.bytes, c.bytes, m2hash.bytes);
            }
            copy(c_old, c);
            i = (i + 1);
        }
        sc_sub(c.bytes, c_old.bytes, rv.cc.bytes);
        return sc_isnonzero(c.bytes) == 0;  
    }
    


    //proveRange and verRange
    //proveRange gives C, and mask such that \sumCi = C
    //   c.f. http://eprint.iacr.org/2015/1098 section 5.1
    //   and Ci is a commitment to either 0 or 2^i, i=0,...,63
    //   thus this proves that "amount" is in [0, 2^64]
    //   mask is a such that C = aG + bH, and b = amount
    //verRange verifies that \sum Ci = C and that each Ci is a commitment to 0 or 2^i
    rangeSig proveRange(key & C, key & mask, const xmr_amount & amount) {
        sc_0(mask.bytes);
        identity(C);
        bits b;
        d2b(b, amount);
        rangeSig sig;
        key64 ai;
        key64 CiH;
        int i = 0;
        for (i = 0; i < ATOMS; i++) {
            skGen(ai[i]);
            if (b[i] == 0) {
                scalarmultBase(sig.Ci[i], ai[i]);
            }
            if (b[i] == 1) {
                addKeys1(sig.Ci[i], ai[i], H2[i]);
            }
            subKeys(CiH[i], sig.Ci[i], H2[i]);
            sc_add(mask.bytes, mask.bytes, ai[i].bytes);
            addKeys(C, C, sig.Ci[i]);
        }
        sig.asig = GenASNL(ai, sig.Ci, CiH, b);
        return sig;
    }

    //proveRange and verRange
    //proveRange gives C, and mask such that \sumCi = C
    //   c.f. http://eprint.iacr.org/2015/1098 section 5.1
    //   and Ci is a commitment to either 0 or 2^i, i=0,...,63
    //   thus this proves that "amount" is in [0, 2^64]
    //   mask is a such that C = aG + bH, and b = amount
    //verRange verifies that \sum Ci = C and that each Ci is a commitment to 0 or 2^i
    bool verRange(const key & C, const rangeSig & as) {
        key64 CiH;
        int i = 0;
        key Ctmp = identity();
        for (i = 0; i < 64; i++) {
            subKeys(CiH[i], as.Ci[i], H2[i]);
            addKeys(Ctmp, Ctmp, as.Ci[i]);
        }
        bool reb = equalKeys(C, Ctmp);
        bool rab = VerASNL(as.Ci, CiH, as.asig);
        return (reb && rab);
    }

    //Ring-ct MG sigs
    //Prove: 
    //   c.f. http://eprint.iacr.org/2015/1098 section 4. definition 10. 
    //   This does the MG sig on the "dest" part of the given key matrix, and 
    //   the last row is the sum of input commitments from that column - sum output commitments
    //   this shows that sum inputs = sum outputs
    //Ver:    
    //   verifies the above sig is created corretly
    mgSig proveRctMG(const ctkeyM & pubs, const ctkeyV & inSk, const ctkeyV &outSk, const ctkeyV & outPk, unsigned int index, key txnFeeKey) {
        mgSig mg;
        //setup vars
        size_t cols = pubs.size();
        CHECK_AND_ASSERT_THROW_MES(cols >= 1, "Empty pubs");
        size_t rows = pubs[0].size();
        CHECK_AND_ASSERT_THROW_MES(rows >= 1, "Empty pubs");
        for (size_t i = 1; i < cols; ++i) {
          CHECK_AND_ASSERT_THROW_MES(pubs[i].size() == rows, "pubs is not rectangular");
        }
        CHECK_AND_ASSERT_THROW_MES(inSk.size() == rows, "Bad inSk size");
        CHECK_AND_ASSERT_THROW_MES(outSk.size() == outPk.size(), "Bad outSk/outPk size");

        keyV sk(rows + 1);
        keyV tmp(rows + 1);
        size_t i = 0, j = 0;
        for (i = 0; i < rows + 1; i++) {
            sc_0(sk[i].bytes);
            identity(tmp[i]);
        }
        keyM M(cols, tmp);
        //create the matrix to mg sig
        for (i = 0; i < cols; i++) {
            M[i][rows] = identity();
            for (j = 0; j < rows; j++) {
                M[i][j] = pubs[i][j].dest;
                addKeys(M[i][rows], M[i][rows], pubs[i][j].mask); //add input commitments in last row
            }
        }
        sc_0(sk[rows].bytes);
        for (j = 0; j < rows; j++) {
            sk[j] = copy(inSk[j].dest);
            sc_add(sk[rows].bytes, sk[rows].bytes, inSk[j].mask.bytes); //add masks in last row
        }
        for (i = 0; i < cols; i++) {
            for (size_t j = 0; j < outPk.size(); j++) {
                subKeys(M[i][rows], M[i][rows], outPk[j].mask); //subtract output Ci's in last row
            }
            //subtract txn fee output in last row
            subKeys(M[i][rows], M[i][rows], txnFeeKey);
        }
        for (size_t j = 0; j < outPk.size(); j++) {
            sc_sub(sk[rows].bytes, sk[rows].bytes, outSk[j].mask.bytes); //subtract output masks in last row..
        }
        key message = cn_fast_hash(outPk);
        return MLSAG_Gen(message, M, sk, index);
    }


    //Ring-ct MG sigs
    //Prove: 
    //   c.f. http://eprint.iacr.org/2015/1098 section 4. definition 10. 
    //   This does the MG sig on the "dest" part of the given key matrix, and 
    //   the last row is the sum of input commitments from that column - sum output commitments
    //   this shows that sum inputs = sum outputs
    //Ver:    
    //   verifies the above sig is created corretly
    bool verRctMG(mgSig mg, const ctkeyM & pubs, const ctkeyV & outPk, key txnFeeKey) {
        //setup vars
        size_t cols = pubs.size();
        CHECK_AND_ASSERT_THROW_MES(cols >= 1, "Empty pubs");
        size_t rows = pubs[0].size();
        CHECK_AND_ASSERT_THROW_MES(rows >= 1, "Empty pubs");
        for (size_t i = 1; i < cols; ++i) {
          CHECK_AND_ASSERT_THROW_MES(pubs[i].size() == rows, "pubs is not rectangular");
        }

        keyV tmp(rows + 1);
        size_t i = 0, j = 0;
        for (i = 0; i < rows + 1; i++) {
            identity(tmp[i]);
        }
        keyM M(cols, tmp);

        //create the matrix to mg sig
        for (j = 0; j < rows; j++) {
            for (i = 0; i < cols; i++) {
                M[i][j] = pubs[i][j].dest;
                addKeys(M[i][rows], M[i][rows], pubs[i][j].mask); //add Ci in last row
            }
        }
        for (i = 0; i < cols; i++) {
            for (j = 0; j < outPk.size(); j++) {
                subKeys(M[i][rows], M[i][rows], outPk[j].mask); //subtract output Ci's in last row
            }
            //subtract txn fee output in last row
            subKeys(M[i][rows], M[i][rows], txnFeeKey);
        }
        key message = cn_fast_hash(outPk);
        DP("message:");
        DP(message);
        return MLSAG_Ver(message, M, mg);
    }

    //These functions get keys from blockchain
    //replace these when connecting blockchain
    //getKeyFromBlockchain grabs a key from the blockchain at "reference_index" to mix with
    //populateFromBlockchain creates a keymatrix with "mixin" columns and one of the columns is inPk
    //   the return value are the key matrix, and the index where inPk was put (random).    
    void getKeyFromBlockchain(ctkey & a, size_t reference_index) {
        a.mask = pkGen();
        a.dest = pkGen();
    }

    //These functions get keys from blockchain
    //replace these when connecting blockchain
    //getKeyFromBlockchain grabs a key from the blockchain at "reference_index" to mix with
    //populateFromBlockchain creates a keymatrix with "mixin" + 1 columns and one of the columns is inPk
    //   the return value are the key matrix, and the index where inPk was put (random).     
    tuple<ctkeyM, xmr_amount> populateFromBlockchain(ctkeyV inPk, int mixin) {
        int rows = inPk.size();
        ctkeyM rv(mixin + 1, inPk);
        int index = randXmrAmount(mixin);
        int i = 0, j = 0;
        for (i = 0; i <= mixin; i++) {
            if (i != index) {
                for (j = 0; j < rows; j++) {
                    getKeyFromBlockchain(rv[i][j], (size_t)randXmrAmount);
                }
            }
        }
        return make_tuple(rv, index);
    }

    //RingCT protocol
    //genRct: 
    //   creates an rctSig with all data necessary to verify the rangeProofs and that the signer owns one of the
    //   columns that are claimed as inputs, and that the sum of inputs  = sum of outputs.
    //   Also contains masked "amount" and "mask" so the receiver can see how much they received
    //verRct:
    //   verifies that all signatures (rangeProogs, MG sig, sum inputs = outputs) are correct
    //decodeRct: (c.f. http://eprint.iacr.org/2015/1098 section 5.1.1)
    //   uses the attached ecdh info to find the amounts represented by each output commitment 
    //   must know the destination private key to find the correct amount, else will return a random number
    //   Note: For txn fees, the last index in the amounts vector should contain that
    //   Thus the amounts vector will be "one" longer than the destinations vectort
    rctSig genRct(const ctkeyV & inSk, const keyV & destinations, const vector<xmr_amount> amounts, const ctkeyM &mixRing, unsigned int index) {
        CHECK_AND_ASSERT_THROW_MES(amounts.size() == destinations.size() || amounts.size() == destinations.size() + 1, "Different number of amounts/destinations");
        CHECK_AND_ASSERT_THROW_MES(index < mixRing.size(), "Bad index into mixRing");
        for (size_t n = 0; n < mixRing.size(); ++n) {
          CHECK_AND_ASSERT_THROW_MES(mixRing[n].size() == inSk.size(), "Bad mixRing size");
        }

        rctSig rv;
        rv.outPk.resize(destinations.size());
        rv.rangeSigs.resize(destinations.size());
        rv.ecdhInfo.resize(destinations.size());

        size_t i = 0;
        keyV masks(destinations.size()); //sk mask..
        ctkeyV outSk(destinations.size());
        for (i = 0; i < destinations.size(); i++) {
            //add destination to sig
            rv.outPk[i].dest = copy(destinations[i]);
            //compute range proof
            rv.rangeSigs[i] = proveRange(rv.outPk[i].mask, outSk[i].mask, amounts[i]);
            #ifdef DBG
                CHECK_AND_ASSERT_THROW_MES(verRange(rv.outPk[i].mask, rv.rangeSigs[i]), "verRange failed on newly created proof");
            #endif

            //mask amount and mask
            rv.ecdhInfo[i].mask = copy(outSk[i].mask);
            rv.ecdhInfo[i].amount = d2h(amounts[i]);
            ecdhEncode(rv.ecdhInfo[i], destinations[i]);

        }

        //set txn fee
        if (amounts.size() > destinations.size())
        {
          rv.txnFee = amounts[destinations.size()];
        }
        else
        {
          rv.txnFee = 0;
        }
        key txnFeeKey = scalarmultH(d2h(rv.txnFee));

        rv.mixRing = mixRing;
        rv.MG = proveRctMG(rv.mixRing, inSk, outSk, rv.outPk, index, txnFeeKey);
        return rv;
    }

    rctSig genRct(const ctkeyV & inSk, const ctkeyV  & inPk, const keyV & destinations, const vector<xmr_amount> amounts, const int mixin) {
        unsigned int index;
        ctkeyM mixRing;
        tie(mixRing, index) = populateFromBlockchain(inPk, mixin);
        return genRct(inSk, destinations, amounts, mixRing, index);
    }
    
    //RingCT protocol
    //genRct: 
    //   creates an rctSig with all data necessary to verify the rangeProofs and that the signer owns one of the
    //   columns that are claimed as inputs, and that the sum of inputs  = sum of outputs.
    //   Also contains masked "amount" and "mask" so the receiver can see how much they received
    //verRct:
    //   verifies that all signatures (rangeProogs, MG sig, sum inputs = outputs) are correct
    //decodeRct: (c.f. http://eprint.iacr.org/2015/1098 section 5.1.1)
    //   uses the attached ecdh info to find the amounts represented by each output commitment 
    //   must know the destination private key to find the correct amount, else will return a random number    
    bool verRct(const rctSig & rv) {
        CHECK_AND_ASSERT_THROW_MES(rv.outPk.size() == rv.rangeSigs.size(), "Mismatched sizes of rv.outPk and rv.rangeSigs");

        // some rct ops can throw
        try
        {
          size_t i = 0;
          bool rvb = true;
          bool tmp;
          DP("range proofs verified?");
          for (i = 0; i < rv.outPk.size(); i++) {
              tmp = verRange(rv.outPk[i].mask, rv.rangeSigs[i]);
              DP(tmp);
              rvb = (rvb && tmp);
          }
          //compute txn fee
          key txnFeeKey = scalarmultH(d2h(rv.txnFee));
          bool mgVerd = verRctMG(rv.MG, rv.mixRing, rv.outPk, txnFeeKey);
          DP("mg sig verified?");
          DP(mgVerd);

          return (rvb && mgVerd);
        }
        catch(...)
        {
          return false;
        }
    }
    
    //RingCT protocol
    //genRct: 
    //   creates an rctSig with all data necessary to verify the rangeProofs and that the signer owns one of the
    //   columns that are claimed as inputs, and that the sum of inputs  = sum of outputs.
    //   Also contains masked "amount" and "mask" so the receiver can see how much they received
    //verRct:
    //   verifies that all signatures (rangeProogs, MG sig, sum inputs = outputs) are correct
    //decodeRct: (c.f. http://eprint.iacr.org/2015/1098 section 5.1.1)
    //   uses the attached ecdh info to find the amounts represented by each output commitment 
    //   must know the destination private key to find the correct amount, else will return a random number    
    xmr_amount decodeRct(const rctSig & rv, const key & sk, unsigned int i, key & mask) {
        CHECK_AND_ASSERT_THROW_MES(rv.rangeSigs.size() > 0, "Empty rv.rangeSigs");
        CHECK_AND_ASSERT_THROW_MES(rv.outPk.size() == rv.rangeSigs.size(), "Mismatched sizes of rv.outPk and rv.rangeSigs");
        CHECK_AND_ASSERT_THROW_MES(i < rv.ecdhInfo.size(), "Bad index");

        //mask amount and mask
        ecdhTuple ecdh_info = rv.ecdhInfo[i];
        ecdhDecode(ecdh_info, sk);
        mask = ecdh_info.mask;
        key amount = ecdh_info.amount;
        key C = rv.outPk[i].mask;
        DP("C");
        DP(C);
        key Ctmp;
        addKeys2(Ctmp, mask, amount, H);
        DP("Ctmp");
        DP(Ctmp);
        if (equalKeys(C, Ctmp) == false) {
            CHECK_AND_ASSERT_THROW_MES(false, "warning, amount decoded incorrectly, will be unable to spend");
        }
        return h2d(amount);
    }

    xmr_amount decodeRct(const rctSig & rv, const key & sk, unsigned int i) {
      key mask;
      return decodeRct(rv, sk, i, mask);
    }
}
