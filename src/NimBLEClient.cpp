/*
 * NimBLEClient.cpp
 *
 *  Created: on Jan 26 2020
 *      Author H2zero
 * 
 * Originally:
 * BLEClient.cpp
 *
 *  Created on: Mar 22, 2017
 *      Author: kolban
 */
 
#include "sdkconfig.h"
#if defined(CONFIG_BT_ENABLED)

#include "NimBLEClient.h"
#include "NimBLEUtils.h"
#include "NimBLEDevice.h"
/*
#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#define LOG_TAG ""
#else
#include "esp_log.h"
static const char* LOG_TAG = "NimBLEClient";
#endif
*/

#include "NimBLELog.h"

#include <string>
#include <unordered_set>

static const char* LOG_TAG = "NimBLEClient";

/*
 * Design
 * ------
 * When we perform a getService() request, we are asking the BLE server to return each of the services
 * that it exposes.  For each service, we receive a callback which contains details
 * of the exposed service including its UUID.
 *
 * The objects we will invent for a NimBLEClient will be as follows:
 * * NimBLERemoteService - A model of a remote service.
 * * NimBLERemoteCharacteristic - A model of a remote characteristic
 * * NimBLERemoteDescriptor - A model of a remote descriptor.
 *
 * Since there is a hierarchical relationship here, we will have the idea that from a NimBLERemoteService will own
 * zero or more remote characteristics and a NimBLERemoteCharacteristic will own zero or more remote NimBLEDescriptors.
 *
 * We will assume that a NimBLERemoteService contains a map that maps NimBLEUUIDs to the set of owned characteristics
 * and that a NimBLECharacteristic contains a map that maps NimBLEUUIDs to the set of owned descriptors.
 *
 *
 */

NimBLEClient::NimBLEClient()
{
    m_pClientCallbacks = nullptr;
    m_conn_id          = BLE_HS_CONN_HANDLE_NONE;
    m_haveServices     = false;
    m_isConnected      = false;
} // NimBLEClient


/**
 * @brief Destructor, private - only callable by NimBLEDevice::deleteClient
 * to ensure proper disconnect and removal from device list.
 */
NimBLEClient::~NimBLEClient() { 
    //m_isConnected = false;
    //m_semaphoreOpenEvt.give(1);
    //m_semaphoreSearchCmplEvt.give(1);
    //m_semeaphoreSecEvt.give(1);

    // We may have allocated service references associated with this client.  
    // Before we are finished with the client, we must release resources.
    clearServices();
    
    if(m_deleteCallbacks) {
        delete m_pClientCallbacks;
    }
} // ~NimBLEClient


/**
 * @brief Clear any existing services.
 */
void NimBLEClient::clearServices() {
    NIMBLE_LOGD(LOG_TAG, ">> clearServices");
    // Delete all the services.
    for (auto &myPair : m_servicesMap) {
       delete myPair.second;
    }
    m_servicesMap.clear();
    m_haveServices = false;
    NIMBLE_LOGD(LOG_TAG, "<< clearServices");
} // clearServices


/**
 * @brief If the host was reset try to gracefully recover and ensure all semaphores are unblocked
 */
void NimBLEClient::onHostReset() {
    
    // Dont think this is necessary
    /*
    m_isConnected = false; // make sure we change connected status before releasing semaphores
    m_waitingToConnect = false;
    
    m_semaphoreOpenEvt.give(1);
    m_semaphoreSearchCmplEvt.give(1);
    m_semeaphoreSecEvt.give(1);
    for (auto &sPair : m_servicesMap) {
        sPair.second->releaseSemaphores();
    }
    //m_conn_id = BLE_HS_CONN_HANDLE_NONE; // old handle will be invalid, clear it just incase
    
    // tell the user we disconnected
    if (m_pClientCallbacks != nullptr) {
        m_pClientCallbacks->onDisconnect(this);
    }
    */
}
    
    
/**
 * Add overloaded function to ease connect to peer device with not public address
 */
bool NimBLEClient::connect(NimBLEAdvertisedDevice* device, bool refreshServices) {
    NimBLEAddress address =  device->getAddress();
    uint8_t type = device->getAddressType();
    return connect(address, type, refreshServices);
}


