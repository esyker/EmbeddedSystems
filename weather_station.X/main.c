/**
  Generated Main Source File

  Company:
    Microchip Technology Inc.

  File Name:
    main.c

  Summary:
    This is the main file generated using PIC10 / PIC12 / PIC16 / PIC18 MCUs

  Description:
    This header file provides implementations for driver APIs for all modules selected in the GUI.
    Generation Information :
        Product Revision  :  PIC10 / PIC12 / PIC16 / PIC18 MCUs - 1.77
        Device            :  PIC16F18875
        Driver Version    :  2.00
*/

/*
    (c) 2018 Microchip Technology Inc. and its subsidiaries. 
    
    Subject to your compliance with these terms, you may use Microchip software and any 
    derivatives exclusively with Microchip products. It is your responsibility to comply with third party 
    license terms applicable to your use of third party software (including open source software) that 
    may accompany Microchip software.
    
    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER 
    EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY 
    IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS 
    FOR A PARTICULAR PURPOSE.
    
    IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND 
    WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP 
    HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO 
    THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL 
    CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT 
    OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS 
    SOFTWARE.
*/

#include "mcc_generated_files/mcc.h"
#include "I2C/i2c.h"

#define EEAddr_MIN 0x7000 //EEPROM start
#define EEAddr_MAX 0x70FF //EEPROM end
//Offsets of memory positions in relation to EEAddr_MIN
#define MAGICAL_WORD_OFFSET 0x0000
#define NREG_OFFSET 0x0001
#define PMON_OFFSET 0x0002
#define TALA_OFFSET 0x0003
#define ALAT_OFFSET 0x0004
#define ALAL_OFFSET 0x0005
#define ALAF_OFFSET 0x0006
#define CLKH_OFFSET 0x0007
#define CLKM_OFFSET 0x0008
#define CHECK_SUM_OFFSET 0x0009

#define RING_BUFFER_INITR 0x000A //memory position for read index of ring buffer
#define RING_BUFFER_INITW 0x000B //memory position for write index of ring buffer
#define RING_BUFFER_NR    0x000C  //memory position for number of valid registers in buffer
#define RING_BUFFER_iread 0x000D  //next to be transfered (not yet tranfered) register
#define RING_BUFFER_memory 0x000E //# of registers not yet transfered
#define RING_BUFFER_OFFSET 0x000F //ring buffer

#define MAGICAL_WORD 0xAA

#define SOM 0xFD /* start of message */
#define EOM 0xFE /* end of message */
#define RCLK 0xC0 /* read clock */
#define SCLK 0XC1 /* set clock */
#define RTL 0XC2 /* read temperature and luminosity */
#define RPAR 0XC3 /* read parameters */
#define MMP 0XC4 /* modify monitoring period */
#define MTA 0XC5 /* modify time alarm */
#define RALA 0XC6 /* read alarms (temperature, luminosity, active/inactive) */
#define DATL_ 0XC7 /* define alarm temperature and luminosity */
#define AALA 0XC8 /* activate/deactivate alarms */
#define IREG 0XC9 /* information about registers (NREG, nr, iread, iwrite)*/
#define TRGC 0XCA /* transfer registers (curr. position)*/
#define TRGI 0XCB /* transfer registers (index) */
#define NMFL 0XCC /* notification memory (half) full */
#define CMD_OK 0 /* command successful */
#define CMD_ERROR 0xFF /* error in command */

#define PTRC 0xD5
#define TRCACK 0xD6


int currentLum = 0;
int currentTemp = 0;

uint8_t message_received[10];
bool receiving_message = false;
int index_message_received = 0;


/*
                         Main application
 */

int NREG = 30; //number of data registers
int PMON = 5; //sec monitoring period
int TALA = 3; //sec duration of alarm signal (PWM)
int ALAT = 25; //oC threshold for temperature alarm
int ALAL = 2; //threshold for luminosity level alarm
int ALAF = 0; //alarm flag ? initially disabled
int CLKH = 0; //initial value for clock hours
int CLKM = 0; //initial value for clock minutes
int CLKS = 0; //initial value for clock seconds

 
//possibles states of the alarm
enum states_alarm
{
    TURNED_OFF, //LED turned off
    HALF_LUMINOSITY, //PWM Modulation
    TURNED_ON //LED turned on
};


enum states_machine //states the machine can have, used for machine state
{
    NORMAL,
    MENU_CLK,
    MODIFICATION_HOURS_TENS,
    MODIFICATION_HOURS_UNITS,
    MODIFICATION_MINUTES_TENS,
    MODIFICATION_MINUTES_UNITS,
    MENU_ALARM,
    MODIFICATION_ALARM_FLAG,
    MENU_TEMPERATURE,
    MODIFICATION_TEMPERATURE_TENS,
    MODIFICATION_TEMPERATURE_UNITS,
    MENU_LUM,
    MODIFICATION_LUM_LEVEL
};
    
//initialize write index with one more unit than read index
int write_index = 1; 
int read_index = 0;
int nr=0;   
int iread=1;  //next to be transfered (not yet tranfered)
int memory =0;  //# of registers not yet transfered
//initialize the alarmcount to -1 so the alarm is turned off
int alarmcount = -1; //countodown for the alarm PWM modulation (seconds)
int alarmstate = TURNED_OFF; 
int machine_state = NORMAL;

