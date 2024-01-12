/* $IAC_tester: main.c, v1.1 2014/04/07 21:44:00 anders Exp $ */

/*
 * Copyright (c) 2023, Anders Franzen.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * @(#)main.c
 */

#include "sys.h"
#include "sys_env.h"
#include "io.h"
#include "gpio_drv.h"

#include "config.h"

#include <string.h>
#include <ctype.h>


static int config(int argc, char **argv, struct Env *env);
static int step(int argc, char **argv, struct Env *env);
static int pin_configured=0;
static unsigned int ifs;

static unsigned int coil1a_pin_fd;
static unsigned int coil1b_pin_fd;
static unsigned int coil2a_pin_fd;
static unsigned int coil2b_pin_fd;

//static unsigned int one=1;
static unsigned int zero=0;

static struct cmd cmds[] = {
	{"help", generic_help_fnc},
	{"config", config},
	{"step", step},
	{0,0}
};

static struct cmd_node cmn = {
	"iac",
	cmds,
};

static int udelay(unsigned int usec) {
	volatile unsigned int count=0;
	volatile unsigned int utime=(34*usec/7);
	do {
		if (++count>utime) return 0;
	} while (1);

	return 0;
}

static int set_coil(int pin_fd, int val) {
	return io_control(pin_fd, GPIO_SET_PIN, &val, sizeof(val));
}

static int step(int argc, char **argv, struct Env *env) {
	int i;
	int neg=0;
	int n_steps=0;
	char *num_str;
	if (argc<2) {
		fprintf(env->io_fd, "arg should be nr of steps negatives. or pos.\n");
		return -1;
	}

	if (!ifs) {
		fprintf(env->io_fd, "do config first\n");
		return 0;
	}

	if (argv[1][0]=='-') {
		neg=1;
		num_str=&argv[1][1];
	} else {
		num_str=argv[1];
	}


	n_steps=strtoul(num_str, 0, 10);
	for(i=0;i<n_steps;i++) {
		if (!neg) {
			fprintf(env->io_fd, "step nr %d forward\n",i);
			// forward sequence
			set_coil(coil1b_pin_fd,0);
			set_coil(coil1a_pin_fd,1);
			udelay(ifs);
			set_coil(coil2b_pin_fd,0);
			set_coil(coil2a_pin_fd,1);
			udelay(ifs);
			set_coil(coil1a_pin_fd,0);
			set_coil(coil1b_pin_fd,1);
			udelay(ifs);
			set_coil(coil2a_pin_fd,0);
			set_coil(coil2b_pin_fd,1);
			udelay(ifs);
		} else {
			fprintf(env->io_fd, "step nr %d backward\n",i);
			set_coil(coil2a_pin_fd,0);
			set_coil(coil2b_pin_fd,1);
			udelay(ifs);
			set_coil(coil1a_pin_fd,0);
			set_coil(coil1b_pin_fd,1);
			udelay(ifs);
			set_coil(coil2b_pin_fd,0);
			set_coil(coil2a_pin_fd,1);
			udelay(ifs);
			set_coil(coil1b_pin_fd,0);
			set_coil(coil1a_pin_fd,1);
			udelay(ifs);
		}
	}
	set_coil(coil1a_pin_fd,0);
	set_coil(coil1b_pin_fd,0);
	set_coil(coil2a_pin_fd,0);
	set_coil(coil2b_pin_fd,0);
	fprintf(env->io_fd, "Coils off\n");
	return 0;
}

static int setup_pin(unsigned int pin, unsigned int *pin_fd, int io_fd) {
	unsigned int oflags;

	int rc;


	if (pin_configured) {
		fprintf(io_fd, "init pins: already inited\n");
		return -1;
	}

	(*pin_fd)=io_open(GPIO_DRV);

	if ((*pin_fd)<0) {
		fprintf(io_fd,"init pins: failed to open gpiodrv\n");
		return -1;
	}

	oflags=GPIO_DIR(0,GPIO_OUTPUT);
	oflags=GPIO_DRIVE(oflags,GPIO_PUSHPULL);
	oflags=GPIO_SPEED(oflags,GPIO_SPEED_MEDIUM);

//	cs_pin=GPIO_PIN(GPIO_PC,0xd);  // active lo, also led

	rc=io_control(*pin_fd, GPIO_BIND_PIN, &pin, sizeof(pin));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to bind pin\n");
		return -1;
	}

	rc=io_control(*pin_fd, GPIO_SET_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags on pin\n");
		return -1;
	}

	rc=io_control(*pin_fd, GPIO_SET_PIN, &zero, sizeof(zero));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set val on pin\n");
		return -1;
	}
	return 0;
}

static int setup_pins(int io_fd) {
	int rc=0;
	rc=setup_pin(COIL1A, &coil1a_pin_fd, io_fd);
	if (rc<0) return rc;
	rc=setup_pin(COIL1B, &coil1b_pin_fd, io_fd);
	if (rc<0) return rc;
	rc=setup_pin(COIL2A, &coil2a_pin_fd, io_fd);
	if (rc<0) return rc;
	rc=setup_pin(COIL2B, &coil2b_pin_fd, io_fd);
	if (rc<0) return rc;
	return 0;
}

static int config(int argc, char **argv, struct Env *env) {
	if (argc<2) {
		fprintf(env->io_fd, "arg should be step interframeing in uS (typical 2500-6000\n");
		return -1;
	}

	if (!pin_configured) {
		int rc=setup_pins(env->io_fd);
		if (rc<0) {
			fprintf(env->io_fd, "failed to configure io pins\n");
			return -1;
		}
		pin_configured=1;
	}

	ifs=strtoul(argv[1], 0, 10);
	fprintf(env->io_fd, "setting interframe timing to %d uS\n", ifs);
	return 0;
}

extern int init_pin_test();

//int main(void) {
int init_pkg(void) {
//	struct Env e;
//	e.io_fd=0;
//	init_pins(0,0,&e);
	install_cmd_node(&cmn, root_cmd_node);
	init_pin_test();
	printf("back from thread create\n");
	return 0;
}
