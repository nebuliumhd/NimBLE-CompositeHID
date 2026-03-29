#include "XboxGamepadDevice.h"
#include "BleCompositeHID.h"
#include "nimble_composite_platform.h"

HID_DEFINE_TAG("XboxGamepadDevice");

XboxGamepadCallbacks::XboxGamepadCallbacks(XboxGamepadDevice* device)
    : _device(device) {
}

void XboxGamepadCallbacks::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
  XboxGamepadOutputReportData vibrationData = pCharacteristic->getValue<uint64_t>();
  HID_LOG_D(LOG_TAG, "onWrite – DC:%d weak:%d strong:%d dur:%d delay:%d loops:%d",
            vibrationData.dcEnableActuators,
            vibrationData.weakMotorMagnitude, vibrationData.strongMotorMagnitude,
            vibrationData.duration, vibrationData.startDelay, vibrationData.loopCount);
  _device->onVibrate.fire(vibrationData);
}

void XboxGamepadCallbacks::onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
  HID_LOG_D(LOG_TAG, "onRead");
}

void XboxGamepadCallbacks::onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) {
  HID_LOG_D(LOG_TAG, "onSubscribe subValue=%d", subValue);
}

void XboxGamepadCallbacks::onStatus(NimBLECharacteristic* pCharacteristic, int code) {
  HID_LOG_D(LOG_TAG, "onStatus code=%d", code);
}

XboxGamepadDevice::XboxGamepadDevice()
    : _config(new XboxOneSControllerDeviceConfiguration())
    , _extra_input(nullptr)
    , _callbacks(nullptr) {
}

// XboxGamepadDevice methods
XboxGamepadDevice::XboxGamepadDevice(XboxGamepadDeviceConfiguration* config)
    : _config(config)
    , _extra_input(nullptr)
    , _callbacks(nullptr) {
}

XboxGamepadDevice::~XboxGamepadDevice() {
  if (getOutput() && _callbacks) {
    getOutput()->setCallbacks(nullptr);
    delete _callbacks;
    _callbacks = nullptr;
  }

  if (_extra_input) {
    delete _extra_input;
    _extra_input = nullptr;
  }

  if (_config) {
    delete _config;
    _config = nullptr;
  }
}

void XboxGamepadDevice::init(NimBLEHIDDevice* hid) {
    /// Create input characteristic to send events to the computer
  auto input = hid->getInputReport(XBOX_INPUT_REPORT_ID);
    //_extra_input = hid->getInputReport(XBOX_EXTRA_INPUT_REPORT_ID);

    // Create output characteristic to handle events coming from the computer
  auto output = hid->getOutputReport(XBOX_OUTPUT_REPORT_ID);
  _callbacks = new XboxGamepadCallbacks(this);
  output->setCallbacks(_callbacks);

  setCharacteristics(input, output);
}

const BaseCompositeDeviceConfiguration* XboxGamepadDevice::getDeviceConfig() const {
    // Return the device configuration
  return _config;
}

void XboxGamepadDevice::resetInputs() {
  HID_LOCK(_mutex);
  memset(&_inputReport, 0, sizeof(XboxGamepadInputReportData));

  _inputReport.x = XBOX_AXIS_CENTER_OFFSET;
  _inputReport.y = XBOX_AXIS_CENTER_OFFSET;
  _inputReport.z = XBOX_AXIS_CENTER_OFFSET;
  _inputReport.rz = XBOX_AXIS_CENTER_OFFSET;
}

void XboxGamepadDevice::press(uint16_t button) {
  if (!isPressed(button)) {
    {
      HID_LOCK(_mutex);
      _inputReport.buttons |= button;
      HID_LOG_D(LOG_TAG, "press button=0x%04X", button);
    }

    if (_config->getAutoReport()) {
      sendGamepadReport();
    }
  }
}