typedef unsigned char byte ;

//create an interrupt vector used for creating flags when the interrupts are up
//one interrupt for timer, one for switch S1 and one for switch S2
volatile int interrupt_vector[4] = {0};

//structure used for creating an entry for the buffer to save in the registers
typedef struct buffer_entry
{
    byte hour;
    byte minute;
    byte seconds;
    byte temperature;
    byte lum;
} buffer_entry;

//function used for turning off all LEDs
void turnOffAllLeds()
{
   LED_D2_SetLow();
   LED_D3_SetLow();
   LED_D4_SetLow();
   LED_D5_SetLow();  
   
}

//this function is used for showing the "value" in the LEDs (binary representation)
void showValue(int value){
    
    turnOffAllLeds();
    
    //AND with 0001
    if((value&0x1)==0x1)
        LED_D2_SetHigh();
    //AND with 0010
    if((value&0x2)==0x2)
        LED_D3_SetHigh();
    //AND with 0100
    if((value&0x4)==0x4)
        LED_D4_SetHigh();
    //AND with 1000
    if((value&0x8)==0x8)
        LED_D5_SetHigh();
    
}

//turn on PWM on LED D4
void PWM_Output_D4_Enable (void){
    PPSLOCK = 0x55;
    PPSLOCK = 0xAA;
    PPSLOCKbits.PPSLOCKED = 0x00; // unlock PPS
    // Set D4 as the output of PWM6
    RA6PPS = 0x0E;
    PPSLOCK = 0x55;
    PPSLOCK = 0xAA;
    PPSLOCKbits.PPSLOCKED = 0x01; // lock PPS
}

//turn off PWM on LEDD4
void PWM_Output_D4_Disable (void){
    PPSLOCK = 0x55;
    PPSLOCK = 0xAA;
    PPSLOCKbits.PPSLOCKED = 0x00; // unlock PPS
    // Set D4 as GPIO pin
    RA6PPS = 0x00;
    PPSLOCK = 0x55;
    PPSLOCK = 0xAA;
    PPSLOCKbits.PPSLOCKED = 0x01; // lock PPS
}

//auxiliary funtion to write one buffer entry to memory
void write_register(buffer_entry entry)
{
    //each register is 5 bytes so we have to multiply write_index by 5
    DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_OFFSET+write_index*5,entry.hour); 
    DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_OFFSET+write_index*5+1,entry.minute);
    DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_OFFSET+write_index*5+2,entry.seconds);
    DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_OFFSET+write_index*5+3,entry.temperature);
    DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_OFFSET+write_index*5+4,entry.lum);
    
    //when a new regiter is written increments memory
    if (memory < NREG)
    {
        memory++;
        // when memory half full sends a notification
        if (memory == NREG/2)
        {
            EUSART_Write(SOM);
            EUSART_Write(NMFL);
            EUSART_Write(EOM);
        }
        DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_memory, (byte)memory);
    }
   
    //increment write index so that the program doesn't overwrite previous entry
    write_index++;
    //reset write index if the maximum number of registers is reached
    if(write_index>=NREG)
        write_index=0;
    
    //in case the oldest transfered register is written
    if(memory == NREG)
    {
        iread = write_index;
        DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_iread, (byte)iread);
    }
    //increment the number of valid registers
    if(nr < NREG)
		nr++;
    
    DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_NR, nr);
    
    
    DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_INITW, write_index); //update in EEPROM value of write_index
}

//auxiliary funtion to read one buffer entry from memory
buffer_entry read_register(int position)
{   
    buffer_entry entry;
    entry.hour=DATAEE_ReadByte(EEAddr_MIN+RING_BUFFER_OFFSET+position*5);
    entry.minute=DATAEE_ReadByte(EEAddr_MIN+RING_BUFFER_OFFSET+position*5+1);
    entry.seconds=DATAEE_ReadByte(EEAddr_MIN+RING_BUFFER_OFFSET+position*5+2);
    entry.temperature=DATAEE_ReadByte(EEAddr_MIN+RING_BUFFER_OFFSET+position*5+3);
    entry.lum=DATAEE_ReadByte(EEAddr_MIN+RING_BUFFER_OFFSET+position*5+4);
    return entry;
}

//this function uses auxiliary functions write_register an read_register to read previous entry 
//The new entry is saved if there is a change in the luminosity or temperature of the previous entry
void compare_and_save(buffer_entry new_entry)
{   
    //read previous entry
    buffer_entry previous_entry = read_register(read_index);
    //if there is a change in temperature or luminosity the new entry is saved to memory 
    if(previous_entry.temperature!=new_entry.temperature||previous_entry.lum!=new_entry.lum)
    {
        write_register(new_entry);
        //the read_index is incremented only if there is a "write" operation
        read_index++;
        if(read_index==NREG)
            read_index=0;
        DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_INITR, read_index); //update in EEPROM value of read_index
    }  
}

