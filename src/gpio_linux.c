#include <syslog.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "gpio.h"
#include "log.h"

int board_ioctl_init(void) {
	return 0;
}


int board_ioctl(int ioctl_id, int action, int hex, char* string_buf, int string_buf_len, int offset) {
	return 0;
}

static void gpio_linux_dir_init(int addr, int dir ){
	char buf[100];
	int fd;

	/* export gpio to userspace */
	sprintf(buf, "%d\n", addr);
	fd = open("/sys/class/gpio/export", O_WRONLY);
	write(fd, buf, strlen(buf));
	close(fd);

	/* set gpio as output */
	sprintf(buf, "/sys/class/gpio/gpio%d/direction",addr);
	fd = open(buf, O_WRONLY);
	if (dir)
		sprintf(buf, "in\n");
	else
		sprintf(buf, "out\n");
	write(fd, buf, strlen(buf));
	close(fd);
}


void gpio_linux_output_init(int addr){
	gpio_linux_dir_init(addr, 0);
}

void gpio_linux_input_init(int addr){
	gpio_linux_dir_init(addr, 1);
}

int gpio_linux_set(int addr, int val)
{
	char buf[100];
	int fd;

	sprintf(buf, "/sys/class/gpio/gpio%d/value", addr);
	fd = open(buf, O_WRONLY);

	sprintf(buf,"%d\n", val);

	write(fd, buf, strlen(buf));
	close(fd);

	return 0;
}

int gpio_linux_get(int addr)
{
	char buf[100];
	int fd;
	int size;

	sprintf(buf, "/sys/class/gpio/gpio%d/value", addr);
	fd = open(buf, O_RDONLY);

	size = read(fd, buf, 100);
	close(fd);

	if(size){
		return strtol(buf, 0, 0);
	}
	return 0;
}


gpio_state_t gpio_get_state(gpio_t gpio)
{
	return 0;
}

void gpio_set_state(gpio_t gpio, gpio_state_t state, int delay)
{

}
