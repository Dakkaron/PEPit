// ...existing code...

#include "bluetoothHandler.h"
#include <NimBLEDevice.h>
#include <NuSerial.hpp>

#define TARGET_NAME       "PEPit"
#define DEVICE_NAME       "PEPit-Tramopoline"
#define PAIRING_PASSKEY   123456

static NimBLEClient* pClient = nullptr;
static NimBLERemoteCharacteristic* pRxChar = nullptr; // Central -> Peripheral (write)
static NimBLERemoteCharacteristic* pTxChar = nullptr; // Peripheral -> Central (notify)
static String inputBuffer = "";
static bool connected = false;

// UUIDs for UART service/characteristics (NuSerial or Nordic UART Service)
static const NimBLEUUID UART_SERVICE_UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static const NimBLEUUID UART_RX_UUID("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"); // Write
static const NimBLEUUID UART_TX_UUID("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"); // Notify


// Use a lambda for notify callback (NimBLE expects std::function, not a class)
static void onTxNotify(NimBLERemoteCharacteristic* pRC, uint8_t* pData, size_t len, bool isNotify) {
  for (size_t i = 0; i < len; ++i) {
    char c = (char)pData[i];
    inputBuffer += c;
  }
}

void initBluetooth() {
  NimBLEDevice::init(DEVICE_NAME);
}

void connectBluetooth() {	
	NimBLEScan* pScan = NimBLEDevice::getScan();
	pScan->setActiveScan(true);
	// Set scan interval and window for maximum coverage
	pScan->setInterval(0x50); // 80 units = 50ms
	pScan->setWindow(0x50);   // 80 units = 50ms (window <= interval)
	pScan->start(1000);
	while (pScan->isScanning()) {
		delay(100);
	}
	NimBLEScanResults results = pScan->getResults();
	Serial.print("Scan complete, found ");
	Serial.print(results.getCount());
	Serial.println(" devices");
	for (int i = 0; i < results.getCount(); ++i) {
		const NimBLEAdvertisedDevice* dev = results.getDevice(i);
		Serial.print("Device ");
		Serial.print(i);
		Serial.print(": Address: ");
		Serial.print(dev->getAddress().toString().c_str());
		Serial.print(", Name: ");
		Serial.print(dev->getName().c_str());
		Serial.print(", ServiceUUIDs: ");
		for (int j = 0; j < dev->getServiceUUIDCount(); ++j) {
			Serial.print(dev->getServiceUUID(j).toString().c_str());
			Serial.print(" ");
		}
		Serial.println();
		// Match by name or by UART service UUID
		bool match = false;
		if (dev->getName() == TARGET_NAME) {
			match = true;
		} else {
			for (int j = 0; j < dev->getServiceUUIDCount(); ++j) {
				if (dev->getServiceUUID(j).equals(UART_SERVICE_UUID)) {
					match = true;
					break;
				}
			}
		}
		if (match) {
			Serial.println("Target device matched, attempting to connect...");
			pClient = NimBLEDevice::createClient();
			if (!pClient->connect(dev)) {
				Serial.println("Failed to connect to device");
				pClient = nullptr;
				continue;
			}
			NimBLERemoteService* pService = pClient->getService(UART_SERVICE_UUID);
			if (!pService) {
				Serial.println("Failed to find UART service");
				pClient->disconnect();
				pClient = nullptr;
				continue;
			}
			pRxChar = pService->getCharacteristic(UART_RX_UUID);
			pTxChar = pService->getCharacteristic(UART_TX_UUID);
			if (!pRxChar || !pTxChar) {
				Serial.println("Failed to find UART characteristics");
				pClient->disconnect();
				pClient = nullptr;
				continue;
			}
			if (pTxChar->canNotify()) {
				Serial.println("Subscribed to TX characteristic");
				pTxChar->subscribe(true, onTxNotify);
			}
			connected = true;
			break;
		}
	}
}

bool isConnected() {
	return connected && pClient && pClient->isConnected();
}

void sendLine(String data) {
	if (isConnected() && pRxChar && pRxChar->canWrite()) {
		String toSend = data + "\n";
		pRxChar->writeValue((uint8_t*)toSend.c_str(), toSend.length(), false);
	}
}

String getLine() {
	int idx = inputBuffer.indexOf('\n');
	if (idx != -1) {
		String line = inputBuffer.substring(0, idx);
		inputBuffer = inputBuffer.substring(idx + 1);
		line.trim();
		return line;
	}
	return "";
}