//function to read "luminosity" 
//in fact, "luminosity" is a voltage and is changed with a potentiometer
adc_result_t readLum ()
{   
    //analogic to digital conversion
    adc_result_t lum = ADCC_GetSingleConversion(channel_ANA0) >> 14;//convert to 4 levels
    //as lum has 16 bits, we shift right 14 bits to get only the two most 
    //significant bits, and therefore 4 levels for luminosity
    return lum;
}

//read value from temperature sensor, using I2C to communicate with the sensor
unsigned char tsttc (void)
{
	unsigned char value;
    do{
        IdleI2C();
        StartI2C(); 
        IdleI2C();

        WriteI2C(0x9a | 0x00); 
        IdleI2C();
        WriteI2C(0x01); 
        IdleI2C();
        RestartI2C(); 
        IdleI2C();
        WriteI2C(0x9a | 0x01); 
        IdleI2C();
        value = ReadI2C(); 
        IdleI2C();
        NotAckI2C(); 
        IdleI2C();
        StopI2C();
    } while (!(value & 0x40));

	IdleI2C();
	StartI2C(); 
    IdleI2C();
	WriteI2C(0x9a | 0x00); 
    IdleI2C();
	WriteI2C(0x00); 
    IdleI2C();
	RestartI2C(); 
    IdleI2C();
	WriteI2C(0x9a | 0x01); 
    IdleI2C();
	value = ReadI2C(); 
    IdleI2C();
	NotAckI2C(); 
    IdleI2C();
	StopI2C();

	return value;
}

//returns the actual temperature as a byte
byte readTemperature ()
{   
    unsigned char c;
	c = tsttc();       		
    return (byte)c;
}

//function used to calculate check sum using only the relevant fields saved to memory
byte calculate_check_sum()
{   
    //the checksum is calculated by summing the content of all relevant memory
    //positions and extracting the least significant byte
    return (byte)
    (DATAEE_ReadByte(EEAddr_MIN+MAGICAL_WORD_OFFSET) +
    DATAEE_ReadByte(EEAddr_MIN+NREG_OFFSET)+
    DATAEE_ReadByte(EEAddr_MIN+PMON_OFFSET)+
    DATAEE_ReadByte(EEAddr_MIN+TALA_OFFSET)+
    DATAEE_ReadByte(EEAddr_MIN+ALAT_OFFSET)+
    DATAEE_ReadByte(EEAddr_MIN+ALAL_OFFSET)+
    DATAEE_ReadByte(EEAddr_MIN+ALAF_OFFSET)+
    DATAEE_ReadByte(EEAddr_MIN+CLKH_OFFSET)+
    DATAEE_ReadByte(EEAddr_MIN+CLKM_OFFSET));
    
}

//function that sets timer interrupt flag to 1, whenever timer generates an interrupt
void TimerInterruptHandler(void)
{   
    //toogle the leds according to the menu, so that the user gets to know wich field he is going to change when pressed switch 2
    switch (machine_state)
    {
        case NORMAL:
            LED_D5_Toggle();
            break;
        case MENU_CLK:
            LED_D5_Toggle();
            break;
        case MENU_ALARM:
            LED_D4_Toggle();
            break;
        case MENU_TEMPERATURE:
            LED_D3_Toggle();
            break;
        case MENU_LUM:
            LED_D2_Toggle();
            break;
         
    }
    
    interrupt_vector[0] = 1;
}

//this function is used to update the alarm LED according to the states, whenever necessary
void updateLEDAlarm()
{
    if(alarmstate==TURNED_ON)
        LED_D4_SetHigh();
    if(alarmstate==TURNED_OFF)
        LED_D4_SetLow();
}


