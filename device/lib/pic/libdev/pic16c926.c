/* Register definitions for pic16c926.
 * This file was automatically generated by:
 *   inc2h.pl V4514
 *   Copyright (c) 2002, Kevin L. Pauba, All Rights Reserved
 */
#include <pic16c926.h>

__data __at (INDF_ADDR) volatile char      INDF;
__sfr  __at (TMR0_ADDR)                    TMR0;
__data __at (PCL_ADDR) volatile char       PCL;
__sfr  __at (STATUS_ADDR)                  STATUS;
__sfr  __at (FSR_ADDR)                     FSR;
__sfr  __at (PORTA_ADDR)                   PORTA;
__sfr  __at (PORTB_ADDR)                   PORTB;
__sfr  __at (PORTC_ADDR)                   PORTC;
__sfr  __at (PORTD_ADDR)                   PORTD;
__sfr  __at (PORTE_ADDR)                   PORTE;
__sfr  __at (PCLATH_ADDR)                  PCLATH;
__sfr  __at (INTCON_ADDR)                  INTCON;
__sfr  __at (PIR1_ADDR)                    PIR1;
__sfr  __at (TMR1L_ADDR)                   TMR1L;
__sfr  __at (TMR1H_ADDR)                   TMR1H;
__sfr  __at (T1CON_ADDR)                   T1CON;
__sfr  __at (TMR2_ADDR)                    TMR2;
__sfr  __at (T2CON_ADDR)                   T2CON;
__sfr  __at (SSPBUF_ADDR)                  SSPBUF;
__sfr  __at (SSPCON_ADDR)                  SSPCON;
__sfr  __at (CCPR1L_ADDR)                  CCPR1L;
__sfr  __at (CCPR1H_ADDR)                  CCPR1H;
__sfr  __at (CCP1CON_ADDR)                 CCP1CON;
__sfr  __at (ADRESH_ADDR)                  ADRESH;
__sfr  __at (ADCON0_ADDR)                  ADCON0;
__sfr  __at (OPTION_REG_ADDR)              OPTION_REG;
__sfr  __at (TRISA_ADDR)                   TRISA;
__sfr  __at (TRISB_ADDR)                   TRISB;
__sfr  __at (TRISC_ADDR)                   TRISC;
__sfr  __at (TRISD_ADDR)                   TRISD;
__sfr  __at (TRISE_ADDR)                   TRISE;
__sfr  __at (PIE1_ADDR)                    PIE1;
__sfr  __at (PCON_ADDR)                    PCON;
__sfr  __at (PR2_ADDR)                     PR2;
__sfr  __at (SSPADD_ADDR)                  SSPADD;
__sfr  __at (SSPSTAT_ADDR)                 SSPSTAT;
__sfr  __at (ADRESL_ADDR)                  ADRESL;
__sfr  __at (ADCON1_ADDR)                  ADCON1;
__sfr  __at (PORTF_ADDR)                   PORTF;
__sfr  __at (PORTG_ADDR)                   PORTG;
__sfr  __at (PMCON1_ADDR)                  PMCON1;
__sfr  __at (LCDSE_ADDR)                   LCDSE;
__sfr  __at (LCDPS_ADDR)                   LCDPS;
__sfr  __at (LCDCON_ADDR)                  LCDCON;
__sfr  __at (LCDD00_ADDR)                  LCDD00;
__sfr  __at (LCDD01_ADDR)                  LCDD01;
__sfr  __at (LCDD02_ADDR)                  LCDD02;
__sfr  __at (LCDD03_ADDR)                  LCDD03;
__sfr  __at (LCDD04_ADDR)                  LCDD04;
__sfr  __at (LCDD05_ADDR)                  LCDD05;
__sfr  __at (LCDD06_ADDR)                  LCDD06;
__sfr  __at (LCDD07_ADDR)                  LCDD07;
__sfr  __at (LCDD08_ADDR)                  LCDD08;
__sfr  __at (LCDD09_ADDR)                  LCDD09;
__sfr  __at (LCDD10_ADDR)                  LCDD10;
__sfr  __at (LCDD11_ADDR)                  LCDD11;
__sfr  __at (LCDD12_ADDR)                  LCDD12;
__sfr  __at (LCDD13_ADDR)                  LCDD13;
__sfr  __at (LCDD14_ADDR)                  LCDD14;
__sfr  __at (LCDD15_ADDR)                  LCDD15;
__sfr  __at (TRISF_ADDR)                   TRISF;
__sfr  __at (TRISG_ADDR)                   TRISG;
__sfr  __at (PMDATA_ADDR)                  PMDATA;
__sfr  __at (PMDATH_ADDR)                  PMDATH;
__sfr  __at (PMADR_ADDR)                   PMADR;
__sfr  __at (PMADRH_ADDR)                  PMADRH;

// 
// bitfield definitions
// 
volatile __ADCON0_bits_t __at(ADCON0_ADDR) ADCON0_bits;
volatile __ADCON1_bits_t __at(ADCON1_ADDR) ADCON1_bits;
volatile __CCP1CON_bits_t __at(CCP1CON_ADDR) CCP1CON_bits;
volatile __INTCON_bits_t __at(INTCON_ADDR) INTCON_bits;
volatile __LCDCON_bits_t __at(LCDCON_ADDR) LCDCON_bits;
volatile __LCDPS_bits_t __at(LCDPS_ADDR) LCDPS_bits;
volatile __LCDSE_bits_t __at(LCDSE_ADDR) LCDSE_bits;
volatile __OPTION_REG_bits_t __at(OPTION_REG_ADDR) OPTION_REG_bits;
volatile __PCON_bits_t __at(PCON_ADDR) PCON_bits;
volatile __PIE1_bits_t __at(PIE1_ADDR) PIE1_bits;
volatile __PIR1_bits_t __at(PIR1_ADDR) PIR1_bits;
volatile __PMCON1_bits_t __at(PMCON1_ADDR) PMCON1_bits;
volatile __PORTA_bits_t __at(PORTA_ADDR) PORTA_bits;
volatile __PORTB_bits_t __at(PORTB_ADDR) PORTB_bits;
volatile __PORTC_bits_t __at(PORTC_ADDR) PORTC_bits;
volatile __PORTD_bits_t __at(PORTD_ADDR) PORTD_bits;
volatile __PORTE_bits_t __at(PORTE_ADDR) PORTE_bits;
volatile __SSPCON_bits_t __at(SSPCON_ADDR) SSPCON_bits;
volatile __SSPSTAT_bits_t __at(SSPSTAT_ADDR) SSPSTAT_bits;
volatile __STATUS_bits_t __at(STATUS_ADDR) STATUS_bits;
volatile __T1CON_bits_t __at(T1CON_ADDR) T1CON_bits;
volatile __T2CON_bits_t __at(T2CON_ADDR) T2CON_bits;
volatile __TRISA_bits_t __at(TRISA_ADDR) TRISA_bits;
volatile __TRISB_bits_t __at(TRISB_ADDR) TRISB_bits;
volatile __TRISC_bits_t __at(TRISC_ADDR) TRISC_bits;
volatile __TRISD_bits_t __at(TRISD_ADDR) TRISD_bits;
volatile __TRISE_bits_t __at(TRISE_ADDR) TRISE_bits;

