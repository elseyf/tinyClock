/* tinyClock, using ATtiny85 and '595 Shift Register
 * by el.seyf
 * Uses "3" Digit Display LTC-4622
 * (really just 2 digits plus a displayable '1' as first digit)
 * Uses a '595 Shift Register and my own SoftSPI implementation
 * 
 * Method for steady clock used from here:
 * http://www.instructables.com/id/Make-an-accurate-Arduino-clock-using-only-one-wire/
 * 
 * Connection:
 * Pin 0 - DIn '595 & BTN
 * Pin 1 - to INT0 (Pin 2)
 * Pin 2 - to Pin 1
 * Pin 3 - STCP '595
 * Pin 4 - SHCP '595
 * 
 * Segments:
 * --------------------------------------
 * Bit:|| 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 ||
 *     ||-------------------------------||
 *     ||D11|D10|D01|D00|DP | C | B | A ||->Send as byte_A
 *     ||D11|D10|D01|D00| G | F | E | D ||->Send as byte_B
 *      ---------------------------------
 * D00-D11 = Selects which of the 'Segment Parts' is supposed to be lit
 * 
 * Segment Mapping:
 *              8h   4h
 * ->Top=Hour  ---- ----
 *            |    |    |
 *            |   16h   |
 *            |    |    |
 *             ---- ----
 *              2h   1h
 * ->Bottom=Minutes
 *                      o =>DP as Sec Indicator
 *              8m   4m
 *             ---- ----
 *            |    |    |
 *           30m   |   15m
 *            |    |    |
 *             ---- ----
 *              2m   1m
 * 
*/

//Library to turn off unneccessary parts:
#include <avr/power.h>

#define CLK 4  //to CLK '595
#define DOUT 0 //to DIN '595
#define BTN DOUT//used to display time, wired to DOUT Pin
#define LATCH 3//to STCP '595
#define A_CLK 1//This connects to INT0, it supplies a steady clock

//Defines which Byte is sent to the Shift Reg to show a Number;
//First send A, then B:
/*Invalid Bits set!
//Will maybe later implement some other stuff to make the clock more 'COOL'
*uint8_t digits_A[10]={0xE0,0xC0,0x60,0xE0,0xC0,0xA0,0xA0,0xE0,0xE0,0xE0};
*uint8_t digits_B[10]={0xE0,0x00,0x61,0x21,0x81,0xA1,0xE1,0x00,0xE1,0xA1};
*/
uint8_t digits_C[4]={0x10,0x20,0x40,0x80};

uint8_t time_A=0;
uint8_t time_B=0;
//'volatile' for accessing these variables ALWAYS from RAM
volatile uint16_t int_count=0;
volatile uint8_t int_comp=0;
volatile uint8_t seconds=0;
uint8_t minutes=0;
uint8_t hours=0;
//Variables for timeout:
uint8_t last_seconds=0;
uint8_t display_time=0;

void setup() {
  //save some battery by disabling unneccessary parts:
  power_adc_disable();
  power_usi_disable();
  power_timer1_disable();
  
  pinMode(CLK,OUTPUT);
  pinMode(DOUT,OUTPUT);
  pinMode(LATCH,OUTPUT);
  digitalWrite(DOUT,LOW);
  digitalWrite(LATCH,LOW);digitalWrite(LATCH,HIGH);
  digitalWrite(CLK,HIGH);
//Constant Clock on A_CLK:
  analogReference(DEFAULT);
  analogWrite(A_CLK,127);
//Interrupt on INT0:
  attachInterrupt(0,timer_int,RISING);
//Clear Content of Shift Register:
  soft_spi_clear();
//After a Reset, display Time for ~5 seconds:
  display_time=5;
}

void loop() {
//BTN and DOUT share a Pin, so it needs to be always reassigned
  pinMode(BTN,INPUT_PULLUP);
//When pressed, turn on the display for ~5sec:
  if(digitalRead(BTN)==LOW){display_time=6;}
  pinMode(DOUT,OUTPUT);
  if(display_time>0){
    calc_lcd_time_print();
  }
  //save some battery life by turning off display:
  else{soft_spi_clear();}
//Only decrease after 1 second:
  if(last_seconds!=seconds){
    if(display_time>0){display_time--;}
  }
  
  if(seconds==60){seconds=0;minutes++;}
  if(minutes==60){minutes=0;hours++;}
  if(hours==24){hours=0;}
//To keep track of whether a second has passed or not:
  last_seconds=seconds;
}

void calc_lcd_time_print(){
//Calculate which elements to light up:
//Hours:
  time_A=((hours&0x08)>>2)|((hours&0x04));
  time_B=((hours&0x01)<<1)|((hours&0x02)<<1)|((hours&0x10)>>1);//time_B is Bit 0,1,4 of hours
  print_lcd(time_A,time_B,1);
//Minutes+Seconds:
  time_A=(((minutes%15)&0x08)>>2)|(((minutes%15)&0x04));
  time_A|=((minutes/15)&0x02)>>1;//Display 30 Minutes
  time_A|=(seconds&0x01)<<3;//Use DP as Seconds Indicator
  time_B=(((minutes%15)&0x01)<<1)|(((minutes%15)&0x02)<<1);
  time_B|=((minutes/15)&0x01);//Display 15 Minutes
  print_lcd(time_A,time_B,2);
}

void print_lcd(uint8_t byte_A, uint8_t byte_B, uint8_t dig){
  dig*=2;
  uint8_t temp=~(byte_A);
          temp&=0x0F;
          temp|=(digits_C[dig-2]);
  soft_spi(temp);
          temp=~(byte_B);
          temp&=0x0F;
          temp|=(digits_C[dig-1]);
  soft_spi(temp);
}

void soft_spi(uint8_t data){
  uint8_t temp=0;
//'595 loads in on rising Edge:
  for(int i=0;i<8;i++){
    temp=(data>>(7-i))&0x01;
    digitalWrite(DOUT,temp);
    digitalWrite(CLK,LOW);
    digitalWrite(CLK,HIGH);
  }
  digitalWrite(LATCH,LOW);
  digitalWrite(LATCH,HIGH);
}

void soft_spi_clear(){
//Clears Register:
  for(int i=0;i<10;i++){
    digitalWrite(DOUT,LOW);
    digitalWrite(CLK,LOW);
    digitalWrite(CLK,HIGH);
  }
  digitalWrite(LATCH,LOW);
  digitalWrite(LATCH,HIGH);
}

void timer_int(){
//PWM Oscillates at 492-494Hz
//Trying to compensate for varying Clock by making every 4th second a bit shorter:
  int_count++;
  if(int_count>(492-(int_comp/3))){
    seconds++;
    int_comp++;
    if(int_comp==4){int_comp=0;}
    int_count=0;}
}