/**
 * @brief Connect to the partner (BLE Server).
 * @param [in] address The address of the partner.
 * @return True on success.
 */
bool NimBLEClient::connect(NimBLEAddress address, uint8_t type, bool refreshServices) {
    NIMBLE_LOGD(LOG_TAG, ">> connect(%s)", address.toString().c_str());
    
    int rc = 0;
    
    if(!NimBLEDevice::m_synced) {
        NIMBLE_LOGE(LOG_TAG, "Host reset, wait for sync.");
        return false;
    }

    if(refreshServices) {
        NIMBLE_LOGE(LOG_TAG, "Refreshing Services for: (%s)", address.toString().c_str());
        clearServices();
    }
    
    m_peerAddress = address;
    ble_addr_t peerAddrt;
    memcpy(&peerAddrt.val, address.getNative(),6);
    peerAddrt.type = type;
    
    m_semaphoreOpenEvt.take("connect");
    
    /* Try to connect the the advertiser.  Allow 30 seconds (30000 ms) for
     * timeout. Loop on BLE_HS_EBUSY if the scan hasn't stopped yet.
     */
    do{
        rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peerAddrt, 30000, NULL,
                            NimBLEClient::handleGapEvent, this);
    }while(rc == BLE_HS_EBUSY);
                         
    if (rc != 0) {
        NIMBLE_LOGE(LOG_TAG, "Error: Failed to connect to device; addr_type=%d "
                    "addr=%s",
                    type, address.toString().c_str());
        m_semaphoreOpenEvt.give();
        m_waitingToConnect = false;
        return false;
    }
    
    m_waitingToConnect = true;
    
    rc = m_semaphoreOpenEvt.wait("connect");   // Wait for the connection to complete.

    if(rc != 0){
        return false;
    }

    if (!m_haveServices) {
        if (!retrieveServices()) {
            // error getting services, make sure we disconnect and release any resources before returning
            disconnect();
            clearServices();
            return false;
        }
        else{
            NIMBLE_LOGD(LOG_TAG, "Found %d services", getServices()->size());
        }
    }
    
    NIMBLE_LOGD(LOG_TAG, "<< connect()");
    return true;
} // connect


/**
 * @brief Called when a characteristic or descriptor requires encryption or authentication to access it.
 * This will pair with the device and bond if enabled.
 * @return True on success.
 */
bool NimBLEClient::secureConnection() {
    
    m_semeaphoreSecEvt.take("secureConnection");
    
    int rc = NimBLEDevice::startSecurity(m_conn_id);
    if(rc != 0){
        m_semeaphoreSecEvt.give();
        return false;
    }
    
    rc = m_semeaphoreSecEvt.wait("secureConnection");
    if(rc != 0){
        return false;
    }
    
    return true;
}
    

/**
 * @brief Disconnect from the peer.
 * @return N/A.
 */
int NimBLEClient::disconnect(uint8_t reason) {
    NIMBLE_LOGD(LOG_TAG, ">> disconnect()");
    int rc = 0;
    if(m_isConnected){
        rc = ble_gap_terminate(m_conn_id, reason);
        if(rc != 0){
            NIMBLE_LOGE(LOG_TAG, "ble_gap_terminate failed: rc=%d %s", rc, NimBLEUtils::returnCodeToString(rc));
        }
    }
    
    return rc;
    NIMBLE_LOGD(LOG_TAG, "<< disconnect()");
} // disconnect


/**
 * @brief Get the connection id for this client.
 * @return The connection id.
 */
uint16_t NimBLEClient::getConnId() {
    return m_conn_id;
} // getConnId


/**
 * @brief Retrieve the address of the peer.
 */
NimBLEAddress NimBLEClient::getPeerAddress() {
    return m_peerAddress;
} // getAddress


/**
 * @brief Ask the BLE server for the RSSI value.
 * @return The RSSI value.
 */
int NimBLEClient::getRssi() {
    NIMBLE_LOGD(LOG_TAG, ">> getRssi()");
    if (!isConnected()) {
        NIMBLE_LOGD(LOG_TAG, "<< getRssi(): Not connected");
        return 0;
    }
    
    int8_t rssiValue = 0;
    int rc = ble_gap_conn_rssi(m_conn_id, &rssiValue);
    if(rc != 0) {
        NIMBLE_LOGE(LOG_TAG, "Failed to read RSSI error code: %d", rc);
        return 0;
    }
    
    return rssiValue;
} // getRssi