void XboxGamepadDevice::release(uint16_t button) {
  if (isPressed(button)) {
    {
      HID_LOCK(_mutex);
      _inputReport.buttons ^= button;
      HID_LOG_D(LOG_TAG, "release button=0x%04X", button);
    }

    if (_config->getAutoReport()) {
      sendGamepadReport();
    }
  }
}

bool XboxGamepadDevice::isPressed(uint16_t button) {
  HID_LOCK(_mutex);
  return (bool)((_inputReport.buttons & button) == button);
}

void XboxGamepadDevice::setLeftThumb(int16_t x, int16_t y) {
  // We need to normalize the values for the sticks
  x = constrain(x, XBOX_STICK_MIN, XBOX_STICK_MAX) + XBOX_AXIS_CENTER_OFFSET;
  y = constrain(y, XBOX_STICK_MIN, XBOX_STICK_MAX) + XBOX_AXIS_CENTER_OFFSET;

  if (_inputReport.x != x || _inputReport.y != y) {
    {
      HID_LOCK(_mutex);
      _inputReport.x = (uint16_t)x;
      _inputReport.y = (uint16_t)y;
    }

    if (_config->getAutoReport()) {
      sendGamepadReport();
    }
  }
}

void XboxGamepadDevice::setRightThumb(int16_t z, int16_t rZ) {
  z = constrain(z, XBOX_STICK_MIN, XBOX_STICK_MAX) + XBOX_AXIS_CENTER_OFFSET;
  rZ = constrain(rZ, XBOX_STICK_MIN, XBOX_STICK_MAX) + XBOX_AXIS_CENTER_OFFSET;

  if (_inputReport.z != z || _inputReport.rz != rZ) {
    {
      HID_LOCK(_mutex);
      _inputReport.z = (uint16_t)z;
      _inputReport.rz = (uint16_t)rZ;
    }

    if (_config->getAutoReport()) {
      sendGamepadReport();
    }
  }
}

void XboxGamepadDevice::setLeftTrigger(uint16_t value) {
  value = constrain(value, XBOX_TRIGGER_MIN, XBOX_TRIGGER_MAX);

  if (_inputReport.brake != value) {
    {
      HID_LOCK(_mutex);
      _inputReport.brake = value;
    }

    if (_config->getAutoReport()) {
      sendGamepadReport();
    }
  }
}

void XboxGamepadDevice::setRightTrigger(uint16_t value) {
  value = constrain(value, XBOX_TRIGGER_MIN, XBOX_TRIGGER_MAX);

  if (_inputReport.accelerator != value) {
    {
      HID_LOCK(_mutex);
      _inputReport.accelerator = value;
    }

    if (_config->getAutoReport()) {
      sendGamepadReport();
    }
  }
}

void XboxGamepadDevice::setTriggers(uint16_t left, uint16_t right) {
  left = constrain(left, XBOX_TRIGGER_MIN, XBOX_TRIGGER_MAX);
  right = constrain(right, XBOX_TRIGGER_MIN, XBOX_TRIGGER_MAX);

  if (_inputReport.brake != left || _inputReport.accelerator != right) {
    {
      HID_LOCK(_mutex);
      _inputReport.brake = left;
      _inputReport.accelerator = right;
    }
    if (_config->getAutoReport()) {
      sendGamepadReport();
    }
  }
}

void XboxGamepadDevice::pressDPadDirection(uint8_t direction) {
  if (!isDPadPressed(direction)) {
    HID_LOG_D(LOG_TAG, "pressDPad direction=%s", dPadDirectionName(direction).c_str());
    {
      HID_LOCK(_mutex);
      _inputReport.hat = direction;
    }

    if (_config->getAutoReport()) {
      sendGamepadReport();
    }
  }
}

