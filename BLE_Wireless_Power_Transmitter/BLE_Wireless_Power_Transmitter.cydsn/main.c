/*******************************************************************************
* File Name: main.c
*
* Version: 1.0
*
* Description:
*  This project demonstrates Wireless Power Transfer profile in Power 
*  Transmitter Unit (PTU) Client role.
*
* Related Document:
*  A4WP Wireless Power Transfer System Baseline System Specification (BSS)
*   V1.2.1
*  BLUETOOTH SPECIFICATION Version 4.1
*
*******************************************************************************
* Copyright 2016, Cypress Semiconductor Corporation.  All rights reserved.
* You may use this file only in accordance with the license, terms, conditions,
* disclaimers, and limitations in the end user license agreement accompanying
* the software package with which this file was provided.
*******************************************************************************/

#include "wptu.h"
#include "common.h"
#include <stdbool.h>

volatile uint32 mainTimer = 0u;

CYBLE_PEER_DEVICE_INFO_T peedDeviceInfo[CYBLE_MAX_ADV_DEVICES];
uint8 advDevices = 0u;
uint8 deviceN = 0u;

uint8 state = STATE_INIT;
uint8 readingDynChar = 0u;

uint8 customCommand = 0u;
uint16 alertCCCD = 0u;
bool requestResponce = true;


CYBLE_GAP_CONN_PARAM_UPDATED_IN_CONTROLLER_T connParameters;


