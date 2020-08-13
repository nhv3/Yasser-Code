#include "AFE4404.h" //Custom C functions for the ARM M4 Cortex
#include "AFE_programmer.h"
#include "Calibration_AFE4404.h"
#include "I2C.h"        // i2c
#include "SEGGER_RTT.h" // debugging
#include "app_error.h"
#include "app_timer.h"
#include "app_util_platform.h"
#include "ble.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "ble_conn_state.h"
#include "ble_hci.h"
#include "ble_srv_common.h"
#include "boards.h"
#include "bsp_btn_ble.h"
#include "custom_board.h"
#include "fds.h"
#include "hrm.h" //Code provided by TI to compute various heartrate parameters
#include "nordic_common.h"
#include "nrf.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "nrf_delay.h"
#include "nrf_drv_gpiote.h"
#include "nrf_drv_ppi.h"
#include "nrf_drv_saadc.h"
#include "nrf_drv_timer.h"
#include "nrf_drv_twi.h"
#include "nrf_drv_clock.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "nrf_sdm.h"
#include "peer_manager.h"
#include "sensor_service.h"
#include "sensorsim.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/**********************************************************************************************************/
/*                                              BEGIN GOLABAL DECLARATIONS                                */
/**********************************************************************************************************/
#define PPG_COLLECTION_TIME 120000

#define DEVICE_NAME "GUTSENS"                   /**< Name of device. Will be included in the advertising data. */
#define MANUFACTURER_NAME "Stanford University" /**< Manufacturer. Will be passed to Device Information Service. */
#define APP_ADV_FAST_INTERVAL 3200                    /**< The advertising interval (in units of 0.625 ms (16 kHz). This value corresponds to 187.5 ms). */
#define APP_ADV_FAST_DURATION 0                      /**< The advertising duration (180 seconds) in units of 10 milliseconds. */

#define APP_BLE_OBSERVER_PRIO 3                                         /**< Application's BLE observer priority. You shouldn't need to modify this value. */
#define APP_BLE_CONN_CFG_TAG 1                                          /**< A tag identifying the SoftDevice BLE configuration. */
#define MIN_CONN_INTERVAL MSEC_TO_UNITS(8, UNIT_1_25_MS)                /**< Minimum acceptable connection interval (7 mseconds). */
#define MAX_CONN_INTERVAL MSEC_TO_UNITS(9, UNIT_1_25_MS)                /**< Maximum acceptable connection interval (0.2 second). */
#define SLAVE_LATENCY 0                                                 /**< Slave latency. */
#define CONN_SUP_TIMEOUT MSEC_TO_UNITS(4000, UNIT_10_MS)                /**< Connection supervisory timeout (4 seconds). */
#define SENSOR_CHAR_TIMER_INTERVAL APP_TIMER_TICKS(PPG_COLLECTION_TIME) //1000 ms intervals

#define FIRST_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(1)    /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(30000) /**< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT 3                       /**< Number of attempts before giving up the connection parameter negotiation. */

#define SEC_PARAM_BOND 1                               /**< Perform bonding. */
#define SEC_PARAM_MITM 0                               /**< Man In The Middle protection not required. */
#define SEC_PARAM_LESC 0                               /**< LE Secure Connections not enabled. */
#define SEC_PARAM_KEYPRESS 0                           /**< Keypress notifications not enabled. */
#define SEC_PARAM_IO_CAPABILITIES BLE_GAP_IO_CAPS_NONE /**< No I/O capabilities. */
#define SEC_PARAM_OOB 0                                /**< Out Of Band data not available. */
#define SEC_PARAM_MIN_KEY_SIZE 7                       /**< Minimum encryption key size. */
#define SEC_PARAM_MAX_KEY_SIZE 16                      /**< Maximum encryption key size. */
#define DEAD_BEEF 0xDEADBEEF                           /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

#define BA_SDA_PIN  11    // SDA signal pin for the current Arduino Breakout Board
#define BA_SCL_PIN 8      // SCL signal pin for the current Arduino Breakout Board
#define AMB_LED_1_VAL 0x2D // Register for the ambient value from phase 1

#define CALIB_PRF_UPDATE 9000000

int RESETZ = 22;
int ADC_RDY = 6;

volatile int readDataFlag = 0;
volatile int g_OneSecondFlag = 0;
volatile int apptimerrunning = 0;
volatile int issue_disconnect = 0;
volatile int calibFinsihed = 0;
volatile char LED_Sel = 1;
volatile int offsetDACcalibFlag = 0; //Flag for the current offset computation of the AFE\
volatile int prfcount = 0;
volatile uint16_t prfcount = 0;
volatile int32_t ticks_now = 0;
int32_t time_tick_update = 0;
int32_t ramp1 = -2097152;
int32_t ramp2 = -2097152;
int32_t ramp3 = -2097152;
ble_gap_addr_t addres;

volatile bool BLE_CONNECTED = false;
volatile bool streaming = false;
volatile bool programming = false;
volatile bool monitorEnabled = false;

