/*********************************************************************
 This is an example for our nRF52 based Bluefruit LE modules

 Pick one up today in the adafruit shop!

 Adafruit invests time and resources providing this open source code,
 please support Adafruit and open-source hardware by purchasing
 products from Adafruit!

 MIT license, check LICENSE for more information
 All text above, and the splash screen below must be included in
 any redistribution
*********************************************************************/

/*
 * This sketch demonstrate the central API(). A additional bluefruit
 * that has bleuart as peripheral is required for the demo.
 */
#include <bluefruit.h>

BLEClientBas  clientBas;  // battery client
BLEClientDis  clientDis;  // device information client
BLEClientUart clientUart; // bleuart client

void setup()
{
  Serial.begin(115200);
//  while ( !Serial ) delay(10);   // for nrf52840 with native usb

  Serial.println("Bluefruit52 Central BLEUART Example");
  Serial.println("-----------------------------------\n");
  
  // Initialize Bluefruit with maximum connections as Peripheral = 0, Central = 1
  // SRAM usage required by SoftDevice will increase dramatically with number of connections
  Bluefruit.begin(0, 1);
  
  Bluefruit.setName("Bluefruit52 Central");

  // Configure Battyer client
  clientBas.begin();  

  // Configure DIS client
  clientDis.begin();

  // Init BLE Central Uart Serivce
  clientUart.begin();
  clientUart.setRxCallback(bleuart_rx_callback);

  // Increase Blink rate to different from PrPh advertising mode
  Bluefruit.setConnLedInterval(250);

  // Callbacks for Central
  Bluefruit.Central.setConnectCallback(connect_callback);
  Bluefruit.Central.setDisconnectCallback(disconnect_callback);

  /* Start Central Scanning
   * - Enable auto scan if disconnected
   * - Interval = 100 ms, window = 80 ms
   * - Don't use active scan
   * - Start(timeout) with timeout = 0 will scan forever (until connected)
   */
  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.setInterval(160, 80); // in unit of 0.625 ms
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.start(0);                   // // 0 = Don't stop scanning after n seconds
}

/**
 * Callback invoked when scanner pick up an advertising data
 * @param report Structural advertising data
 */
void scan_callback(ble_gap_evt_adv_report_t* report)
{
  // Check if advertising contain BleUart service
  if ( Bluefruit.Scanner.checkReportForService(report, clientUart) )
  {
    Serial.print("BLE UART service detected. Connecting ... ");

    // Connect to device with bleuart service in advertising
    Bluefruit.Central.connect(report);
  }else
  {      
    // For Softdevice v6: after received a report, scanner will be paused
    // We need to call Scanner resume() to continue scanning
    Bluefruit.Scanner.resume();
  }
}

/**
 * Callback invoked when an connection is established
 * @param conn_handle
 */
void connect_callback(uint16_t conn_handle)
{
  Serial.println("Connected");

  Serial.print("Dicovering Device Information ... ");
  if ( clientDis.discover(conn_handle) )
  {
    Serial.println("Found it");
    char buffer[32+1];
    
    // read and print out Manufacturer
    memset(buffer, 0, sizeof(buffer));
    if ( clientDis.getManufacturer(buffer, sizeof(buffer)) )
    {
      Serial.print("Manufacturer: ");
      Serial.println(buffer);
    }

    // read and print out Model Number
    memset(buffer, 0, sizeof(buffer));
    if ( clientDis.getModel(buffer, sizeof(buffer)) )
    {
      Serial.print("Model: ");
      Serial.println(buffer);
    }

    Serial.println();
  }else
  {
    Serial.println("Found NONE");
  }

  Serial.print("Dicovering Battery ... ");
  if ( clientBas.discover(conn_handle) )
  {
    Serial.println("Found it");
    Serial.print("Battery level: ");
    Serial.print(clientBas.read());
    Serial.println("%");
  }else
  {
    Serial.println("Found NONE");
  }

  Serial.print("Discovering BLE Uart Service ... ");
  if ( clientUart.discover(conn_handle) )
  {
    Serial.println("Found it");

    Serial.println("Enable TXD's notify");
    clientUart.enableTXD();

    Serial.println("Ready to receive from peripheral");
  }else
  {
    Serial.println("Found NONE");
    
    // disconnect since we couldn't find bleuart service
    Bluefruit.disconnect(conn_handle);
  }  
}