/*******************************************************************************
* Function Name: AppCallBack()
********************************************************************************
*
* Summary:
*   This is an event callback function to receive events from the BLE Component.
*
* Parameters:
*  event - the event code
*  *eventParam - the event parameters
*
* Theory:
* The function is responsible for handling the events generated by the stack.
* It first starts scanning once the stack is initialized. 
* Upon scanning timeout this function enters Hibernate mode.
*
*******************************************************************************/
void AppCallback(uint32 event, void* eventParam)
{
    CYBLE_API_RESULT_T apiResult;
    CYBLE_GATTC_GRP_ATTR_DATA_LIST_T *locAttrData;
    CYBLE_GAPC_ADV_REPORT_T *advReport;
    CYBLE_GAP_AUTH_INFO_T *authInfo;
    uint8 newDevice = 0u;
    uint8 type;
    uint16 i;
    uint16 length;
    uint8 *locHndlUuidList;
    
	switch (event)
	{
		case CYBLE_EVT_STACK_ON: /* This event received when BLE stack is ON. */
            DBG_PRINTF("Bluetooth On \r\n");
            apiResult = CyBle_GapcStartScan(CYBLE_SCANNING_FAST);                   /* Start Limited Discovery */
            if(apiResult != CYBLE_ERROR_OK)
            {
                DBG_PRINTF("StartScan API Error: %xd \r\n", apiResult);
            }
			break;
		case CYBLE_EVT_TIMEOUT: 
            DBG_PRINTF("CYBLE_EVT_TIMEOUT: %x \r\n", *(CYBLE_TO_REASON_CODE_T *)eventParam);
			break;
		case CYBLE_EVT_HARDWARE_ERROR:    /* This event indicates that some internal HW error has occurred. */
            DBG_PRINTF("Hardware Error: %x \r\n", *(uint8 *)eventParam);
			break;
        case CYBLE_EVT_HCI_STATUS:
            DBG_PRINTF("CYBLE_EVT_HCI_STATUS: %x \r\n", *(uint8 *)eventParam);
			break;
    	case CYBLE_EVT_STACK_BUSY_STATUS:
            DBG_PRINTF("CYBLE_EVT_STACK_BUSY_STATUS: %x\r\n", CyBle_GattGetBusStatus());
            break;
            
        /**********************************************************
        *                       GAP Events
        ***********************************************************/
        /* This event provides the remote device lists during discovery process. */
        case CYBLE_EVT_GAPC_SCAN_PROGRESS_RESULT:
            {    
                CYBLE_PRU_ADV_SERVICE_DATA_T serviceData;
                advReport = (CYBLE_GAPC_ADV_REPORT_T *)eventParam;
                
                if(WptsScanProcessEventHandler(advReport, &serviceData) != 0u)
                {
                    DBG_PRINTF("Advertisement report: eventType = %x, peerAddrType - %x, ", 
                        advReport->eventType, advReport->peerAddrType);
                    DBG_PRINTF("peerBdAddr - #");
                    for(newDevice = 1u, i = 0u; i < advDevices; i++)
                    {
                        /* Compare device address with already logged one */
                        if((memcmp(peedDeviceInfo[i].peerAddr.bdAddr, advReport->peerBdAddr, CYBLE_GAP_BD_ADDR_SIZE) == 0)) 
                        {
                            DBG_PRINTF("%x: ",i);
                            newDevice = 0u;
                            break;
                        }
                    }
                    if(newDevice != 0u)                    
                    {
                        if(advDevices < CYBLE_MAX_ADV_DEVICES)
                        {
                            memcpy(peedDeviceInfo[advDevices].peerAddr.bdAddr, advReport->peerBdAddr, CYBLE_GAP_BD_ADDR_SIZE);
                            peedDeviceInfo[advDevices].peerAddr.type = advReport->peerAddrType;
                            peedDeviceInfo[advDevices].peerAdvServData = serviceData;
                            DBG_PRINTF("%x: ",advDevices);
                            advDevices++;
                        }
                    }
                    for(i = CYBLE_GAP_BD_ADDR_SIZE; i > 0u; i--)
                    {
                        DBG_PRINTF("%2.2x", advReport->peerBdAddr[i-1]);
                    }
                    DBG_PRINTF(", rssi - %d dBm,\r\n data - ", advReport->rssi);
                    for( i = 0; i < advReport->dataLen; i++)
                    {
                        DBG_PRINTF("%2.2x ", advReport->data[i]);
                    }
                    DBG_PRINTF("\r\n");
                }
            }
            break;
	    case CYBLE_EVT_GAPC_SCAN_START_STOP:
            DBG_PRINTF("CYBLE_EVT_GAPC_SCAN_START_STOP, state: %x\r\n", CyBle_GetState());
            if(CyBle_GetState() == CYBLE_STATE_DISCONNECTED)
            {
                if(state == STATE_CONNECTING)
                {
                    DBG_PRINTF("GAPC_END_SCANNING\r\n");
                    /* Connect to selected device */
                    cyBle_connectingTimeout = 5;
                    apiResult = CyBle_GapcConnectDevice(&peedDeviceInfo[deviceN].peerAddr);
                    if(apiResult != CYBLE_ERROR_OK)
                    {
                        DBG_PRINTF("ConnectDevice API Error: %x \r\n", apiResult);
                    }
                }
                else
                {
                    /* Fast scanning period complete,
                     * go to low power mode (Hibernate mode) and wait for an external
                     * user event to wake up the device again */
                    DBG_PRINTF("Hibernate \r\n");
                    UpdateLedState();
                #if (DEBUG_UART_ENABLED == ENABLED)
                    while((UART_DEB_SpiUartGetTxBufferSize() + UART_DEB_GET_TX_FIFO_SR_VALID) != 0);
                #endif /* (DEBUG_UART_ENABLED == ENABLED) */
                    SW2_ClearInterrupt();
                    Wakeup_Interrupt_ClearPending();
                    Wakeup_Interrupt_Start();
                    CySysPmHibernate();
                }
            }
            break;
       case CYBLE_EVT_GAP_AUTH_COMPLETE:
            authInfo = (CYBLE_GAP_AUTH_INFO_T *)eventParam;
            (void)authInfo;
            DBG_PRINTF("AUTH_COMPLETE: security:%x, bonding:%x, ekeySize:%x, authErr %x \r\n", 
                                    authInfo->security, authInfo->bonding, authInfo->ekeySize, authInfo->authErr);
            break;
        case CYBLE_EVT_GAP_AUTH_FAILED:
            DBG_PRINTF("AUTH_FAILED: %x \r\n", *(uint8 *)eventParam);
            break;
        case CYBLE_EVT_GAP_DEVICE_CONNECTED:
            connParameters = *(CYBLE_GAP_CONN_PARAM_UPDATED_IN_CONTROLLER_T *)eventParam;
            DBG_PRINTF("CYBLE_EVT_GAP_DEVICE_CONNECTED: %x, %x(%d ms), %x, %x \r\n",   
                connParameters.status,
                connParameters.connIntv,
                connParameters.connIntv *5 /4,
                connParameters.connLatency,
                connParameters.supervisionTO);
            state = STATE_CONNECTED;
            UpdateLedState();
            
            if(peedDeviceInfo[deviceN].peerAdvServData.wptsServiceHandle != 0u)
            {
                /* Use quick discovery method based on WPT service handle received in the advertising packet */
                CyBle_WptscDiscovery(peedDeviceInfo[deviceN].peerAdvServData.wptsServiceHandle);
                
                DBG_PRINTF("WPTS %x: ",  cyBle_wptsc.serviceHandle);
                for(i = 0u; i < CYBLE_WPTS_CHAR_COUNT; i++)
                {
                    DBG_PRINTF("Char %x=%x, CCCD=%x ", i, CyBle_WptscGetCharacteristicValueHandle(i),
                                                          CyBle_WptscGetCharacteristicDescriptorHandle(i,0u));
                }
                DBG_PRINTF("\r\n");
                /* Initiate to read PRU Static Parameter characteristic value */
                customCommand = '4';
            }
            else
            {
                /* When the service handle is unknown, use the standard discovery procedure */
                customCommand = 's';
            }
            break;
        case CYBLE_EVT_GAP_DEVICE_DISCONNECTED:
            DBG_PRINTF("DEVICE_DISCONNECTED: \r\n");
            apiResult = CyBle_GapcStartScan(CYBLE_SCANNING_FAST);                   /* Start Limited Discovery */
            if(apiResult != CYBLE_ERROR_OK)
            {
                DBG_PRINTF("StartScan API Error: %xd \r\n", apiResult);
            }
            requestResponce = true;
            state = STATE_DISCONNECTED;
            break;
        case CYBLE_EVT_GAP_ENCRYPT_CHANGE:
            DBG_PRINTF("ENCRYPT_CHANGE: %d \r\n", *(uint8 *)eventParam);
            break;
        case CYBLE_EVT_GAPC_CONNECTION_UPDATE_COMPLETE:           
            connParameters = *(CYBLE_GAP_CONN_PARAM_UPDATED_IN_CONTROLLER_T *)eventParam;
            DBG_PRINTF("CYBLE_EVT_GAPC_CONNECTION_UPDATE_COMPLETE: %x, %x(%d ms), %x, %x \r\n",
                connParameters.status,
                connParameters.connIntv,
                connParameters.connIntv *5 /4,
                connParameters.connLatency,
                connParameters.supervisionTO);
            break;
        case CYBLE_EVT_GAP_KEYINFO_EXCHNGE_CMPLT:
            DBG_PRINTF("CYBLE_EVT_GAP_KEYINFO_EXCHNGE_CMPLT \r\n");
            break;
            
        /**********************************************************
        *                       GATT Events
        ***********************************************************/

        case CYBLE_EVT_GATTC_ERROR_RSP:
            DBG_PRINTF("GATT_ERROR_RSP: opcode: %x,  handle: %x,  errorcode: %x \r\n",
                ((CYBLE_GATTC_ERR_RSP_PARAM_T *)eventParam)->opCode,
                ((CYBLE_GATTC_ERR_RSP_PARAM_T *)eventParam)->attrHandle,
                ((CYBLE_GATTC_ERR_RSP_PARAM_T *)eventParam)->errorCode);
            break;
        case CYBLE_EVT_GATT_CONNECT_IND:
            DBG_PRINTF("CYBLE_EVT_GATT_CONNECT_IND: %x, %x \r\n", 
                (*(CYBLE_CONN_HANDLE_T *)eventParam).attId, (*(CYBLE_CONN_HANDLE_T *)eventParam).bdHandle);
            break;
        case CYBLE_EVT_GATT_DISCONNECT_IND:
            DBG_PRINTF("GATT_DISCONNECT_IND \r\n");
            break;
        case CYBLE_EVT_GATTC_HANDLE_VALUE_NTF:
            DBG_PRINTF("CYBLE_EVT_GATT_HANDLE_VALUE_NTF: handle: %x, Value:", ((CYBLE_GATTC_HANDLE_VALUE_IND_PARAM_T *)eventParam)->handleValPair.attrHandle );
            ShowValue(&((CYBLE_GATTC_HANDLE_VALUE_IND_PARAM_T *)eventParam)->handleValPair.value, 0u);
            break;  
        case CYBLE_EVT_GATTC_HANDLE_VALUE_IND:
            DBG_PRINTF("CYBLE_EVT_GATTC_HANDLE_VALUE_IND: handle: %x, Value:", ((CYBLE_GATTC_HANDLE_VALUE_IND_PARAM_T *)eventParam)->handleValPair.attrHandle );
            ShowValue(&((CYBLE_GATTC_HANDLE_VALUE_IND_PARAM_T *)eventParam)->handleValPair.value, 0u);
            break;
        case CYBLE_EVT_GATTC_INDICATION:
            DBG_PRINTF("CYBLE_EVT_GATTC_INDICATION: handle: %x, Value:", ((CYBLE_GATTC_HANDLE_VALUE_IND_PARAM_T *)eventParam)->handleValPair.attrHandle );
            ShowValue(&((CYBLE_GATTC_HANDLE_VALUE_IND_PARAM_T *)eventParam)->handleValPair.value, 0u);
            break;
        case CYBLE_EVT_GATTC_READ_RSP:
            DBG_PRINTF("CYBLE_EVT_GATT_READ_RSP: ");
            ShowValue(&((CYBLE_GATTC_READ_RSP_PARAM_T *)eventParam)->value, 0u);
            break;            
        case CYBLE_EVT_GATTC_READ_BLOB_RSP:
            DBG_PRINTF("CYBLE_EVT_GATTC_READ_BLOB_RSP: ");
            ShowValue(&((CYBLE_GATTC_READ_RSP_PARAM_T *)eventParam)->value, 0u);
            break;
        case CYBLE_EVT_GATTC_WRITE_RSP:
            DBG_PRINTF("CYBLE_EVT_GATT_WRITE_RSP \r\n");
            break;            
        case CYBLE_EVT_GATTC_XCHNG_MTU_RSP:
            DBG_PRINTF("CYBLE_EVT_GATT_XCHNG_MTU_RSP \r\n");
            break;
        case CYBLE_EVT_GATTC_READ_BY_GROUP_TYPE_RSP: /* Response to CYBLE_DiscoverAllPrimServices() */
            DBG_PRINTF("CYBLE_EVT_GATT_READ_BY_GROUP_TYPE_RSP: ");
            locAttrData = &(*(CYBLE_GATTC_READ_BY_GRP_RSP_PARAM_T *)eventParam).attrData;
            for(i = 0u; i < locAttrData -> attrLen; i ++)
            { 
                DBG_PRINTF("%2.2x ",*(uint8 *)(locAttrData->attrValue + i));
            }
                DBG_PRINTF("\r\n");
            break;        
        case CYBLE_EVT_GATTC_READ_BY_TYPE_RSP:      /* Response to CYBLE_DiscoverAllCharacteristicsOfService() */
            DBG_PRINTF("CYBLE_EVT_GATT_READ_BY_TYPE_RSP: ");
            locAttrData = &(*(CYBLE_GATTC_READ_BY_TYPE_RSP_PARAM_T *)eventParam).attrData;
            for(i = 0u; i < locAttrData -> attrLen; i ++)
            { 
                DBG_PRINTF("%2.2x ",*(uint8 *)(locAttrData->attrValue + i));
            }
            DBG_PRINTF("\r\n");
            break;
        case CYBLE_EVT_GATTC_FIND_INFO_RSP:
            DBG_PRINTF("CYBLE_EVT_GATT_FIND_INFO_RSP: ");
            type = (*(CYBLE_GATTC_FIND_INFO_RSP_PARAM_T *)eventParam).uuidFormat;
            locHndlUuidList = (*(CYBLE_GATTC_FIND_INFO_RSP_PARAM_T *)eventParam).handleValueList.list;
            (void)locHndlUuidList;
            if(type == CYBLE_GATT_16_BIT_UUID_FORMAT)
            {
                length = CYBLE_ATTR_HANDLE_LEN + CYBLE_GATT_16_BIT_UUID_SIZE;
            }
            else
            {
                length = CYBLE_ATTR_HANDLE_LEN + CYBLE_GATT_128_BIT_UUID_SIZE;
            }
            for(i = 0u; i < (*(CYBLE_GATTC_FIND_INFO_RSP_PARAM_T *)eventParam).handleValueList.byteCount; i += length)
            {
                if(type == CYBLE_GATT_16_BIT_UUID_FORMAT)
                {
                    DBG_PRINTF("%2.2x %2.2x,",CyBle_Get16ByPtr(locHndlUuidList + i),
                                              CyBle_Get16ByPtr(locHndlUuidList + i + CYBLE_ATTR_HANDLE_LEN));
                }
                else
                {
                    DBG_PRINTF("UUID128");
                }
            }
            DBG_PRINTF("\r\n");
            break;
        case CYBLE_EVT_GATTC_FIND_BY_TYPE_VALUE_RSP:
            DBG_PRINTF("CYBLE_EVT_GATT_FIND_BY_TYPE_VALUE_RSP \r\n");
            break;
                
        /**********************************************************
        *                       Discovery Events 
        ***********************************************************/
        case CYBLE_EVT_GATTC_DISCOVERY_COMPLETE:
            DBG_PRINTF("CYBLE_EVT_SERVER_DISCOVERY_COMPLETE \r\n");
            DBG_PRINTF("GATT %x-%x Char: %x, cccd: %x, \r\n", 
                cyBle_serverInfo[CYBLE_SRVI_GATT].range.startHandle,
                cyBle_serverInfo[CYBLE_SRVI_GATT].range.endHandle,
                cyBle_gattc.serviceChanged.valueHandle,
                cyBle_gattc.cccdHandle);
            DBG_PRINTF("\r\nWPTS %x-%x: ",  cyBle_serverInfo[CYBLE_SRVI_WPTS].range.startHandle,
                                       cyBle_serverInfo[CYBLE_SRVI_WPTS].range.endHandle);
            for(i = 0u; i < CYBLE_WPTS_CHAR_COUNT; i++)
            {
                DBG_PRINTF("Char %x=%x, CCCD=%x ", i, CyBle_WptscGetCharacteristicValueHandle(i),
                                                      CyBle_WptscGetCharacteristicDescriptorHandle(i,0u));
            }
            DBG_PRINTF("\r\n");
            DBG_PRINTF("\r\n");
            /* Statrt configuration procedure */
            if(state == STATE_CONNECTED) 
            {
                /* Initiate reading the PRU Static Parameter characteristic value */
                customCommand = '4';
            }
        break;

        /**********************************************************
        *                       Other Events
        ***********************************************************/
        case CYBLE_EVT_PENDING_FLASH_WRITE:
            /* Inform application that flash write is pending. Stack internal data 
            * structures are modified and require to be stored in Flash using 
            * CyBle_StoreBondingData() */
            DBG_PRINTF("CYBLE_EVT_PENDING_FLASH_WRITE\r\n");
            break;
		default:
            DBG_PRINTF("OTHER event: %lx \r\n", event);
			break;
	}
}


