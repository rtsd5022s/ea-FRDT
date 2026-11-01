// engine_dpd_codec.h — Shared DPD triplet codec: 3 BCD digits (0-999) <-> 10-bit DPD.
// Reused by dpd32, dpd64, dpd128 (see formats/dpd32.h, dpd64.h, dpd128.h).
#pragma once
#include "common.h"
#include <cstring>

namespace fw {

// Encode: d2 d1 d0 each 0-9 (BCD), return 10-bit DPD
inline unsigned dpd_encode3(unsigned d2, unsigned d1, unsigned d0) {
    unsigned a3=d2>>3, a2=(d2>>2)&1, a1=(d2>>1)&1, a0=d2&1;
    unsigned b3=d1>>3, b2=(d1>>2)&1, b1=(d1>>1)&1, b0=d1&1;
    unsigned c3=d0>>3, c2=(d0>>2)&1, c1=(d0>>1)&1, c0=d0&1;
    unsigned w=(a3<<2)|(b3<<1)|c3;
    switch (w) {
    case 0: // 0 a2a1a0 b2b1b0 c2c1c0
        return (a2<<8)|(a1<<7)|(a0<<6)|(b2<<5)|(b1<<4)|(b0<<3)|(c2<<2)|(c1<<1)|c0;
    case 1: // 1 00 a2a1a0 b2b1b0 c0
        return (1u<<9)|(a2<<6)|(a1<<5)|(a0<<4)|(b2<<3)|(b1<<2)|(b0<<1)|c0;
    case 2: // 1 01 a2a1a0 b0 c2c1c0
        return (1u<<9)|(1u<<7)|(a2<<6)|(a1<<5)|(a0<<4)|(b0<<3)|(c2<<2)|(c1<<1)|c0;
    case 4: // 1 11 a0 b2b1b0 c2c1c0
        return (1u<<9)|(1u<<8)|(1u<<7)|(a0<<6)|(b2<<5)|(b1<<4)|(b0<<3)|(c2<<2)|(c1<<1)|c0;
    case 3: // 1 10 00 a2a1a0 b0 c0
        return (1u<<9)|(1u<<8)|(0u<<7)|(0u<<6)|(0u<<5)|(a2<<4)|(a1<<3)|(a0<<2)|(b0<<1)|c0;
    case 5: // 1 10 01 a0 b2b1b0 c0
        return (1u<<9)|(1u<<8)|(0u<<7)|(0u<<6)|(1u<<5)|(a0<<4)|(b2<<3)|(b1<<2)|(b0<<1)|c0;
    case 6: // 1 10 10 a0 b0 c2c1c0
        return (1u<<9)|(1u<<8)|(0u<<7)|(1u<<6)|(0u<<5)|(a0<<4)|(b0<<3)|(c2<<2)|(c1<<1)|c0;
    case 7: // 1 10 11 a0 b0 c0 00
        return (1u<<9)|(1u<<8)|(0u<<7)|(1u<<6)|(1u<<5)|(a0<<4)|(b0<<3)|(c0<<2);
    }
    return 0;
}

// Decode: 10-bit DPD -> 3 BCD digits (0-9)
inline void dpd_decode3(unsigned dpd, unsigned& d2, unsigned& d1, unsigned& d0) {
    if (!(dpd & 0x200u)) {
        // case 0: 0 a2a1a0 b2b1b0 c2c1c0
        d2 = (dpd>>6)&7; d1 = (dpd>>3)&7; d0 = dpd&7;
        return;
    }
    unsigned p8=(dpd>>8)&1, p7=(dpd>>7)&1;
    unsigned b6=(dpd>>6)&1, b5=(dpd>>5)&1, b4=(dpd>>4)&1;
    unsigned b3=(dpd>>3)&1, b2=(dpd>>2)&1, b1=(dpd>>1)&1, b0=dpd&1;
    if (p8==0 && p7==0) {
        // w=1: d0>=8
        d2=(b6<<2)|(b5<<1)|b4; d1=(b3<<2)|(b2<<1)|b1; d0=8|b0;
    } else if (p8==0 && p7==1) {
        // w=2: d1>=8
        d2=(b6<<2)|(b5<<1)|b4; d1=8|b3; d0=(b2<<2)|(b1<<1)|b0;
    } else if (p8==1 && p7==1) {
        // w=4: d2>=8
        d2=8|b6; d1=(b5<<2)|(b4<<1)|b3; d0=(b2<<2)|(b1<<1)|b0;
    } else {
        // p8=1,p7=0: w=3/5/6/7, sub-code b6b5
        if (b6==0 && b5==0) {
            // w=3: d1>=8, d0>=8
            d2=(b4<<2)|(b3<<1)|b2; d1=8|b1; d0=8|b0;
        } else if (b6==0 && b5==1) {
            // w=5: d2>=8, d0>=8
            d2=8|b4; d1=(b3<<2)|(b2<<1)|b1; d0=8|b0;
        } else if (b6==1 && b5==0) {
            // w=6: d2>=8, d1>=8
            d2=8|b4; d1=8|b3; d0=(b2<<2)|(b1<<1)|b0;
        } else {
            // w=7: all >=8
            d2=8|b4; d1=8|b3; d0=8|b2;
        }
    }
}

} // namespace fw
