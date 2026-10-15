#pragma once
#include <cstdint>

namespace tvbridge {
inline bool windowOpen(uint32_t until,uint32_t now){
 return until&&static_cast<int32_t>(until-now)>0;
}
// Initial handshake admission is not authorization: private identities may only
// resolve during encryption. allowAuthentication performs the final trust check.
inline bool allowConnection(bool accepting,bool pairing,bool hasTrustedBond,bool occupied){
 return accepting&&!occupied&&(pairing||hasTrustedBond);
}
inline bool allowAuthentication(bool encrypted,bool authenticated,bool bonded,
                                bool admittedTrusted,bool pairing,bool rejectedPairing){
 return encrypted&&authenticated&&bonded&&!rejectedPairing&&(admittedTrusted||pairing);
}
}