/*******************************************************************************
* Function Name: Timer_Interrupt
********************************************************************************
*
* Summary:
*  Handles the Interrupt Service Routine for the WDT timer.
*  Blinking Blue LED during scanning process.
*
*******************************************************************************/
CY_ISR(Timer_Interrupt)
{
    if(CySysWdtGetInterruptSource() & WDT_INTERRUPT_SOURCE)
    {
        static uint8 led = LED_OFF;
        
        /* Blink LED to indicate that device scans */
        if(CyBle_GetState() == CYBLE_STATE_SCANNING)
        {
            led ^= LED_ON;
            Scanning_LED_Write(led);
        }
        else if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
        {
            if(readingDynChar != 0u)
            {
                /* Send Read request for PRU Dynamic Parameter characteristic */
                customCommand = '5';
            }
        }
        else
        {
            /* nothing else */
        }
        
        /* Indicate that timer is raised to the main loop */
        mainTimer++;
        
        /* Clears interrupt request  */
        CySysWdtClearInterrupt(WDT_INTERRUPT_SOURCE);
    }
}

void UpdateLedState(void)
{
   
    if(CyBle_GetState() == CYBLE_STATE_DISCONNECTED)
    {   
        Scanning_LED_Write(LED_OFF);
    }
    else if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
    {
        Scanning_LED_Write(LED_ON);
    }
    else
    {
        /* Scanning LED is handled in Timer_Interrupt */
    }
}

