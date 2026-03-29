#ifndef NIMBLE_COMPOSITE_HID_H
#define NIMBLE_COMPOSITE_HID_H

#include "nimconfig.h"
#include "BleConnectionStatus.h"
#include "NimBLEHIDDevice.h"
#include "NimBLECharacteristic.h"
#include "BLEHostConfiguration.h"
#include "BaseCompositeDevice.h"

#include <functional>
#include <vector>

/**
 * @brief NimBLE composite HID device.
 *
 * Standalone mode (no setServer call):
 *   begin() spawns a FreeRTOS task that calls NimBLEDevice::init(), creates a
 *   server, registers HID services, and starts advertising — self-contained.
 *
 * External-server mode (call setServer() before begin()):
 *   begin() runs synchronously on the provided server.  The caller is
 *   responsible for NimBLEDevice::init(), security config, and advertising.
 *   _connectionStatus is still registered as the server callback so that
 *   isConnected() and connection-parameter tuning work correctly.
 */
class BleCompositeHID {
public:
  BleCompositeHID(std::string deviceName = "NimBLE Composite HID",
                  std::string deviceManufacturer = "Unknown",
                  uint8_t batteryLevel = 100);
  ~BleCompositeHID();

  void setServer(NimBLEServer* server);

  void begin();
  void begin(const BLEHostConfiguration& config);
  void end();

  void addDevice(BaseCompositeDevice* device);
  bool isConnected();

  void setBatteryLevel(uint8_t level);

  // Called by BaseCompositeDevice::queueDeferredReport().
  // On all platforms reports are dispatched immediately; a SafeQueue-based
  // async path can be enabled on ESP32 in the future.
  void queueDeviceDeferredReport(std::function<void()>&& reportFunc);
  void sendDeferredReports();

  uint8_t batteryLevel;
  std::string deviceManufacturer;
  std::string deviceName;

protected:
  virtual void onStarted(NimBLEServer* pServer) {}

private:
  void _setupHid(NimBLEServer* pServer);
  static void taskServer(void* pvParameter);

  BLEHostConfiguration _configuration;
  BleConnectionStatus* _connectionStatus;
  NimBLEHIDDevice* _hid;
  NimBLEServer* _server = nullptr;

  // Cached & byte-swapped config values (set in begin() before _setupHid)
  uint16_t _vidSource;
  uint16_t _vid;
  uint16_t _pid;
  uint16_t _guidVersion;
  uint16_t _hidType;
  std::string _modelNumber;
  std::string _softwareRevision;
  std::string _serialNumber;
  std::string _firmwareRevision;
  std::string _hardwareRevision;

  std::vector<BaseCompositeDevice*> _devices;
  TaskHandle_t _autoSendTaskHandle;
};

#endif // NIMBLE_COMPOSITE_HID_H
