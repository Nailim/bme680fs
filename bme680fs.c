#include <u.h>
#include <libc.h>

#include <ctype.h>

#include <fcall.h>
#include <thread.h>
#include <9p.h>


/* mas definitions */
#define MASK_CTRL_MEAS_MODE 0x03

#define MASK_CTRL_REG_70 	0x08
#define MASK_CTRL_REG_71 	0x1F
#define MASK_CTRL_REG_72 	0x47
#define MASK_CTRL_REG_73 	0x10
#define MASK_CTRL_REG_74 	0xFF
#define MASK_CTRL_REG_75 	0x1D


/* 9p filesystem functions */
typedef struct Devfile Devfile;

void	initfs(char *dirname);

void	fsstart(Srv *);
void	fsread(Req *r);
void	fswrite(Req *r);
void	fsend(Srv *);


/* I2C device functions */
void	openi2cdev(void);
void	initi2cdev(void);
void	deiniti2cdev(void);
void	closei2cdev(void);


/* program logic functions, called from fs functions using I2C functions */
char*	fsreadctl(Req *r);
char*	fswritectl(Req *r);

char*	fsreadtemp(Req *r);
char*	fswritetemp(Req *r);

char*	fsreadpress(Req *r);
char*	fswritepress(Req *r);

char*	fsreadhum(Req *r);
char*	fswritehum(Req *r);

char*	fsreadgas(Req *r);
char*	fswritegas(Req *r);

char*	fsreadall(Req *r);
char*	fswriteall(Req *r);


/* I2C device logic functions, implemented calls and processing */
typedef struct CalibrationData CalibrationData;
typedef struct AllMeasurments AllMeasurments;
typedef struct AllParameters AllParameters;

uchar	bme680getchipid(void);
uchar	bme680softreset(void);
void	bme680resetparameters(void);
void	bme680readcalibrationdata(void);
float	bme680gettemp(void);
float	bme680getpress(void);
float	bme680gethum(void);
void	bme680readall(void);


struct CalibrationData {
	/* data types are not recognizible from the official datasheet */
	/* referenced from BSEC Arduino library - https://github.com/boschsensortec/BSEC-Arduino-library/blob/master/src/bme68x/bme68x.c */

	/* temperature calibration parameters */
	unsigned short par_t1;
	short par_t2;
	char par_t3;

	/* pressure calibration parameters */
	unsigned short par_p1;
	short par_p2;
	char par_p3;
	short par_p4;
	short par_p5;
	char par_p6;
	char par_p7;
	short par_p8;
	short par_p9;
	unsigned char par_p10;

	/* humidity calibration parameters */
	unsigned short par_h1;
	unsigned short par_h2;
	char par_h3;
	char par_h4;
	char par_h5;
	unsigned char par_h6;
	char par_h7;

	/* gas calibration parameters */
	char par_g1;
	short par_g2;
	char par_g3;

	char res_heat_range;
	char res_heat_val;
	char range_sw_error;
};

struct AllMeasurments {
	/* calculated measuirment results */
	float temp;
	float pres;
	float hum;
	float gas;
	long time;
};

struct AllParameters {
	/* parameters controling measurments */
	char filter;
	char temp;
	char pres;
	char hum;
	char gastime;
	short gastemp;
};


struct Devfile {
	char	*name;
	char*	(*fsread)(Req*);
	char*	(*fswrite)(Req*);
	int	mode;
};

Srv fs = {
	.start = fsstart,
	.read = fsread,
	.write = fswrite,
	.end = fsend,
};

Devfile files[] = {
	{ "ctl", fsreadctl, fswritectl, DMEXCL|0666 },
	{ "temp", fsreadtemp, fswritetemp, DMEXCL|0444 },
	{ "press", fsreadpress, fswritepress, DMEXCL|0444 },
	{ "hum", fsreadhum, fswritehum, DMEXCL|0444 },
	{ "gas", fsreadgas, fswritegas, DMEXCL|0444 },
	{ "all", fsreadall, fswriteall, DMEXCL|0444 },
};

const float gas_range_const_array1_f[16] = {
	/* as per the datasheet */
	1.0,
	1.0,
	1.0,
	1.0,
	1.0,
	0.99,
	1.0,
	0.992,
	1.0,
	1.0,
	0.998,
	0.995,
	1.0,
	0.99,
	1.0,
	1.0,
};

const float gas_range_const_array2_f[16] = {
	/* as per the datasheet */
	8000000.0,
	4000000.0,
	2000000.0,
	1000000.0,
	499500.4995,
	248262.1648,
	125000.0,
	63004.03226,
	31281.28128,
	15625.0,
	7812.5,
	3906.25,
	1953.125,
	976.5625,
	488.28125,
	244.140625,
};


static int i2cfd;
static CalibrationData caldat;
static AllMeasurments mesdat;
static AllParameters pardat;


enum
{
	osrs_t,
	osrs_p,
	osrs_h,
	filter,
	gas_wait,
	gas_temp,
	reset,
};

static char *cmds[] = {
	[osrs_t]	= "osrs_t",
	[osrs_p]	= "osrs_p",
	[osrs_h]	= "osrs_h",
	[filter]	= "filter",
	[gas_wait]	= "gas_wait",
	[gas_temp]	= "gas_temp",
	[reset]		= "reset",
	nil
};