/*******************************************************************************
* Function Name: WDT_Start
********************************************************************************
*
* Summary:
*  Configures WDT to trigger an interrupt every second.
*
*******************************************************************************/

void WDT_Start(void)
{
    /* Unlock the WDT registers for modification */
    CySysWdtUnlock(); 
    /* Setup ISR */
    WDT_Interrupt_StartEx(&Timer_Interrupt);
    /* Write the mode to generate interrupt on match */
    CySysWdtWriteMode(WDT_COUNTER, CY_SYS_WDT_MODE_INT);
    /* Configure the WDT counter clear on a match setting */
    CySysWdtWriteClearOnMatch(WDT_COUNTER, WDT_COUNTER_ENABLE);
    /* Configure the WDT counter match comparison value */
    CySysWdtWriteMatch(WDT_COUNTER, WDT_1SEC);
    /* Reset WDT counter */
    CySysWdtResetCounters(WDT_COUNTER);
    /* Enable the specified WDT counter */
    CySysWdtEnable(WDT_COUNTER_MASK);
    /* Lock out configuration changes to the Watchdog timer registers */
    CySysWdtLock();    
}


/*******************************************************************************
* Function Name: WDT_Stop
********************************************************************************
*
* Summary:
*  This API stops the WDT timer.
*
*******************************************************************************/
void WDT_Stop(void)
{
    /* Unlock the WDT registers for modification */
    CySysWdtUnlock(); 
    /* Disable the specified WDT counter */
    CySysWdtDisable(WDT_COUNTER_MASK);
    /* Locks out configuration changes to the Watchdog timer registers */
    CySysWdtLock();    
}