void XboxGamepadDevice::pressDPadDirectionFlag(XboxDpadFlags direction) {
  if ((direction & (XboxDpadFlags::NORTH | XboxDpadFlags::SOUTH)) == (XboxDpadFlags::NORTH | XboxDpadFlags::SOUTH)) {
    HID_LOG_D(LOG_TAG, "Filtering opposite dpad N+S");
    direction = (XboxDpadFlags)(direction ^ (uint8_t)(XboxDpadFlags::NORTH | XboxDpadFlags::SOUTH));
  }
  if ((direction & (XboxDpadFlags::EAST | XboxDpadFlags::WEST)) == (XboxDpadFlags::EAST | XboxDpadFlags::WEST)) {
    HID_LOG_D(LOG_TAG, "Filtering opposite dpad E+W");
    direction = (XboxDpadFlags)(direction ^ (uint8_t)(XboxDpadFlags::EAST | XboxDpadFlags::WEST));
  }

  pressDPadDirection(dPadDirectionToValue(direction));
}

void XboxGamepadDevice::releaseDPad() {
  pressDPadDirection(XBOX_BUTTON_DPAD_NONE);
}

bool XboxGamepadDevice::isDPadPressed(uint8_t direction) {
  HID_LOCK(_mutex);
  return _inputReport.hat == direction;
}

bool XboxGamepadDevice::isDPadPressedFlag(XboxDpadFlags direction) {
  HID_LOCK(_mutex);
  if (direction == XboxDpadFlags::NORTH) {
    return _inputReport.hat == XBOX_BUTTON_DPAD_NORTH;
  } else if (direction == (XboxDpadFlags::NORTH & XboxDpadFlags::EAST)) {
    return _inputReport.hat == XBOX_BUTTON_DPAD_NORTHEAST;
  } else if (direction == XboxDpadFlags::EAST) {
    return _inputReport.hat == XBOX_BUTTON_DPAD_EAST;
  } else if (direction == (XboxDpadFlags::SOUTH & XboxDpadFlags::EAST)) {
    return _inputReport.hat == XBOX_BUTTON_DPAD_SOUTHEAST;
  } else if (direction == XboxDpadFlags::SOUTH) {
    return _inputReport.hat == XBOX_BUTTON_DPAD_SOUTH;
  } else if (direction == (XboxDpadFlags::SOUTH & XboxDpadFlags::WEST)) {
    return _inputReport.hat == XBOX_BUTTON_DPAD_SOUTHWEST;
  } else if (direction == XboxDpadFlags::WEST) {
    return _inputReport.hat == XBOX_BUTTON_DPAD_WEST;
  } else if (direction == (XboxDpadFlags::NORTH & XboxDpadFlags::WEST)) {
    return _inputReport.hat == XBOX_BUTTON_DPAD_NORTHWEST;
  }
  return false;
}

void XboxGamepadDevice::pressShare() {
  if (!(_inputReport.share & XBOX_BUTTON_SHARE)) {
    {
      HID_LOCK(_mutex);
      _inputReport.share |= XBOX_BUTTON_SHARE;
    }

    if (_config->getAutoReport()) {
      sendGamepadReport();
    }
  }
}

void XboxGamepadDevice::releaseShare() {
  if (_inputReport.share & XBOX_BUTTON_SHARE) {
    {
      HID_LOCK(_mutex);
      _inputReport.share ^= XBOX_BUTTON_SHARE;
    }

    if (_config->getAutoReport()) {
      sendGamepadReport();
    }
  }
}

void XboxGamepadDevice::sendGamepadReport(bool defer) {
  if (defer || _config->getAutoDefer()) {
    queueDeferredReport(std::bind(&XboxGamepadDevice::sendGamepadReportImpl, this));
  } else {
    sendGamepadReportImpl();
  }
}

void XboxGamepadDevice::sendGamepadReportImpl() {
  auto input = getInput();
  auto parentDevice = this->getParent();

  if (!input || !parentDevice)
    return;

  if (!parentDevice->isConnected())
    return;

  {
    HID_LOCK(_mutex);
    size_t packedSize = sizeof(_inputReport);
    HID_LOG_D(LOG_TAG, "sendReport size=%d", packedSize);
    input->setValue((uint8_t*)&_inputReport, packedSize);
  }
  input->notify();
}
