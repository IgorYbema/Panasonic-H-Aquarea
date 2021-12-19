#define MQTT_RETAIN_VALUES 1

// void decode_heatpump_data(char* data, String actData[], PubSubClient &mqtt_client, void (*log_message)(char*), char* mqtt_topic_base, unsigned int updateAllTime);

// String unknown(byte input);
// String getBit1and2(byte input);
// String getBit3and4(byte input);
// String getBit5and6(byte input);
// String getBit7and8(byte input);
// String getBit3and4and5(byte input);
// String getLeft5bits(byte input);
// String getRight3bits(byte input);
// String getIntMinus1(byte input);
// String getIntMinus128(byte input);
// String getIntMinus1Div5(byte input);
// String getIntMinus1Times10(byte input);
// String getIntMinus1Times50(byte input);
// String getOpMode(byte input);
// String getEnergy(byte input);
// String getHeatMode(byte input);
// String getModel(byte input);

#define NUMBER_OF_TOPICS 94 //last topic number + 1

static const char * topics[] = {
  "Heatpump_State",          //TOP0
  "Pump_Flow",               //TOP1
  "Force_DHW_State",         //TOP2
  "Quiet_Mode_Schedule",     //TOP3
  "Operating_Mode_State",    //TOP4
  "Main_Inlet_Temp",         //TOP5
  "Main_Outlet_Temp",        //TOP6
  "Main_Target_Temp",        //TOP7
  "Compressor_Freq",         //TOP8
  "DHW_Target_Temp",         //TOP9
  "DHW_Temp",                //TOP10
  "Operations_Hours",        //TOP11
  "Operations_Counter",      //TOP12
  "Main_Schedule_State",     //TOP13
  "Outside_Temp",            //TOP14
  "Heat_Energy_Production",  //TOP15
  "Heat_Energy_Consumption", //TOP16
  "Powerful_Mode_Time",      //TOP17
  "Quiet_Mode_Level",        //TOP18
  "Holiday_Mode_State",      //TOP19
  "ThreeWay_Valve_State",    //TOP20
  "Outside_Pipe_Temp",       //TOP21
  "DHW_Heat_Delta",          //TOP22
  "Heat_Delta",              //TOP23
  "Cool_Delta",              //TOP24
  "DHW_Holiday_Shift_Temp",  //TOP25
  "Defrosting_State",        //TOP26
  "Z1_Heat_Request_Temp",    //TOP27
  "Z1_Cool_Request_Temp",    //TOP28
  "Z1_Heat_Curve_Target_High_Temp",      //TOP29
  "Z1_Heat_Curve_Target_Low_Temp",       //TOP30
  "Z1_Heat_Curve_Outside_High_Temp",     //TOP31
  "Z1_Heat_Curve_Outside_Low_Temp",      //TOP32
  "Room_Thermostat_Temp",    //TOP33
  "Z2_Heat_Request_Temp",    //TOP34
  "Z2_Cool_Request_Temp",    //TOP35
  "Z1_Water_Temp",           //TOP36
  "Z2_Water_Temp",           //TOP37
  "Cool_Energy_Production",  //TOP38
  "Cool_Energy_Consumption", //TOP39
  "DHW_Energy_Production",   //TOP40
  "DHW_Energy_Consumption",  //TOP41
  "Z1_Water_Target_Temp",    //TOP42
  "Z2_Water_Target_Temp",    //TOP43
  "Error",                   //TOP44
  "Room_Holiday_Shift_Temp", //TOP45
  "Buffer_Temp",             //TOP46
  "Solar_Temp",              //TOP47
  "Pool_Temp",               //TOP48
  "Main_Hex_Outlet_Temp",    //TOP49
  "Discharge_Temp",          //TOP50
  "Inside_Pipe_Temp",        //TOP51
  "Defrost_Temp",            //TOP52
  "Eva_Outlet_Temp",         //TOP53
  "Bypass_Outlet_Temp",      //TOP54
  "Ipm_Temp",                //TOP55
  "Z1_Temp",                 //TOP56
  "Z2_Temp",                 //TOP57
  "DHW_Heater_State",        //TOP58
  "Room_Heater_State",       //TOP59
  "Internal_Heater_State",   //TOP60
  "External_Heater_State",   //TOP61
  "Fan1_Motor_Speed",        //TOP62
  "Fan2_Motor_Speed",        //TOP63
  "High_Pressure",           //TOP64
  "Pump_Speed",              //TOP65
  "Low_Pressure",            //TOP66
  "Compressor_Current",      //TOP67
  "Force_Heater_State",      //TOP68
  "Sterilization_State",     //TOP69
  "Sterilization_Temp",      //TOP70
  "Sterilization_Max_Time",  //TOP71
  "Z1_Cool_Curve_Target_High_Temp",      //TOP72
  "Z1_Cool_Curve_Target_Low_Temp",       //TOP73
  "Z1_Cool_Curve_Outside_High_Temp",     //TOP74
  "Z1_Cool_Curve_Outside_Low_Temp",      //TOP75
  "Heating_Mode",            //TOP76
  "Heating_Off_Outdoor_Temp",//TOP77
  "Heater_On_Outdoor_Temp",  //TOP78
  "Heat_To_Cool_Temp",       //TOP79
  "Cool_To_Heat_Temp",       //TOP80
  "Cooling_Mode",            //TOP81
  "Z2_Heat_Curve_Target_High_Temp",      //TOP82
  "Z2_Heat_Curve_Target_Low_Temp",       //TOP83
  "Z2_Heat_Curve_Outside_High_Temp",     //TOP84
  "Z2_Heat_Curve_Outside_Low_Temp",      //TOP85
  "Z2_Cool_Curve_Target_High_Temp",      //TOP86
  "Z2_Cool_Curve_Target_Low_Temp",       //TOP87
  "Z2_Cool_Curve_Outside_High_Temp",     //TOP88
  "Z2_Cool_Curve_Outside_Low_Temp",      //TOP89
  "Room_Heater_Operations_Hours", //TOP90
  "DHW_Heater_Operations_Hours",  //TOP91
  "Heat_Pump_Model", //TOP92,
  "Pump_Duty", //TOP93
};