/*******************************************************************************
* Function Name: LowPowerImplementation()
********************************************************************************
* Summary:
* Implements low power in the project.
*
* Parameters:
* None
*
* Return:
* None
*
* Theory:
* The function tries to enter deep sleep as much as possible - whenever the 
* BLE is idle and the UART transmission/reception is not happening. At all other
* times, the function tries to enter CPU sleep.
*
*******************************************************************************/
static void LowPowerImplementation(void)
{
    CYBLE_LP_MODE_T bleMode;
    uint8 interruptStatus;
    
    /* For advertising and connected states, implement deep sleep 
     * functionality to achieve low power in the system. For more details
     * on the low power implementation, refer to the Low Power Application 
     * Note.
     */
    if((CyBle_GetState() == CYBLE_STATE_SCANNING) || 
       (CyBle_GetState() == CYBLE_STATE_CONNECTED))
    {
        /* Request BLE subsystem to enter into Deep-Sleep mode between connection and advertising intervals */
        bleMode = CyBle_EnterLPM(CYBLE_BLESS_DEEPSLEEP);
        /* Disable global interrupts */
        interruptStatus = CyEnterCriticalSection();
        /* When BLE subsystem has been put into Deep-Sleep mode */
        if(bleMode == CYBLE_BLESS_DEEPSLEEP)
        {
            /* And it is still there or ECO is on */
            if((CyBle_GetBleSsState() == CYBLE_BLESS_STATE_ECO_ON) || 
               (CyBle_GetBleSsState() == CYBLE_BLESS_STATE_DEEPSLEEP))
            {
                /* Put the CPU into Sleep mode and let SCB to continue sending debug data and receive commands */
                CySysPmSleep();
            }
        }
        else /* When BLE subsystem has been put into Sleep mode or is active */
        {
            /* And hardware hasn't finished Tx/Rx operation - put the CPU into Sleep mode */
            if(CyBle_GetBleSsState() != CYBLE_BLESS_STATE_EVENT_CLOSE)
            {
                CySysPmSleep();
            }
        }
        /* Enable global interrupt */
        CyExitCriticalSection(interruptStatus);
    }
}