//this function is called whenever timer interrupt flag is 1, this is, every time a timer has generated an interrupt
void processTime()
{   
    //if we aren't changing the time in the states of the machine MODIFICATION_HOURS_TENS ,MODIFICATION_HOURS_UNITS,
    //MODIFICATION_MINUTES_TENS or MODIFICATION_MINUTES_UNITS time is incremented
    if(machine_state != MODIFICATION_HOURS_TENS && machine_state != MODIFICATION_HOURS_UNITS && 
            machine_state != MODIFICATION_MINUTES_TENS && machine_state != MODIFICATION_MINUTES_UNITS)
    {
        CLKS++;
        if(CLKS == 60) //a minute has passed
        {
            CLKS = 0;   
            CLKM++;
            if(CLKM == 60) //an hour has passed
            {
                CLKH++;
                CLKM = 0;
                if(CLKH == 24) //24 hours have passed
                    CLKH = 0;

                DATAEE_WriteByte(EEAddr_MIN+CLKH_OFFSET,(byte) CLKH);//update hours value in memory
            }
            DATAEE_WriteByte(EEAddr_MIN+CLKM_OFFSET, (byte) CLKM); //update minutes value in memory
            DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, calculate_check_sum()); //update check sum

        }
    }
    
    if(machine_state == NORMAL)
    {
        //decrement alarmcount to when then count reaches 0 turn off the PWM
        if (alarmstate == HALF_LUMINOSITY)
        {
            alarmcount--;
        }
        //every PMON seconds read new values from the luminosity and temperature sensors
        if(PMON!=0 && CLKS%PMON==0) 
        {   
            buffer_entry new_entry;
            new_entry.lum = readLum();
            currentLum = new_entry.lum;
            switch (new_entry.lum) //show in LEDs D2 and D3 the luminosity level (binary representation)
            {   
                case 0:
                    LED_D2_SetLow();
                    LED_D3_SetLow();
                    break;
                case 1:
                    LED_D2_SetHigh();
                    LED_D3_SetLow();
                    break;
                case 2:
                    LED_D2_SetLow();
                    LED_D3_SetHigh();
                    break;
                case 3:
                    LED_D2_SetHigh();
                    LED_D3_SetHigh();
                    break;        
            }

            new_entry.temperature=readTemperature();
            currentTemp = new_entry.temperature;
            new_entry.seconds=CLKS;
            new_entry.hour=CLKH;
            new_entry.minute=CLKM;
            //generate PWM when the thresholds are surpassed
            if((new_entry.temperature > ALAT || new_entry.lum > ALAL) && alarmstate==TURNED_OFF)
            {
                LED_D4_SetLow();
                //update state of the alarm
                alarmcount = TALA;
                alarmstate = HALF_LUMINOSITY;
                ALAF = 1; 
                //enable PWM
                PWM_Output_D4_Enable();
                DATAEE_WriteByte(EEAddr_MIN+ALAF_OFFSET,(byte) ALAF); //update state in memory
                DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, calculate_check_sum());//update checksum
            }
            compare_and_save(new_entry); //save nem entry in the ring buffer if it is different from the previous one
        }
        //disable alarm when alarm count reaches 0
        if(alarmcount == 0 && alarmstate == HALF_LUMINOSITY)
        {
            PWM_Output_D4_Disable();
            LED_D4_SetHigh();
            //update alarm state
            alarmstate = TURNED_ON;
            ALAF = 1;
            DATAEE_WriteByte(EEAddr_MIN+ALAF_OFFSET,(byte) ALAF);
            DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, calculate_check_sum());
        }
        updateLEDAlarm(); //update LEDs according to alarm_state (used for when device restarts)
    }
    
}



//function that sets S1 interrupt flag to 1, whenever switch 1 generates an interrupt
void Switch1InterruptHandler (void)
{  
    interrupt_vector[1] = 1;
}

//function that sets S2 interrupt flag to 1, whenever switch 2 generates an interrupt
void Switch2InterruptHandler (void)
{  
    interrupt_vector[2] = 1; 
}



//this function is called whenever S1 interrupt flag is 1, this is, every time S1 has generated an interrupt
void processS1()
{   
    
    //implement states machine
     switch (machine_state)
    {
        case NORMAL:
            //is alarm is ON or in PWM mode, S1 only deactivates the alarm
            if(alarmstate==TURNED_ON)
            {
                //update alarm state
                ALAF = 0;
                alarmstate = TURNED_OFF;
                LED_D4_SetLow();
                
                //update in memory
                DATAEE_WriteByte(EEAddr_MIN+ALAF_OFFSET,(byte) ALAF);
                DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, calculate_check_sum());
                return;
            }

            else if(alarmstate == HALF_LUMINOSITY)
            {
                //update alarm state
                PWM_Output_D4_Disable();
                alarmstate=TURNED_OFF;
                ALAF = 0;
                LED_D4_SetLow();
                //update in memory
                DATAEE_WriteByte(EEAddr_MIN+ALAF_OFFSET,(byte) ALAF);
                DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, calculate_check_sum());
                return;
            }
            machine_state=MENU_CLK;
            turnOffAllLeds();
            break;
        case MENU_CLK:
            machine_state=MENU_ALARM;
            turnOffAllLeds();
            break;
        case MODIFICATION_HOURS_TENS:
            machine_state=MODIFICATION_HOURS_UNITS;
            showValue(CLKH%10); //show hour units in leds
            break;
        case MODIFICATION_HOURS_UNITS:
            machine_state=MODIFICATION_MINUTES_TENS;
            showValue(CLKM/10); //show minute tens
            break;
        case MODIFICATION_MINUTES_TENS:
            machine_state=MODIFICATION_MINUTES_UNITS;
            showValue(CLKM%10); //show minute units
            break;
        case MODIFICATION_MINUTES_UNITS:
            machine_state=MENU_ALARM;
            //commit changes in hours and minutes to memory 
            DATAEE_WriteByte(EEAddr_MIN+CLKH_OFFSET,(byte) CLKH);
            DATAEE_WriteByte(EEAddr_MIN+CLKM_OFFSET,(byte) CLKM);
            DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, calculate_check_sum());
            turnOffAllLeds();
            break;
        case MENU_ALARM:
            machine_state=MENU_TEMPERATURE;
            turnOffAllLeds();
            break;
        case MODIFICATION_ALARM_FLAG:
            machine_state=MENU_TEMPERATURE;
            //commit change in alarm state to memory
            DATAEE_WriteByte(EEAddr_MIN+ALAF_OFFSET,(byte) ALAF);
            DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, calculate_check_sum());
            turnOffAllLeds();
            break;
        case MENU_TEMPERATURE:
            machine_state=MENU_LUM;
            turnOffAllLeds();
            break;
        case MODIFICATION_TEMPERATURE_TENS:
            machine_state=MODIFICATION_TEMPERATURE_UNITS;
            showValue(ALAT%10); //show temperature threshold units in LEDs
            break;
        case MODIFICATION_TEMPERATURE_UNITS:
            machine_state=MENU_LUM;
            //commit changes in temperature threshold to memory
            DATAEE_WriteByte(EEAddr_MIN+ALAT_OFFSET,(byte) ALAT);
            DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, calculate_check_sum());
            turnOffAllLeds();
            break;
        case MENU_LUM:
            machine_state=NORMAL;
            turnOffAllLeds();
            break;
        case MODIFICATION_LUM_LEVEL:
            machine_state=NORMAL;
            //commit changes in luminosity threshold to memory
            DATAEE_WriteByte(EEAddr_MIN+ALAL_OFFSET,(byte) ALAL);
            DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, calculate_check_sum());
            turnOffAllLeds();
            break;
    }
}

