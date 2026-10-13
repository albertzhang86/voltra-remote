#pragma once
namespace tvbridge {
void begin();
void loop();
void openPairing();
void disconnect();
bool connected();
bool pairing();
int passkey();
}