void
initfs(char *dirname)
{
	File *devdir;
	int i;

	fs.tree = alloctree(nil, nil, 0555, nil);
	if(fs.tree == nil){
		sysfatal("initfs: alloctree: %r");
	}

	if((devdir = createfile(fs.tree->root, dirname, nil, DMDIR|0555, nil)) == nil){
		sysfatal("initfs: createfile: %s: %r", dirname);
	}

	for(i = 0; i < nelem(files); i++){
		if(createfile(devdir, files[i].name, nil, files[i].mode, files + i) == nil){
			sysfatal("initfs: createfile: %s: %r", files[i].name);
		}
	}
}


void
fsstart(Srv *)
{
	openi2cdev();
	initi2cdev();
}

void
fsread(Req *r)
{
	Devfile *f;

	r->ofcall.count = 0;
	f = r->fid->file->aux;
	respond(r, f->fsread(r));
}

void
fswrite(Req *r)
{
	Devfile *f;

	r->ofcall.count = 0;
	f = r->fid->file->aux;
	respond(r, f->fswrite(r));
}

void
fsend(Srv *)
{
	deiniti2cdev();
	closei2cdev();
}


void
openi2cdev(void)
{
	i2cfd = -1;

	/* default location of bme680 device is 0x77*/
	if(access("/dev/i2c.77.data", 0) != 0){
		if(bind("#J77", "/dev", MBEFORE) < 0){
		    sysfatal("no J77 device");
        }
    }
	i2cfd = open("/dev/i2c.77.data", ORDWR);
	if(i2cfd < 0){
		sysfatal("cannot open i2c.77.data file");
    }
}

void
initi2cdev(void)
{
	uchar res;

	/* check device id */
	res = bme680getchipid();
	if(res != 0x61){
		/* value 0x61 is the id of bme680 device */
		sysfatal("device responded with wrong chip ID for bme680");
	}

	/* perform soft reset */
	res = bme680softreset();
	if(res != 0x0){
		/* value 0x61 is the id of bme680 device */
		sysfatal("bme680 device cannot be reset");
	}

	/* read calibration data */
	bme680readcalibrationdata();

	/* set parameters */
	bme680resetparameters();

	/*
	fprint(1, "par_t1 val: %d\n", caldat.par_t1);
	fprint(1, "par_t2 val: %d\n", caldat.par_t2);
	fprint(1, "par_t3 val: %d\n", caldat.par_t3);

	fprint(1, "par_p1 val: %d\n", caldat.par_p1);
	fprint(1, "par_p2 val: %d\n", caldat.par_p2);
	fprint(1, "par_p3 val: %d\n", caldat.par_p3);
	fprint(1, "par_p4 val: %d\n", caldat.par_p4);
	fprint(1, "par_p5 val: %d\n", caldat.par_p5);
	fprint(1, "par_p6 val: %d\n", caldat.par_p6);
	fprint(1, "par_p7 val: %d\n", caldat.par_p7);
	fprint(1, "par_p8 val: %d\n", caldat.par_p8);
	fprint(1, "par_p9 val: %d\n", caldat.par_p9);
	fprint(1, "par_p10 val: %d\n", caldat.par_p10);

	fprint(1, "par_h1 val: %d\n", caldat.par_h1);
	fprint(1, "par_h2 val: %d\n", caldat.par_h2);
	fprint(1, "par_h3 val: %d\n", caldat.par_h3);
	fprint(1, "par_h4 val: %d\n", caldat.par_h4);
	fprint(1, "par_h5 val: %d\n", caldat.par_h5);
	fprint(1, "par_h6 val: %d\n", caldat.par_h6);
	fprint(1, "par_h7 val: %d\n", caldat.par_h7);

	fprint(1, "par_g1 val: %d\n", caldat.par_g1);
	fprint(1, "par_g2 val: %d\n", caldat.par_g2);
	fprint(1, "par_g3 val: %d\n", caldat.par_g3);
	fprint(1, "res_heat_val val: %d\n", caldat.res_heat_val);
	fprint(1, "res_heat_range val: %d\n", caldat.res_heat_range);
	fprint(1, "ragenge_sw_error val: %d\n", caldat.range_sw_error);
	*/
}

void
deiniti2cdev(void)
{
	/* nothing really to do wuth this device */
	return;
}

void
closei2cdev(void)
{
	close(i2cfd);

	unmount("#J77", "/dev");
}




uchar
bme680getchipid(void)
{
	uchar cmd;
	uchar res;

	cmd = 0xD0;  /* location of id register */

	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &res, 1, 0);

	/* bme680 device id is 0x61 */
	return res;
}

uchar
bme680softreset(void)
{
	uchar cmd[2];
	uchar res;

	cmd[0] = 0xE0;  /* location of reset register */
	cmd[1] = 0xB6;  /* reset command */

	pwrite(i2cfd, &cmd[0], 2, 0);
	sleep(5);
	pread(i2cfd, &res, 1, 0);

	return res;
}

void
bme680resetparameters(void)
{
	/* set default parameters for measurments */
	pardat.filter = 0x00;	/* disable low pass filter */
	pardat.pres = 0x04;		/* 8x oversampling */
	pardat.temp = 0x03;		/* 4x oversampling */
	pardat.hum = 0x03;		/* 4x oversampling */
	pardat.gastime = 0x59;	/* 100ms = 4 * 25ms */
	pardat.gastemp = 300.0;	/* 300 deg C */

	/* init all measurments structure */
	mesdat.temp = 0.0;
	mesdat.pres = 0.0;
	mesdat.hum = 0.0;
	mesdat.gas = 0.0;
	mesdat.time = 0;
}