//this function is called whenever S1 interrupt flag is 1, this is, every time S1 has generated an interrupt
void processS2()
{   
    //implement states machine
    switch (machine_state)
    {

        case MENU_CLK:
            machine_state=MODIFICATION_HOURS_TENS;
            showValue(CLKH/10); //show hours tens in LEDs
            break;
        case MODIFICATION_HOURS_TENS:
            CLKH+=10; //increment hours tens
            if(CLKH>=24)
                CLKH=CLKH%10; //if hour is bigger than 24, keep the value of units and value of tens becomes 0
            showValue(CLKH/10); //show hours tens
            break;
        case MODIFICATION_HOURS_UNITS:
            CLKH++; //increment hours units
            if(CLKH%10 == 0)
                CLKH -= 10; //guaratee to keep value of tens ex: 19->20 can't be, has to be 19->10
            if(CLKH>=24)
                CLKH = 20; //can't be bigger than 23
            showValue(CLKH%10); //show hours units
            break;
        case MODIFICATION_MINUTES_TENS:
            CLKM+=10; //increment minutes tens
            if(CLKM>=60)
                CLKM=CLKM%10; //minutes can't be over 59
            showValue(CLKM/10); //show minutes tens
            break;
        case MODIFICATION_MINUTES_UNITS:
            CLKM++; //increment minutes units
            if(CLKM%10 == 0) 
                CLKM -= 10; //guaratee to keep value of tens ex: 19->20 can't be, has to be 19->10
            showValue(CLKM%10); //show minutes units
            break;
        case MENU_ALARM:
            machine_state=MODIFICATION_ALARM_FLAG;
            turnOffAllLeds(); 
            break;
        case MODIFICATION_ALARM_FLAG:
            turnOffAllLeds();
            //switch state of alarm
            if(alarmstate == TURNED_OFF)
            {
                alarmstate = TURNED_ON;
                ALAF = 1;
                LED_D2_SetHigh();
            }
            else if(alarmstate == TURNED_ON)
            {
                alarmstate = TURNED_OFF;
                ALAF = 0;
                LED_D2_SetLow();
            }
                
            break;
        case MENU_TEMPERATURE:
            machine_state=MODIFICATION_TEMPERATURE_TENS;
            showValue(ALAT/10);
            break;
        case MODIFICATION_TEMPERATURE_TENS:
            ALAT+=10;
            if(ALAT>50)
                ALAT=ALAT%10;
            showValue(ALAT/10);
            break;
        case MODIFICATION_TEMPERATURE_UNITS:
            ALAT++;
            if(ALAT%10 == 0)
                ALAT -= 10;
            showValue(ALAT%10);
            break;
        case MENU_LUM:
            machine_state=MODIFICATION_LUM_LEVEL;
            showValue(ALAL);
            break;
        case MODIFICATION_LUM_LEVEL:
            ALAL++; //increment luminosity threshold
            ALAL=ALAL%4; //threshold is between 0 and 3
            showValue(ALAL);
            break;
    }
}

