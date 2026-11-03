#include "battery.h"
#include "BirdConfig.h"

/* Ядро Adafruit nRF52: analogRead 10 бит, REFSEL=0.6 В, GAIN=1/6 -> 3.6 В шкала. */
#define ADC_FULL_SCALE_MV 3600.0f
#define ADC_MAX           1023.0f
#define ADC_SAMPLES       8

/* Поправка по мультиметру, можно менять с телефона (batcal=115). */
static uint8_t _calPct = BIRDY_VBAT_CAL_PCT;

void batterySetCalPct(uint8_t pct) {
	if (pct < 50) pct = 50;
	if (pct > 200) pct = 200;
	_calPct = pct;
	Serial.printf("[bat] калибровка: %u %%\n", (unsigned)_calPct);
}

uint8_t batteryGetCalPct(void) { return _calPct; }

void batteryBegin(void) {
	pinMode(BIRDY_VBAT_PIN, INPUT);
	analogReadResolution(10);
	Serial.printf("[bat] pin=%d R_top=%lu R_bot=%lu\n",
	              (int)BIRDY_VBAT_PIN,
	              (unsigned long)BIRDY_VBAT_R_TOP,
	              (unsigned long)BIRDY_VBAT_R_BOT);
}

uint16_t batteryReadPinMv(void) {
	uint32_t acc = 0;
	for (int i = 0; i < ADC_SAMPLES; i++) acc += analogRead(BIRDY_VBAT_PIN);
	const float raw = (float)acc / (float)ADC_SAMPLES;
	return (uint16_t)(raw * (ADC_FULL_SCALE_MV / ADC_MAX) + 0.5f);
}

uint16_t batteryReadMv(void) {
	const float pin_mv = (float)batteryReadPinMv();
	const float k = ((float)BIRDY_VBAT_R_TOP + (float)BIRDY_VBAT_R_BOT) / (float)BIRDY_VBAT_R_BOT;
	const float cal = (float)_calPct / 100.0f; /* поправка по мультиметру */
	return (uint16_t)(pin_mv * k * cal + 0.5f);
}

uint8_t batteryPercent(uint16_t mv) {
	/* Кривая разряда Li-Ion (без нагрузки, приблизительно). */
	static const uint16_t kMv[]  = {4200, 4100, 4000, 3900, 3800, 3700, 3600, 3500, 3400, 3300, 3200};
	static const uint8_t  kPct[] = { 100,   95,   88,   78,   66,   52,   38,   24,   12,    5,    0};
	const int n = (int)(sizeof(kMv) / sizeof(kMv[0]));

	if (mv >= kMv[0]) return 100;
	if (mv <= kMv[n - 1]) return 0;
	for (int i = 0; i < n - 1; i++) {
		if (mv <= kMv[i] && mv >= kMv[i + 1]) {
			const uint16_t span = kMv[i] - kMv[i + 1];
			const uint8_t dPct = kPct[i] - kPct[i + 1];
			const uint16_t dMv = kMv[i] - mv;
			return (uint8_t)(kPct[i] - (dPct * dMv) / span);
		}
	}
	return 0;
}
