#pragma once
namespace tvbridge {
struct MixedTwinOffer {
 bool available=false,pending=false,owned=false;
 char peerAddress[18]{},peerName[32]{},message[64]{};
};
MixedTwinOffer mixedTwinOffer();
bool canRequestMixedTwin();
bool requestMixedTwin();
void begin();
void loop();
void openPairing();
void disconnect();
void findTrainers();
bool forgetTvs();
bool connected();
bool trusted();
bool findingTrainers();
bool pairing();
int passkey();
}
