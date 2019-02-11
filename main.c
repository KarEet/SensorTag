/*
 *  ======== main.c ========
 * Author: Eetu Karvonen
 */
/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Clock.h>

/* TI-RTOS Header files */
#include <ti/drivers/I2C.h>
#include <ti/drivers/i2c/I2CCC26XX.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/mw/display/Display.h>
#include <ti/mw/display/DisplayExt.h>

/* Board Header files */
#include "Board.h"
#include "sensors/mpu9250.h"

/* JTKJ Header files */
#include "wireless/comm_lib.h"
#include "stdio.h"
#include <string.h>
#include <inttypes.h>

/* Task Stacks */
#define STACKSIZE 2048
Char labTaskStack[STACKSIZE];
Char commTaskStack[STACKSIZE];

//tilakone muuttujat

uint8_t myState = 1;


/* JTKJ: Display */
Display_Handle hDisplay;

//MPU muuttujat, suoraan esimerkistä
static PIN_Handle hMpuPin;
static PIN_State MpuPinState;
static PIN_Config MpuPinConfig[] = {
    Board_MPU_POWER  | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};

// MPU9250 uses its own I2C interface
static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1
};

/* JTKJ: Pin Button1 configured as power button */
static PIN_Handle hPowerButton;
static PIN_State sPowerButton;
PIN_Config cPowerButton[] = {
    Board_BUTTON1 | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
    PIN_TERMINATE
};
PIN_Config cPowerWake[] = {
    Board_BUTTON1 | PIN_INPUT_EN | PIN_PULLUP | PINCC26XX_WAKEUP_NEGEDGE,
    PIN_TERMINATE
};

/* JTKJ: Pin Button0 configured as input */
static PIN_Handle hButton0;
static PIN_State sButton0;
PIN_Config cButton0[] = {
    Board_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE, // JTKJ: CONFIGURE BUTTON 0 AS INPUT (SEE LECTURE MATERIAL)
    PIN_TERMINATE
};

/* JTKJ: Leds */
static PIN_Handle hLed;
static PIN_State sLed;
PIN_Config cLed[] = {
    Board_LED0 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX, // JTKJ: CONFIGURE LEDS AS OUTPUT (SEE LECTURE MATERIAL)
    PIN_TERMINATE
};

uint8_t tunnistus(float z[20]) {
    
    //1 = walk, 0 = elevator
    uint8_t palautus = 0;
    uint8_t i = 0;
    float suurin = -255;
    float pienin = 255;
    float kynnys = 0.4;
    
    while (i < 20) {
        if (z[i] < pienin) {
            pienin = z[i];
        }
        if (z[i] > suurin) {
            suurin = z[i];
        }
	    i++;
    }
    float erotus = suurin - pienin;
	if (fabs(erotus) > kynnys) {
		palautus = 1;
	}
	return palautus;
}

/* JTKJ: Handle for power button */
Void powerButtonFxn(PIN_Handle handle, PIN_Id pinId) {

    Display_clear(hDisplay);
    Display_close(hDisplay);
    Task_sleep(100000 / Clock_tickPeriod);

	PIN_close(hPowerButton);

    PINCC26XX_setWakeup(cPowerWake);
	Power_shutdown(NULL,0);
}

/* JTKJ: WRITE HERE YOUR HANDLER FOR BUTTON0 PRESS */

Void button0Fxn(PIN_Handle handle, PIN_Id pinId) {
    
    if (myState == 1) {
        PIN_setOutputValue( hLed, Board_LED0, !PIN_getOutputValue( Board_LED0 ) );
        myState = 2;
    }
}

/* JTKJ: Communication Task */
Void commTask(UArg arg0, UArg arg1) {

    char payload[16];
    uint16_t senderAddr;

    // Radio to receive mode
	int32_t result = StartReceive6LoWPAN();
	if(result != true) {
		System_abort("Wireless receive mode failed");
	}

    while (1) {

        // DO __NOT__ PUT YOUR SEND MESSAGE FUNCTION CALL HERE!! 

    	// NOTE THAT COMMUNICATION WHILE LOOP DOES NOT NEED Task_sleep
    	// It has lower priority than main loop (set by course staff)
    	
    	if (GetRXFlag()) {

            memset(payload,0,16);
            Receive6LoWPAN(&senderAddr, payload, 16);
            System_printf(payload);
            System_flush();
            
            Display_print0(hDisplay, 9, 1, "Received:");
            Display_print0(hDisplay, 11, 1, payload);
            
      }
        
    }
}