volatile bool CALIBRATION_ENABLED = true;
volatile bool CALIBRATION_FINISHED = false;
unsigned long AFE44xx_SPO2_Data_buf[6];

extern volatile int Calibration; // Global variable which is controlled by the library. Keep checking the status of the variable
extern long CTR;
extern unsigned long AFE44xx_Current_Register_Settings[5];
extern unsigned char HeartRate;
extern uint32_t Settings[5];
extern uint8_t setting_8b[20];

int32_t txString[14];
int32_t Red[2] = {0, 0};
int32_t Green[2] = {0, 0};
int32_t Nir[2] = {0, 0};
int32_t test[6] = {0, 0, 0, 0, 0, 0};
uint32_t programming_data = 0;

uint16_t stream_service;
uint16_t prog_service;
uint16_t time_monitor_service;
uint16_t readback_service;
uint16_t monitor_reload = 0;

uint8_t developer_mode = 0;
uint8_t LED1_drive = 0x03;
uint8_t LED2_drive = 0x03;
uint8_t LED3_drive = 0x03;
uint8_t TIA_R1 = 0x02;
uint8_t TIA_C1 = 0x00;
uint8_t TIA_R2 = 0x02;
uint8_t TIA_C2 = 0x00;
uint8_t en_sep = 0x00;
uint8_t DAC1_off = 0x00;
uint8_t DAC2_off = 0x00;
uint8_t DAC3_off = 0x00;
uint8_t DACA_off = 0x00;

volatile uint8_t LED_phase = 0x07; //holds the LED phase, Default value is 7 which means were are enabling three phase mode
volatile uint32_t LED_cntrl = 0x0030CF;
volatile uint32_t main_tia_gain = 0x000003;
volatile uint32_t sep_gain =  0x008003;
volatile uint32_t dac_set = 0x000000;

NRF_BLE_GATT_DEF(m_gatt);                                /**< GATT module instance. */
NRF_BLE_QWR_DEF(m_qwr);                                  /**< Context for the Queued Write module.*/
BLE_ADVERTISING_DEF(m_advertising);                      /**< Advertising module instance. */
static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID; /**< Handle of the current connection. */

ble_os_t m_sensor_service;                                                      //  Declare a service structure for sensor application
APP_TIMER_DEF(m_sensor_char_timer_id);                                          // Declare an app_timer id variable and define sensor timer interval and define a timer interval
static ble_uuid_t m_adv_uuids[] = {{BLE_UUID_SERVICE_UUID, BLE_UUID_TYPE_BLE}}; // Use UUIDs for service(s) used in ysensor application.

signed long AFE_Buffer[6] = {0, 0, 0, 0, 0, 0};

/**********************************************************************************************************/
/*                                              BEGIN FUNCTION DECLARATIONS                               */
/**********************************************************************************************************/

//Disables the interrupt for the PPG AFE
void AFE4404_ADCRDY_Interrupt_Disable(void) {
  nrf_drv_gpiote_in_event_disable(ADC_RDY);
}

//Enables the interrupt for the PPG AFE
void AFE4404_ADCRDY_Interrupt_Enable(void) {
  nrf_drv_gpiote_in_event_enable(ADC_RDY, true);
}

//NRF Callback for error handling
void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name) {
  app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

/**@brief Clear bond information from persistent storage.
 */
static void delete_bonds(void) {
  ret_code_t err_code;

  NRF_LOG_INFO("Erase bonds!");

  err_code = pm_peers_delete();
  APP_ERROR_CHECK(err_code);
}

/**@brief Function for starting advertising.
 */
static void advertising_start(bool erase_bonds) {
  if (erase_bonds == true) {
    delete_bonds();
    // Advertising is started by PM_EVT_PEERS_DELETED_SUCEEDED event
  } else {
    ret_code_t err_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);

    APP_ERROR_CHECK(err_code);
  }
}

// Timer callback - on expiration the timer turns off the AFE4404 and resets all of the tracking parameters and lets the BLE stack know its time to disconnect
static void timer_timeout_handler(void *p_context) {
  apptimerrunning = 0;
  readDataFlag = 0;
  issue_disconnect = 1;
  AFE4404_ADCRDY_Interrupt_Disable();
  AFE4404_Enable_HWPDN();
}

/**@brief Function for handling Peer Manager events.
 *
 * @param[in] p_evt  Peer Manager event.
 */
