#pragma once
namespace tvbridge {
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