void
bme680readcalibrationdata(void)
{
	uchar cmd;
	uchar buf;

	/* read temperature calibration data */
	cmd = 0xE9;		/* par_t1 address - 16bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_t1, 2, 0);
	cmd = 0x8A;		/* par_t2 address - 16bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_t2, 2, 0);
	cmd = 0x8C;		/* par_t3 address - 8bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_t3, 1, 0);

	/* read pressure calibration data */
	cmd = 0x8E;		/* par_p1 address - 16bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_p1, 2, 0);
	cmd = 0x90;		/* par_p2 address - 16bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_p2, 2, 0);
	cmd = 0x92;		/* par_p3 address - 8bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_p3, 1, 0);
	cmd = 0x94;		/* par_p4 address - 16bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_p4, 2, 0);
	cmd = 0x96;		/* par_p5 address - 16bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_p5, 2, 0);
	cmd = 0x99;		/* par_p6 address - 8bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_p6, 1, 0);
	cmd = 0x98;		/* par_p7 address - 8bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_p7, 1, 0);
	cmd = 0x9C;		/* par_p8 address - 16bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_p8, 2, 0);
	cmd = 0x9E;		/* par_p9 address - 16bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_p9, 2, 0);
	cmd = 0xA0;		/* par_p10 address - 8bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_p10, 1, 0);

	/* read humidity calibration data */
	/* par_h1 (lsb 4bit <3:0>), par_h2 (lsb 4bit <7:4>) */
	cmd = 0xE2;
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &buf, 1, 0);
	cmd = 0xE3;		/* par_h1 (msb) - 12bit */
	caldat.par_h1 = 0;	/* clear since we read in only 1 byte but the var is 2 bytes long */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_h1, 1, 0);
	caldat.par_h1 = (caldat.par_h1<<4) | ((unsigned short)(buf & 0x0F));	/* msb + lsb */
	cmd = 0xE1;		/* par_h2 (msb) - 12bit */
	caldat.par_h2 = 0;	/* clear since we read in only 1 byte but the var is 2 bytes long */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_h2, 1, 0);
	caldat.par_h2 = (caldat.par_h2<<4) | (((unsigned short)(buf & 0xF0))>>4);	/* msb + lsb */
	cmd = 0xE4;		/* par_h3 address - 8bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_h3, 1, 0);
	cmd = 0xE5;		/* par_h4 address - 8bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_h4, 1, 0);
	cmd = 0xE6;		/* par_h5 address - 8bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_h5, 1, 0);
	cmd = 0xE7;		/* par_h6 address - 8bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_h6, 1, 0);
	cmd = 0xE8;		/* par_h7 address - 8bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_h7, 1, 0);

	/* gas humidity calibration data */
	cmd = 0xED;		/* par_g1 address - 8bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_g1, 1, 0);
	cmd = 0xEB;		/* par_g2 address - 16bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_g2, 2, 0);
	cmd = 0xEE;		/* par_g3 address - 8bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.par_g3, 1, 0);

	cmd = 0x02;		/* res_heat_range (lsb) - 2bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.res_heat_range, 1, 0);
	caldat.res_heat_range = (((char)(caldat.res_heat_range & 0x30))>>4);
	cmd = 0x00;		/* res_heat_val address - 8bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.res_heat_val, 1, 0);
	cmd = 0x04;		/* range_sw_error address - 4bit */
	pwrite(i2cfd, &cmd, 1, 0);
	pread(i2cfd, &caldat.range_sw_error, 1, 0);
	caldat.range_sw_error = (((char)(caldat.range_sw_error & 0xF0))>>4);
}

float
bme680gettemp(void)
{
	int temp_raw;
	float temp_comp;

	float var1;
	float var2;
	float t_fine;

	uchar cmd[2];
	uchar buf[3];
	uchar reg_val[6];	// read regs 0x70-0x75 to ONLY change their value


	/* get sensor control registry state ... */

	/* wait if any measurment is in progress */
	cmd[0] = 0x74;
	do {
		pwrite(i2cfd, &cmd[0], 1, 0);
		pread(i2cfd, &buf[0], 1, 0);
	} while ((buf[0] & MASK_CTRL_MEAS_MODE) > 0);
	/* read control registries */
	cmd[0] = 0x70;
	pwrite(i2cfd, &cmd[0], 1, 0);
	pread(i2cfd, &reg_val[0], 6, 0);


	/* set humidity settings */

	/* 0x72 - ctrl_hum - osrs_h<2:0> - skip humidity measurment */
	cmd[0] = 0x72;
	cmd[1] = (reg_val[2] & ~MASK_CTRL_REG_72) | (MASK_CTRL_REG_72 & 0x00);
	pwrite(i2cfd, &cmd[0], 2, 0);

	/* 0x75 - config - filter<4:2> - low pass filter for temp and pressure */
	cmd[0] = 0x75;
	cmd[1] = (reg_val[5] & ~MASK_CTRL_REG_75) | (MASK_CTRL_REG_75 & (pardat.filter<<2));
	pwrite(i2cfd, &cmd[0], 2, 0);


	/* set gas settings */

	/* 0x71 - ctrl_gas_1 - run_gass<4> - skip gass measurment */
	cmd[0] = 0x71;
	cmd[1] = (reg_val[1] & ~MASK_CTRL_REG_71) | (MASK_CTRL_REG_71 & (0x00<<4));
	pwrite(i2cfd, &cmd[0], 2, 0);


	/* set temperature and pressure settings settings + set forced mode - execute measurment */
	/* 0x74 - ctrl_meas - osrs_t<7:5> osrs_p<4:2> mode<1:0> - set temp, skip pressure measurment, FORCED MODE */
	cmd[0] = 0x74;
	cmd[1] = (reg_val[4] & ~MASK_CTRL_REG_74) | (MASK_CTRL_REG_74 & ((pardat.temp<<5) + (0x00<<2) + 0x01));	
	pwrite(i2cfd, &cmd[0], 2, 0);

	/* wait while measurment is in progress */
	cmd[0] = 0x74;
	do {
		pwrite(i2cfd, &cmd[0], 1, 0);
		pread(i2cfd, &buf[0], 1, 0);
	} while ((buf[0] & MASK_CTRL_MEAS_MODE) > 0);


	/* read temperature from adc */
	cmd[0] = 0x22;
	pwrite(i2cfd, &cmd[0], 1, 0);
	pread(i2cfd, &buf[0], 3, 0);

	/* calculate values */
	temp_raw = ((int)((int)buf[0]<<16) | (int)((int)buf[1]<<8) | (int)buf[2])>>4;

	var1 = (((float)temp_raw / 16384.0f) - ((float)caldat.par_t1 / 1024.0f)) * ((float)caldat.par_t2);
	var2 = ((((float)temp_raw / 131072.0f) - ((float)caldat.par_t1 / 8192.0f)) * 
			(((float)temp_raw / 131072.0f) - ((float)caldat.par_t1 / 8192.0f))) * ((float)caldat.par_t3 * 16.0f);
	t_fine = var1 + var2;

	temp_comp = t_fine / 5120.0;

	return temp_comp;
}

