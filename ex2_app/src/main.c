/*
	Viikkotehtävä 2, RTOS-ohjelmointi
	Tavoiteltavaa pistemäärää en osaa sanoa, koska ajatus "tämä tehtävä ei vaatisi RTOS-taskeja
	koska jatkuvaa suorittamisen tarvetta ei ole" vaivasi liikaa ja ryhdyin sooloilemaan.
	5 pistettä kelpaisi mutten ihmettelisi vaikka poikkeaminen tehtävänannosta toisi vähemmän.

	Toteutuksessa siis käytössä kolme lediä ja neljä nappia;
		- nappi 1 muuttaa tilan liikennevaloksi/ledistä toiseen siirtyväksi
		- nappi 2 pistää ledit tauolle (jää senhetkiseen tilaan)
		- nappi 3 pistää senhetkisen ledin välkkymään pois ja päälle
		- nappi 4 vaihtaa aktiiviseksi lediksi syklin seuraavan

	Napeilla on kiinnitetyt interruptit, ledejä hallitaan laittamalla pyyntöjä work queueen,
	ledien välkytys hoituu timerilla (joka myös laittaa pyyntöjä työjonoon) ja tilaa seurataan
	värin kertovalla intillä ja tilaa bittipohjaisesti säilyttävällä uint8:lla. Koska pause on
	yksittäinen bitti tilassa, erillistä muuttujaa ei tarvita taukoa edeltävää tilaa säilyttämään
	vaan olemassa olevien bittien rauhaan jättäminen riittää.

	Käytössä ongelmana ilmenee ajoittainen tuplapainallus napin painalluksen yhteydessä, jonka oletan
	johtuvan debouncen puutteesta tai vastaavasta?
*/

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <inttypes.h>

int init_leds(void);
int init_buttons(void);
void handle_timer_expiry(struct k_timer *timer);
void handle_led_work(struct k_work *work);
void set_leds(uint8_t led_mask);
void button_cycle_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void button_pause_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void button_blink_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void button_color_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

// Configure buttons
#define BUTTON_0 DT_ALIAS(sw0)
#define BUTTON_1 DT_ALIAS(sw1)
#define BUTTON_2 DT_ALIAS(sw2)
#define BUTTON_3 DT_ALIAS(sw3)
static struct gpio_dt_spec buttons[4] = {GPIO_DT_SPEC_GET_OR(BUTTON_0, gpios, {0}),
	GPIO_DT_SPEC_GET_OR(BUTTON_1, gpios, {1}),
	GPIO_DT_SPEC_GET_OR(BUTTON_2, gpios, {2}),
	GPIO_DT_SPEC_GET_OR(BUTTON_3, gpios, {3})};
static struct gpio_callback button_data[4];
void (*callbacks[4])(const struct device *dev, struct gpio_callback *cb, uint32_t pins) = {
	button_cycle_handler, button_pause_handler, button_blink_handler, button_color_handler};

// Configure leds
enum Colors {RED, YELLOW, GREEN};
enum States {CYCLE, PAUSED, BLINK};
uint8_t led_color = RED;
uint8_t program_state = BIT(CYCLE);
static struct gpio_dt_spec leds[4] = {GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios)};

// Timer setup
k_timeout_t time_left = K_SECONDS(1);
const k_timeout_t blink_duration = K_SECONDS(1);
K_TIMER_DEFINE(led_timer, handle_timer_expiry, NULL);
K_WORK_DEFINE(led_worker, handle_led_work);

// Main program
int main(void) {
	int ret = init_leds();
	if (ret != 0) {
		printk("Failed led init");
        return 1;
	}

	ret = init_buttons();
	if (ret != 0) {
		printk("Failed button init");
        return 1;
	}

	k_timer_start(&led_timer, blink_duration, blink_duration);

	while (true) {
		printk("Hello from main, color %u, state %u\n", led_color, program_state);
		k_msleep(10000);
		// k_yield();
	}

	return 0;
}

// Initialize leds
int  init_leds(void) {
	int ret = 0;

	for (int i = 0; i < 3; i++) {
		// Led pin initialization
		ret = gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_ACTIVE | GPIO_INPUT);
		if (ret < 0) {
			printk("Error: Led %u configure failed\n", i + 1);
			return ret;
		}
		// set led off
		gpio_pin_set_dt(&leds[i], 0);

		printk("Led %u initialized ok\n", i + 1);
	}

	return 0;
}

// Button initialization
int init_buttons(void) {
	int ret;

	for (int i = 0; i < 4; i++) {
		if (!gpio_is_ready_dt(&buttons[i])) {
			printk("Error: button %u is not ready\n", i + 1);
			return -1;
		}

		ret = gpio_pin_configure_dt(&buttons[i], GPIO_INPUT);
		if (ret != 0) {
			printk("Error: failed to configure pin %u\n", i + 1);
			return -1;
		}

		ret = gpio_pin_interrupt_configure_dt(&buttons[i], GPIO_INT_EDGE_TO_ACTIVE);
		if (ret != 0) {
			printk("Error: failed to configure interrupt on pin %u\n", i + 1);
			return -1;
		}

		gpio_init_callback(&button_data[i], callbacks[i], BIT(buttons[i].pin));
		gpio_add_callback(buttons[i].port, &button_data[i]);
		printk("Set up button %u ok\n", i + 1);
	}

	return 0;
}

void button_cycle_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	program_state &= (BIT(PAUSED) | BIT(CYCLE));
	program_state |= BIT(CYCLE);

	k_work_submit(&led_worker);
}

void button_pause_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	// Pause/unpause timer, flip pause bit
	if (program_state & BIT(PAUSED)) {
		k_timer_start(&led_timer, time_left, blink_duration);
	} else {
		time_left.ticks = k_timer_remaining_ticks(&led_timer);
		k_timer_stop(&led_timer);
	}
	program_state ^= BIT(PAUSED);
}

void button_blink_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	program_state &= (BIT(PAUSED) | BIT(BLINK));
	program_state |= BIT(BLINK);

	k_work_submit(&led_worker);
}

void button_color_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	if (program_state & BIT(PAUSED)) {
		return;
	}
	led_color = (led_color == GREEN) ? RED : ++led_color;

	k_work_submit(&led_worker);
}

void handle_timer_expiry(struct k_timer *timer){
	k_work_submit(&led_worker);
}

void handle_led_work(struct k_work *work) {
	if (program_state & BIT(PAUSED)) {
		return;
	}

	uint8_t led_mask = BIT(led_color);

	// Decipher led profile from state
	if (program_state & BIT(CYCLE)) {
		led_color = (led_color == GREEN) ? RED : ++led_color;
	} else if (program_state & BIT(BLINK)) {
		led_mask ^= gpio_pin_get_dt(&leds[led_color]) << led_color;
	}

	set_leds(led_mask);

	k_yield();
}

void set_leds(uint8_t led_mask) {
	for (int i = 0; i < 3; i++) {
		// Extracts each bit from the mask and feeds it to the associated led
		gpio_pin_set_dt(&leds[i], ((BIT(i) & led_mask) >> i));
	}
}