//this function is used only when he program starts to read previous values of 
//the parameters (ALAL,ALAF, etc) and time from memory
void MemoryInitialize()
{
    byte magicalWord = DATAEE_ReadByte(EEAddr_MIN+MAGICAL_WORD_OFFSET);
    
    byte checkSum=calculate_check_sum();
    
    byte checkSum_mem =(byte)DATAEE_ReadByte(EEAddr_MIN+CHECK_SUM_OFFSET);
    
    //if there is an error in memory
    if (magicalWord != MAGICAL_WORD || checkSum_mem != checkSum)
    {   
        //write default values to memory
        DATAEE_WriteByte(EEAddr_MIN+MAGICAL_WORD_OFFSET, MAGICAL_WORD);
        DATAEE_WriteByte(EEAddr_MIN+NREG_OFFSET, (byte)NREG);
        DATAEE_WriteByte(EEAddr_MIN+PMON_OFFSET, (byte)PMON);
        DATAEE_WriteByte(EEAddr_MIN+TALA_OFFSET, (byte)TALA);
        DATAEE_WriteByte(EEAddr_MIN+ALAT_OFFSET, (byte)ALAT);
        DATAEE_WriteByte(EEAddr_MIN+ALAL_OFFSET, (byte)ALAL);
        DATAEE_WriteByte(EEAddr_MIN+ALAF_OFFSET,(byte) ALAF);
        DATAEE_WriteByte(EEAddr_MIN+CLKH_OFFSET,(byte) CLKH);
        DATAEE_WriteByte(EEAddr_MIN+CLKM_OFFSET, (byte)CLKM);
        DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, (byte)checkSum);
        DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_INITW,(byte) write_index);
        DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_INITR,(byte) read_index);
        DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_NR, (byte)nr);
        DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_iread, (byte)iread);
        DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_memory, (byte)memory);
    }
    else //if there is no error in memory, the program reads the previous values from memory
    {
        NREG=DATAEE_ReadByte(EEAddr_MIN+NREG_OFFSET);
        PMON=DATAEE_ReadByte(EEAddr_MIN+PMON_OFFSET);
        TALA=DATAEE_ReadByte(EEAddr_MIN+TALA_OFFSET);
        ALAT=DATAEE_ReadByte(EEAddr_MIN+ALAT_OFFSET);
        ALAL=DATAEE_ReadByte(EEAddr_MIN+ALAL_OFFSET);
        ALAF=DATAEE_ReadByte(EEAddr_MIN+ALAF_OFFSET);
        CLKH=DATAEE_ReadByte(EEAddr_MIN+CLKH_OFFSET);
        CLKM=DATAEE_ReadByte(EEAddr_MIN+CLKM_OFFSET);
        read_index=DATAEE_ReadByte(EEAddr_MIN+ RING_BUFFER_INITR);
        write_index=DATAEE_ReadByte(EEAddr_MIN+ RING_BUFFER_INITW);
        nr=DATAEE_ReadByte(EEAddr_MIN+ RING_BUFFER_NR);
        iread=DATAEE_ReadByte(EEAddr_MIN+ RING_BUFFER_iread);
        memory=DATAEE_ReadByte(EEAddr_MIN+ RING_BUFFER_memory);
        if(ALAF==1)
        {
            alarmstate=TURNED_ON; //restore alarm_state as well
        }
    }
    
}
//function that proccess the content of messages received
void process_message(int size_message)
{
    buffer_entry registo;
    int i=0;
    int aux_read_index=0;
    //instruction in message_received[0]
    switch(message_received[0])
    {
        case RCLK:
            if (size_message!=1)
            {
                EUSART_Write(SOM);
                EUSART_Write(RCLK);
                EUSART_Write(CMD_ERROR);
                EUSART_Write(EOM);
            }
            else
            {
                EUSART_Write(SOM);
                EUSART_Write(RCLK);
                EUSART_Write((byte)CLKH);
                EUSART_Write((byte)CLKM);
                EUSART_Write((byte)CLKS);
                EUSART_Write(EOM);
            }
            
            break;
        case SCLK:
            if (size_message!=4 || message_received[1] > 23 || message_received[1] < 0 ||
                message_received[2] > 59 || message_received[2] < 0 || message_received[3] > 59 || message_received[3] < 0)
            {
                EUSART_Write(SOM);
                EUSART_Write(SCLK);
                EUSART_Write(CMD_ERROR);
                EUSART_Write(EOM);
            }
            else
            {
                CLKH = message_received[1];
                DATAEE_WriteByte(EEAddr_MIN+CLKH_OFFSET,(byte) CLKH);
                CLKM = message_received[2];
                DATAEE_WriteByte(EEAddr_MIN+CLKM_OFFSET, (byte)CLKM);
                DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, calculate_check_sum());
                CLKS = message_received[3];
                EUSART_Write(SOM);
                EUSART_Write(SCLK);
                EUSART_Write(CMD_OK);
                EUSART_Write(EOM);
            }
            break;
        case RTL:
            if (size_message!=1)
            {
                EUSART_Write(SOM);
                EUSART_Write(RTL);
                EUSART_Write(CMD_ERROR);
                EUSART_Write(EOM);
            }
            else
            {
                EUSART_Write(SOM);
                EUSART_Write(RTL);
                EUSART_Write((byte)currentTemp);
                EUSART_Write((byte)currentLum);
                EUSART_Write(EOM);
            }

            break;
        case RPAR:
            if (size_message!=1)
            {
                EUSART_Write(SOM);
                EUSART_Write(RPAR);
                EUSART_Write(CMD_ERROR);
                EUSART_Write(EOM);
            }
            else
            {
                EUSART_Write(SOM);
                EUSART_Write(RPAR);
                EUSART_Write((byte)PMON);
                EUSART_Write((byte)TALA);
                EUSART_Write(EOM);
            }
            break;
            
        case MMP:
            if (size_message!=2 || message_received[1] > 99 || message_received[1] < 0)
            {
                EUSART_Write(SOM);
                EUSART_Write(MMP);
                EUSART_Write(CMD_ERROR);
                EUSART_Write(EOM);
            }
            else
            {
                PMON = message_received[1];
                DATAEE_WriteByte(EEAddr_MIN+PMON_OFFSET, (byte)PMON);
                DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, calculate_check_sum());
                EUSART_Write(SOM);
                EUSART_Write(MMP);
                EUSART_Write(CMD_OK);
                EUSART_Write(EOM);
            }
            break;
        case MTA:
            if (size_message!=2 || message_received[1] > 60 || message_received[1] < 0)
            {
                EUSART_Write(SOM);
                EUSART_Write(MTA);
                EUSART_Write(CMD_ERROR);
                EUSART_Write(EOM);
            }
            else 
            {
                TALA = message_received[1];
                DATAEE_WriteByte(EEAddr_MIN+TALA_OFFSET, (byte)TALA);
                DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, calculate_check_sum());
                EUSART_Write(SOM);
                EUSART_Write(MTA);
                EUSART_Write(CMD_OK);
                EUSART_Write(EOM);
            }
            break;
        case RALA:
            if (size_message!=1)
            {
                EUSART_Write(SOM);
                EUSART_Write(RALA);
                EUSART_Write(CMD_ERROR);
                EUSART_Write(EOM);
            }
            else
            {
                EUSART_Write(SOM);
                EUSART_Write(RALA);
                EUSART_Write((byte)ALAT);
                EUSART_Write((byte)ALAL);
                EUSART_Write((byte)ALAF);
                EUSART_Write(EOM); 
            }

            break;
        case DATL_:
            if (size_message!=3 || message_received[1] > 50 || message_received[1] < 0 ||
                 message_received[2] > 3 || message_received[2] < 0)
            {
                EUSART_Write(SOM);
                EUSART_Write(DATL_);
                EUSART_Write(CMD_ERROR);
                EUSART_Write(EOM);
            }
            else
            {
                ALAT = message_received[1];
                ALAL = message_received[2];
                EUSART_Write(SOM);
                EUSART_Write(DATL_);
                EUSART_Write(CMD_OK);
                EUSART_Write(EOM);
                DATAEE_WriteByte(EEAddr_MIN+ALAT_OFFSET, (byte)ALAT);
                DATAEE_WriteByte(EEAddr_MIN+ALAL_OFFSET, (byte)ALAL);
                DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, calculate_check_sum()); 
            }

            break;
        case AALA:
            if (size_message!=2 || message_received[1] > 1 || message_received[1] < 0)
            {
                EUSART_Write(SOM);
                EUSART_Write(AALA);
                EUSART_Write(CMD_ERROR);
                EUSART_Write(EOM);
            }
            else
            {
                ALAF = message_received[1];
                if(ALAF==1)
                {
                    alarmstate=TURNED_ON;
                    
                }
                else if(ALAF == 0)
                {
                    alarmstate=TURNED_OFF;
                }
                DATAEE_WriteByte(EEAddr_MIN+ALAF_OFFSET, (byte)ALAF);
                DATAEE_WriteByte(EEAddr_MIN+CHECK_SUM_OFFSET, calculate_check_sum());
                EUSART_Write(SOM);
                EUSART_Write(AALA);
                EUSART_Write(CMD_OK);
                EUSART_Write(EOM);
            }
            break; 
            
        case IREG:
            if (size_message!=1)
            {
                EUSART_Write(SOM);
                EUSART_Write(IREG);
                EUSART_Write(CMD_ERROR);
                EUSART_Write(EOM);
            }
            else
            {
                EUSART_Write(SOM);
                EUSART_Write(IREG);
                EUSART_Write((byte)NREG);
                EUSART_Write((byte)nr);
                EUSART_Write((byte)iread);
                EUSART_Write((byte)write_index);
                EUSART_Write(EOM);
            }
            break;
        case TRGC:
            if (size_message!=2 || message_received[1] < 0 || message_received[1] > NREG)
            {
                EUSART_Write(SOM);
                EUSART_Write(TRGC);
                EUSART_Write(CMD_ERROR);
                EUSART_Write(EOM);
            }
            else
            {
                EUSART_Write(SOM);
                EUSART_Write(TRGC);
                
                //if there is registers to be transfered
                if(memory != 0)
                {
                    for (i = 0; i < message_received[1]; i++)
                    {   
                        registo = read_register(iread);
                        EUSART_Write(registo.hour);
                        EUSART_Write(registo.minute);
                        EUSART_Write(registo.seconds);
                        EUSART_Write(registo.temperature);
                        EUSART_Write(registo.lum);
                        //# of registers to be sent decrements
                        memory--;
                        
                        //register yet to be sent increments
                        iread++;
                        if(iread >= NREG)
                          iread = 0;
                        
                        if(memory==0)
                        {
                            break;
                        }
                    }
                }
                EUSART_Write(EOM); 
                DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_memory, (byte)memory);
                DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_iread, (byte)iread);
                
            }
            break;
            //when there is a periodic trransfer from the processing task to be able to 
            //distinguish periodic transfers from non periodic transfers
         case PTRC:
             if (size_message!=1)
            {
                EUSART_Write(SOM);
                EUSART_Write(TRCACK);
                EUSART_Write(CMD_ERROR);
                EUSART_Write(EOM);
            }
            else
            {
                EUSART_Write(SOM);
                EUSART_Write(TRCACK);
                
                if(memory != 0)
                {
                    for (i = 0; i < NREG; i++)
                    {   
                        registo = read_register(iread);
                        EUSART_Write(registo.hour);
                        EUSART_Write(registo.minute);
                        EUSART_Write(registo.seconds);
                        EUSART_Write(registo.temperature);
                        EUSART_Write(registo.lum);
                        memory--;
                        
                        iread++;
                        if(iread >= NREG)
                          iread = 0;
                        
                        if(memory==0)
                        {
                            break;
                        }
                    }
                }
                EUSART_Write(EOM); 
                DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_memory, (byte)memory);
                DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_iread, (byte)iread);
            }
            break;
         case TRGI:
            if (size_message!=3 || message_received[1] < 0 || message_received[1] > NREG ||
                message_received[2] < 0 || message_received[2] > (NREG-1) )
            {
                EUSART_Write(SOM);
                EUSART_Write(TRGI);
                EUSART_Write(CMD_ERROR);
                EUSART_Write(EOM);
            }
            else
            {
                EUSART_Write(SOM);
                EUSART_Write(TRGI);
                //oldest register in ring buffer
                int oldest = (write_index - nr);
                //adjust index to buffer size
                while(oldest < 0)
                    oldest = oldest + NREG;
                
                //index of the last register to transfer
                aux_read_index = (oldest + message_received[2]);
                //adjust index to buffer size
                while(aux_read_index >= NREG)
                    aux_read_index = aux_read_index - NREG;
                
                //if is registers in buffer
                if(nr != 0)
                {
                    for (i = 0; i< message_received[1]; i++)
                    {   
                        registo = read_register(aux_read_index);
                        EUSART_Write(aux_read_index);
                        EUSART_Write(registo.minute);
                        EUSART_Write(registo.seconds);
                        EUSART_Write(registo.temperature);
                        EUSART_Write(registo.lum);
                        
                        if (aux_read_index == iread)
                        {
                            iread=(iread+1)%NREG;
                            memory--;
                        }
                        
                        aux_read_index=(aux_read_index+1)%NREG;;

                        if(aux_read_index==write_index)
                        {
                            break;
                        }
                    }
                }
                EUSART_Write(EOM); 
                DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_memory, (byte)memory);
                DATAEE_WriteByte(EEAddr_MIN+RING_BUFFER_iread, (byte)iread);
            }
            break;   
    }
}