float
bme680getpress(void)
{
	int temp_raw;
	int pres_raw;
	float pres_comp;

	float var1;
	float var2;
	float var3;
	float t_fine;

	uchar cmd[2];
	uchar buf[3];
	uchar reg_val[6];	// read regs 0x70-0x75 to ONLY change their value


	/* get sensor control registry state ... */

	/* wait if any measurment is in progress */
	cmd[0] = 0x74;
	do {
		pwrite(i2cfd, &cmd[0], 1, 0);
		pread(i2cfd, &buf[0], 1, 0);
	} while ((buf[0] & MASK_CTRL_MEAS_MODE) > 0);
	/* read control registries */
	cmd[0] = 0x70;
	pwrite(i2cfd, &cmd[0], 1, 0);
	pread(i2cfd, &reg_val[0], 6, 0);


	/* set humidity settings */

	/* 0x72 - ctrl_hum - osrs_h<2:0> - skip humidity measurment */
	cmd[0] = 0x72;
	cmd[1] = (reg_val[2] & ~MASK_CTRL_REG_72) | (MASK_CTRL_REG_72 & 0x00);
	pwrite(i2cfd, &cmd[0], 2, 0);

	/* 0x75 - config - filter<4:2> - low pass filter for temp and pressure */
	cmd[0] = 0x75;
	cmd[1] = (reg_val[5] & ~MASK_CTRL_REG_75) | (MASK_CTRL_REG_75 & (pardat.filter<<2));
	pwrite(i2cfd, &cmd[0], 2, 0);


	/* set gas settings */

	/* 0x71 - ctrl_gas_1 - run_gass<4> - skip gass measurment */
	cmd[0] = 0x71;
	cmd[1] = (reg_val[1] & ~MASK_CTRL_REG_71) | (MASK_CTRL_REG_71 & (0x00<<4));
	pwrite(i2cfd, &cmd[0], 2, 0);


	/* set temperature and pressure settings settings + set forced mode - execute measurment */
	/* 0x74 - ctrl_meas - osrs_t<7:5> osrs_p<4:2> mode<1:0> - set temp, set pressure, FORCED MODE */
	cmd[0] = 0x74;
	cmd[1] = (reg_val[4] & ~MASK_CTRL_REG_74) | (MASK_CTRL_REG_74 & ((pardat.temp<<5) + (pardat.pres<<2) + 0x01));
	pwrite(i2cfd, &cmd[0], 2, 0);

	/* wait while measurment is in progress */
	cmd[0] = 0x74;
	do {
		pwrite(i2cfd, &cmd[0], 1, 0);
		pread(i2cfd, &buf[0], 1, 0);
	} while ((buf[0] & MASK_CTRL_MEAS_MODE) > 0);


	/* read temperature from adc */
	cmd[0] = 0x22;
	pwrite(i2cfd, &cmd[0], 1, 0);
	pread(i2cfd, &buf[0], 3, 0);

	/* calculate values */
	temp_raw = ((int)((int)buf[0]<<16) | (int)((int)buf[1]<<8) | (int)buf[2])>>4;

	var1 = (((float)temp_raw / 16384.0f) - ((float)caldat.par_t1 / 1024.0f)) * ((float)caldat.par_t2);
	var2 = ((((float)temp_raw / 131072.0f) - ((float)caldat.par_t1 / 8192.0f)) * 
			(((float)temp_raw / 131072.0f) - ((float)caldat.par_t1 / 8192.0f))) * ((float)caldat.par_t3 * 16.0f);
	t_fine = var1 + var2;


	/* read pressure from adc */
	cmd[0] = 0x1F;
	pwrite(i2cfd, &cmd[0], 1, 0);
	pread(i2cfd, &buf[0], 3, 0);

	/* calculate values */
	pres_raw = ((int)((int)buf[0]<<16) | (int)((int)buf[1]<<8) | (int)buf[2])>>4;

	var1 = ((float)t_fine / 2.0f) - 64000.0f;
	var2 = var1 * var1 * ((float)caldat.par_p6 / 131072.0f);
	var2 = var2 + (var1 * (float)caldat.par_p5 * 2.0f);
	var2 = (var2 / 4.0) + ((float)caldat.par_p4 * 65536.0f);
	var1 = ((((float)caldat.par_p3 * var1 * var1) / 16384.0f) + ((float)caldat.par_p2 * var1)) / 524288.0f;
	var1 = (1.0f + (var1 / 32768.0f)) * (float)caldat.par_p1;
	pres_comp = 1048576.0f - (float)pres_raw;
	pres_comp = ((pres_comp - (var2 / 4096.0f)) * 6250.0f) / var1;
	var1 = ((float)caldat.par_p9 * pres_comp * pres_comp) / 2147483648.0f;
	var2 = pres_comp * ((float)caldat.par_p8 / 32768.0f);
	var3 = (pres_comp / 256.0f) * (pres_comp / 256.0f) * (pres_comp / 256.0f) * (caldat.par_p10 / 131072.0f);
	pres_comp = pres_comp + (var1 + var2 + var3 + ((float)caldat.par_p7 * 128.0f)) / 16.0f;

	return pres_comp;
}