/**
 * @brief Get the service BLE Remote Service instance corresponding to the uuid.
 * @param [in] uuid The UUID of the service being sought.
 * @return A reference to the Service or nullptr if don't know about it.
 */
NimBLERemoteService* NimBLEClient::getService(const char* uuid) {
    return getService(NimBLEUUID(uuid));
} // getService


/**
 * @brief Get the service object corresponding to the uuid.
 * @param [in] uuid The UUID of the service being sought.
 * @return A reference to the Service or nullptr if don't know about it.
 */
NimBLERemoteService* NimBLEClient::getService(NimBLEUUID uuid) {
    NIMBLE_LOGD(LOG_TAG, ">> getService: uuid: %s", uuid.toString().c_str());

    if (!m_haveServices) {
        return nullptr;
    }
    std::string uuidStr = uuid.toString();
    for (auto &myPair : m_servicesMap) {
        if (myPair.first == uuidStr) {
            NIMBLE_LOGD(LOG_TAG, "<< getService: found the service with uuid: %s", uuid.toString().c_str());
            return myPair.second;
        }
    } 
    NIMBLE_LOGD(LOG_TAG, "<< getService: not found");
    return nullptr;
} // getService


/**
 * @Get a pointer to the map of found services.
 */ 
std::map<std::string, NimBLERemoteService*>* NimBLEClient::getServices() {
    return &m_servicesMap;
}


/**
 * @brief Ask the remote %BLE server for its services.
 * A %BLE Server exposes a set of services for its partners.  Here we ask the server for its set of
 * services and wait until we have received them all.
 * We then ask for the characteristics for each service found and their desciptors.
 * @return true on success otherwise false if an error occurred
 */ 
bool NimBLEClient::retrieveServices() {
/*
 * Design
 * ------
 * We invoke ble_gattc_disc_all_svcs.  This will request a list of the services exposed by the
 * peer BLE partner to be returned in the callback function provided.
 */
 
    NIMBLE_LOGD(LOG_TAG, ">> retrieveServices");
    //clearServices(); // Clear any services that may exist.
    
    if(!m_isConnected){
        NIMBLE_LOGE(LOG_TAG, "Disconnected, could not retrieve services -aborting");
        return false;
    }

    m_semaphoreSearchCmplEvt.take("retrieveServices");
    
    int rc = ble_gattc_disc_all_svcs(m_conn_id, NimBLEClient::serviceDiscoveredCB, this);
    
    if (rc != 0) {
        NIMBLE_LOGE(LOG_TAG, "ble_gattc_disc_all_svcs: rc=%d %s", rc, NimBLEUtils::returnCodeToString(rc));
        m_haveServices = false;
        m_semaphoreSearchCmplEvt.give();
        return false;
    }
    
    // wait until we have all the services
    // If sucessful, remember that we now have services.
    m_haveServices = (m_semaphoreSearchCmplEvt.wait("retrieveServices") == 0);
    if(m_haveServices){
        for (auto &myPair : m_servicesMap) {
            // if we were disconnected try to recover gracefully and release all resources
            if(!m_isConnected || !myPair.second->retrieveCharacteristics()) {
                NIMBLE_LOGE(LOG_TAG, "Disconnected, could not retrieve characteristics -aborting");
                //clearServices();
                return false;
            }
        }
        
        NIMBLE_LOGD(LOG_TAG, "<< retrieveServices");
        return true;
    }
    else {
        // if there was an error make sure we release any resources
        NIMBLE_LOGE(LOG_TAG, "Could not retrieve services");
        //clearServices();
        return false;
    }
} // getServices


/**
 * @brief STATIC Callback for the service discovery API function. 
 * When a service is found or there is none left or there was an error
 * the API will call this and report findings.
 */
