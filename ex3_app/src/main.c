/*
	Viikkotehtävä 3, RTOS-ohjelmointi

	Ohjelma muuttui refaktoroinnissa hiukan
*/

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <inttypes.h>

// Setup
int init_buttons(void);
int init_uart(void);

// I/O
void button_0_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void button_1_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void button_2_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void button_3_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

// Game logic
#define SCREEN_HEIGHT 32
#define SCREEN_WIDTH 33
#define BAR_HEIGHT 3
typedef struct State {
	uint8_t bar1;
	uint8_t bar2;
	uint8_t x_pixel;
	uint8_t y_pixel;
	uint16_t x_precise;
	uint16_t y_precise;
	uint16_t speed;
} StateData;
StateData calculate_positions(StateData stateData);
void draw_row(uint8_t *buffer, StateData stateData, uint16_t rowNo);

// Configure buttons
static struct gpio_dt_spec buttons[4] = {GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw0), gpios, {0}),
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw1), gpios, {1}),
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw2), gpios, {2}),
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw3), gpios, {3})};
static struct gpio_callback button_data[4];
void (*callbacks[4])(const struct device *dev, struct gpio_callback *cb, uint32_t pins) = {
	button_0_handler, button_1_handler, button_2_handler, button_3_handler};

// Configure UART
static const struct device *const uart_device = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));

// Set up threads
void frame_task(void *, void *, void *);
void uart_task(void *, void *, void *);
#define STACKSIZE 512
K_THREAD_DEFINE(tid1, STACKSIZE, frame_task, NULL, NULL, NULL, 4, 0, 100);
K_THREAD_DEFINE(tid2, STACKSIZE, uart_task, NULL, NULL, NULL, 5, 0, 100);

// FIFO
K_FIFO_DEFINE(data_fifo);
typedef struct fifo_data {
	void *fifo_reserved;
	StateData frame;
} FifoData;

// Main program
int main(void) {
	int ret = 0;

	ret = init_buttons();
	if (ret != 0) {
		printk("Failed button init\n");
        return 1;
	}

	ret = init_uart();
	if (ret != 0) {
		printk("Failed UART init\n");
		return 1;
	}

	while (true) {
		//printk("Hello from main");
		k_msleep(10000);
	}

	return 0;
}

int init_buttons(void) {
	int ret;

	for (int i = 0; i < 2; i++) {
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

int init_uart(void) {
	if (!device_is_ready(uart_device)) {
		return -1;
	}

	printk("UART initialized.\n");

	return 0;
}

void button_0_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {

}

void button_1_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {

}

void button_2_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {

}

void button_3_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {

}

void frame_task(void *, void *, void *) {
	StateData stateData = {5, 5, 5, 5, 5, 5, 5};

	//
	while (true) {
		//printk("Frame\n");

		// Calculate ball position
		stateData = calculate_positions(stateData);

		// Generate the frame and send it off to be printed
		// Pass the frame to UART to be printed at convenience
		FifoData *buffer = k_malloc(sizeof(FifoData));
		buffer->frame = stateData;

		k_fifo_put(&data_fifo, buffer);

		// Draw a frame once every (?) ms
		k_msleep(500);
	}
}

StateData calculate_positions(StateData state) {
	StateData newState = state;

	newState.bar1 = (state.bar1 == (SCREEN_HEIGHT - (2 + BAR_HEIGHT))) ? 1 : ++state.bar1;
	newState.bar2 = (state.bar2 == (SCREEN_HEIGHT - (2 + BAR_HEIGHT))) ? 1 : ++state.bar2;
	newState.x_precise = (state.x_precise >= ((SCREEN_WIDTH - 2) * 100)) ? 200 : (state.x_precise + 100);
	newState.y_precise = (state.y_precise >= ((SCREEN_HEIGHT - 1) * 100)) ? 200 : (state.y_precise + 100);
	newState.x_pixel = newState.x_precise / 100;
	newState.y_pixel = newState.y_precise / 100;

	return newState;
}

void uart_task(void *, void *, void *) {
	FifoData *data = NULL;
	int err = 0;
	uint16_t rowsDrawn = 0;
	uint8_t buffer[SCREEN_WIDTH] = {'.'};

	printk("Start uart\r\n");

	while (true) {
		data = k_fifo_get(&data_fifo, K_FOREVER);

		if (data == NULL) {
			k_msleep(1);
			continue;
		}

		// Fetch frame from game logic
		while (rowsDrawn < SCREEN_HEIGHT) {
			if (err == 0) {
				// If there was no error with the previous row's transmission, convert frame into new row
				draw_row(buffer, data->frame, rowsDrawn);
				rowsDrawn++;
			}

			err = uart_tx(uart_device, buffer, SCREEN_WIDTH, 10000);

			// Entering newline into the buffer doesn't work for [some reason], so this is currently necessary
			printk("\n", rowsDrawn);
		}

		rowsDrawn = 0;
		k_free(data);
		//k_yield();
	}
}

void draw_row(uint8_t *buffer, StateData stateData, uint16_t rowNo) {
	// Upper and  bottom borders fill up the whole row
	if (rowNo == 0 || rowNo == (SCREEN_HEIGHT - 1)) {
		for (int i = 0; i < SCREEN_WIDTH; i++) {
			*(buffer + i) = '_';
		}

		*(buffer + (SCREEN_WIDTH - 1)) = '\n';

		return;
	}

	// Flush out the buffer
	for (int j = 0; j < SCREEN_WIDTH; j++) {
		*(buffer + j) = ' ';
	}

	// The usual additions
	*(buffer + 0) = '|';
	*(buffer + (SCREEN_WIDTH - 2)) = '|';
	*(buffer + (SCREEN_WIDTH - 1)) = ' ';

	// Place the paddles
	if (stateData.bar1 >= rowNo && stateData.bar1 < (rowNo + BAR_HEIGHT)) {
		*(buffer + 1) = 'H';
	}

	if (stateData.bar2 >= rowNo && stateData.bar2 < (rowNo + BAR_HEIGHT)) {
		*(buffer + (SCREEN_WIDTH - 3)) = 'H';
	}

	// Draw the ball
	if (stateData.y_pixel == rowNo) {
		*(buffer + stateData.x_pixel) = 'O';
	}
}