float
bme680gethum(void)
{
	int hum_raw;
	int temp_raw;
	float hum_comp;
	float temp_comp;

	float var1;
	float var2;
	float var3;
	float var4;
	float t_fine;

	uchar cmd[2];
	uchar buf[3];
	uchar reg_val[6];	// read regs 0x70-0x75 to ONLY change their value


	/* get sensor control registry state ... */

	/* wait if any measurment is in progress */
	cmd[0] = 0x74;
	do {
		pwrite(i2cfd, &cmd[0], 1, 0);
		pread(i2cfd, &buf[0], 1, 0);
	} while ((buf[0] & MASK_CTRL_MEAS_MODE) > 0);
	/* read control registries */
	cmd[0] = 0x70;
	pwrite(i2cfd, &cmd[0], 1, 0);
	pread(i2cfd, &reg_val[0], 6, 0);


	/* set humidity settings */

	/* 0x72 - ctrl_hum - osrs_h<2:0> - set humidity */
	cmd[0] = 0x72;
	cmd[1] = (reg_val[2] & ~MASK_CTRL_REG_72) | (MASK_CTRL_REG_72 & pardat.hum);
	pwrite(i2cfd, &cmd[0], 2, 0);

	/* 0x75 - config - filter<4:2> - low pass filter for temp and pressure */
	cmd[0] = 0x75;
	cmd[1] = (reg_val[5] & ~MASK_CTRL_REG_75) | (MASK_CTRL_REG_75 & (pardat.filter<<2));
	pwrite(i2cfd, &cmd[0], 2, 0);


	/* set gas settings */

	/* 0x71 - ctrl_gas_1 - run_gass<4> - skip gass measurment */
	cmd[0] = 0x71;
	cmd[1] = (reg_val[1] & ~MASK_CTRL_REG_71) | (MASK_CTRL_REG_71 & (0x00<<4));
	pwrite(i2cfd, &cmd[0], 2, 0);


	/* set temperature and pressure settings settings + set forced mode - execute measurment */
	/* 0x74 - ctrl_meas - osrs_t<7:5> osrs_p<4:2> mode<1:0> - set temp, skip pressure measurment, FORCED MODE */
	cmd[0] = 0x74;
	cmd[1] = (reg_val[4] & ~MASK_CTRL_REG_74) | (MASK_CTRL_REG_74 & ((pardat.temp<<5) + (0x00<<2) + 0x01));
	pwrite(i2cfd, &cmd[0], 2, 0);

	/* wait while measurment is in progress */
	cmd[0] = 0x74;
	do {
		pwrite(i2cfd, &cmd[0], 1, 0);
		pread(i2cfd, &buf[0], 1, 0);
	} while ((buf[0] & MASK_CTRL_MEAS_MODE) > 0);


	/* read temperature from adc */
	cmd[0] = 0x22;
	pwrite(i2cfd, &cmd[0], 1, 0);
	pread(i2cfd, &buf[0], 3, 0);

	/* calculate values */
	temp_raw = ((int)((int)buf[0]<<16) | (int)((int)buf[1]<<8) | (int)buf[2])>>4;

	var1 = (((float)temp_raw / 16384.0f) - ((float)caldat.par_t1 / 1024.0f)) * ((float)caldat.par_t2);
	var2 = ((((float)temp_raw / 131072.0f) - ((float)caldat.par_t1 / 8192.0f)) * 
			(((float)temp_raw / 131072.0f) - ((float)caldat.par_t1 / 8192.0f))) * ((float)caldat.par_t3 * 16.0f);
	t_fine = var1 + var2;

	temp_comp = t_fine / 5120.0;


	/* read humidity from adc */
	cmd[0] = 0x25;
	pwrite(i2cfd, &cmd[0], 1, 0);
	pread(i2cfd, &buf[0], 2, 0);

	/* calculate values */
	hum_raw = ((int)(((int)buf[0])<<8) | (int)buf[1]);

	var1 = (float)((float)hum_raw) - (((float)caldat.par_h1 * 16.0f) + (((float)caldat.par_h3 / 2.0f) * temp_comp));
    var2 = var1 * ((float)(((float)caldat.par_h2 / 262144.0f) * 
			(1.0f + (((float)caldat.par_h4 / 16384.0f) * temp_comp) + (((float)caldat.par_h5 / 1048576.0f) * temp_comp * temp_comp))));
    var3 = (float)caldat.par_h6 / 16384.0f;
    var4 = (float)caldat.par_h7 / 2097152.0f;
    hum_comp = var2 + ((var3 + (var4 * temp_comp)) * var2 * var2);

	return hum_comp;
}

