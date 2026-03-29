#include "nimconfig.h"
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include "NimBLEHIDDevice.h"
#include "HIDTypes.h"
#include "HIDKeyboardTypes.h"

#include "BleCompositeHID.h"
#include "BleConnectionStatus.h"
#include "nimble_composite_platform.h"

HID_DEFINE_TAG("BleCompositeHID");

#define NIMBLE_COMPOSITE_HID_NAME_MAX_LEN 31

#define SERVICE_UUID_DEVICE_INFORMATION   "180A"
#define CHARACTERISTIC_UUID_MODEL_NUMBER  "2A24"
#define CHARACTERISTIC_UUID_SW_REVISION   "2A28"
#define CHARACTERISTIC_UUID_SERIAL_NUMBER "2A25"
#define CHARACTERISTIC_UUID_FW_REVISION   "2A26"
#define CHARACTERISTIC_UUID_HW_REVISION   "2A27"

// ─────────────────────────────────────────────────────────────────────────────

BleCompositeHID::BleCompositeHID(std::string deviceName,
                                 std::string deviceManufacturer,
                                 uint8_t batteryLevel)
    : _hid(nullptr), _server(nullptr)
{
  this->deviceName = deviceName.substr(0, NIMBLE_COMPOSITE_HID_NAME_MAX_LEN - 1);
  this->deviceManufacturer = deviceManufacturer;
  this->batteryLevel = batteryLevel;
  this->_connectionStatus = new BleConnectionStatus();
}

BleCompositeHID::~BleCompositeHID() {
  delete this->_connectionStatus;
}

void BleCompositeHID::setServer(NimBLEServer* server) {
  _server = server;
}

void BleCompositeHID::begin() {
  this->begin(BLEHostConfiguration());
}

void BleCompositeHID::begin(const BLEHostConfiguration& config) {
  _configuration = config;

  _modelNumber      = _configuration.getModelNumber();
  _softwareRevision = _configuration.getSoftwareRevision();
  _serialNumber     = _configuration.getSerialNumber();
  _firmwareRevision = _configuration.getFirmwareRevision();
  _hardwareRevision = _configuration.getHardwareRevision();

  _vidSource   = _configuration.getVidSource();
  _vid         = _configuration.getVid();
  _pid         = _configuration.getPid();
  _guidVersion = _configuration.getGuidVersion();
  _hidType     = _configuration.getHidType();

#ifndef PNPVersionField
  // Legacy byte-swap required for NimBLE-Arduino <= 1.4.1.
  auto swapBytes = [](uint16_t v) -> uint16_t {
    return (uint16_t)((lowByte(v) << 8) | highByte(v));
  };
  _vid         = swapBytes(_vid);
  _pid         = swapBytes(_pid);
  _guidVersion = swapBytes(_guidVersion);
#endif

  if (_server != nullptr) {
    // External-server mode: synchronous — caller owns init, security, advertising.
    HID_LOG_I(LOG_TAG, "Using external server, setting up HID synchronously");
    _setupHid(_server);
  } else {
    // Standalone mode: spawn FreeRTOS task that creates server and handles advertising.
    HID_LOG_I(LOG_TAG, "Standalone mode — spawning server task");
    xTaskCreate(BleCompositeHID::taskServer, "hid_server", 20000, (void*)this, 5, NULL);
  }
}

void BleCompositeHID::end() {
  vTaskDelete(_autoSendTaskHandle);
}

bool BleCompositeHID::isConnected() {
  return _connectionStatus->isConnected();
}

void BleCompositeHID::setBatteryLevel(uint8_t level) {
  batteryLevel = level;
  if (_hid) {
    _hid->setBatteryLevel(batteryLevel, isConnected());
  }
}

void BleCompositeHID::addDevice(BaseCompositeDevice* device) {
  device->_parent = this;
  _devices.push_back(device);
}

void BleCompositeHID::queueDeviceDeferredReport(std::function<void()>&& reportFunc) {
  // Cross-platform: execute immediately.
  // A SafeQueue-based async path for ESP32 can be added behind
  // #if defined(ESP32) && defined(NIMBLE_HID_ENABLE_QUEUE) in the future.
  reportFunc();
}

