/*
 * NimBLEService.cpp
 *
 *  Created: on March 2, 2020
 *      Author H2zero
 * 
 * Originally:
 *
 * BLEService.cpp
 *
 *  Created on: Mar 25, 2017
 *      Author: kolban
 */

// A service is identified by a UUID.  A service is also the container for one or more characteristics.

#include "sdkconfig.h"
#if defined(CONFIG_BT_ENABLED)
//#include <esp_err.h>
//#include <esp_gatts_api.h>

#include "NimBLEService.h"
#include "NimBLEUtils.h"
#include "NimBLELog.h"

//#include <iomanip>
//#include <sstream>
#include <string>

//#include "BLEServer.h"
//#include "GeneralUtils.h"

static const char* LOG_TAG = "NimBLEService"; // Tag for logging.

#define NULL_HANDLE (0xffff)


/**
 * @brief Construct an instance of the BLEService
 * @param [in] uuid The UUID of the service.
 * @param [in] numHandles The maximum number of handles associated with the service.
 */
NimBLEService::NimBLEService(const char* uuid, uint16_t numHandles) : NimBLEService(NimBLEUUID(uuid), numHandles) {
}


/**
 * @brief Construct an instance of the BLEService
 * @param [in] uuid The UUID of the service.
 * @param [in] numHandles The maximum number of handles associated with the service.
 */
NimBLEService::NimBLEService(NimBLEUUID uuid, uint16_t numHandles) {
	m_uuid      = uuid;
	m_handle    = NULL_HANDLE;
	m_pServer   = nullptr;
	//m_serializeMutex.setName("BLEService");
	m_lastCreatedCharacteristic = nullptr;
	m_numHandles = numHandles;
} // NimBLEService


/**
 * @brief Create the service.
 * Create the service.
 * @param [in] gatts_if The handle of the GATT server interface.
 * @return N/A.
 */

void NimBLEService::executeCreate(NimBLEServer* pServer) {
	NIMBLE_LOGD(LOG_TAG, ">> executeCreate() - Creating service service uuid: %s", getUUID().toString().c_str());
	m_pServer          = pServer;
//	m_semaphoreCreateEvt.take("executeCreate"); // Take the mutex and release at event ESP_GATTS_CREATE_EVT

//	esp_gatt_srvc_id_t srvc_id;
//	srvc_id.is_primary = true;
//	srvc_id.id.inst_id = m_instId;
//	srvc_id.id.uuid    = *m_uuid.getNative();
//	esp_err_t errRc = ::esp_ble_gatts_create_service(getServer()->getGattsIf(), &srvc_id, m_numHandles); // The maximum number of handles associated with the service.

//	if (errRc != ESP_OK) {
//		ESP_LOGE(LOG_TAG, "esp_ble_gatts_create_service: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
//		return;
//	}

//	m_semaphoreCreateEvt.wait("executeCreate");
	NIMBLE_LOGD(LOG_TAG, "<< executeCreate");
} // executeCreate


/**
 * @brief Delete the service.
 * Delete the service.
 * @return N/A.
 */

void NimBLEService::executeDelete() {
	NIMBLE_LOGD(LOG_TAG, ">> executeDelete()");
//	m_semaphoreDeleteEvt.take("executeDelete"); // Take the mutex and release at event ESP_GATTS_DELETE_EVT

//	esp_err_t errRc = ::esp_ble_gatts_delete_service(getHandle());

//	if (errRc != ESP_OK) {
//		ESP_LOGE(LOG_TAG, "esp_ble_gatts_delete_service: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
//		return;
//	}

//	m_semaphoreDeleteEvt.wait("executeDelete");
	NIMBLE_LOGD(LOG_TAG, "<< executeDelete");
} // executeDelete


/**
 * @brief Dump details of this BLE GATT service.
 * @return N/A.
 */
void NimBLEService::dump() {
	NIMBLE_LOGD(LOG_TAG, "Service: uuid:%s, handle: 0x%.2x",
		m_uuid.toString().c_str(),
		m_handle);
//	NIMBLE_LOGD(LOG_TAG, "Characteristics:\n%s", m_characteristicMap.toString().c_str());
} // dump


/**
 * @brief Get the UUID of the service.
 * @return the UUID of the service.
 */