void
bme680readall(void)
{
	/* special feature - to avoid burning up the sensor (ask me how I know) the gas reading will be limited to a certain period only */
	/* too frequent gas measurment can be also called by the fs, since the function is executed everytime the interface file is opened */
	int curtime = time(nil);

	/* so if time difference is less than (what ever is in the if statement - counts as seconds) just return previous values */
	if(curtime - mesdat.time < 5){
		return;
	}


	int temp_raw;
	int pres_raw;
	int hum_raw;
	int gas_raw;

	int gas_range;
	
	float temp_comp;
	float pres_comp;
	float hum_comp;
	float gas_comp;

	float var1;
	float var2;
	float var3;
	float var4;
	float var5;
	float t_fine;

	unsigned char res_heat_x;

	uchar cmd[2];
	uchar buf[3];
	uchar reg_val[6];	// read regs 0x70-0x75 to ONLY change their value


	/* STEP 1 - get temperature for ambient temperature refenrence*/

	/* get sensor control registry state ... */

	/* wait if any measurment is in progress */
	cmd[0] = 0x74;
	do {
		pwrite(i2cfd, &cmd[0], 1, 0);
		pread(i2cfd, &buf[0], 1, 0);
	} while ((buf[0] & MASK_CTRL_MEAS_MODE) > 0);
	/* read control registries */
	cmd[0] = 0x70;
	pwrite(i2cfd, &cmd[0], 1, 0);
	pread(i2cfd, &reg_val[0], 6, 0);


	/* set humidity settings */

	/* 0x72 - ctrl_hum - osrs_h<2:0> - skipp humidity measurment */
	cmd[0] = 0x72;
	cmd[1] = (reg_val[2] & ~MASK_CTRL_REG_72) | (MASK_CTRL_REG_72 & 0x00);
	pwrite(i2cfd, &cmd[0], 2, 0);

	/* 0x75 - config - filter<4:2> - low pass filter for temp and pressure */
	cmd[0] = 0x75;
	cmd[1] = (reg_val[5] & ~MASK_CTRL_REG_75) | (MASK_CTRL_REG_75 & (pardat.filter<<2));
	pwrite(i2cfd, &cmd[0], 2, 0);


	/* set gas settings */

	/* 0x71 - ctrl_gas_1 - run_gass<4> - skip gass measurment */
	cmd[0] = 0x71;
	cmd[1] = (reg_val[1] & ~MASK_CTRL_REG_71) | (MASK_CTRL_REG_71 & (0x00<<4));
	pwrite(i2cfd, &cmd[0], 2, 0);


	/* set temperature and pressure settings settings + set forced mode - execute measurment */
	/* 0x74 - ctrl_meas - osrs_t<7:5> osrs_p<4:2> mode<1:0> - set temp, skip pressure measurment, FORCED MODE */
	cmd[0] = 0x74;
	cmd[1] = (reg_val[4] & ~MASK_CTRL_REG_74) | (MASK_CTRL_REG_74 & ((pardat.temp<<5) + (0x00<<2) + 0x01));	
	pwrite(i2cfd, &cmd[0], 2, 0);

	/* wait while measurment is in progress */
	cmd[0] = 0x74;
	do {
		pwrite(i2cfd, &cmd[0], 1, 0);
		pread(i2cfd, &buf[0], 1, 0);
	} while ((buf[0] & MASK_CTRL_MEAS_MODE) > 0);


	/* read temperature from adc */
	cmd[0] = 0x22;
	pwrite(i2cfd, &cmd[0], 1, 0);
	pread(i2cfd, &buf[0], 3, 0);


	/* calculate values */
	temp_raw = ((int)((int)buf[0]<<16) | (int)((int)buf[1]<<8) | (int)buf[2])>>4;

	var1 = (((float)temp_raw / 16384.0f) - ((float)caldat.par_t1 / 1024.0f)) * ((float)caldat.par_t2);
	var2 = ((((float)temp_raw / 131072.0f) - ((float)caldat.par_t1 / 8192.0f)) * 
			(((float)temp_raw / 131072.0f) - ((float)caldat.par_t1 / 8192.0f))) * ((float)caldat.par_t3 * 16.0f);
	t_fine = var1 + var2;

	temp_comp = t_fine / 5120.0;


	/* STEP 2 - mesure gas with known ambient temperature */


	/* get sensor control registry state ... read control registries */
	cmd[0] = 0x70;
	pwrite(i2cfd, &cmd[0], 1, 0);
	pread(i2cfd, &reg_val[0], 6, 0);


	/* calculate heater resistance*/
	var1 = ((float)caldat.par_g1 / 16.0f) + 49.0f;
	var2 = (((float)caldat.par_g2 / 32768.0f) * 0.0005f) + 0.00235f;
	var3 = (float)caldat.par_g3 / 1024.0f;
	var4 = var1 * (1.0 + (var2 * (float)pardat.gastemp));
	var5 = var4 + (var3 * (float)temp_comp);
	res_heat_x = (unsigned char)(3.4f * ((var5 * (4.0f / (4.0f + (float)caldat.res_heat_range)) * (1.0f/(1.0f + ((float)caldat.res_heat_val * 0.002f)))) - 25));


	/* set humidity settings */

	/* 0x72 - ctrl_hum - osrs_h<2:0> - set hum */
	cmd[0] = 0x72;
	cmd[1] = (reg_val[2] & ~MASK_CTRL_REG_72) | (MASK_CTRL_REG_72 & (pardat.hum));
	pwrite(i2cfd, &cmd[0], 2, 0);

	/* 0x75 - config - filter<4:2> - low pass filter for temp and pressure */
	cmd[0] = 0x75;
	cmd[1] = (reg_val[5] & ~MASK_CTRL_REG_75) | (MASK_CTRL_REG_75 & (pardat.filter<<2));
	pwrite(i2cfd, &cmd[0], 2, 0);


	/* set gas settings - only use one set point used so far */

	/* 0x64 - gas_wait_0 - how long to heat*/
	cmd[0] = 0x64;
	cmd[1] = pardat.gastime;
	pwrite(i2cfd, &cmd[0], 2, 0);
	/* 0x5A - res_heat_0 - how much to heat */
	cmd[0] = 0x5A;
	cmd[1] = res_heat_x;	/* calculated above */
	pwrite(i2cfd, &cmd[0], 2, 0);
	/* 0x71 - ctrl_gas_1 - run_gass<4> nb_conv<3:0> - enable gass measurment, select defined heater settings */
	cmd[0] = 0x71;
	cmd[1] = (reg_val[1] & ~MASK_CTRL_REG_71) | (MASK_CTRL_REG_71 & ((0x01<<4) + 0x00));
	pwrite(i2cfd, &cmd[0], 2, 0);


	/* set temperature and pressure settings settings + set forced mode - execute measurment */
	/* 0x74 - ctrl_meas - osrs_t<7:5> osrs_p<4:2> mode<1:0> - set temp, set pres, FORCED MODE */
	cmd[0] = 0x74;
	cmd[1] = (reg_val[4] & ~MASK_CTRL_REG_74) | (MASK_CTRL_REG_74 & ((pardat.temp<<5) + (pardat.pres<<2) + 0x01));
	pwrite(i2cfd, &cmd[0], 2, 0);


	/* wait while measurment is in progress */
	cmd[0] = 0x74;
	do {
		pwrite(i2cfd, &cmd[0], 1, 0);
		pread(i2cfd, &buf[0], 1, 0);
	} while ((buf[0] & MASK_CTRL_MEAS_MODE) > 0);


	/* read pressure from adc */
	cmd[0] = 0x1F;
	pwrite(i2cfd, &cmd[0], 1, 0);
	pread(i2cfd, &buf[0], 3, 0);

	/* calculate values */
	pres_raw = ((int)((int)buf[0]<<16) | (int)((int)buf[1]<<8) | (int)buf[2])>>4;

	var1 = ((float)t_fine / 2.0f) - 64000.0f;
	var2 = var1 * var1 * ((float)caldat.par_p6 / 131072.0f);
	var2 = var2 + (var1 * (float)caldat.par_p5 * 2.0f);
	var2 = (var2 / 4.0) + ((float)caldat.par_p4 * 65536.0f);
	var1 = ((((float)caldat.par_p3 * var1 * var1) / 16384.0f) + ((float)caldat.par_p2 * var1)) / 524288.0f;
	var1 = (1.0f + (var1 / 32768.0f)) * (float)caldat.par_p1;
	pres_comp = 1048576.0f - (float)pres_raw;
	pres_comp = ((pres_comp - (var2 / 4096.0f)) * 6250.0f) / var1;
	var1 = ((float)caldat.par_p9 * pres_comp * pres_comp) / 2147483648.0f;
	var2 = pres_comp * ((float)caldat.par_p8 / 32768.0f);
	var3 = (pres_comp / 256.0f) * (pres_comp / 256.0f) * (pres_comp / 256.0f) * (caldat.par_p10 / 131072.0f);
	pres_comp = pres_comp + (var1 + var2 + var3 + ((float)caldat.par_p7 * 128.0f)) / 16.0f;


	/* read humidity from adc */
	cmd[0] = 0x25;
	pwrite(i2cfd, &cmd[0], 1, 0);
	pread(i2cfd, &buf[0], 2, 0);

	/* calculate values */
	hum_raw = ((int)(((int)buf[0])<<8) | (int)buf[1]);

	var1 = (float)((float)hum_raw) - (((float)caldat.par_h1 * 16.0f) + (((float)caldat.par_h3 / 2.0f) * temp_comp));
    var2 = var1 * ((float)(((float)caldat.par_h2 / 262144.0f) * 
			(1.0f + (((float)caldat.par_h4 / 16384.0f) * temp_comp) + (((float)caldat.par_h5 / 1048576.0f) * temp_comp * temp_comp))));
    var3 = (float)caldat.par_h6 / 16384.0f;
    var4 = (float)caldat.par_h7 / 2097152.0f;
    hum_comp = var2 + ((var3 + (var4 * temp_comp)) * var2 * var2);


	/* read gas from adc */
	cmd[0] = 0x2A;
	pwrite(i2cfd, &cmd[0], 1, 0);
	pread(i2cfd, &buf[0], 2, 0);

	/* calculate values */
	gas_raw = ((int)((int)buf[0]<<2) | (int)((int)buf[1]>>6));
	gas_range = (int)(buf[1] & 0x0F);

	var1 = (1340.0f + 5.0f * (float)caldat.range_sw_error) * (float)gas_range_const_array1_f[gas_range];
	gas_comp = var1 * (float)gas_range_const_array2_f[gas_range] / ((float)gas_raw - 512.0f + var1);


	mesdat.temp = temp_comp;
	mesdat.pres = pres_comp;
	mesdat.hum = hum_comp;
	mesdat.gas = gas_comp;
	mesdat.time = time(nil);
}