void BleCompositeHID::sendDeferredReports() {
  // Reports are dispatched immediately in queueDeviceDeferredReport; nothing to flush.
}

// ─────────────────────────────────────────────────────────────────────────────
// Core HID setup — shared by both standalone and external-server modes.
// Does NOT call NimBLEDevice::init(), set security auth, or start advertising.
// ─────────────────────────────────────────────────────────────────────────────

void BleCompositeHID::_setupHid(NimBLEServer* pServer) {
  pServer->setCallbacks(_connectionStatus);
  pServer->advertiseOnDisconnect(true);

  _hid = new NimBLEHIDDevice(pServer);

  HID_LOG_D(LOG_TAG, "Building HID report descriptor from %d device(s)", _devices.size());

  // Build composite HID report descriptor from all registered devices.
  const size_t kBufSize = 2048;
  uint8_t tempDesc[kBufSize];
  int descSize = 0;

  for (auto device : _devices) {
    device->init(_hid);

    auto* cfg     = device->getDeviceConfig();
    size_t devSize = cfg->makeDeviceReport(tempDesc + descSize, kBufSize);

    if (devSize == 0 || devSize >= BLE_ATT_ATTR_MAX_LEN) {
      HID_LOG_E(LOG_TAG, "Device report size %d invalid (max %d)", devSize, BLE_ATT_ATTR_MAX_LEN);
      return;
    }
    descSize += (int)devSize;
    HID_LOG_D(LOG_TAG, "Device '%s' report size: %d", cfg->getDeviceName(), devSize);
  }

  HID_LOG_D(LOG_TAG, "Total HID descriptor size: %d", descSize);

  uint8_t reportMap[descSize];
  memcpy(reportMap, tempDesc, descSize);
  _hid->setReportMap(reportMap, descSize);
  _hid->setManufacturer(deviceManufacturer);

  // Populate Device Information Service characteristics.
  NimBLEService* pDIS = pServer->getServiceByUUID(SERVICE_UUID_DEVICE_INFORMATION);
  if (pDIS) {
    auto addStr = [&](const char* uuid, const std::string& val) {
      BLECharacteristic* c = pDIS->createCharacteristic(uuid, NIMBLE_PROPERTY::READ);
      c->setValue(val);
    };
    addStr(CHARACTERISTIC_UUID_MODEL_NUMBER,  _modelNumber);
    addStr(CHARACTERISTIC_UUID_SW_REVISION,   _softwareRevision);
    addStr(CHARACTERISTIC_UUID_SERIAL_NUMBER, _serialNumber);
    addStr(CHARACTERISTIC_UUID_FW_REVISION,   _firmwareRevision);
    addStr(CHARACTERISTIC_UUID_HW_REVISION,   _hardwareRevision);
  }

  _hid->setPnp(_vidSource, _vid, _pid, _guidVersion);
  _hid->setHidInfo(0x00, 0x01);

  // In NimBLE-Arduino 2.x services are registered when the server is started;
  // startServices() is deprecated (stub that returns void/true). Omitting it here
  // is correct — the services will be live once advertising begins.

  _hid->setBatteryLevel(batteryLevel);

  onStarted(pServer);
  HID_LOG_I(LOG_TAG, "HID setup complete (VID=0x%04X PID=0x%04X)", _vid, _pid);
}

// ─────────────────────────────────────────────────────────────────────────────
// Standalone task — owns the full BLE lifecycle when no external server given.
// ─────────────────────────────────────────────────────────────────────────────

void BleCompositeHID::taskServer(void* pvParameter) {
  BleCompositeHID* self = (BleCompositeHID*)pvParameter;

  NimBLEDevice::init(self->deviceName);
  NimBLEDevice::setSecurityAuth(true, false, false); // bond, no MITM, no SC

  NimBLEServer* pServer = NimBLEDevice::createServer();
  self->_setupHid(pServer);

  NimBLEAdvertising* pAdv = pServer->getAdvertising();
  pAdv->setAppearance(self->_hidType);
  pAdv->setName(self->deviceName);
  pAdv->addServiceUUID(self->_hid->getHidService()->getUUID());
  pAdv->start();

  HID_LOG_I(LOG_TAG, "Standalone advertising started");

  vTaskDelay(portMAX_DELAY);
}