static void pm_evt_handler(pm_evt_t const *p_evt) {
  ret_code_t err_code;

  switch (p_evt->evt_id) {
  case PM_EVT_BONDED_PEER_CONNECTED: {
    NRF_LOG_INFO("Connected to a previously bonded device.");
  } break;

  case PM_EVT_CONN_SEC_SUCCEEDED: {
    NRF_LOG_INFO("Connection secured: role: %d, conn_handle: 0x%x, procedure: %d.",
        ble_conn_state_role(p_evt->conn_handle),
        p_evt->conn_handle,
        p_evt->params.conn_sec_succeeded.procedure);
  } break;

  case PM_EVT_CONN_SEC_FAILED: {
    /* Often, when securing fails, it shouldn't be restarted, for security reasons.
             * Other times, it can be restarted directly.
             * Sometimes it can be restarted, but only after changing some Security Parameters.
             * Sometimes, it cannot be restarted until the link is disconnected and reconnected.
             * Sometimes it is impossible, to secure the link, or the peer device does not support it.
             * How to handle this error is highly application dependent. */
  } break;

  case PM_EVT_CONN_SEC_CONFIG_REQ: {
    // Reject pairing request from an already bonded peer.
    pm_conn_sec_config_t conn_sec_config = {.allow_repairing = false};
    pm_conn_sec_config_reply(p_evt->conn_handle, &conn_sec_config);
  } break;

  case PM_EVT_STORAGE_FULL: {
    // Run garbage collection on the flash.
    err_code = fds_gc();
    if (err_code == FDS_ERR_NO_SPACE_IN_QUEUES) {
      // Retry.
    } else {
      APP_ERROR_CHECK(err_code);
    }
  } break;

  case PM_EVT_PEERS_DELETE_SUCCEEDED: {
    advertising_start(false);
  } break;

  case PM_EVT_PEER_DATA_UPDATE_FAILED: {
    // Assert.
    APP_ERROR_CHECK(p_evt->params.peer_data_update_failed.error);
  } break;

  case PM_EVT_PEER_DELETE_FAILED: {
    // Assert.
    APP_ERROR_CHECK(p_evt->params.peer_delete_failed.error);
  } break;

  case PM_EVT_PEERS_DELETE_FAILED: {
    // Assert.
    APP_ERROR_CHECK(p_evt->params.peers_delete_failed_evt.error);
  } break;

  case PM_EVT_ERROR_UNEXPECTED: {
    // Assert.
    APP_ERROR_CHECK(p_evt->params.error_unexpected.error);
  } break;

  case PM_EVT_CONN_SEC_START:
  case PM_EVT_PEER_DATA_UPDATE_SUCCEEDED:
  case PM_EVT_PEER_DELETE_SUCCEEDED:
  case PM_EVT_LOCAL_DB_CACHE_APPLIED:
  case PM_EVT_LOCAL_DB_CACHE_APPLY_FAILED:
    // This can happen when the local DB has changed.
  case PM_EVT_SERVICE_CHANGED_IND_SENT:
  case PM_EVT_SERVICE_CHANGED_IND_CONFIRMED:
  default:
    break;
  }
}

/**@brief Function for the Timer initialization.
 *
 * @details Initializes the timer module. This creates and starts application timers.
 */
static void timers_init(void) {
   
  // Initialize timer module.
  ret_code_t errr_code = app_timer_init();
  APP_ERROR_CHECK(errr_code);

  // Create a timer instance associated with our sensor app
  
  app_timer_create(&m_sensor_char_timer_id, APP_TIMER_MODE_SINGLE_SHOT, timer_timeout_handler);
}

/**@brief Function for the GAP initialization.
 *
 * @details This function sets up all the necessary GAP (Generic Access Profile) parameters of the
 *          device including the device name, appearance, and the preferred connection parameters.
 */
