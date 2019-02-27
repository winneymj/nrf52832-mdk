/* mbed Microcontroller Library
 * Copyright (c) 2006-2013 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <events/mbed_events.h>
#include <mbed.h>
#include "ble/BLE.h"
#include "SecurityManager.h"
#include "LEDService.h"
#include "Adafruit_GFX.h"
#include "Adafruit_SSD1306.h"


/** This example demonstrates all the basic setup required
 *  for pairing and setting up link security both as a central and peripheral
 *
 *  The example is implemented as two classes, one for the peripheral and one
 *  for central inheriting from a common base. They are run in sequence and
 *  require a peer device to connect to.
 *
 *  During the test output is written on the serial connection to monitor its
 *  progress.
 */

static const uint8_t DEVICE_NAME[] = "SM_device";
static const uint16_t uuid16_list[] = {LEDService::LED_SERVICE_UUID};

LEDService *ledServicePtr;
SPI mySPI(p5, p6, p7); // mosi, miso, sclk
Adafruit_SSD1306_Spi *display;

/** Base class for both peripheral and central. The same class that provides
 *  the logic for the application also implements the SecurityManagerEventHandler
 *  which is the interface used by the Security Manager to communicate events
 *  back to the applications. You can provide overrides for a selection of events
 *  your application is interested in.
 */
