/*
 * NimBLEClient.h
 *
 *  Created: on Jan 26 2020
 *      Author H2zero
 * 
 * Originally:
 * BLEClient.h
 *
 *  Created on: Mar 22, 2017
 *      Author: kolban
 */

#ifndef MAIN_NIMBLECLIENT_H_
#define MAIN_NIMBLECLIENT_H_

#if defined(CONFIG_BT_ENABLED)
#include "sdkconfig.h"

#include "NimBLEAdvertisedDevice.h"
#include "NimBLERemoteService.h"
#include "NimBLEAddress.h"

#include <map>
#include <string>

class NimBLERemoteService;
class NimBLEClientCallbacks;
class NimBLEAdvertisedDevice;

/**
 * @brief A model of a %BLE client.
 */
class NimBLEClient {
public:
    bool                                       connect(NimBLEAdvertisedDevice* device, bool refreshServices = false);
    bool                                       connect(NimBLEAddress address, uint8_t type = BLE_ADDR_TYPE_PUBLIC, bool refreshServices = false);   // Connect to the remote BLE Server
    int                                        disconnect(uint8_t reason = BLE_ERR_REM_USER_CONN_TERM);                  // Disconnect from the remote BLE Server
    NimBLEAddress                              getPeerAddress();              // Get the address of the remote BLE Server
    int                                        getRssi();                     // Get the RSSI of the remote BLE Server
    std::map<std::string, NimBLERemoteService*>*  getServices();                 // Get a map of the services offered by the remote BLE Server
    NimBLERemoteService*                          getService(const char* uuid);  // Get a reference to a specified service offered by the remote BLE server.
    NimBLERemoteService*                          getService(NimBLEUUID uuid);   // Get a reference to a specified service offered by the remote BLE server.
    std::string                                getValue(NimBLEUUID serviceUUID, NimBLEUUID characteristicUUID);   // Get the value of a given characteristic at a given service.
    bool                                       setValue(NimBLEUUID serviceUUID, NimBLEUUID characteristicUUID, std::string value);   // Set the value of a given characteristic at a given service.
    bool                                       isConnected();                 // Return true if we are connected.
    void                                       setClientCallbacks(NimBLEClientCallbacks *pClientCallbacks, bool deleteCallbacks = true);
    std::string                                toString();                    // Return a string representation of this client.
    uint16_t                                   getConnId();
    uint16_t                                   getMTU();
    bool                                       secureConnection();


private:
    NimBLEClient();
    ~NimBLEClient();
    friend class NimBLEDevice;
    friend class NimBLERemoteService;

    static int          handleGapEvent(struct ble_gap_event *event, void *arg);
    static int          serviceDiscoveredCB(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_svc *service, void *arg);
    void                clearServices();   // Clear any existing services.
    bool                retrieveServices();  //Retrieve services from the server
    void                onHostReset();

    NimBLEAddress    m_peerAddress = NimBLEAddress("\0\0\0\0\0\0");   // The BD address of the remote server.
    uint16_t         m_conn_id;
    bool             m_haveServices = false;    // Have we previously obtain the set of services from the remote server.
    bool             m_isConnected = false;     // Are we currently connected.
    bool             m_waitingToConnect =false;
    bool             m_deleteCallbacks = true;
    //uint16_t       m_mtu = 23;


    NimBLEClientCallbacks*  m_pClientCallbacks = nullptr;

    FreeRTOS::Semaphore     m_semaphoreOpenEvt       = FreeRTOS::Semaphore("OpenEvt");
    FreeRTOS::Semaphore     m_semaphoreSearchCmplEvt = FreeRTOS::Semaphore("SearchCmplEvt");
    FreeRTOS::Semaphore     m_semeaphoreSecEvt       = FreeRTOS::Semaphore("Security");

    std::map<std::string, NimBLERemoteService*> m_servicesMap;

}; // class NimBLEClient 


/**
 * @brief Callbacks associated with a %BLE client.
 */
class NimBLEClientCallbacks {
public:
    virtual ~NimBLEClientCallbacks() {};
    virtual void onConnect(NimBLEClient *pClient) = 0;
    virtual void onDisconnect(NimBLEClient *pClient) = 0;
    virtual uint32_t onPassKeyRequest(){return 0;}
    virtual void onPassKeyNotify(uint32_t pass_key){}
    virtual bool onSecurityRequest(){return false;}
    virtual void onAuthenticationComplete(ble_gap_conn_desc){};
    virtual bool onConfirmPIN(uint32_t pin){return false;}
};

#endif // CONFIG_BT_ENABLED
#endif /* MAIN_NIMBLECLIENT_H_ */