static void gap_params_init(void) {
  ret_code_t err_code;
  ble_gap_conn_params_t gap_conn_params;
  ble_gap_conn_sec_mode_t sec_mode;

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

  err_code = sd_ble_gap_device_name_set(&sec_mode,
      (const uint8_t *)DEVICE_NAME,
      strlen(DEVICE_NAME));
  APP_ERROR_CHECK(err_code);

  memset(&gap_conn_params, 0, sizeof(gap_conn_params));

  gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
  gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
  gap_conn_params.slave_latency = SLAVE_LATENCY;
  gap_conn_params.conn_sup_timeout = CONN_SUP_TIMEOUT;

  err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
  APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the GATT module.
 */
static void gatt_init(void) {
  ret_code_t err_code = nrf_ble_gatt_init(&m_gatt, NULL);
  APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling Queued Write Module errors.
 *
 * @details A pointer to this function will be passed to each service which may need to inform the
 *          application about an error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void nrf_qwr_error_handler(uint32_t nrf_error) {
  APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for initializing services that will be used by the application.
 */
static void services_init(void) {

  uint32_t err_code;
  nrf_ble_qwr_init_t qwr_init = {0};

  // Initialize Queued Write Module.
  qwr_init.error_handler = nrf_qwr_error_handler;

  err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
  APP_ERROR_CHECK(err_code);

  //FROM_SERVICE_TUTORIAL: Add code to initialize the services used by the application.
  sensor_service_init(&m_sensor_service);
}

/**@brief Function for handling the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection Parameters Module which
 *          are passed to the application.
 *          @note All this function does is to disconnect. This could have been done by simply
 *                setting the disconnect_on_fail config parameter, but instead we use the event
 *                handler mechanism to demonstrate its use.
 *
 * @param[in] p_evt  Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t *p_evt) {
  ret_code_t err_code;

  if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED) {
    err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
    APP_ERROR_CHECK(err_code);
  }
}

/**@brief Function for handling a Connection Parameters error.
 *
 * @param[in] nrf_error  Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error) {
  APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void) {
  ret_code_t err_code;
  ble_conn_params_init_t cp_init;

  memset(&cp_init, 0, sizeof(cp_init));

  cp_init.p_conn_params = NULL;
  cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
  cp_init.next_conn_params_update_delay = NEXT_CONN_PARAMS_UPDATE_DELAY;
  cp_init.max_conn_params_update_count = MAX_CONN_PARAMS_UPDATE_COUNT;
  cp_init.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
  cp_init.disconnect_on_fail = false;
  cp_init.evt_handler = on_conn_params_evt;
  cp_init.error_handler = conn_params_error_handler;

  err_code = ble_conn_params_init(&cp_init);
  APP_ERROR_CHECK(err_code);
}

/**@brief Function for putting the chip into sleep mode.
 *
 * @note This function will not return.
 */
static void sleep_mode_enter(void) {
  ret_code_t err_code;

  err_code = bsp_indication_set(BSP_INDICATE_IDLE);
  APP_ERROR_CHECK(err_code);

  // Prepare wakeup buttons.
  err_code = bsp_btn_ble_sleep_mode_prepare();
  APP_ERROR_CHECK(err_code);

  // Go to system-off mode (this function will not return; wakeup will cause a reset).
  err_code = sd_power_system_off();
  APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed to the application.
 *
 * @param[in] ble_adv_evt  Advertising event.
 */
static void on_adv_evt(ble_adv_evt_t ble_adv_evt) {
  ret_code_t err_code;

  switch (ble_adv_evt) {
  case BLE_ADV_EVT_FAST:
    NRF_LOG_INFO("Fast advertising.");
    err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING);
    APP_ERROR_CHECK(err_code);
    break;

  case BLE_ADV_EVT_IDLE:
    sleep_mode_enter();
    break;

  default:
    break;
  }
}

/**@brief Function for handling BLE events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 * @param[in]   p_context   Unused.
 */
static void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context) {
  ret_code_t err_code = NRF_SUCCESS;
  ble_gatts_evt_write_t const *write_evt;

  switch (p_ble_evt->header.evt_id) {
  case BLE_GAP_EVT_DISCONNECTED:
  AFE4404_ADCRDY_Interrupt_Disable();
    //app_timer_stop(m_sensor_char_timer_id);
    //Check if the timer expired which means we already turned the AFE off
      AFE4404_Enable_HWPDN();

    
    apptimerrunning = 0;
    readDataFlag = 0;
    issue_disconnect = 0;
    Calibration = 1;
    streaming = false;
    programming = false;
    nrf_pwr_mgmt_run();

    //err_code = sd_power_system_off();
   // APP_ERROR_CHECK(err_code);

    break;

  case BLE_GAP_EVT_CONNECTED:
    NRF_LOG_INFO("Connected.");
    printf("Device Connected ::: Completed \n");
    err_code = bsp_indication_set(BSP_INDICATE_CONNECTED);
    APP_ERROR_CHECK(err_code);

    m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
    err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
    APP_ERROR_CHECK(err_code);
    AFE4404_Init();
    update_AFE_phase(LED_phase); //recall the scheme if set

  //Recall the last saved prog update
    AFE4404_Reg_Write(0, 0x0); //CONTROL0
    AFE4404_Reg_Write(AFE_DAC_SETTING_REG,dac_set);
    AFE4404_Reg_Write(AFE_TIAAMBGAIN,main_tia_gain);
    AFE4404_Reg_Write(AFE_TIAGAIN,sep_gain);
    AFE4404_Reg_Write(AFE_LEDCNTRL,LED_cntrl);
    AFE4404_Reg_Write(AFE_CONTROL0, 0x00000001); //seal the changes

    AFE4404_ADCRDY_Interrupt_Enable();
    prfcount = 0;
    Calibration = 1;

    sd_ble_gap_tx_power_set(BLE_GAP_TX_POWER_ROLE_CONN, BLE_CONN_HANDLE_INVALID, -16);
    break;

  case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
    NRF_LOG_DEBUG("PHY update request.");
    ble_gap_phys_t const phys =
        {
            .rx_phys = BLE_GAP_PHY_AUTO,
            .tx_phys = BLE_GAP_PHY_AUTO,
        };
    err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
    APP_ERROR_CHECK(err_code);
  } break;

  case BLE_GATTC_EVT_TIMEOUT:
    // Disconnect on GATT Client timeout event.
    NRF_LOG_DEBUG("GATT Client Timeout.");
    err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
        BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    APP_ERROR_CHECK(err_code);
    break;

  case BLE_GATTS_EVT_TIMEOUT:
    // Disconnect on GATT Server timeout event.
    NRF_LOG_DEBUG("GATT Server Timeout.");
    err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
        BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    APP_ERROR_CHECK(err_code);
    break;

  case BLE_GATTS_EVT_WRITE:

    write_evt = &p_ble_evt->evt.gatts_evt.params.write;
    uint16_t handle = write_evt->handle;

    //Handle the case where we want to stream out data
    if (handle == stream_service) //Note that the stream_service handle is not the db handle, its the cccd
    {
      streaming = true;
      if(developer_mode==1)
      {
        CALIBRATION_FINISHED =true;
      }

    }

    else if (handle == prog_service) {
      //Setup an allocated chunk in memory to read out the data from the gatt server.
      uint8_t prog_data[14] = {0x00};
      ble_gatts_value_t temp;
      temp.p_value = prog_data;
      temp.len = 14;
      temp.offset = 0;

      sd_ble_gatts_value_get(BLE_CONN_HANDLE_INVALID, prog_service, &temp); //Read the prog_service write event

      //Unpack the data
      developer_mode = prog_data[0];
      LED_phase = prog_data[1];
      LED1_drive = prog_data[2];
      LED2_drive = prog_data[3];
      LED3_drive = prog_data[4];
      en_sep = prog_data[5];
      TIA_R1 = prog_data[6];
      TIA_C1 = prog_data[7];
      TIA_R2 = prog_data[8];
      TIA_C2 = prog_data[9];
      DAC1_off = prog_data[10];
      DAC2_off = prog_data[11];
      DAC3_off = prog_data[12];
      DACA_off = prog_data[13];

      printf("%X\n", DAC3_off);
      printf("%X\n", DACA_off);
      programming = true;
    }

    else if (handle == time_monitor_service) {
      uint8_t time_data[3] = {0x00};
      ble_gatts_value_t temp;
      temp.p_value = time_data;
      temp.len = 3;
      temp.offset = 0;
      uint32_t length = 0;

      sd_ble_gatts_value_get(BLE_CONN_HANDLE_INVALID, time_monitor_service, &temp); //Read the prog_service write event
      monitorEnabled = time_data[0];
      monitor_reload = ((uint16_t)time_data[2] << 8) | time_data[1]; //Need to stitch the last two bytes to get value;

      if (monitorEnabled == 1) {
        length = APP_TIMER_TICKS(monitor_reload * 1000); //convert seconds to ms incremenets for the timer
      }

      else 
      {
        length = 8388607; //Set the max to be 14 days, this exceeds the lifetime of the device. But we NEED the rtc timer to time sync the data time stamps.
      }

      //When connected; start app timer to let the data be captured and sent out
      //app_timer_start(m_sensor_char_timer_id, length, NULL);
      apptimerrunning = 1; //reguardless if the monitor is enabled, we need to go ahead and make the main loop run by giving this value a HIGH
    }

    break;

    //This might seem odd, but we need to issue the disconnect after a CONN_PARAM_UPDATE has occured. For some reason if we issue the disconnect command in the timer
    //interrupt handler, the softdevice context derails and we get a kernel failure. However, this helps us avoid poor context handling.
  case BLE_GATTS_EVT_HVN_TX_COMPLETE:
    if (issue_disconnect == 1) {
      //  printf("Timer Expired! ::: Disconnecting ::: Completed \n");
      err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
      APP_ERROR_CHECK(err_code);
      issue_disconnect = 0;
      
    }
    break;

  default:
    break;
  }
}

/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void) {
  ret_code_t err_code;

  err_code = nrf_sdh_enable_request();
  APP_ERROR_CHECK(err_code);

  // Configure the BLE stack using the default settings.
  // Fetch the start address of the application RAM.
  uint32_t ram_start = 0;
  err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
  APP_ERROR_CHECK(err_code);

  // Enable BLE stack.
  err_code = nrf_sdh_ble_enable(&ram_start);
  APP_ERROR_CHECK(err_code);

  // Register a handler for BLE events.
  NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);

  //SENSOR_JOB: Step 3.C Set up a BLE event observer to call ble_sensor_service_on_ble_evt() to do housekeeping of ble connections related to sensor service and characteristics.
  NRF_SDH_BLE_OBSERVER(m_sensor_service_observer, APP_BLE_OBSERVER_PRIO, ble_sensor_service_on_ble_evt, (void *)&m_sensor_service);
}