int NimBLEClient::serviceDiscoveredCB(
                uint16_t conn_handle, 
                const struct ble_gatt_error *error,
                const struct ble_gatt_svc *service, void *arg) 
{
    NIMBLE_LOGD(LOG_TAG,"Service Discovered >> status: %d handle: %d", error->status, conn_handle);
    NimBLEClient *peer = (NimBLEClient*)arg;
    int rc=0;

    // Make sure the service discovery is for this device
    if(peer->getConnId() != conn_handle){
        return 0;
    }

    switch (error->status) {
        case 0: {
            // Found a service - add it to the map
            NimBLERemoteService* pRemoteService = new NimBLERemoteService(peer, service);
            peer->m_servicesMap.insert(std::pair<std::string, NimBLERemoteService*>(pRemoteService->getUUID().toString(), pRemoteService));

            break;
        }
        case BLE_HS_EDONE:{
            // All services discovered; start discovering characteristics. 

            NIMBLE_LOGD(LOG_TAG,"Giving search semaphore - completed");
            peer->m_semaphoreSearchCmplEvt.give(0);
            rc = 0;
            break;
        }
        default:
            // Error; abort discovery.
            rc = error->status;
            break;
    }

    if (rc != 0) {
        // pass non-zero to semaphore on error to indicate an error finding services
        peer->m_semaphoreSearchCmplEvt.give(1); 
    }
    NIMBLE_LOGD(LOG_TAG,"<< Service Discovered. status: %d", rc);
    return rc;
}


/**
 * @brief Get the value of a specific characteristic associated with a specific service.
 * @param [in] serviceUUID The service that owns the characteristic.
 * @param [in] characteristicUUID The characteristic whose value we wish to read.
 * @returns characteristic value or an empty string if not found
 */
std::string NimBLEClient::getValue(NimBLEUUID serviceUUID, NimBLEUUID characteristicUUID) {
    NIMBLE_LOGD(LOG_TAG, ">> getValue: serviceUUID: %s, characteristicUUID: %s", serviceUUID.toString().c_str(), characteristicUUID.toString().c_str());
    
    std::string ret = "";
    NimBLERemoteService* pService = getService(serviceUUID);
    
    if(pService != nullptr) {
        NimBLERemoteCharacteristic* pChar = pService->getCharacteristic(characteristicUUID);
        if(pChar != nullptr) {
            ret = pChar->readValue();
        }
    }

    NIMBLE_LOGD(LOG_TAG, "<<getValue");
    return ret;
} // getValue


/**
 * @brief Set the value of a specific characteristic associated with a specific service.
 * @param [in] serviceUUID The service that owns the characteristic.
 * @param [in] characteristicUUID The characteristic whose value we wish to write.
 * @returns true if successful otherwise false
 */
bool NimBLEClient::setValue(NimBLEUUID serviceUUID, NimBLEUUID characteristicUUID, std::string value) {
    NIMBLE_LOGD(LOG_TAG, ">> setValue: serviceUUID: %s, characteristicUUID: %s", serviceUUID.toString().c_str(), characteristicUUID.toString().c_str());
    
    bool ret = false;
    NimBLERemoteService* pService = getService(serviceUUID);
    
    if(pService != nullptr) {
        NimBLERemoteCharacteristic* pChar = pService->getCharacteristic(characteristicUUID);
        if(pChar != nullptr) {
            ret = pChar->writeValue(value);
        }
    }
    
    NIMBLE_LOGD(LOG_TAG, "<< setValue");
    return ret;
} // setValue



/**
 * @brief Get the current mtu of this connection.
 */
uint16_t NimBLEClient::getMTU() {
    return ble_att_mtu(m_conn_id);
}



