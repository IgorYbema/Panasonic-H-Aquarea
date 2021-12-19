typedef unsigned char byte;

#define PANASONICQUERYSIZE 110
extern byte panasonicQuery[PANASONICQUERYSIZE];

#define OPTIONALPCBQUERYSIZE 19
extern byte optionalPCBQuery[OPTIONALPCBQUERYSIZE];

struct {
  const char *name;
  // unsigned int (*func)(char *msg, unsigned char **cmd, char **log_msg);
} commands[] = {
  // set heatpump state to on by sending 1
  { "SetHeatpump" },
  // set pump state to on by sending 1
  { "SetPump" },
  // set pump speed
  { "SetPumpSpeed" },
  // set 0 for Off mode, set 1 for Quiet mode 1, set 2 for Quiet mode 2, set 3 for Quiet mode 3
  { "SetQuietMode" },
  // z1 heat request temp -  set from -5 to 5 to get same temperature shift point or set direct temp
  { "SetZ1HeatRequestTemperature" },
  // z1 cool request temp -  set from -5 to 5 to get same temperature shift point or set direct temp
  { "SetZ1CoolRequestTemperature" },
  // z2 heat request temp -  set from -5 to 5 to get same temperature shift point or set direct temp
  { "SetZ2HeatRequestTemperature" },
  // z2 cool request temp -  set from -5 to 5 to get same temperature shift point or set direct temp
  { "SetZ2CoolRequestTemperature" },
  // set mode to force DHW by sending 1
  { "SetForceDHW" },
  // set mode to force defrost  by sending 1
  { "SetForceDefrost" },
  // set mode to force sterilization by sending 1
  { "SetForceSterilization" },
  // set Holiday mode by sending 1, off will be 0
  { "SetHolidayMode" },
  // set Powerful mode by sending 0 = off, 1 for 30min, 2 for 60min, 3 for 90 min
  { "SetPowerfulMode" },
  // set Heat pump operation mode  3 = DHW only, 0 = heat only, 1 = cool only, 2 = Auto, 4 = Heat+DHW, 5 = Cool+DHW, 6 = Auto + DHW
  { "SetOperationMode" },
  // set DHW temperature by sending desired temperature between 40C-75C
  { "SetDHWTemp" },
  // optional PCB
  { "SetHeatCoolMode" },
  { "SetCompressorState" },
  { "SetSmartGridMode" },
  { "SetExternalThermostat1State" },
  { "SetExternalThermostat2State" },
  { "SetDemandControl" },
  { "SetPoolTemp" },
  { "SetBufferTemp" },
  { "SetZ1RoomTemp" },
  { "SetZ1WaterTemp" },
  { "SetZ2RoomTemp" },
  { "SetZ2WaterTemp" },
  { "SetSolarTemp" }
};

extern void setCommand(struct rules_t *obj, uint16_t step, uint16_t pos);
extern void getCommand(struct rules_t *obj, uint16_t step);