/* JTKJ: laboratory exercise task */
Void labTask(UArg arg0, UArg arg1) {

    //I2C_Handle      i2c;
    //I2C_Params      i2cParams;
    I2C_Handle i2cMPU; // INTERFACE FOR MPU9250 SENSOR
	I2C_Params i2cMPUParams;

	float ax, ay, az, gx, gy, gz;
	float zaccs[20];

    /* jtkj: Create I2C for usage */
    //I2C_Params_init(&i2cParams);
    //i2cParams.bitRate = I2C_400kHz;
    
    //i2c aukaisu:
    /*i2c = I2C_open(Board_I2C0, &i2cParams);
    if (i2c == NULL) {
        System_abort("Error Initializing I2C\n");
    }*/
    
    // MPU I2C usage
    I2C_Params_init(&i2cMPUParams);
    i2cMPUParams.bitRate = I2C_400kHz;
    i2cMPUParams.custom = (uintptr_t)&i2cMPUCfg;
    
    i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);
    if (i2cMPU == NULL) {
        System_abort("Error Initializing I2CMPU\n");
    }
    
    PIN_setOutputValue(hMpuPin,Board_MPU_POWER, Board_MPU_POWER_ON);

    // WAIT 100MS FOR THE SENSOR TO POWER UP
	Task_sleep(100000 / Clock_tickPeriod);
    System_printf("MPU9250: Power ON\n");
    System_flush();

    // JTKJ: SETUP SENSOR HERE
    System_printf("MPU9250: Setup and calibration...\n");
	System_flush();

	mpu9250_setup(&i2cMPU);

	System_printf("MPU9250: Setup and calibration OK\n");
	System_flush();
	
	I2C_close(i2cMPU);
    

    /* JTKJ: Init Display */
    Display_Params displayParams;
	displayParams.lineClearMode = DISPLAY_CLEAR_BOTH;
    Display_Params_init(&displayParams);

    hDisplay = Display_open(Display_Type_LCD, &displayParams);
    if (hDisplay == NULL) {
        System_abort("Error initializing Display\n");
    }
    
    Display_clear(hDisplay);
    Display_print0(hDisplay, 2, 1, "Hello");
    

    // JTKJ: main loop
    while (1) {

    	switch (myState) {
    	    case 1:
                
                Display_print0(hDisplay, 5, 1, "Press button to");
                Display_print0(hDisplay, 7, 1, "start measuring");
    	        
    	        System_printf("Idle\n");
                System_flush();
    	        break;
    	    case 2:
    	        i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);
                if (i2cMPU == NULL) {
                    System_abort("Error Initializing I2CMPU\n");
                }
                
                int i = 0;
                Display_clear(hDisplay);
                Display_print0(hDisplay, 2, 1, "Measuring...");
                
                System_printf("Measuring...");
            	System_flush();
                while (i<20) {
                    mpu9250_get_data(&i2cMPU, &ax, &ay, &az, &gx, &gy, &gz);
                    zaccs[i] = az;
                    i++;
                    Task_sleep(500000 / Clock_tickPeriod);
                }
                
                System_printf(" ..done\n");
            	System_flush();
            	
            	Display_print0(hDisplay, 4, 1, "Measuring done");
                
                PIN_setOutputValue( hLed, Board_LED0, !PIN_getOutputValue( Board_LED0 ) );
                
                I2C_close(i2cMPU);
                
                Display_print0(hDisplay, 6, 1, "Detecting...");
                
                if (tunnistus(zaccs) == 0) {           //==hissi
                    myState = 3;
                } else if (tunnistus(zaccs) == 1) {    //==kävely
                    myState = 4;
                }

    	        break;
    	    case 3:
    	        System_printf("Elevator\n");
            	System_flush();
            	Display_clear(hDisplay);
            	Display_print0(hDisplay, 2, 1, "Used elevator");
            	myState = 1;
            	break;
            case 4:
                System_printf("Walked\n");
            	System_flush();
            	Display_clear(hDisplay);
            	Display_print0(hDisplay, 2, 1, "Used stairs");
            	Display_print0(hDisplay, 3, 1, "Well done!");
            	
            	// Send message
            	char payload[16];
            	sprintf(payload, "Walking is best");
            	Send6LoWPAN(IEEE80154_SERVER_ADDR, payload, 16);
            	StartReceive6LoWPAN();
            	
            	myState = 1;
            	break;
    	}

    	// JTKJ: Do not remove sleep-call from here!
    	Task_sleep(1000000 / Clock_tickPeriod);
    }
}

Int main(void) {

    // Task variables
	Task_Handle hLabTask;
	Task_Params labTaskParams;
	Task_Handle hCommTask;
	Task_Params commTaskParams;

    // Initialize board
    Board_initGeneral();
    Board_initI2C();
    
    //Init MPU pin
    hMpuPin = PIN_open(&MpuPinState, MpuPinConfig);
    if (hMpuPin == NULL) {
    	System_abort("Pin open failed!");
    }

	/* JTKJ: Power Button */
	hPowerButton = PIN_open(&sPowerButton, cPowerButton);
	if(!hPowerButton) {
		System_abort("Error initializing power button shut pins\n");
	}
	if (PIN_registerIntCb(hPowerButton, &powerButtonFxn) != 0) {
		System_abort("Error registering power button callback function");
	}

    // JTKJ: INITIALIZE BUTTON0 HERE
    
    hButton0 = PIN_open(&sButton0, cButton0);
    if(!hButton0) {
    	System_abort("Error initializing power button shut pins\n");
    }
    if (PIN_registerIntCb(hButton0, &button0Fxn) != 0) {
		System_abort("Error registering power button callback function");
	}

    /* JTKJ: Init Leds */
    hLed = PIN_open(&sLed, cLed);
    if(!hLed) {
        System_abort("Error initializing LED pin\n");
    }

    /* JTKJ: Init Main Task */
    Task_Params_init(&labTaskParams);
    labTaskParams.stackSize = STACKSIZE;
    labTaskParams.stack = &labTaskStack;
    labTaskParams.priority=2;

    hLabTask = Task_create(labTask, &labTaskParams, NULL);
    if (hLabTask == NULL) {
    	System_abort("Task create failed!");
    }

    /* JTKJ: Init Communication Task */
    Init6LoWPAN();

    Task_Params_init(&commTaskParams);
    commTaskParams.stackSize = STACKSIZE;
    commTaskParams.stack = &commTaskStack;
    commTaskParams.priority=1;
    
    hCommTask = Task_create(commTask, &commTaskParams, NULL);
    if (hCommTask == NULL) {
    	System_abort("Task create failed!");
    }

    // JTKJ: Send hello to console
    System_printf("Hello world!\n");
    System_flush();

    /* Start BIOS */
    BIOS_start();

    return (0);
}

