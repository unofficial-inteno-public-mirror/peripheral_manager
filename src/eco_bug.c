#include <stdio.h>
#include "i2c.h"
#include "log.h"
#include "gpio.h"

int debug_level=0;

int main(){
        int value;
        int old=-1;
        long long loops=0;

	gpio_init();

        while (1){
                value = board_ioctl( BOARD_IOCTL_GET_GPIO, 0, 0, NULL, 36, 0);
                loops++;

                if (old != value ){
                        printf("\nNew value is %d\n",value);
                        old=value;
                }
                printf("loops %lld\r",loops);
        }
}
