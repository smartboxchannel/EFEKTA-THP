//#include "Debug.h"
#include "battery.h"
#include "hal_adc.h"
#include "utils.h"
#include "OSAL.h"
#include "zcl.h"
#include "zcl_general.h"
#include "bdb_interface.h"
// (( 3 * 1.15 ) / (( 2^14 / 2 ) - 1 )) * 1000 (not correct)
// #define MULTI (float) 0.4211939934
// this coefficient calculated using
// https://docs.google.com/spreadsheets/d/1qrFdMTo0ZrqtlGUoafeB3hplhU3GzDnVWuUK4M9OgNo/edit?usp=sharing
#define MULTI (float)0.443

#define VOLTAGE_MIN 2.0
#define VOLTAGE_MAX 3.3

#ifndef ZCL_BATTERY_REPORT_INTERVAL
    #define ZCL_BATTERY_REPORT_INTERVAL ((uint32) 1800000) //30 minutes
#endif

#ifndef ZCL_BATTERY_REPORT_DELAY
    #define ZCL_BATTERY_REPORT_DELAY 5 * 1000
#endif

#ifndef ZCL_BATTERY_REPORT_REPORT_CONVERTER
#define ZCL_BATTERY_REPORT_REPORT_CONVERTER(millivolts) getBatteryRemainingPercentageZCLCR2032(millivolts)
#endif

#define POWER_CFG ZCL_CLUSTER_ID_GEN_POWER_CFG

#define ZCL_BATTERY_REPORT_EVT 0x0001

uint8 zclBattery_Voltage = 0xff;
uint8 zclBattery_PercentageRemainig = 0xff;
uint16 zclBattery_RawAdc = 0xff;
static uint8 Seq2Num = 0;
afAddrType_t zcl_Dst2Addr;
uint8 getBatteryVoltageZCL(uint16 millivolts) {
    uint8 volt8 = (uint8)(millivolts / 100);
    if ((millivolts - (volt8 * 100)) > 50) {
        return volt8 + 1;
    } else {
        return volt8;
    }
}
// return millivolts
uint16 getBatteryVoltage(void) {
    HalAdcSetReference(HAL_ADC_REF_125V);
    zclBattery_RawAdc = adcReadSampled(HAL_ADC_CHANNEL_VDD, HAL_ADC_RESOLUTION_14, HAL_ADC_REF_125V, 10);
    return (uint16)(zclBattery_RawAdc * MULTI);
}

uint8 getBatteryRemainingPercentageZCL(uint16 millivolts) { return (uint8)mapRange(VOLTAGE_MIN, VOLTAGE_MAX, 0.0, 200.0, millivolts); }

uint8 getBatteryRemainingPercentageZCLCR2032(uint16 volt16) {
    float battery_level;
    if (volt16 >= 3200) {
        battery_level = 100;
    } else if (volt16 > 2200) {
        battery_level = 100 - ((3200 - volt16) * 90) / 1000;
    } else if (volt16 > 1800) {
        battery_level = 10 - ((2200 - volt16) * 10) / 400;
    } else {
        battery_level = 0;
    }
    return (uint8)(battery_level * 2);
}

void zclBattery_Report(void) {
    uint16 millivolts = getBatteryVoltage();
    zclBattery_Voltage = getBatteryVoltageZCL(millivolts);
    zclBattery_PercentageRemainig = ZCL_BATTERY_REPORT_REPORT_CONVERTER(millivolts);

    //LREP("Battery voltageZCL=%d prc=%d voltage=%d\r\n", zclBattery_Voltage, zclBattery_PercentageRemainig, millivolts);

#if BDB_REPORTING
    bdb_RepChangedAttrValue(1, POWER_CFG, ATTRID_POWER_CFG_BATTERY_PERCENTAGE_REMAINING);
#else
    const uint8 NUM_ATTRIBUTES = 2;

  zclReportCmd_t *pReportCmd;

  pReportCmd = osal_mem_alloc(sizeof(zclReportCmd_t) +
                              (NUM_ATTRIBUTES * sizeof(zclReport_t)));
  if (pReportCmd != NULL) {
    pReportCmd->numAttr = NUM_ATTRIBUTES;

    pReportCmd->attrList[0].attrID = ATTRID_POWER_CFG_BATTERY_VOLTAGE;
    pReportCmd->attrList[0].dataType = ZCL_DATATYPE_UINT8;
    pReportCmd->attrList[0].attrData = (void *)(&zclBattery_Voltage);

    pReportCmd->attrList[1].attrID = ATTRID_POWER_CFG_BATTERY_PERCENTAGE_REMAINING;
    pReportCmd->attrList[1].dataType = ZCL_DATATYPE_UINT8;
    pReportCmd->attrList[1].attrData = (void *)(&zclBattery_PercentageRemainig);

    zcl_Dst2Addr.addrMode = (afAddrMode_t)Addr16Bit;
    zcl_Dst2Addr.addr.shortAddr = 0;
    zcl_Dst2Addr.endPoint = 1;

    zcl_SendReportCmd(1, &zcl_Dst2Addr,
                      ZCL_CLUSTER_ID_GEN_POWER_CFG, pReportCmd,
                      ZCL_FRAME_SERVER_CLIENT_DIR, true, Seq2Num++);
  }

  osal_mem_free(pReportCmd);
#endif
}

uint8 zclBattery_TaskId = 0;

void zclBattery_Init(uint8 task_id) {
    zclBattery_TaskId = task_id;
#if BDB_REPORTING
    osal_start_reload_timer(zclBattery_TaskId, ZCL_BATTERY_REPORT_EVT, ZCL_BATTERY_REPORT_INTERVAL);
#endif
}

uint16 zclBattery_event_loop(uint8 task_id, uint16 events) {
    //LREP("zclBattery_event_loop 0x%X\r\n", events);
    if (events & ZCL_BATTERY_REPORT_EVT) {
        //LREPMaster("ZCL_BATTERY_REPORT_EVT\r\n");
        zclBattery_Report();
        return (events ^ ZCL_BATTERY_REPORT_EVT);
    }
    return 0;
}

void zclBattery_HandleKeys(uint8 portAndAction, uint8 keyCode) {
    osal_start_timerEx(zclBattery_TaskId, ZCL_BATTERY_REPORT_EVT, ZCL_BATTERY_REPORT_DELAY);
}