/**@brief Function for the Peer Manager initialization.
 */
static void peer_manager_init(void) {
  ble_gap_sec_params_t sec_param;
  ret_code_t err_code;

  err_code = pm_init();
  APP_ERROR_CHECK(err_code);

  memset(&sec_param, 0, sizeof(ble_gap_sec_params_t));

  // Security parameters to be used for all security procedures.
  sec_param.bond = SEC_PARAM_BOND;
  sec_param.mitm = SEC_PARAM_MITM;
  sec_param.lesc = SEC_PARAM_LESC;
  sec_param.keypress = SEC_PARAM_KEYPRESS;
  sec_param.io_caps = SEC_PARAM_IO_CAPABILITIES;
  sec_param.oob = SEC_PARAM_OOB;
  sec_param.min_key_size = SEC_PARAM_MIN_KEY_SIZE;
  sec_param.max_key_size = SEC_PARAM_MAX_KEY_SIZE;
  sec_param.kdist_own.enc = 1;
  sec_param.kdist_own.id = 1;
  sec_param.kdist_peer.enc = 1;
  sec_param.kdist_peer.id = 1;

  err_code = pm_sec_params_set(&sec_param);
  APP_ERROR_CHECK(err_code);

  err_code = pm_register(pm_evt_handler);
  APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling events from the BSP module.
 *
 * @param[in]   event   Event generated when button is pressed.
 */
static void bsp_event_handler(bsp_event_t event) {
  ret_code_t err_code;

  switch (event) {
  case BSP_EVENT_SLEEP:
    sleep_mode_enter();
    break; // BSP_EVENT_SLEEP

  case BSP_EVENT_DISCONNECT:
    err_code = sd_ble_gap_disconnect(m_conn_handle,
        BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    if (err_code != NRF_ERROR_INVALID_STATE) {
      APP_ERROR_CHECK(err_code);
    }
    break; // BSP_EVENT_DISCONNECT

  case BSP_EVENT_WHITELIST_OFF:
    if (m_conn_handle == BLE_CONN_HANDLE_INVALID) {
      err_code = ble_advertising_restart_without_whitelist(&m_advertising);
      if (err_code != NRF_ERROR_INVALID_STATE) {
        APP_ERROR_CHECK(err_code);
      }
    }
    break; // BSP_EVENT_KEY_0

  default:
    break;
  }
}

/**@brief Function for initializing the Advertising functionality.
 */
static void advertising_init(void) {
  ret_code_t err_code;
  ble_advertising_init_t init;

  memset(&init, 0, sizeof(init));

  init.advdata.name_type = BLE_ADVDATA_FULL_NAME;
  init.advdata.include_appearance = true;
  init.advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;

  init.srdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
  init.srdata.uuids_complete.p_uuids = m_adv_uuids;

  init.config.ble_adv_fast_enabled  = true;
    init.config.ble_adv_fast_interval = APP_ADV_FAST_INTERVAL;
    init.config.ble_adv_fast_timeout  = APP_ADV_FAST_DURATION;

  init.evt_handler = on_adv_evt;

  err_code = ble_advertising_init(&m_advertising, &init);
  APP_ERROR_CHECK(err_code);

  ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}

/**@brief Function for handling the idle state (main loop).
 *
 * @details If there is no pending log operation, then sleep until next the next event occurs.
 */
static void idle_state_handle(void) {
  if (NRF_LOG_PROCESS() == false) {
    nrf_pwr_mgmt_run();
  }
}

/**@brief Function for initializing the nrf log module.
 */
static void log_init(void) {
  ret_code_t err_code = NRF_LOG_INIT(NULL);
  APP_ERROR_CHECK(err_code);
  NRF_LOG_DEFAULT_BACKENDS_INIT();
}

/**@brief Function for initializing power management.
 */
static void power_management_init(void) {
  ret_code_t err_code;
  err_code = nrf_pwr_mgmt_init();
  APP_ERROR_CHECK(err_code);
}

//@brief Function for computing mean of an array (16b)
double array_mean(int low, int high, uint16_t a[]) {
  double sum = 0.0000;
  int i;
  for (i = low; i < high; i++)
    sum += (double)a[i];
  return ((double)sum / (high - low));
}

void ADCRDY_HANDLER(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
  // readDataFlag = 1; //Set the readflag high to let us know that data has been captured on the ADC
  readDataFlag = 1; //Set the readflag high to let us know that data has been captured on the ADC
  //Grab the timer flag from the clock timer
  ticks_now = app_timer_cnt_get();
}

/**********************************************************************************************************/
/* AFE4404_ADCRDY_PIN_INIT											                          */
/**********************************************************************************************************/
void ADC_READY_GPIO_HANDLER_INIT(void) {
  ret_code_t err_code;

  err_code = nrf_drv_gpiote_init();
  APP_ERROR_CHECK(err_code);

  /*Initialize the input pin to sense High to Low transition to grab the data after the ADC has dumped its value*/
  nrf_drv_gpiote_in_config_t in_config = GPIOTE_CONFIG_IN_SENSE_HITOLO(true);
  nrf_drv_gpiote_out_config_t out_config = GPIOTE_CONFIG_OUT_SIMPLE(true);
  /*Set the pin input to Pulldown mode to keep the state defined when no driver present*/
  in_config.pull = NRF_GPIO_PIN_PULLDOWN;
  out_config.init_state = NRF_GPIOTE_INITIAL_VALUE_HIGH;

  /*Send Init config to GPIOTE handler*/
  err_code = nrf_drv_gpiote_in_init(ADC_RDY, &in_config, ADCRDY_HANDLER);
  APP_ERROR_CHECK(err_code);

}
//Function for calibrating the DAC offset with PD disconnected. Runs once at start up and then ceases. Its for the inherient offset in the input.
void DAC_CALIBRATION_AUTOSTART(void) {

  while (offsetDACcalibFlag == 0) {
    if (readDataFlag == 1) {
      readDataFlag = 0;
      offsetDACcalibFlag = OFFSET_DAC_Code_Est(AFE4404_Reg_Read(AMB_LED_1_VAL)); //Read the ambient value and figure out how much current offset there is
    }

    if (offsetDACcalibFlag == 1) {
      AFE4404_ADCRDY_Interrupt_Disable();
    }
  }
}

//handler for three phase calibration. Each case will sequentially feed in the correct LED value and associated AMB1 value for calibration
void three_phase_calib(signed long LED1, signed long LED2, signed long LED3, signed long AMB1, uint8_t LED_phase) {
  switch (LED_Sel) {

  case 1: //LED1 Green
    CalibrateAFE4404(LED1, AMB1, LED_phase);
    break;

  case 2: //LED 2 Red
    CalibrateAFE4404(LED2, AMB1, LED_phase);
    break;

  case 3: //LED3 NIR
    CalibrateAFE4404(LED3, AMB1, LED_phase);
    break;

  default:
    break;
  }
}

//Calibration routine for green-nir handler
void green_nir_calib(signed long LED1, signed long LED2, signed long LED3, signed long AMB1, uint8_t LED_phase) {
  switch (LED_Sel) {

  case 1: //LED1 Green
    CalibrateAFE4404(LED1, LED2, LED_phase);
    break;

  case 3:                                    //LED3 NIR
    CalibrateAFE4404(LED3, AMB1, LED_phase); //Use LED2 as the ambient
    break;

  default:
    break;
  }
}

//calibration routine for green-red handler
void green_red_calib(signed long LED1, signed long LED2, signed long LED3, signed long AMB1, uint8_t LED_phase) {
  switch (LED_Sel) {

  case 1: //LED1 Green
    CalibrateAFE4404(LED1, AMB1, LED_phase);
    break;

  case 2:                                    //LED 2 Red
    CalibrateAFE4404(LED2, AMB1, LED_phase); //Use LED3 as the ambient cancellation reference
    break;

  default:
    break;
  }
}

//Calibration routine for red-nir handler
void red_nir_calib(signed long LED1, signed long LED2, signed long LED3, signed long AMB1, uint8_t LED_phase) {
  switch (LED_Sel) {

  case 2:                                    //LED 2 Red
    CalibrateAFE4404(LED2, LED1, LED_phase); //Use the green LED phase as the ambient calibration for red
    break;

  case 3: //LED3 NIR
    CalibrateAFE4404(LED3, AMB1, LED_phase);
    break;

  default:
    break;
  }
}

//Routine handler for calibrating each LED individually
void postCalib(signed long LED1, signed long LED2, signed long LED3, signed long AMB1, uint8_t LED_phase) {
  switch (LED_phase) {
  case 0x01: //NIR phase only
    CalibrateAFE4404(LED3, AMB1, LED_phase);
    break;

  case 0x02: //green phase only
    CalibrateAFE4404(LED1, AMB1, LED_phase);
    break;

  case 0x03: //green and nir phase
    green_nir_calib(LED1, LED2, LED3, AMB1, LED_phase);
    break;

  case 0x04: //red ohase only
    CalibrateAFE4404(LED2, AMB1, LED_phase);
    break;

  case 0x05: //red and nir phase
    red_nir_calib(LED1, LED2, LED3, AMB1, LED_phase);
    break;

  case 0x06: //red-green phase
    green_red_calib(LED1, LED2, LED3, AMB1, LED_phase);
    break;

  case 0x07: //all phases enabled
    three_phase_calib(LED1, LED2, LED3, AMB1, LED_phase);
    three_phase_calib(LED1, LED2, LED3, AMB1, LED_phase);
    break;
  }
}

void soft_disconnect(void) //for use outside of the ble softdevice handler
{
  app_timer_stop(m_sensor_char_timer_id);
  apptimerrunning = 0;
  readDataFlag = 0;
  AFE4404_ADCRDY_Interrupt_Disable();
  AFE4404_Enable_HWPDN();

  uint32_t err_code;
  err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
  APP_ERROR_CHECK(err_code);
}

/**********************************************************************************************************/
/*                                                  MAIN APP ENTRY                                        */
/**********************************************************************************************************/
int main(void) {

  //Boot up operations for LOG, Timer, Power-MAN, and I2C
  bool erase_bonds;
  log_init();
  timers_init();
  power_management_init();
  ADC_READY_GPIO_HANDLER_INIT();
  printf("STEP 1 ::: Initalizations ::: Completed\n");
  nrf_delay_ms(10);
  /*Initialize I2C interface - 400k baud*/
  I2C_init(BA_SDA_PIN, BA_SCL_PIN);

  /*Initialize AFE4404 */
  AFE4404_RESETZ_Init(); //Initialize the ACTIVE LOW input
  AFE4404_ADCRDY_Interrupt_Disable();

  AFE4404_Trigger_HWReset(); //Restart the device and reset HW config defaults

  /*Program the AFE4404*/
  AFE4404_Init(); //Initilaizes all the ports needed perihperal to the AFE

  printf("STEP 2 ::: PPG AFE Bring Up ::: Completed\n");

  AFE4404_ADCRDY_Interrupt_Enable();
  DAC_CALIBRATION_AUTOSTART();
  LED_Sel = 1;
  initCalibrationRoutine();

  //Power Down the AFE after the DAC-Calibration is completed
  AFE4404_Enable_HWPDN();

  printf("STEP 3 ::: DAC Offset Calibrated ::: Completed\n");

  //Set up the BLE peripheral server with softdevice handler routines, begin advertising
  ble_stack_init();
  gap_params_init();
  gatt_init();
  services_init();
  advertising_init();
  conn_params_init();
  peer_manager_init();
  advertising_start(erase_bonds);

  		ret_code_t err_code = sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
		APP_ERROR_CHECK(err_code);
		err_code = sd_power_mode_set(NRF_POWER_MODE_LOWPWR);
		APP_ERROR_CHECK(err_code);


  printf("STEP 4 ::: BLE Setup  ::: Completed\n");

  /*ENTER WHILE LOOP*/
  while (1) {
    //idle_state_handle(); //sleep! 

  //sd_app_evt_wait(); 
    bool streaming_select = ((apptimerrunning == 1) && (readDataFlag == 1) && (streaming)); //This will be true when we want to stream data to the phone.
    bool programming_select = ((apptimerrunning == 1) && (programming));                    //This will be true when we only want to programm the device.
    //If the app timer is running then we are in a valid connection and we are okay to collect data and send it out
    if (streaming_select) {

      AFE4404_ADCRDY_Interrupt_Disable(); //Stop the processing and go ahead with dumping the data in;
      readDataFlag = 0;                   //Clear Read flag and pump out current ADC values

      //Store reads locally into buffer
      AFE_Buffer[0] = AFE4404_Reg_Read(42); //LED2
      AFE_Buffer[1] = AFE4404_Reg_Read(44); //LED1
      AFE_Buffer[2] = AFE4404_Reg_Read(43); //LED3
      AFE_Buffer[3] = AFE4404_Reg_Read(45); //AMB1

      //Create new buffers to hold each piece of data with the timestamp
      //Need 4 Bytes for the reading, and 4 bytes for the timestamp. This covers the entire 32 bit range. So each service will have 8 bytes being sent, for a total of 24 bytes for data
      time_tick_update = ticks_now;
      test[0] = (int32_t)(AFE_Buffer[0]); //Get red-AMB1 val reg
      test[1] = (int32_t)(AFE_Buffer[1]); //Get green-AMB1 val reg
      test[2] = (int32_t)(AFE_Buffer[2]); //Get NIR-AMB1 val reg
      test[3] = (int32_t)(AFE_Buffer[3]); //Get AMB1 val reg
      test[4] = time_tick_update;

      //Use memcpy to get the correct blocks from the data to then send on over to the sensor update function
      uint8_t test_nibble[20];

      memcpy(test_nibble, &test, sizeof(test));

      sensor2_characteristic_update(&m_sensor_service, test_nibble);
      prfcount++;

      //We want Calibrate the system each time that we wake-up to start measuring values
      if (CALIBRATION_ENABLED && (Calibration == 1)) //first time we enter the statement
      {
          postCalib(AFE_Buffer[1], AFE_Buffer[0], AFE_Buffer[2], AFE_Buffer[3], LED_phase);
        }

        if (prfcount == CALIB_PRF_UPDATE) //Each count update is at 10ms, or whatever the set sampling frequency is. Default = 15 minutes
        {
          g_OneSecondFlag = 1;
          Calibration = 1; //This will raise the flag for the calibration to be redone for the PPG sensor
          prfcount = 0;
        }

       if (CALIBRATION_FINISHED) //This is to send the updated afe configuration identified by the calib routine
        {

          CALIBRATION_FINISHED = false;
          afe_read_settings(LED_phase);

          sensor1_characteristic_update(&m_sensor_service, setting_8b);
        }


      AFE4404_ADCRDY_Interrupt_Enable();
    }

    //If programming is selected, program the AFE here
    else if (programming_select) {
      printf("Prog Service Update \n");
      program_AFE4404(developer_mode, LED_phase, LED1_drive, LED2_drive, LED3_drive, TIA_R1, TIA_C1, en_sep, TIA_R2, TIA_C2, DAC1_off, DAC2_off, DAC3_off, DACA_off);
      programming = false;
      soft_disconnect();
    }

    nrf_pwr_mgmt_run();
  } //End of while change log

   
}