# Generally evolving RTOS program

A series of exercises focusing on RTOS and incremental development methods. Each exercise should continue from the code of the last one, which will be implemented through copying the previous folder for the sake of keeping each exercise separate. Would love to use better version control but clear separation into different exercises is the priority.

## Changelog

### v2 (ex3) - Refactor, data transfer

11.9. Doing slight refactor of code. Currently prints screen to terminal, has paddles and ball moving in set pattern. Somehow managed to soft-brick the nRF5340DK, switching to a 7002 until issue is resolved (requires removal of two button interrupts as the 7002 only has 2 buttons instead of 4).

### v1 (ex2) - Light cycles through scheduling

9.9. - Initial: Ex2, a few leds cycling or blinking based on button-selected states