char*
fsreadctl(Req *r)
{
	char out[128];

	snprint(out, sizeof(out), "osrs_t: 0x%02x, osrs_p: 0x%02x, osrs_h: 0x%02x, filter: 0x%02x, gas_wait: 0x%02x, gas_temp %03d\n", 
			pardat.temp, pardat.pres, pardat.hum, pardat.filter, pardat.gastime, pardat.gastemp);

	readstr(r, out);
	return nil;
}

char*
fswritectl(Req *r)
{
	int si, i, cmd;
	char *s;
	long para;

	cmd = -1;

	si = 0;
	s = r->ifcall.data;

	/* clear whitespace before command */
	for(;(si<r->ifcall.count)&&(isspace(*s));){
		si++;
		s++;
	}

	/* search for command string */
	for(i=0; cmds[i]!=nil; i++){
		if(strncmp(cmds[i], s, strlen(cmds[i])) == 0){
			s = s + strlen(cmds[i]);
			cmd = i;
			break;
		}
	}

	/* clear whitespace before parameter */
	for(;(si<r->ifcall.count)&&(isspace(*s));){
		si++;
		s++;
	}

	switch (cmd)
	{
	case osrs_t:
		para = strtol(s, nil, 16);
		pardat.temp = (char)para;
		if(para > 0x05){
			pardat.temp = 0x05;	
		}
		break;
	case osrs_p:
		para = strtol(s, nil, 16);
		pardat.pres = (char)para;
		if(para > 0x05){
			pardat.pres = 0x05;	
		}
		break;
	case osrs_h:
		para = strtol(s, nil, 16);
		pardat.hum = (char)para;
		if(para > 0x05){
			pardat.hum = 0x05;	
		}
		break;
	case filter:
		para = strtol(s, nil, 16);
		pardat.filter = (char)para;
		if(para > 0x07){
			pardat.temp = 0x07;	
		}
		break;
	case gas_wait:
		para = strtol(s, nil, 16);
		pardat.gastime = (char)para;
		break;
	case gas_temp:
		para = strtol(s, nil, 10);
		pardat.gastemp = (short)para;
		if(para > 400){
			pardat.gastemp = 400;	
		}
		break;
	case reset:
		/* no parameters gere */
		/* perform reset - ignore reset status, if we're here if was able to reset on init */
		bme680softreset();
		bme680resetparameters();
		break;

	default:
		break;
	}

	return nil;
}

