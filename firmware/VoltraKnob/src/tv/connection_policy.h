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
enum class SubscriptionChange { Ignore, Revoke, Renew };
// A CCCD transition is a logical app-session boundary, even when Android keeps
// the bonded physical connection alive after closing/reopening its GATT client.
inline SubscriptionChange subscriptionChange(bool samePeer,bool authorized,bool enabled,uint16_t value){
 if(!samePeer||!authorized||value>3)return SubscriptionChange::Ignore;
 if(!(value&1))return SubscriptionChange::Revoke;
 return enabled?SubscriptionChange::Ignore:SubscriptionChange::Renew;
}
inline uint32_t renewedEpoch(uint32_t previous,uint32_t random){
 const uint32_t next=random|1u;
 return next==previous?next+2u:next; // Odd + 2 remains nonzero, including wrap.
}
inline bool logicalSessionReady(bool authenticated,bool notifications,uint32_t epoch){
 return authenticated&&notifications&&epoch;
}
}