NimBLEUUID NimBLEService::getUUID() {
	return m_uuid;
} // getUUID


/**
 * @brief Start the service.
 * Here we wish to start the service which means that we will respond to partner requests about it.
 * Starting a service also means that we can create the corresponding characteristics.
 * @return Start the service.
 */
 
bool NimBLEService::start() {
// We ask the BLE runtime to start the service and then create each of the characteristics.
// We start the service through its local handle which was returned in the ESP_GATTS_CREATE_EVT event
// obtained as a result of calling esp_ble_gatts_create_service().
//
	NIMBLE_LOGD(LOG_TAG, ">> start(): Starting service (esp_ble_gatts_start_service): %s", toString().c_str());
    
	int rc = 0;
	// Nimble requires an array of services to be sent to the api
	// Since we are adding 1 at a time we create an array of 2 and set the type
	// of the second service to 0 to indicate the end of the array.
    ble_gatt_svc_def svc[2];
	ble_gatt_chr_def* pChtr_a = nullptr;
    
    svc[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    svc[0].uuid = (const ble_uuid_t*)&m_uuid.getNative()->u;
	uint8_t numChtrs = m_characteristicMap.getSize();
	if(numChtrs < 1){
		svc[0].characteristics = NULL;
	}else{
		pChtr_a = new ble_gatt_chr_def[numChtrs+1];
		NimBLECharacteristic* pCharacteristic = m_characteristicMap.getFirst();
		for(int i=0; i < numChtrs; i++) {
			pChtr_a[i].uuid = (const ble_uuid_t*)&pCharacteristic->getUUID().getNative()->u;
			pCharacteristic = m_characteristicMap.getNext();
		}
		
		pChtr_a[numChtrs].uuid = NULL;
		svc[0].characteristics = (const ble_gatt_chr_def*)pChtr_a;
	}
	
	svc[1].type = 0;
    
    rc = ble_gatts_count_cfg((const ble_gatt_svc_def*)&svc);
    if (rc != 0) {
        NIMBLE_LOGE(LOG_TAG, "ble_gatts_count_cfg failed, rc= %d, %s", rc, NimBLEUtils::returnCodeToString(rc));
		if(pChtr_a != nullptr) {
			delete[] pChtr_a;
		}
        return false;
    }
    
    rc = ble_gatts_add_svcs((const ble_gatt_svc_def*)&svc);
    if (rc != 0) {
        NIMBLE_LOGE(LOG_TAG, "ble_gatts_add_svcs, rc= %d, %s", rc, NimBLEUtils::returnCodeToString(rc));
		if(pChtr_a != nullptr) {
			delete[] pChtr_a;
		}
        return false;
    }
    
	if(pChtr_a != nullptr) {
		delete[] pChtr_a;
	}
	
    return true;
/*	if (m_handle == NULL_HANDLE) {
		ESP_LOGE(LOG_TAG, "<< !!! We attempted to start a service but don't know its handle!");
		return;
	}

	BLECharacteristic *pCharacteristic = m_characteristicMap.getFirst();

	while (pCharacteristic != nullptr) {
		m_lastCreatedCharacteristic = pCharacteristic;
		pCharacteristic->executeCreate(this);

		pCharacteristic = m_characteristicMap.getNext();
	}
	// Start each of the characteristics ... these are found in the m_characteristicMap.

	m_semaphoreStartEvt.take("start");
	esp_err_t errRc = ::esp_ble_gatts_start_service(m_handle);

	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "<< esp_ble_gatts_start_service: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
		return;
	}
	m_semaphoreStartEvt.wait("start");
*/
	NIMBLE_LOGD(LOG_TAG, "<< start()");
} // start


/**
 * @brief Stop the service.
 */
 /*
void BLEService::stop() {
// We ask the BLE runtime to start the service and then create each of the characteristics.
// We start the service through its local handle which was returned in the ESP_GATTS_CREATE_EVT event
// obtained as a result of calling esp_ble_gatts_create_service().
	ESP_LOGD(LOG_TAG, ">> stop(): Stopping service (esp_ble_gatts_stop_service): %s", toString().c_str());
	if (m_handle == NULL_HANDLE) {
		ESP_LOGE(LOG_TAG, "<< !!! We attempted to stop a service but don't know its handle!");
		return;
	}

	m_semaphoreStopEvt.take("stop");
	esp_err_t errRc = ::esp_ble_gatts_stop_service(m_handle);

	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "<< esp_ble_gatts_stop_service: rc=%d %s", errRc, GeneralUtils::errorToString(errRc));
		return;
	}
	m_semaphoreStopEvt.wait("stop");

	ESP_LOGD(LOG_TAG, "<< stop()");
} // start
*/

/**
 * @brief Set the handle associated with this service.
 * @param [in] handle The handle associated with the service.
 */
void NimBLEService::setHandle(uint16_t handle) {
	NIMBLE_LOGD(LOG_TAG, ">> setHandle - Handle=0x%.2x, service UUID=%s)", handle, getUUID().toString().c_str());
	if (m_handle != NULL_HANDLE) {
		NIMBLE_LOGE(LOG_TAG, "!!! Handle is already set %.2x", m_handle);
		return;
	}
	m_handle = handle;
	NIMBLE_LOGD(LOG_TAG, "<< setHandle");
} // setHandle


/**
 * @brief Get the handle associated with this service.
 * @return The handle associated with this service.
 */
uint16_t NimBLEService::getHandle() {
	return m_handle;
} // getHandle


/**
 * @brief Add a characteristic to the service.
 * @param [in] pCharacteristic A pointer to the characteristic to be added.
 */
void NimBLEService::addCharacteristic(NimBLECharacteristic* pCharacteristic) {
	// We maintain a mapping of characteristics owned by this service.  These are managed by the
	// BLECharacteristicMap class instance found in m_characteristicMap.  We add the characteristic
	// to the map and then ask the service to add the characteristic at the BLE level (ESP-IDF).

	NIMBLE_LOGD(LOG_TAG, ">> addCharacteristic()");
	NIMBLE_LOGD(LOG_TAG, "Adding characteristic: uuid=%s to service: %s",
		pCharacteristic->getUUID().toString().c_str(),
		toString().c_str());

	// Check that we don't add the same characteristic twice.
	if (m_characteristicMap.getByUUID(pCharacteristic->getUUID()) != nullptr) {
		NIMBLE_LOGW(LOG_TAG, "<< Adding a new characteristic with the same UUID as a previous one");
		//return;
	}

	// Remember this characteristic in our map of characteristics.  At this point, we can lookup by UUID
	// but not by handle.  The handle is allocated to us on the ESP_GATTS_ADD_CHAR_EVT.
	m_characteristicMap.setByUUID(pCharacteristic, pCharacteristic->getUUID());

	NIMBLE_LOGD(LOG_TAG, "<< addCharacteristic()");
} // addCharacteristic


/**
 * @brief Create a new BLE Characteristic associated with this service.
 * @param [in] uuid - The UUID of the characteristic.
 * @param [in] properties - The properties of the characteristic.
 * @return The new BLE characteristic.
 */
NimBLECharacteristic* NimBLEService::createCharacteristic(const char* uuid, uint32_t properties) {
	return createCharacteristic(NimBLEUUID(uuid), properties);
}


/**
 * @brief Create a new BLE Characteristic associated with this service.
 * @param [in] uuid - The UUID of the characteristic.
 * @param [in] properties - The properties of the characteristic.
 * @return The new BLE characteristic.
 */
NimBLECharacteristic* NimBLEService::createCharacteristic(NimBLEUUID uuid, uint32_t properties) {
	NimBLECharacteristic* pCharacteristic = new NimBLECharacteristic(uuid, properties);
	addCharacteristic(pCharacteristic);
	return pCharacteristic;
} // createCharacteristic


/**
 * @brief Handle a GATTS server event.
 */
 /*
void BLEService::handleGATTServerEvent(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param) {
	switch (event) {
		// ESP_GATTS_ADD_CHAR_EVT - Indicate that a characteristic was added to the service.
		// add_char:
		// - esp_gatt_status_t status
		// - uint16_t attr_handle
		// - uint16_t service_handle
		// - esp_bt_uuid_t char_uuid

		// If we have reached the correct service, then locate the characteristic and remember the handle
		// for that characteristic.
		case ESP_GATTS_ADD_CHAR_EVT: {
			if (m_handle == param->add_char.service_handle) {
				BLECharacteristic *pCharacteristic = getLastCreatedCharacteristic();
				if (pCharacteristic == nullptr) {
					ESP_LOGE(LOG_TAG, "Expected to find characteristic with UUID: %s, but didnt!",
							BLEUUID(param->add_char.char_uuid).toString().c_str());
					dump();
					break;
				}
				pCharacteristic->setHandle(param->add_char.attr_handle);
				m_characteristicMap.setByHandle(param->add_char.attr_handle, pCharacteristic);
				break;
			} // Reached the correct service.
			break;
		} // ESP_GATTS_ADD_CHAR_EVT


		// ESP_GATTS_START_EVT
		//
		// start:
		// esp_gatt_status_t status
		// uint16_t service_handle
		case ESP_GATTS_START_EVT: {
			if (param->start.service_handle == getHandle()) {
				m_semaphoreStartEvt.give();
			}
			break;
		} // ESP_GATTS_START_EVT

		// ESP_GATTS_STOP_EVT
		//
		// stop:
		// esp_gatt_status_t status
		// uint16_t service_handle
		//
		case ESP_GATTS_STOP_EVT: {
			if (param->stop.service_handle == getHandle()) {
				m_semaphoreStopEvt.give();
			}
			break;
		} // ESP_GATTS_STOP_EVT


		// ESP_GATTS_CREATE_EVT
		// Called when a new service is registered as having been created.
		//
		// create:
		// * esp_gatt_status_t status
		// * uint16_t service_handle
		// * esp_gatt_srvc_id_t service_id
		// * - esp_gatt_id id
		// *   - esp_bt_uuid uuid
		// *   - uint8_t inst_id
		// * - bool is_primary
		//
		case ESP_GATTS_CREATE_EVT: {
			if (getUUID().equals(BLEUUID(param->create.service_id.id.uuid)) && m_instId == param->create.service_id.id.inst_id) {
				setHandle(param->create.service_handle);
				m_semaphoreCreateEvt.give();
			}
			break;
		} // ESP_GATTS_CREATE_EVT


		// ESP_GATTS_DELETE_EVT
		// Called when a service is deleted.
		//
		// delete:
		// * esp_gatt_status_t status
		// * uint16_t service_handle
		//
		case ESP_GATTS_DELETE_EVT: {
			if (param->del.service_handle == getHandle()) {
				m_semaphoreDeleteEvt.give();
			}
			break;
		} // ESP_GATTS_DELETE_EVT

		default:
			break;
	} // Switch

	// Invoke the GATTS handler in each of the associated characteristics.
	m_characteristicMap.handleGATTServerEvent(event, gatts_if, param);
} // handleGATTServerEvent
*/


NimBLECharacteristic* NimBLEService::getCharacteristic(const char* uuid) {
	return getCharacteristic(NimBLEUUID(uuid));
}


NimBLECharacteristic* NimBLEService::getCharacteristic(NimBLEUUID uuid) {
	return m_characteristicMap.getByUUID(uuid);
}


/**
 * @brief Return a string representation of this service.
 * A service is defined by:
 * * Its UUID
 * * Its handle
 * @return A string representation of this service.
 */
std::string NimBLEService::toString() {
	std::string res = "UUID: " + getUUID().toString();
	char hex[5];
	snprintf(hex, sizeof(hex), "%04x", getHandle());
	res += ", handle: 0x";
	res += hex;
	return res;
} // toString


/**
 * @brief Get the last created characteristic.
 * It is lamentable that this function has to exist.  It returns the last created characteristic.
 * We need this because the descriptor API is built around the notion that a new descriptor, when created,
 * is associated with the last characteristics created and we need that information.
 * @return The last created characteristic.
 */
NimBLECharacteristic* NimBLEService::getLastCreatedCharacteristic() {
	return m_lastCreatedCharacteristic;
} // getLastCreatedCharacteristic


/**
 * @brief Get the BLE server associated with this service.
 * @return The BLEServer associated with this service.
 */
NimBLEServer* NimBLEService::getServer() {
	return m_pServer;
} // getServer

#endif // CONFIG_BT_ENABLED