/**
 * Callback invoked when a connection is dropped
 * @param conn_handle
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  (void) conn_handle;
  (void) reason;
  
  Serial.print("Disconnected, reason = 0x"); Serial.println(reason, HEX);
}

/**
 * Callback invoked when uart received data
 * @param uart_svc Reference object to the service where the data 
 * arrived. In this example it is clientUart
 */
void bleuart_rx_callback(BLEClientUart& uart_svc)
{
  Serial.print("[RX]: ");
  
  while ( uart_svc.available() )
  {
    Serial.print( (char) uart_svc.read() );
  }

  Serial.println();
}

void loop()
{
  if ( Bluefruit.Central.connected() )
  {
    // Not discovered yet
    if ( clientUart.discovered() )
    {
      // Discovered means in working state
      // Get Serial input and send to Peripheral
      if ( Serial.available() )
      {
        delay(2); // delay a bit for all characters to arrive
        
        char str[20+1] = { 0 };
        int intstr = 0;
        char sendstr[4+1] = {0};
        Serial.readBytes(str, 20);
        intstr = atoi(str);
        Serial.println(intstr);
        switch(intstr){
        case 0:
          strcpy(sendstr, "!B10");
          break;
        case 1:
          strcpy(sendstr, "!B11");
          break;
        case 2:
          strcpy(sendstr, "!B12");
          break;
        case 3:
          strcpy(sendstr, "!B13");
          break;
        case 4:
          strcpy(sendstr, "!B14");
          break;
        case 5:
          strcpy(sendstr, "!B15");
          break;
        case 6:
          strcpy(sendstr, "!B16");
          break;
        case 7:
          strcpy(sendstr, "!B17");
          break;
        case 8:
          strcpy(sendstr, "!B18");
          break;
        case 9:
          strcpy(sendstr, "!B19");
          break;
        case 10:
          strcpy(sendstr, "!B1A");
          break;
        case 11:
          strcpy(sendstr, "!B1B");
          break;
        case 12:
          strcpy(sendstr, "!B1C");
          break;
        case 13:
          strcpy(sendstr, "!B1D");
          break;
        case 14:
         strcpy(sendstr, "!B1E");
         break;
        case 15:
          strcpy(sendstr, "!B1F");
          break;
        case 16:
          strcpy(sendstr, "!B20");
          break;
        case 17:
          strcpy(sendstr, "!B21");
          break;
        case 18:
          strcpy(sendstr, "!B22");
        case 19:
          strcpy(sendstr, "!B23");
        case 20:
          strcpy(sendstr, "!B24");
          break;
        case 21:
          strcpy(sendstr, "!B25");
          break;
        case 22:
          strcpy(sendstr, "!B26");
          break;
        case 23:
          strcpy(sendstr, "!B27");
          break;
        case 24:
          strcpy(sendstr, "!B28");
          break;
        case 25:
          strcpy(sendstr, "!B29");
          break;
        case 26:
          strcpy(sendstr, "!B2A");
          break;
        case 27:
          strcpy(sendstr, "!B2B");
          break;
        case 28:
          strcpy(sendstr, "!B2C");
          break;
        case 29:
          strcpy(sendstr, "!B2D");
          break;
        case 30:
          strcpy(sendstr, "!B2E");
          break;
        case 31:
          strcpy(sendstr, "!B2F");
          break;
        }
        clientUart.print( sendstr );
      }
    }
  }
}
