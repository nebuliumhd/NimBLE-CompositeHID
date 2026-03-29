#ifndef NIMBLE_COMPOSITE_HID_CONNECTION_STATUS_H
#define NIMBLE_COMPOSITE_HID_CONNECTION_STATUS_H

#include "nimconfig.h"
#include <NimBLEServer.h>
#include "NimBLECharacteristic.h"
#include "NimBLEConnInfo.h"

class BleConnectionStatus : public NimBLEServerCallbacks {
public:
  BleConnectionStatus(void);
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;
  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override;
  bool isConnected();

private:
  bool connected = false;
};

#endif // NIMBLE_COMPOSITE_HID_CONNECTION_STATUS_H
