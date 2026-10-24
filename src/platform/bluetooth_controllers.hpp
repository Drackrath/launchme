#pragma once
#include "../model/devices.hpp"
#include <functional>

// Connected controllers plus remembered Classic Bluetooth gamepads. These rows
// belong only to the devices panel, never to the couch counter or input reader.
std::vector<devices::Controller> controllerPanelControllers();
std::wstring setBluetoothControllerConnection(const std::wstring& id, bool connect,
                                             const std::function<bool()>& cancelled);