/**
 * @brief Handle a received GAP event.
 *
 * @param [in] event
 * @param [in] arg = pointer to the client instance
 */
 /*STATIC*/ int NimBLEClient::handleGapEvent(struct ble_gap_event *event, void *arg) {
     
    NimBLEClient* client = (NimBLEClient*)arg; 
    //struct ble_gap_conn_desc desc;
    //struct ble_hs_adv_fields fields;
    int rc;

    NIMBLE_LOGI(LOG_TAG, "Got Client event %s for conn handle: %d this handle is: %d", NimBLEUtils::gapEventToString(event->type), event->connect.conn_handle, client->m_conn_id);

    // Execute handler code based on the type of event received.
    switch(event->type) {

        case BLE_GAP_EVENT_DISCONNECT: {
            if(!client->m_isConnected)
                return 0;
            
            if(client->m_conn_id != event->disconnect.conn.conn_handle)
                return 0;
            
            NIMBLE_LOGI(LOG_TAG, "disconnect; reason=%d ", event->disconnect.reason);
            //print_conn_desc(&event->disconnect.conn);
            //MODLOG_DFLT(INFO, "\n");

    /*      
            switch(event->disconnect.reason) {
                case BLE_HS_ETIMEOUT_HCI:
                case BLE_HS_EOS:
                case BLE_HS_ECONTROLLER:
                case BLE_HS_ENOTSYNCED:
                    break;
                default: 
                    break;
            }
    */
    
            client->m_semaphoreOpenEvt.give(1);
            client->m_semaphoreSearchCmplEvt.give(1);
            client->m_semeaphoreSecEvt.give(1);
            
            if (client->m_pClientCallbacks != nullptr) {
                client->m_pClientCallbacks->onDisconnect(client);
            }
            
            //client->m_conn_id = BLE_HS_CONN_HANDLE_NONE;
            
            // Remove the device from ignore list so we can scan it again
            NimBLEDevice::removeIgnored(client->m_peerAddress);
            
            client->m_isConnected = false;
            client->m_waitingToConnect=false;
            
            return 0;
        } // BLE_GAP_EVENT_DISCONNECT

        case BLE_GAP_EVENT_CONNECT: {

            if(!client->m_waitingToConnect)
                return 0;
            
            //if(client->m_conn_id != BLE_HS_CONN_HANDLE_NONE)
            //  return 0;
            
            client->m_waitingToConnect=false;
            
            if (event->connect.status == 0) {
                // Connection successfully established.
                NIMBLE_LOGI(LOG_TAG, "\nConnection established");
                
                client->m_conn_id = event->connect.conn_handle;
                
            //  rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            //  assert(rc == 0);
            //  print_conn_desc(&desc);
            //  MODLOG_DFLT(INFO, "\n");

                //  BLEDevice::updatePeerDevice(this, true, m_gattc_if);
                client->m_isConnected = true;
                
                if (client->m_pClientCallbacks != nullptr) {
                    client->m_pClientCallbacks->onConnect(client);
                }
                // Incase of a multiconnecting device we ignore this device when scanning since we are already connected to it
                NimBLEDevice::addIgnored(client->m_peerAddress);
                client->m_semaphoreOpenEvt.give(0);

            } else {
                // Connection attempt failed
                NIMBLE_LOGE(LOG_TAG, "Error: Connection failed; status=%d",
                            event->connect.status);
                client->m_semaphoreOpenEvt.give(event->connect.status);
            }

            return 0;
        } // BLE_GAP_EVENT_CONNECT

        case BLE_GAP_EVENT_NOTIFY_RX: {
            if(client->m_conn_id != event->notify_rx.conn_handle)
                return 0;
            
            NIMBLE_LOGD(LOG_TAG, "Notify Recieved for handle: %d",event->notify_rx.attr_handle);
            if(!client->m_haveServices)
                return 0;
            
            for(auto &sPair : client->m_servicesMap){
                // Dont waste cycles searching services without this handle in their range
                if(sPair.second->getEndHandle() < event->notify_rx.attr_handle) {
                    continue;
                }
                auto cMap = sPair.second->getCharacteristicsByHandle();
                NIMBLE_LOGD(LOG_TAG, "checking service %s for handle: %d", sPair.second->getUUID().toString().c_str(),event->notify_rx.attr_handle);
                auto characteristic = cMap->find(event->notify_rx.attr_handle);
                if(characteristic != cMap->end()) {
                    NIMBLE_LOGD(LOG_TAG, "Got Notification for characteristic %s", characteristic->second->toString().c_str());
                    
                    if (characteristic->second->m_notifyCallback != nullptr) {
                        NIMBLE_LOGD(LOG_TAG, "Invoking callback for notification on characteristic %s", characteristic->second->toString().c_str());
                        characteristic->second->m_notifyCallback(characteristic->second, event->notify_rx.om->om_data, event->notify_rx.om->om_len, !event->notify_rx.indication);
                    }
                    
                    break;
                }
            }
            
            return 0;
        } // BLE_GAP_EVENT_NOTIFY_RX
        
        case BLE_GAP_EVENT_L2CAP_UPDATE_REQ: {
            NIMBLE_LOGD(LOG_TAG, "Peer requesting to update connection parameters");
            //NimBLEDevice::gapEventHandler(event, arg);
            return 0;
        }
        
/*      case BLE_GAP_EVENT_CONN_UPDATE: {
            return 0;
        }
*/
        
        case BLE_GAP_EVENT_ENC_CHANGE: {
            if(client->m_conn_id != event->enc_change.conn_handle)
                return 0; //BLE_HS_ENOTCONN BLE_ATT_ERR_INVALID_HANDLE
            
            if(NimBLEDevice::m_securityCallbacks != nullptr) {
                struct ble_gap_conn_desc desc;
                rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
                assert(rc == 0);
                client->m_pClientCallbacks->onAuthenticationComplete(desc);
            }
            
            client->m_semeaphoreSecEvt.give(event->enc_change.status);
            //NimBLEDevice::gapEventHandler(event, arg);
            return 0;
        }
        
        case BLE_GAP_EVENT_PASSKEY_ACTION: {
            struct ble_sm_io pkey = {0};
            //int key = 0;
            
            if(client->m_conn_id != event->passkey.conn_handle) 
                return 0;

            if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
                pkey.action = event->passkey.params.action;
                pkey.passkey = NimBLEDevice::m_passkey; // This is the passkey to be entered on peer
                rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
                NIMBLE_LOGD(LOG_TAG, "ble_sm_inject_io result: %d", rc);
                
            } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
                NIMBLE_LOGD(LOG_TAG, "Passkey on device's display: %d", event->passkey.params.numcmp);
                pkey.action = event->passkey.params.action;
                if(client->m_pClientCallbacks != nullptr) {
                    pkey.numcmp_accept = client->m_pClientCallbacks->onConfirmPIN(event->passkey.params.numcmp);
                }else{
                    pkey.numcmp_accept = false;
                }
                rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
                NIMBLE_LOGD(LOG_TAG, "ble_sm_inject_io result: %d", rc);
                
            //TODO: Handle out of band pairing      
            } else if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
                static uint8_t tem_oob[16] = {0};
                pkey.action = event->passkey.params.action;
                for (int i = 0; i < 16; i++) {
                    pkey.oob[i] = tem_oob[i];
                }
                rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
                NIMBLE_LOGD(LOG_TAG, "ble_sm_inject_io result: %d", rc);
            ////////    
            } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
                NIMBLE_LOGD(LOG_TAG, "Enter the passkey");
                pkey.action = event->passkey.params.action;
                
                if(client->m_pClientCallbacks != nullptr) {
                    pkey.passkey = client->m_pClientCallbacks->onPassKeyRequest();
                    NIMBLE_LOGD(LOG_TAG, "Sending passkey: %d", pkey.passkey);
                }else{
                    pkey.passkey = 0;
                    NIMBLE_LOGE(LOG_TAG, "No Callback! Sending 0 as the passkey");
                }