char*
fsreadtemp(Req *r)
{
	char out[16];
	float temp = bme680gettemp();

	snprint(out, sizeof(out), "%.2f C\n", temp);

	readstr(r, out);
	return nil;
}

char*
fswritetemp(Req *r)
{
	/* no writing to temp */
	USED(r);
	return nil;
}

char*
fsreadpress(Req *r)
{
	char out[16];
	float press = bme680getpress();

	snprint(out, sizeof(out), "%.2f hPa\n", press / 100.0f);

	readstr(r, out);
	return nil;
}

char*
fswritepress(Req *r)
{
	/* no writing to press */
	USED(r);
	return nil;
}

char*
fsreadhum(Req *r)
{
	char out[16];
	float hum = bme680gethum();

	snprint(out, sizeof(out), "%.2f %%r.H.\n", hum);

	readstr(r, out);
	return nil;
}

char*
fswritehum(Req *r)
{
	/* no writing to hum */
	USED(r);
	return nil;
}

char*
fsreadgas(Req *r)
{
	char out[16];
	bme680readall();

	snprint(out, sizeof(out), "%.2f Ohm\n", mesdat.gas);

	readstr(r, out);
	return nil;
}

char*
fswritegas(Req *r)
{
	/* no writing to gas */
	USED(r);
	return nil;
}

char*
fsreadall(Req *r)
{
	char out[128];
	bme680readall();

	snprint(out, sizeof(out), "temp(C):\t%.2f\thum(%%r.H.):\t%.2f\tpres(hPa):\t%.2f\tgas(Ohm):\t%.2f\n", mesdat.temp, mesdat.hum, mesdat.pres / 100.0f, mesdat.gas);

	readstr(r, out);
	return nil;
}

char*
fswriteall(Req *r)
{
	/* no writing to gas */
	USED(r);
	return nil;
}




void
usage(void)
{
	fprint(2, "usage: %s [-m mntpt] [-s srvname]\n", argv0);
	exits("usage");
}


void
threadmain(int argc, char *argv[])
{
	char *srvname, *mntpt;

	srvname = "bme680";
	mntpt = "/mnt";

	ARGBEGIN {
	case 'm':
		mntpt = ARGF();
		break;
	case 's':
		srvname = ARGF();
		break;
	default:
		usage();
	} ARGEND

	initfs(srvname);

	threadpostmountsrv(&fs, srvname, mntpt, MBEFORE);

	threadexits(nil);
}