/*******************************************************************************
* Function Name: main()
********************************************************************************
* Summary:
*  Main function for the project.
*
* Parameters:
*  None
*
* Return:
*  None
*
* Theory:
*  The function starts BLE and UART components.
*  This function processes all BLE events and also implements the low power 
*  functionality.
*
*******************************************************************************/
int main()
{
    CYBLE_API_RESULT_T apiResult;
    char8 command = 0u;
    
    CyGlobalIntEnable;              /* Enable interrupts */
    UART_DEB_Start();               /* Start communication component */
    DBG_PRINTF("BLE Wireless Power Transmitter Example Project \r\n");
    
    /* Start BLE component */
    CyBle_Start(AppCallback);    
    
    /* Register service specific callback functions */
    WptsInit();
    
    /* Start general timer */
    WDT_Start();

    for(;;)
    {
        /* Process all the generated events. */
        CyBle_ProcessEvents();

        /* To achieve low power in the device */
        LowPowerImplementation();
        
        /* Get new command */
        if(command == 0u)
        {
            command = UART_DEB_UartGetChar();
        }
        
        /* Handle received from terminal locally generated commands */
        if(((command != 0u) || (customCommand != 0u)) && (requestResponce != false))
        {
            if((command == 0u) && (customCommand != 0u))
            {
                command = customCommand;
                customCommand = 0u;
            }
            switch(command)
            {
                case 'c':                   /* connect  */
                    CyBle_GapcStopScan(); 
                    state = STATE_CONNECTING;
                    break;
                case 'v':
                    apiResult = CyBle_GapcCancelDeviceConnection();
                    DBG_PRINTF("CyBle_GapcCancelDeviceConnection: %x\r\n" , apiResult);
                    break;
                case 'd':                   /* disconnect */
                    apiResult = CyBle_GapDisconnect(cyBle_connHandle.bdHandle); 
                    if(apiResult != CYBLE_ERROR_OK)
                    {
                        DBG_PRINTF("DisconnectDevice API Error: %x \r\n", apiResult);
                    }
                    break;
                case 's':
                    /* Disable sequential read of PRU Dynamic Parameter characteristic before discovery procedure */
                    readingDynChar = 0u;
                    /* And clean pending command */
                    customCommand = 0u;
                    apiResult = CyBle_GattcStartDiscovery(cyBle_connHandle);
                    DBG_PRINTF("StartDiscovery \r\n");
                    if(apiResult != CYBLE_ERROR_OK)
                    {
                        DBG_PRINTF("StartDiscovery API Error: %x \r\n", apiResult);
                    }
                    break;
                case 'z':                   /* select peer device  */
                    DBG_PRINTF("Select Device:\n"); 
                    while((command = UART_DEB_UartGetChar()) == 0);
                    if((command >= '0') && (command <= '9'))
                    {
                        deviceN = (uint8)(command - '0');
                        DBG_PRINTF("%c\n",command); /* print number */
                    }
                    else
                    {
                        DBG_PRINTF(" Wrong digit \r\n");
                        break;
                    }
                    break;
                case '1':                   /* Enable Notification */
                    alertCCCD |= CYBLE_CCCD_NOTIFICATION;
                    apiResult = CyBle_WptscSetCharacteristicDescriptor(cyBle_connHandle, CYBLE_WPTS_PRU_ALERT,  
                        CYBLE_WPTS_CCCD, sizeof(alertCCCD), (uint8 *)&alertCCCD);
                    DBG_PRINTF("Enable Alert Notification, apiResult: %x \r\n", apiResult);
                    if(apiResult == CYBLE_ERROR_OK)
                    {
                        requestResponce = false;
                    }
                    break;
                case '2':                   /* Enable Indication */
                    alertCCCD |= CYBLE_CCCD_INDICATION;
                    apiResult = CyBle_WptscSetCharacteristicDescriptor(cyBle_connHandle, CYBLE_WPTS_PRU_ALERT,  
                        CYBLE_WPTS_CCCD, sizeof(alertCCCD), (uint8 *)&alertCCCD);
                    DBG_PRINTF("Enable Alert Indication, apiResult: %x \r\n", apiResult);
                    if(apiResult == CYBLE_ERROR_OK)
                    {
                        requestResponce = false;
                    }
                    break;
                case '3':                   /* Disable Notification and Indication */
                    alertCCCD = 0u;
                    apiResult = CyBle_WptscSetCharacteristicDescriptor(cyBle_connHandle, CYBLE_WPTS_PRU_ALERT,  
                        CYBLE_WPTS_CCCD, sizeof(alertCCCD), (uint8 *)&alertCCCD);
                    DBG_PRINTF("Disable Alert Notification and Indication, apiResult: %x \r\n", apiResult);
                    if(apiResult == CYBLE_ERROR_OK)
                    {
                        requestResponce = false;
                    }
                    break;
                case '4':                   /* Send Read request for PRU Static Parameter characteristic */
                    apiResult = CyBle_WptscGetCharacteristicValue(cyBle_connHandle, CYBLE_WPTS_PRU_STATIC_PAR);
                    DBG_PRINTF("Get PRU Static Parameter char value, apiResult: %x \r\n", apiResult);
                    if(apiResult == CYBLE_ERROR_OK)
                    {
                        requestResponce = false;
                    }
                    break;
                case '5':                   /* Send Read request for PRU Dynamic Parameter characteristic */
                    apiResult = CyBle_WptscGetCharacteristicValue(cyBle_connHandle, CYBLE_WPTS_PRU_DYNAMIC_PAR);
                    DBG_PRINTF("Get PRU Dynamic Parameter char value, apiResult: %x \r\n", apiResult);
                    if(apiResult == CYBLE_ERROR_OK)
                    {
                        requestResponce = false;
                    }
                    break;
                case '6':                   /* Enable Charging */
                    peedDeviceInfo[deviceN].pruControl.enables = PRU_CONTROL_ENABLES_ENABLE_CHARGE_INDICATOR;
                    apiResult = CyBle_WptscSetCharacteristicValue(cyBle_connHandle, CYBLE_WPTS_PRU_CONTROL,
                        sizeof(peedDeviceInfo[deviceN].pruControl), (uint8 *)&peedDeviceInfo[deviceN].pruControl);
                    DBG_PRINTF("Set PRU Control char (enable charging), apiResult: %x \r\n", apiResult);
                    if(apiResult == CYBLE_ERROR_OK)
                    {
                        requestResponce = false;
                    }
                    break;
                case '7':                   /* Disable Charging */
                    peedDeviceInfo[deviceN].pruControl.enables &= ~PRU_CONTROL_ENABLES_ENABLE_CHARGE_INDICATOR;
                    apiResult = CyBle_WptscSetCharacteristicValue(cyBle_connHandle, CYBLE_WPTS_PRU_CONTROL,
                        sizeof(peedDeviceInfo[deviceN].pruControl), (uint8 *)&peedDeviceInfo[deviceN].pruControl);
                    DBG_PRINTF("Set PRU Control char (disable charging), apiResult: %x \r\n", apiResult);
                    if(apiResult == CYBLE_ERROR_OK)
                    {
                        requestResponce = false;
                    }
                    break;
                case '8':                   /* Enable sequential read of PRU Dynamic Parameter characteristic */
                    readingDynChar = 1u;
                    break;
                case '9':                   /* Disable sequential read of PRU Dynamic Parameter characteristic */
                    readingDynChar = 0u;
                    break;
                case 'h':  /* Help menu */
                    DBG_PRINTF("\r\n");
                    DBG_PRINTF("Available commands:\r\n");
                    DBG_PRINTF(" \'h\' - Help menu.\r\n");
                    DBG_PRINTF(" \'z\' + 'Number' - Select peer device.\r\n");
                    DBG_PRINTF(" \'c\' - Send connect request to peer device.\r\n");
                    DBG_PRINTF(" \'d\' - Send disconnect request to peer device.\r\n");
                    DBG_PRINTF(" \'v\' - Cancel connection request.\r\n");
                    DBG_PRINTF(" \'s\' - Start discovery procedure.\r\n");
                    DBG_PRINTF(" \'1\' - Enable notifications for Alert characteristic.\r\n");
                    DBG_PRINTF(" \'2\' - Enable indications for Alert characteristic.\r\n");
                    DBG_PRINTF(" \'3\' - Disable notifications and indication for Alert characteristic.\r\n");
                    DBG_PRINTF(" \'4\' - Send Read request for PRU Static Parameter characteristic.\r\n");
                    DBG_PRINTF(" \'5\' - Send Read request for PRU Dynamic Parameter characteristic.\r\n");
                    DBG_PRINTF(" \'6\' - Send Enable Charging command to PRU control characteristic.\r\n");
                    DBG_PRINTF(" \'7\' - Send Disable Charging command to PRU control characteristic.\r\n");
                    DBG_PRINTF(" \'8\' - Enable sequential read of PRU Dynamic Parameter characteristic.\r\n");
                    DBG_PRINTF(" \'9\' - Disable sequential read of PRU Dynamic Parameter characteristic.\r\n");
                    break;
            }
            command = 0u;
        }
    }
}

/* [] END OF FILE */