class SMDevice : private mbed::NonCopyable<SMDevice>,
                 public SecurityManager::EventHandler
{

public:
    SMDevice(BLE &ble, events::EventQueue &event_queue) :
        _led1(LED1, 0),
        _ble(ble),
        _event_queue(event_queue),
        _handle(0),
        _is_connecting(false) { };

    virtual ~SMDevice()
    {
        if (_ble.hasInitialized()) {
            _ble.shutdown();
        }
    };

    /** Start BLE interface initialisation */
    void run()
    {
        printf("SMDevice:run: ENTER\r\n");

        ble_error_t error;

        /* to show we're running we'll blink every 500ms */
        _event_queue.call_every(500, this, &SMDevice::blink);

        if (_ble.hasInitialized()) {
            printf("Ble instance already initialised.\r\n");
            return;
        }

        /* this will inform us off all events so we can schedule their handling
         * using our event queue */
        _ble.onEventsToProcess(
            makeFunctionPointer(this, &SMDevice::schedule_ble_events)
        );

        error = _ble.init(this, &SMDevice::on_init_complete);

        if (error) {
            printf("Error returned by BLE::init.\r\n");
            return;
        }

        /* this will not return until shutdown */
        _event_queue.dispatch_forever();

        printf("SMDevice:run: EXIT\r\n");
    };

    /* event handler functions */

    /** Respond to a pairing request. This will be called by the stack
     * when a pairing request arrives and expects the application to
     * call acceptPairingRequest or cancelPairingRequest */
    virtual void pairingRequest(
        ble::connection_handle_t connectionHandle
    ) {
        printf("Pairing requested. Authorising.\r\n");
        _ble.securityManager().acceptPairingRequest(connectionHandle);
    }

    /** Inform the application of a successful pairing. Terminate the demonstration. */
    virtual void pairingResult(
        ble::connection_handle_t connectionHandle,
        SecurityManager::SecurityCompletionStatus_t result
    ) {
        printf("SMDevice:pairingResult: ENTER\r\n");
        if (result == SecurityManager::SEC_STATUS_SUCCESS) {
            printf("Pairing successful\r\n");
        } else {
            printf("Pairing failed\r\n");
        }

        // /* disconnect in 500 ms */
        // _event_queue.call_in(
        //     500, &_ble.gap(),
        //     &Gap::disconnect, _handle, Gap::REMOTE_USER_TERMINATED_CONNECTION
        // );
        printf("SMDevice:pairingResult: EXIT\r\n");
    }

    /** Inform the application of change in encryption status. This will be
     * communicated through the serial port */
    virtual void linkEncryptionResult(
        ble::connection_handle_t connectionHandle,
        ble::link_encryption_t result
    ) {
        printf("SMDevice:linkEncryptionResult: ENTER\r\n");
        if (result == ble::link_encryption_t::ENCRYPTED) {
            printf("Link ENCRYPTED\r\n");
        } else if (result == ble::link_encryption_t::ENCRYPTED_WITH_MITM) {
            printf("Link ENCRYPTED_WITH_MITM\r\n");
        } else if (result == ble::link_encryption_t::NOT_ENCRYPTED) {
            printf("Link NOT_ENCRYPTED\r\n");
        }
        printf("SMDevice:linkEncryptionResult: EXIT\r\n");
    }

private:
    /** Override to start chosen activity when initialisation completes */
    virtual void start() = 0;

    /**
     * This callback allows the LEDService to receive updates to the ledState Characteristic.
     *
     * @param[in] params
     *     Information about the characterisitc being updated.
     */
    void onDataWrittenCallback(const GattWriteCallbackParams *params) {
        printf("SMDevice:onDataWrittenCallback: ENTER\r\n");
        if ((params->handle == ledServicePtr->getValueHandle()) && (params->len == 1)) {
            printf("onDataWrittenCallback:" + *(params->data));
            printf("\r\n");
            // actuatedLED = *(params->data);
        }
        printf("SMDevice:onDataWrittenCallback: EXIT\r\n");
    }

    /** This is called when BLE interface is initialised and starts the demonstration */
    void on_init_complete(BLE::InitializationCompleteCallbackContext *event)
    {
        printf("SMDevice:on_init_complete: ENTER\r\n");
        ble_error_t error;

        if (event->error) {
            printf("Error during the initialisation\r\n");
            return;
        }

        /* If the security manager is required this needs to be called before any
         * calls to the Security manager happen. */
        error = _ble.securityManager().init();

        if (error) {
            printf("Error during init %d\r\n", error);
            return;
        }

        /* Tell the security manager to use methods in this class to inform us
         * of any events. Class needs to implement SecurityManagerEventHandler. */
        _ble.securityManager().setSecurityManagerEventHandler(this);

        /* print device address */
        Gap::AddressType_t addr_type;
        Gap::Address_t addr;
        _ble.gap().getAddress(&addr_type, addr);
        printf("Device address: %02x:%02x:%02x:%02x:%02x:%02x\r\n",
               addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

        /* when scanning we want to connect to a peer device so we need to
         * attach callbacks that are used by Gap to notify us of events */
        _ble.gap().onConnection(this, &SMDevice::on_connect);
        _ble.gap().onDisconnection(this, &SMDevice::on_disconnect);

        /* handle timeouts, for example when connection attempts fail */
        _ble.gap().onTimeout().add(this, &SMDevice::on_timeout);

        _ble.gattServer().onDataWritten(this, &SMDevice::onDataWrittenCallback);
        
        bool initialValueForLEDCharacteristic = false;
        ledServicePtr = new LEDService(_ble, initialValueForLEDCharacteristic);

        /* start test in 500 ms */
        _event_queue.call_in(500, this, &SMDevice::start);

        printf("SMDevice:on_init_complete: EXIT\r\n");
    };

    /** This is called by Gap to notify the application we connected */
    virtual void on_connect(const Gap::ConnectionCallbackParams_t *connection_event) = 0;

    /** This is called by Gap to notify the application we disconnected,
     *  in our case it ends the demonstration. */
    void on_disconnect(const Gap::DisconnectionCallbackParams_t *event)
    {
        printf("SMDevice:on_disconnect: ENTER\r\n");
        printf("SMDevice:on_disconnect: Reason=0x%X\r\n", event->reason);
        // printf("Disconnected - demonstration ended \r\n");
        // _event_queue.break_dispatch();
        BLE::Instance().gap().startAdvertising();
        printf("SMDevice:on_disconnect: EXIT\r\n");
    };

    /** End demonstration unexpectedly. Called if timeout is reached during advertising,
     * scanning or connection initiation */
    void on_timeout(const Gap::TimeoutSource_t source)
    {
        printf("SMDevice:on_timeout: ENTER\r\n");
        printf("Unexpected timeout - aborting \r\n");
        // _event_queue.break_dispatch();
        printf("SMDevice:on_timeout: EXIT\r\n");
    };

    /** Schedule processing of events from the BLE in the event queue. */
    void schedule_ble_events(BLE::OnEventsToProcessCallbackContext *context)
    {
        // printf("SMDevice:schedule_ble_events: ENTER\r\n");
        _event_queue.call(mbed::callback(&context->ble, &BLE::processEvents));
        // printf("SMDevice:schedule_ble_events: EXIT\r\n");
    };

    /** Blink LED to show we're running */
    void blink(void)
    {
        Gap::GapState_t gapState = _ble.gap().getState();
        printf("Gap State:Advertising=%s, connected=%s\r\n", gapState.advertising ? "On" : "Off", gapState.connected ? "Yes" : "No");

        // Solid led if advertising
        if (gapState.advertising) {
            _led1.write(0);
        }
        else if (gapState.connected) {
            _led1 = !_led1;
        }
    };

private:
    DigitalOut _led1;

protected:
    BLE &_ble;
    events::EventQueue &_event_queue;
    ble::connection_handle_t _handle;
    bool _is_connecting;
};

/** A peripheral device will advertise, accept the connection and request
 * a change in link security. */
class SMDevicePeripheral : public SMDevice {
public:
    SMDevicePeripheral(BLE &ble, events::EventQueue &event_queue)
        : SMDevice(ble, event_queue) { }

    virtual void start()
    {
        printf("SMDevicePeripheral:start: ENTER\r\n");
        /* Set up and start advertising */

        ble_error_t error;
        GapAdvertisingData advertising_data;

        /* add advertising flags */
        advertising_data.addFlags(GapAdvertisingData::LE_GENERAL_DISCOVERABLE | GapAdvertisingData::BREDR_NOT_SUPPORTED);

        /* add device name */
        advertising_data.addData(GapAdvertisingData::COMPLETE_LOCAL_NAME, DEVICE_NAME, sizeof(DEVICE_NAME));

        advertising_data.addData(GapAdvertisingData::COMPLETE_LIST_16BIT_SERVICE_IDS, (uint8_t *)uuid16_list, sizeof(uuid16_list));

        error = _ble.gap().setAdvertisingPayload(advertising_data);

        if (error) {
            printf("Error during Gap::setAdvertisingPayload\r\n");
            return;
        }

        /* advertise to everyone */
        _ble.gap().setAdvertisingType(GapAdvertisingParams::ADV_CONNECTABLE_UNDIRECTED);
        /* how many milliseconds between advertisements, lower interval
         * increases the chances of being seen at the cost of more power */
        _ble.gap().setAdvertisingInterval(5000);
        _ble.gap().setAdvertisingTimeout(0);

        error = _ble.gap().startAdvertising();

        if (error) {
            printf("Error during Gap::startAdvertising.\r\n");
            return;
        }

        /** This tells the stack to generate a pairingRequest event
         * which will require this application to respond before pairing
         * can proceed. Setting it to false will automatically accept
         * pairing. */
        _ble.securityManager().setPairingRequestAuthorisation(true);

        printf("SMDevicePeripheral:start: EXIT\r\n");
    };

    /** This is called by Gap to notify the application we connected,
     *  in our case it immediately requests a change in link security */
    virtual void on_connect(const Gap::ConnectionCallbackParams_t *connection_event)
    {
        printf("SMDevicePeripheral:on_connect: ENTER\r\n");
        ble_error_t error;

        /* store the handle for future Security Manager requests */
        _handle = connection_event->handle;

        /* Request a change in link security. This will be done
        * indirectly by asking the master of the connection to
        * change it. Depending on circumstances different actions
        * may be taken by the master which will trigger events
        * which the applications should deal with. */
        error = _ble.securityManager().setLinkSecurity(
            _handle,
            SecurityManager::SECURITY_MODE_ENCRYPTION_NO_MITM
        );

        if (error) {
            printf("Error during SM::setLinkSecurity %d\r\n", error);
            return;
        }
        printf("SMDevicePeripheral:on_connect: EXIT\r\n");
    };
};

int main()
{
    display = new Adafruit_SSD1306_Spi(mySPI, p0, p1, p2, 32, 128);

    printf("\r\n main: ENTER \r\n\r\n");
    BLE& ble = BLE::Instance();
    events::EventQueue queue;

    printf("\r\n PERIPHERAL \r\n\r\n");
    SMDevicePeripheral peripheral(ble, queue);
    peripheral.run();

    printf("\r\n main: EXIT \r\n\r\n");
    return 0;
}