void main(void)
{   
    // initialize the device
    SYSTEM_Initialize();
    ADCC_DisableContinuousConversion();
    // When using interrupts, you need to set the Global and Peripheral Interrupt Enable bits
    // Use the following macros to:

    // Enable the Global Interrupts
    INTERRUPT_GlobalInterruptEnable();

    // Enable the Peripheral Interrupts
    INTERRUPT_PeripheralInterruptEnable();
    
    //restore previous values from memory
    MemoryInitialize();
    
    //initialize I2C
    i2c1_driver_open();
    I2C_SCL = 1;
    I2C_SDA = 1;
    WPUC3 = 1;
    WPUC4 = 1;
    
    //restart LEDs
    PWM_Output_D4_Disable();
    turnOffAllLeds();
    
    
    //assign the interrupt handlers to their interrupts 
    TMR1_SetInterruptHandler(TimerInterruptHandler);
    IOCBF4_SetInterruptHandler(Switch1InterruptHandler);
    IOCCF5_SetInterruptHandler(Switch2InterruptHandler);
    //EUSART_SetRxInterruptHandler(RxInterruptHandler);
    //EUSART_SetTxInterruptHandler(TxInterruptHandler);
    
    
  
    while (1) { 
        if(interrupt_vector[0] == 1)
        {
            interrupt_vector[0] = 0;
            processTime();
        }
        
        if(interrupt_vector[1] == 1)
        {
            interrupt_vector[1] = 0;
            processS1();
        }
        if(interrupt_vector[2] == 1)
        {
           interrupt_vector[2] = 0; 
           processS2();
        }
        if(interrupt_vector[3] == 1)
        {
           interrupt_vector[3] = 0; 
        }
        if(EUSART_is_rx_ready())    //if there is a message ready
        {
            uint8_t bit_received = EUSART_Read(); //read a byte
            if(bit_received == SOM) //if is the start of the message
            {
                receiving_message = true;
                index_message_received = 0; //reset of index buffer 
            }
            else if(bit_received == EOM)  //if is the end of the message
            {
                receiving_message = false;
                process_message(index_message_received);   // processes the message
                index_message_received = 0;
                
            }
            else if(receiving_message)  
            {
                //saves the byte in a buffer
                message_received[index_message_received] = bit_received;    
                index_message_received++;
            }
        }
        //when there is no interrupt generated and when the alarm PWM isn't being used, the processor is put to sleep
        //if(interrupt_vector[0] == 0 && interrupt_vector[1] == 0 && interrupt_vector[2] == 0 && alarmstate!=HALF_LUMINOSITY)
          //  SLEEP();
    }
}
/**
 End of File
*/