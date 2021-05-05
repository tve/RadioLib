#include "SX1280.h"
#if !defined(RADIOLIB_EXCLUDE_SX128X)

SX1280::SX1280(Module* mod) : SX1281(mod) {

}

int16_t SX1280::range(bool master, uint32_t addr) {
  // start ranging
  int16_t state = startRanging(master, addr);
  RADIOLIB_ASSERT(state);

  // wait until ranging is finished
  uint32_t start = Module::millis();
  while(!Module::digitalRead(_mod->getIrq())) {
    Module::yield();
    if(Module::millis() - start > 10000) {
      clearIrqStatus();
      standby();
      return(ERR_RANGING_TIMEOUT);
    }
  }

  // clear interrupt flags
  state = clearIrqStatus();
  RADIOLIB_ASSERT(state);

  // set mode to standby
  state = standby();

  return(state);
}

int16_t SX1280::startRanging(bool master, uint32_t addr) {
  // check active modem
  uint8_t modem = getPacketType();
  if(!((modem == SX128X_PACKET_TYPE_LORA) || (modem == SX128X_PACKET_TYPE_RANGING))) {
    return(ERR_WRONG_MODEM);
  }

  // set mode to standby
  int16_t state = standby();
  RADIOLIB_ASSERT(state);

  // ensure modem is set to ranging
  if(modem == SX128X_PACKET_TYPE_LORA) {
    state = setPacketType(SX128X_PACKET_TYPE_RANGING);
    RADIOLIB_ASSERT(state);
  }

  // set modulation parameters
  state = setModulationParams(_sf, _bw, _cr);
  RADIOLIB_ASSERT(state);

  // set packet parameters
  state = setPacketParamsLoRa(_preambleLengthLoRa, _headerType, _payloadLen, _crcLoRa);
  RADIOLIB_ASSERT(state);

  // check all address bits
  uint8_t regValue;
  state = readRegister(SX128X_REG_SLAVE_RANGING_ADDRESS_WIDTH, &regValue, 1);
  RADIOLIB_ASSERT(state);
  regValue &= 0b00111111;
  regValue |= 0b11000000;
  state = writeRegister(SX128X_REG_SLAVE_RANGING_ADDRESS_WIDTH, &regValue, 1);
  RADIOLIB_ASSERT(state);

  // set remaining parameter values
  uint32_t addrReg = SX128X_REG_SLAVE_RANGING_ADDRESS_BYTE_3;
  uint32_t irqMask = SX128X_IRQ_RANGING_SLAVE_RESP_DONE | SX128X_IRQ_RANGING_SLAVE_REQ_DISCARD;
  uint32_t irqDio1 = SX128X_IRQ_RANGING_SLAVE_RESP_DONE;
  if(master) {
    addrReg = SX128X_REG_MASTER_RANGING_ADDRESS_BYTE_3;
    irqMask = SX128X_IRQ_RANGING_MASTER_RES_VALID | SX128X_IRQ_RANGING_MASTER_TIMEOUT;
    irqDio1 = SX128X_IRQ_RANGING_MASTER_RES_VALID;
  }

  // set ranging address
  uint8_t addrBuff[] = { (uint8_t)((addr >> 24) & 0xFF), (uint8_t)((addr >> 16) & 0xFF), (uint8_t)((addr >> 8) & 0xFF), (uint8_t)(addr & 0xFF) };
  state = writeRegister(addrReg, addrBuff, 4);
  RADIOLIB_ASSERT(state);

  // set DIO mapping
  state = setDioIrqParams(irqMask, irqDio1);
  RADIOLIB_ASSERT(state);

  // set calibration values
  static const uint16_t calTable[3][6] = {
    { 10299, 10271, 10244, 10242, 10230, 10246 },
    { 11486, 11474, 11453, 11426, 11417, 11401 },
    { 13308, 13493, 13528, 13515, 13430, 13376 }
  };
  uint16_t val = 0;
  switch(_bw) {
    case(SX128X_LORA_BW_406_25):
      val = calTable[0][_sf];
      break;
    case(SX128X_LORA_BW_812_50):
      val = calTable[1][_sf];
      break;
    case(SX128X_LORA_BW_1625_00):
      val = calTable[2][_sf];
      break;
    default:
      return(ERR_INVALID_BANDWIDTH);
  }
  uint8_t calBuff[] = { (uint8_t)((val >> 8) & 0xFF), (uint8_t)(val & 0xFF) };
  state = writeRegister(SX128X_REG_RANGING_CALIBRATION_MSB, calBuff, 2);
  RADIOLIB_ASSERT(state);

  // set role and start ranging
  if(master) {
    state = setRangingRole(SX128X_RANGING_ROLE_MASTER);
    RADIOLIB_ASSERT(state);

    state = setTx(SX128X_TX_TIMEOUT_NONE);
    RADIOLIB_ASSERT(state);

  } else {
    state = setRangingRole(SX128X_RANGING_ROLE_SLAVE);
    RADIOLIB_ASSERT(state);

    state = setRx(SX128X_RX_TIMEOUT_INF);
    RADIOLIB_ASSERT(state);

  }

  return(state);
}

float SX1280::getRangingResult() {
  // set mode to standby XOSC
  int16_t state = standby(SX128X_STANDBY_XOSC);
  RADIOLIB_ASSERT(state);

  // enable clock
  uint8_t data[4];
  state = readRegister(SX128X_REG_RANGING_LORA_CLOCK_ENABLE, data, 1);
  RADIOLIB_ASSERT(state);

  data[0] |= (1 << 1);
  state = writeRegister(SX128X_REG_RANGING_LORA_CLOCK_ENABLE, data, 1);
  RADIOLIB_ASSERT(state);

  // set result type to filtered
  state = readRegister(SX128X_REG_RANGING_TYPE, data, 1);
  RADIOLIB_ASSERT(state);

  data[0] &= 0xCF;
  data[0] |= (1 << 4);
  state = writeRegister(SX128X_REG_RANGING_TYPE, data, 1);
  RADIOLIB_ASSERT(state);

  // read the register values
  state = readRegister(SX128X_REG_RANGING_RESULT_MSB, &data[0], 1);
  RADIOLIB_ASSERT(state);
  state = readRegister(SX128X_REG_RANGING_RESULT_MID, &data[1], 1);
  RADIOLIB_ASSERT(state);
  state = readRegister(SX128X_REG_RANGING_RESULT_LSB, &data[2], 1);
  RADIOLIB_ASSERT(state);

  // set mode to standby RC
  state = standby();
  RADIOLIB_ASSERT(state);

  // calculate the real result
  uint32_t raw = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
  return((float)raw * 150.0 / (4.096 * _bwKhz));
}

#endif