;
                rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
                NIMBLE_LOGD(LOG_TAG, "ble_sm_inject_io result: %d", rc);
                
            } else if (event->passkey.params.action == BLE_SM_IOACT_NONE) {
                NIMBLE_LOGD(LOG_TAG, "No passkey action required");
            }

            return 0;
        }
        
        default: {
            //NimBLEUtils::dumpGapEvent(event, arg);
            return 0;
        }
    } // Switch
} // handleGapEvent


/**
 * @brief Are we connected to a server?
 * @return True if we are connected and false if we are not connected.
 */
bool NimBLEClient::isConnected() {
    return m_isConnected;
} // isConnected


/**
 * @brief Set the callbacks that will be invoked.
 */
void NimBLEClient::setClientCallbacks(NimBLEClientCallbacks* pClientCallbacks, bool deleteCallbacks) {
    m_pClientCallbacks = pClientCallbacks;
    m_deleteCallbacks = deleteCallbacks;
} // setClientCallbacks


/**
 * @brief Return a string representation of this client.
 * @return A string representation of this client.
 */
std::string NimBLEClient::toString() {
    std::string res = "peer address: " + m_peerAddress.toString();
    res += "\nServices:\n";
    for (auto &myPair : m_servicesMap) {
        res += myPair.second->toString() + "\n";
    }

    return res;
} // toString


#endif // CONFIG_BT_ENABLED
