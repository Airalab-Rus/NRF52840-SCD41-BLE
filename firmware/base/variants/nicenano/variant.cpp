/*
  Variant для теста SCD41 на nRF52840 SuperMini.
  Прямая карта цифровых пинов: индекс = номер пина nRF52 (0..31 = P0.00..P0.31).
  Тогда Wire.setPins(17, 20) = SDA=P0.17, SCL=P0.20 — реальная распайка датчика.
*/
#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"
#include "nrf.h"

const uint32_t g_ADigitalPinMap[] =
{
  0,  1,  2,  3,  4,  5,  6,  7,   // D0..D7  = P0.00..P0.07
  8,  9,  10, 11, 12, 13, 14, 15,  // D8..D15 = P0.08..P0.15
  16, 17, 18, 19, 20, 21, 22, 23,  // D16..D23 = P0.16..P0.23
  24, 25, 26, 27, 28, 29, 30, 31,  // D24..D31 = P0.24..P0.31
  32, 33, 34                       // D32..D34 = P1.00..P1.02
};
