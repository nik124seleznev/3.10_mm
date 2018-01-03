#define VL6180_DEBUG
#ifdef VL6180_DEBUG
#define VL6180ADB printk //[LGE_UPDATE][yonghwan.lym@lge.com][2014-12-16] af kernel log enable
#else
#define VL6180ADB(x, ...)
#endif

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>

#include <mach/mt_gpio.h>
#include <mach/mt_pm_ldo.h>
#include <linux/slab.h>


#include <linux/gpio.h>
#include <linux/interrupt.h>

#include "vl6180.h"
#include "vl6180_i2c.h"

#define LASER_I2C_BUSNUM 1
static struct i2c_board_info kd_laser_dev __initdata = { I2C_BOARD_INFO("VL6180", 0x29) };

#define VL6180_DRVNAME "VL6180"
#define VL6180_LASER_WRITE_ID           0x29
#define CAMERA_SLAVE_ID                 (0xA0>>1)

static spinlock_t g_VL6180_SpinLock;
static struct i2c_client *g_pstVL6180_I2Cclient;

static dev_t g_VL6180_devno;
static struct cdev *g_pVL6180_CharDrv;

static struct class *laser_class;
static int g_VL6180_Opened;

#define IDENTIFICATION__MODEL_ID				0x000
#define IDENTIFICATION__REVISION_ID				0x002
#define REVISION_NOT_CALIBRATED					0x020
#define REVISION_CALIBRATED						0x030
#define FIRMWARE__BOOTUP						0x119
#define RESULT__RANGE_STATUS					0x04D
#define GPIO_HV_PAD01__CONFIG					0x132
#define SYSRANGE__MAX_CONVERGENCE_TIME			0x01C
#define SYSRANGE__RANGE_CHECK_ENABLES			0x02D
#define SYSRANGE__MAX_CONVERGENCE_TIME			0x01C
#define SYSRANGE__EARLY_CONVERGENCE_ESTIMATE	0x022
#define SYSTEM__FRESH_OUT_OF_RESET				0x016
#define SYSRANGE__PART_TO_PART_RANGE_OFFSET		0x024
#define SYSRANGE__CROSSTALK_COMPENSATION_RATE	0x01E
#define SYSRANGE__CROSSTALK_VALID_HEIGHT		0x021
#define SYSRANGE__RANGE_IGNORE_VALID_HEIGHT		0x025
#define SYSRANGE__RANGE_IGNORE_THRESHOLD		0x026
#define SYSRANGE__MAX_AMBIENT_LEVEL_MULT		0x02C
#define SYSALS__INTERMEASUREMENT_PERIOD			0x03E
#define SYSRANGE__INTERMEASUREMENT_PERIOD		0x01B
#define SYSRANGE__START							0x018
#define RESULT__RANGE_VAL						0x062
#define RESULT__RANGE_STRAY						0x063
#define RESULT__RANGE_RAW						0x064
#define RESULT__RANGE_RETURN_SIGNAL_COUNT		0x06C
#define RESULT__RANGE_REFERENCE_SIGNAL_COUNT	0x070
#define RESULT__RANGE_RETURN_AMB_COUNT			0x074
#define RESULT__RANGE_REFERENCE_AMB_COUNT		0x078
#define RESULT__RANGE_RETURN_CONV_TIME			0x07C
#define RESULT__RANGE_REFERENCE_CONV_TIME		0x080
#define SYSTEM__INTERRUPT_CLEAR					0x015
#define RESULT__INTERRUPT_STATUS_GPIO			0x04F
#define SYSTEM__MODE_GPIO1						0x011
#define SYSTEM__INTERRUPT_CONFIG_GPIO			0x014
#define RANGE__RANGE_SCALER						0x096
#define SYSRANGE__PART_TO_PART_RANGE_OFFSET		0x024
#define LOW_LIGHT_RETURN_RATE	 				1800
#define HIGH_LIGHT_RETURN_RATE					5000
#define LOW_LIGHT_XTALK_RATIO					100
#define HIGH_LIGHT_XTALK_RATIO	 				35
#define LOW_LIGHT_IGNORETHRES_RATIO				100
#define HIGH_LIGHT_IGNORETHRES_RATIO 	 		28
#define DEFAULT_CROSSTALK     					4 // 12 for ST Glass; 2 for LG Glass
#define DEFAULT_IGNORETHRES 					0 // 32 fior ST Glass; 0 for LG Glass
#define FILTERNBOFSAMPLES						10
#define FILTERSTDDEVSAMPLES						6
#define MINFILTERSTDDEVSAMPLES					3
#define MINFILTERVALIDSTDDEVSAMPLES				4
#define FILTERINVALIDDISTANCE					65535
#define IT_EEP_REG 								0x800
#define FJ_EEP_REG 								0x8B0
#define COMPLEX_FILTER

uint32_t measurementIndex = 0;
uint32_t defaultZeroVal = 0;
uint32_t defaultAvgVal = 0;
uint32_t noDelayZeroVal = 0;
uint32_t noDelayAvgVal = 0;
uint32_t previousAvgDiff = 0;
uint16_t lastTrueRange[FILTERNBOFSAMPLES] = {0,0,0,0,0,0,0,0,0,0};
uint32_t lastReturnRates[FILTERNBOFSAMPLES] = {0,0,0,0,0,0,0,0,0,0};
uint32_t previousRangeStdDev = 0;
uint32_t previousStdDevLimit = 0;
uint32_t previousReturnRateStdDev = 0;
uint16_t stdFilteredReads = 0;
uint32_t m_chipid = 0;
uint16_t lastMeasurements[8] = {0,0,0,0,0,0,0,0};
uint16_t averageOnXSamples = 4;
uint16_t currentIndex = 0;

void BabyBear_ParameterOptimization(uint32_t ambientRate);
uint32_t BabyBear_damper(uint32_t inData, uint32_t ambientRate, uint32_t LowLightRatio, uint32_t HighLightRatio);

#ifdef COMPLEX_FILTER
void VL6180_InitComplexFilter(void);
uint16_t VL6180_ComplexFilter(uint16_t m_trueRange_mm, uint16_t rawRange, uint32_t m_rtnSignalRate, uint32_t m_rtnAmbientRate, uint16_t errorCode);
uint32_t VL6180_StdDevDamper(uint32_t AmbientRate, uint32_t SignalRate, uint32_t StdDevLimitLowLight, uint32_t StdDevLimitLowLightSNR, uint32_t StdDevLimitHighLight, uint32_t StdDevLimitHighLightSNR);
#else
void VL6180_InitLiteFilter(void);
uint16_t VL6180_LiteFilter(uint16_t m_trueRange_mm, uint16_t rawRange, uint32_t m_rtnSignalRate, uint32_t m_rtnAmbientRate, uint16_t errorCode);
#endif

static struct vl6180_proxy_ctrl *proxy_ctrl;

static int proxy_get_count = 0;

int vl6180_enable_gpio(int en)
{
	int ret = 0;

	if(en) {
#if 1
		VL6180ADB("[VL6180] %s:%d // GPIO19(%d) GPIO9(%d) GPIO_MODE_00(%d))\n",
		__func__, __LINE__, GPIO19, GPIO9,GPIO_MODE_00);

		if (ret = mt_set_gpio_mode(GPIO_LDAF_EN,GPIO_LDAF_EN_M_GPIO))
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio mode failed!! ret(%d)\n", __func__, __LINE__, ret);}
		else
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio mode success!! ret(%d)\n", __func__, __LINE__, ret);}

	    if (ret = mt_set_gpio_dir(GPIO_LDAF_EN,GPIO_DIR_OUT))
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio dir failed!! ret(%d)\n", __func__, __LINE__, ret);}
		else
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio mode success!! ret(%d)\n", __func__, __LINE__, ret);}
	    if (ret = mt_set_gpio_out(GPIO_LDAF_EN,GPIO_OUT_ZERO))
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio failed!! ret(%d)\n", __func__, __LINE__, ret);}
		else
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio mode success!! ret(%d)\n", __func__, __LINE__, ret);}
	    mdelay(2);
	    if (ret = mt_set_gpio_out(GPIO_LDAF_EN,GPIO_OUT_ONE))
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio failed!! ret(%d)\n", __func__, __LINE__, ret);}
		else
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio mode success!! ret(%d)\n", __func__, __LINE__, ret);}
	    mdelay(5);
#endif


	} else {
		VL6180ADB("[VL6180] %s:%d // GPIO19(%d) GPIO9(%d) GPIO_MODE_00(%d))\n",
		__func__, __LINE__, GPIO19, GPIO9,GPIO_MODE_00);

		if (ret = mt_set_gpio_mode(GPIO_LDAF_EN,GPIO_LDAF_EN_M_GPIO))
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio mode failed!! ret(%d)\n", __func__, __LINE__, ret);}
		else
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio mode success!! ret(%d)\n", __func__, __LINE__, ret);}

	    if (ret = mt_set_gpio_dir(GPIO_LDAF_EN,GPIO_DIR_OUT))
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio dir failed!! ret(%d)\n", __func__, __LINE__, ret);}
		else
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio mode success!! ret(%d)\n", __func__, __LINE__, ret);}
	    if (ret = mt_set_gpio_out(GPIO_LDAF_EN,GPIO_OUT_ONE))
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio failed!! ret(%d)\n", __func__, __LINE__, ret);}
		else
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio mode success!! ret(%d)\n", __func__, __LINE__, ret);}
	    mdelay(2);
	    if (ret = mt_set_gpio_out(GPIO_LDAF_EN,GPIO_OUT_ZERO))
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio failed!! ret(%d)\n", __func__, __LINE__, ret);}
		else
			{VL6180ADB("[6180]%s:%d[CAMERA SENSOR] set gpio mode success!! ret(%d)\n", __func__, __LINE__, ret);}
	    mdelay(5);

	}

	mdelay(2);
	return 0;
}

int32_t vl6180_i2c_read(const char *buf, int count)
{
	int ret;
	struct i2c_adapter *adap = g_pstVL6180_I2Cclient->adapter;
	struct i2c_msg msgs[] = {
		{
			.addr	= VL6180_LASER_WRITE_ID, /* client->addr, 0x50<<1 */
			.flags	= 0,
			.len	= 2,
			.buf	= buf,
		}, {
			.addr	= VL6180_LASER_WRITE_ID, /* client->addr, */
			.flags	= I2C_M_RD,
			.len	= count,
			.buf	= buf,
		},
	};
	ret = i2c_transfer(adap, msgs, ARRAY_SIZE(msgs));
#if 0 // QMC, Liger
	if (ret != count) {
#else // mediatek
	if (ret != 2) {
#endif
		pr_err("vl6180_i2c_read error!! ret(%d) count(%d)\n", ret, count);
		proxy_ctrl->i2c_fail_cnt++;
		return -1;//EREMOTEIO;
	}
    return 1;
}

int32_t vl6180_i2c_write(const char *buf, int count)
{
	int ret;
	struct i2c_adapter *adap = g_pstVL6180_I2Cclient->adapter;

#if 0 // QMC/LG-AP code
	struct i2c_msg msg;

	msg.addr = LASER_SLAVE_ID;
	msg.flags = proxy_ctrl->client->flags & I2C_M_TEN;
	msg.len = count;
	msg.buf = (char *)buf;
#else
	struct i2c_msg msg = {
							.addr = VL6180_LASER_WRITE_ID,
							.flags = proxy_ctrl->client->flags & I2C_M_TEN,
							.buf = (char *)buf,
							 .len = count
						  };
#endif

	ret = i2c_transfer(adap, &msg, 1);
	if(ret < 0) {
		proxy_ctrl->i2c_fail_cnt++;
	}

	if (ret < 0) {
		pr_err("vl6180_camera_i2c_write error!!\n");
		proxy_ctrl->i2c_fail_cnt++;
		return -EREMOTEIO;
	}

	return (ret == 1) ? count : ret;
}

int32_t vl6180_camera_i2c_read(const char *buf, int count)
{
	int ret;
	struct i2c_adapter *adap = proxy_ctrl->client->adapter;
	struct i2c_msg msgs[] = {
		{
			.addr	= CAMERA_SLAVE_ID, /* client->addr, 0x50<<1 */
			.flags	= 0,
			.len	= 2,
			.buf	= buf,
		}, {
			.addr	= CAMERA_SLAVE_ID, /* client->addr, */
			.flags	= I2C_M_RD,
			.len	= count,
			.buf	= buf,
		},
	};
    int result = 0;
	result = i2c_transfer(adap, msgs, ARRAY_SIZE(msgs));

	if (result != count) {
		pr_err("vl6180_camera_i2c_read error!!\n");
		proxy_ctrl->i2c_fail_cnt++;
		return -EREMOTEIO;
	}
	return 1;
}

int32_t vl6180_camera_i2c_write(const char *buf, int count)
{
	int ret;
	struct i2c_adapter *adap = proxy_ctrl->client->adapter;
	struct i2c_msg msgs[] = {
		{
			.addr	= CAMERA_SLAVE_ID, /* client->addr, 0x50<<1 */
			.flags	= 0,
			.len	= count,
			.buf	= buf,
		},
	};
	ret = i2c_transfer(adap, msgs, 1);

	if (ret < 0) {
		pr_err("vl6180_camera_i2c_write error!!\n");
		proxy_ctrl->i2c_fail_cnt++;
		return -EREMOTEIO;
	}

	return (ret == 1) ? count : ret;
}


int32_t proxy_i2c_read(uint32_t addr, uint16_t *data, enum vl6180_ldaf_i2c_data_type data_type)
{
	int32_t ret = 0;
	if (data_type==1)
		ProxyRead8bit(addr, data);
	else if (data_type==2)
		ProxyRead16bit(addr, data);
	return ret;
}
int32_t proxy_i2c_write(uint32_t addr, uint16_t data, enum vl6180_ldaf_i2c_data_type data_type)
{
	int32_t ret = 0;
	ProxyWrite8bit(addr, data);
	return ret;
}

int32_t proxy_i2c_write_seq(uint32_t addr, uint8_t *data, uint16_t num_byte)
{
	int32_t ret = 0;
#if 0
	ret = proxy_ctrl->i2c_client.i2c_func_tbl->i2c_write_seq(&proxy_ctrl->i2c_client, addr, data, num_byte);

	if(ret < 0) {
		proxy_ctrl->i2c_fail_cnt++;
		pr_err("i2c_fail_cnt = %d\n", proxy_ctrl->i2c_fail_cnt);
	}
#endif
	return ret;
}

int32_t proxy_i2c_read_seq(uint32_t addr, uint8_t *data, uint16_t num_byte)
{
	int32_t ret = 0;
#if 0
	ret = proxy_ctrl->i2c_client.i2c_func_tbl->i2c_read_seq(&proxy_ctrl->i2c_client, addr, &data[0], num_byte);

	if(ret < 0){
		proxy_ctrl->i2c_fail_cnt++;
		pr_err("i2c_fail_cnt = %d\n", proxy_ctrl->i2c_fail_cnt);
	}
#endif
	return ret;
}

int32_t proxy_i2c_e2p_write(uint16_t addr, uint16_t data, enum vl6180_ldaf_i2c_data_type data_type)
{
	int32_t ret = 0;
	char buf[4] = {0};

	buf[0] = addr >> 8;
	buf[1] = addr & 0xFF;
	buf[2] = data >> 8;
	buf[3] = data & 0xFF;

	vl6180_camera_i2c_write(buf, 4);

	return ret;
}

int32_t proxy_i2c_e2p_read(uint16_t addr, uint16_t *data, enum vl6180_ldaf_i2c_data_type data_type)
{
	int32_t ret = 0;
#if 0 // Enable Later
	struct vl6180_camera_i2c_client *proxy_i2c_client = NULL;
	proxy_i2c_client = &proxy_ctrl->i2c_client;

	proxy_i2c_client->cci_client->sid = 0xA0 >> 1;
	ret = proxy_i2c_client->i2c_func_tbl->i2c_read(proxy_i2c_client, addr, data, data_type);
	proxy_i2c_client->cci_client->sid = proxy_ctrl->sid_proxy;
#else
	CameraRead16bit(addr,data);
#endif
	return ret;
}

int16_t OffsetCalibration(void)
{
	int timeOut = 0;
	int i = 0;
	int measuredDistance = 0;
	int realDistance = 200;
	int8_t measuredOffset = 0;
	uint16_t chipidRangeStart = 0;
	uint16_t statusCode = 0;
	uint16_t distance = 0;

	pr_err("OffsetCalibration start!\n");

	proxy_i2c_write( SYSRANGE__PART_TO_PART_RANGE_OFFSET,0, 1);
	proxy_i2c_write( SYSRANGE__CROSSTALK_COMPENSATION_RATE, 0, 1);
	proxy_i2c_write( SYSRANGE__CROSSTALK_COMPENSATION_RATE+1, 0, 1);

	for(i = 0; i < 10; i++){
	     proxy_i2c_write( SYSRANGE__START, 1, 1);
	     timeOut = 0;
	     do{
		 	proxy_i2c_read( SYSRANGE__START, &chipidRangeStart, 1);
	        proxy_i2c_read( RESULT__RANGE_STATUS, &statusCode, 1);

            timeOut += 1;
            if(timeOut>2000)
				return -1;

	      }
		 while(!(((statusCode&0x01) == 0x01)&&(chipidRangeStart == 0x00)));
	     proxy_i2c_read( RESULT__RANGE_VAL, &distance, 1);
	     distance *= 3;
	     measuredDistance = measuredDistance + distance;
	}
	measuredDistance = measuredDistance / 10;
	measuredOffset = (realDistance - measuredDistance) / 3;

	pr_err("OffsetCalibration end!\n");

    return measuredOffset;
}

uint16_t proxy_get_from_sensor(void)
{
	uint16_t dist = 0;
	uint16_t chipidInter = 0;
	int i = 0;
	int useAveraging = 0; // 1= do rolling averaring; 0 = no rolling averaging
	int nbOfValidData = 0;
	int minValidData = 4;
	uint16_t distAcc = 0;
	uint16_t newDistance = 0;
	uint16_t chipidCount = 0;
	uint32_t rawRange = 0;
	uint32_t m_rtnConvTime = 0;
	uint32_t m_rtnSignalRate = 0;
	uint32_t m_rtnAmbientRate = 0;
	uint32_t m_rtnSignalCount = 0;
	uint32_t m_refSignalCount = 0;
	uint32_t m_rtnAmbientCount = 0;
	uint32_t m_refAmbientCount = 0;
	uint32_t m_refConvTime = 0;
	uint32_t m_refSignalRate = 0;
	uint32_t m_refAmbientRate = 0;
	uint32_t cRtnSignalCountMax = 0x7FFFFFFF;
	uint32_t cDllPeriods = 6;
	uint32_t rtnSignalCountUInt = 0;
	uint32_t calcConvTime = 0;
	uint16_t chipidRangeStart = 0;
	uint16_t statusCode = 0;
	uint16_t errorCode = 0;

	proxy_i2c_read( SYSRANGE__START, &chipidRangeStart, 1);
	proxy_i2c_read( RESULT__RANGE_STATUS, &statusCode, 1);
	errorCode = statusCode >> 4;
	proxy_i2c_read( RESULT__INTERRUPT_STATUS_GPIO, &chipidInter, 1);
	proxy_i2c_read( RESULT__RANGE_VAL, &dist, 1);
	dist *= 3;
	proxy_i2c_read( RESULT__RANGE_RAW, &chipidCount,1);

	rawRange = (uint32_t)chipidCount;

	ProxyRead32bit(RESULT__RANGE_RETURN_SIGNAL_COUNT, &rtnSignalCountUInt);

	if(rtnSignalCountUInt > cRtnSignalCountMax)
		rtnSignalCountUInt = 0;

	m_rtnSignalCount = rtnSignalCountUInt;

	ProxyRead32bit(RESULT__RANGE_REFERENCE_SIGNAL_COUNT, &m_refSignalCount);
	ProxyRead32bit(RESULT__RANGE_RETURN_AMB_COUNT, &m_rtnAmbientCount);
	ProxyRead32bit(RESULT__RANGE_REFERENCE_AMB_COUNT, &m_refAmbientCount);
	ProxyRead32bit(RESULT__RANGE_RETURN_CONV_TIME, &m_rtnConvTime);
	ProxyRead32bit(RESULT__RANGE_REFERENCE_CONV_TIME, &m_refConvTime);

	calcConvTime = m_refConvTime;
	if (m_rtnConvTime > m_refConvTime)
		calcConvTime = m_rtnConvTime;
	if(calcConvTime == 0)
		calcConvTime = 63000;

	m_rtnSignalRate  = (m_rtnSignalCount*1000)/calcConvTime;
	m_refSignalRate  = (m_refSignalCount*1000)/calcConvTime;
	m_rtnAmbientRate = (m_rtnAmbientCount * cDllPeriods*1000)/calcConvTime;
	m_refAmbientRate = (m_rtnAmbientCount * cDllPeriods*1000)/calcConvTime;

	BabyBear_ParameterOptimization((uint32_t) m_rtnAmbientRate);

	if(((statusCode&0x01) == 0x01)&&(chipidRangeStart == 0x00)) {
		if(useAveraging == 1) {
			for(i = 0; i < 7; i++) {
				lastMeasurements[i] = lastMeasurements[i+1];
			}
			if(rawRange != 255)
				lastMeasurements[7] = dist;
			else
				lastMeasurements[7] = 65535;

			if(currentIndex<8) {
				minValidData = (currentIndex+1)/2;
				currentIndex++;
			}
			else
				minValidData = 4;

			nbOfValidData = 0;
			distAcc = 0;
			newDistance = 255*3; // Max distance, equivalent as when no target
			for(i = 7; i >= 0; i--) {
				if(lastMeasurements[i] != 65535) {
					// This measurement is valid
					nbOfValidData = nbOfValidData+1;
					distAcc = distAcc + lastMeasurements[i];
					if(nbOfValidData >= minValidData) {
						newDistance = distAcc/nbOfValidData;
						break;
					}
				}
			}
			// Copy the new distance
			dist = newDistance;
		}

		#ifdef COMPLEX_FILTER
				dist = VL6180_ComplexFilter(dist, rawRange*3, m_rtnSignalRate, m_rtnAmbientRate, errorCode);
		#else
				dist = VL6180_LiteFilter(dist, rawRange*3, m_rtnSignalRate, m_rtnAmbientRate, errorCode);
		#endif

		// Start new measurement
		proxy_i2c_write( SYSRANGE__START, 0x01, 1);
		m_chipid = dist;

	}

	else
	   dist = m_chipid;

	mutex_lock(&proxy_ctrl->proxy_lock);
	proxy_ctrl->proxy_info->proxy_val[1] = proxy_ctrl->proxy_info->proxy_val[0];
	proxy_ctrl->proxy_info->proxy_val[0] = dist;
	proxy_ctrl->proxy_info->proxy_val[2] = (proxy_ctrl->proxy_info->proxy_val[0] + proxy_ctrl->proxy_info->proxy_val[1]) / 2;

	proxy_ctrl->proxy_info->proxy_conv = calcConvTime;
	proxy_ctrl->proxy_info->proxy_sig = m_rtnSignalRate;
	proxy_ctrl->proxy_info->proxy_amb = m_rtnAmbientRate;
	proxy_ctrl->proxy_info->proxy_raw = rawRange*3;
	mutex_unlock(&proxy_ctrl->proxy_lock);

#if 1
	//pr_err("%s: [LDAF_DEBUG] proxy_value = [%d/%d][%d]\n", __func__, proxy_ctrl->proxy_info->proxy_val[0], proxy_ctrl->proxy_info->proxy_val[1], proxy_ctrl->proxy_info->proxy_val[2]);
	//pr_err("%s: [LDAF_DEBUG] proxy_conv = %d\n", __func__, proxy_ctrl->proxy_info->proxy_conv);
	//pr_err("%s: [LDAF_DEBUG] proxy_sig = %d\n", __func__, proxy_ctrl->proxy_info->proxy_sig);
	//pr_err("%s: [LDAF_DEBUG] proxy_amb = %d\n", __func__, proxy_ctrl->proxy_info->proxy_amb);
	//pr_err("%s: [LDAF_DEBUG] proxy_raw = %d\n", __func__, proxy_ctrl->proxy_info->proxy_raw);
#endif

	return dist;

}

static void get_proxy(struct work_struct *work)
{
	uint16_t * proxy = &proxy_ctrl->last_proxy;
	int16_t offset = 0;
	int16_t calCount = 0;
	int16_t finVal = 0;
	uint16_t moduleId = 0;
	uint8_t shiftModuleId = 0;

	while(1) {
		if(!proxy_ctrl->pause_workqueue) {

			if(proxy_ctrl->proxy_cal) {
				mutex_lock(&proxy_ctrl->proxy_lock);
				proxy_ctrl->proxy_info->cal_done = 0;  //cal done
				mutex_unlock(&proxy_ctrl->proxy_lock);
				offset = OffsetCalibration();
				pr_err("write offset = %x to eeprom\n", offset);

				proxy_i2c_e2p_read(0x700, &moduleId, 2);
				shiftModuleId = moduleId >> 8;

				if ((offset < 11) && (offset > (-21))) {
					if((shiftModuleId == 0x01) || (shiftModuleId == 0x02)) {
						proxy_i2c_e2p_read(IT_EEP_REG, &finVal, 2);
						calCount = finVal >> 8;

						calCount++;
						finVal = (calCount << 8) | (0x00FF & offset);
						proxy_i2c_e2p_write(IT_EEP_REG, finVal, 2);

						pr_err("KSY read inot cal count = %x to eeprom\n", finVal);
					}
					else if(shiftModuleId == 0x03) {
						proxy_i2c_e2p_read(FJ_EEP_REG, &finVal, 2);
						calCount = finVal >> 8;

						calCount++;
						finVal = (calCount << 8) | (0x00FF & offset);
						proxy_i2c_e2p_write(FJ_EEP_REG, finVal, 2);

						pr_err("KSY read fj cal count = %x to eeprom\n", finVal);
					}
					else {
						proxy_i2c_e2p_read(IT_EEP_REG, &finVal, 2);
						calCount = finVal >> 8;

						calCount++;
						finVal = (calCount << 8) | (0x00FF & offset);
						proxy_i2c_e2p_write(IT_EEP_REG, finVal, 2);

						pr_err("KSY read default (inot) cal count = %x to eeprom\n", finVal);
					}

					mutex_lock(&proxy_ctrl->proxy_lock);
					proxy_ctrl->proxy_info->cal_count = calCount;
					proxy_ctrl->proxy_cal = 0;
					proxy_ctrl->proxy_info->cal_done = 1;  //cal done
					proxy_ctrl->proxy_cal = 0;
					mutex_unlock(&proxy_ctrl->proxy_lock);
				}
				else{   // Calibration failed by spec out
					mutex_lock(&proxy_ctrl->proxy_lock);
					proxy_ctrl->proxy_info->cal_done = 2;  //cal fail
					proxy_ctrl->proxy_cal = 0;
					mutex_unlock(&proxy_ctrl->proxy_lock);
				}
			}

			*proxy = proxy_get_from_sensor();
		}
		if(proxy_ctrl->i2c_fail_cnt >= proxy_ctrl->max_i2c_fail_thres) {
			pr_err("proxy workqueue force end due to i2c fail!\n");
			break;
		}
		msleep(10);
		if(proxy_ctrl->exit_workqueue)
			break;
	}
	pr_err("end workqueue!\n");
}
int16_t stop_proxy(void)
{
	pr_err("stop_proxy!\n");
	if(proxy_ctrl->exit_workqueue == 0) {
		if(proxy_ctrl->wq_init_success) {
			proxy_ctrl->exit_workqueue = 1;
			destroy_workqueue(proxy_ctrl->work_thread);
			proxy_ctrl->work_thread = NULL;
			pr_err("destroy_workqueue!\n");
		}
	}
	return 0;
}

void vl6180_proxy_stop_by_pd_off()
{
	uint32_t rc;

	pr_err("%s\n", __func__);

	if(!proxy_ctrl || proxy_ctrl->exit_workqueue) {
		return;
	}

	rc = stop_proxy();
	if(rc) {
		pr_err("failed to stop proxy");
	}

	rc = vl6180_enable_gpio(false);
	if(rc) {
		pr_err("failed to disable gpio");
	}
}

int16_t pause_proxy(void)
{
	pr_err("pause_proxy!\n");
	proxy_ctrl->pause_workqueue = 1;
	pr_err("pause_workqueue = %d\n", proxy_ctrl->pause_workqueue);
	return 0;
}
int16_t restart_proxy(void)
{
	pr_err("restart_proxy!\n");
	proxy_ctrl->pause_workqueue = 0;
	pr_err("pause_workqueue = %d\n", proxy_ctrl->pause_workqueue);
	return 0;
}
uint16_t vl6180_proxy_thread_start(void)
{
	pr_err("vl6180_proxy_thread_start, proxy_ctrl->exit_workqueue = %d\n", proxy_ctrl->exit_workqueue);

	if(proxy_ctrl->exit_workqueue) {
		proxy_ctrl->exit_workqueue = 0;
		proxy_ctrl->work_thread = create_singlethread_workqueue("my_work_thread");
		if(!proxy_ctrl->work_thread) {
			pr_err("creating work_thread fail!\n");
			return 1;
		}

		proxy_ctrl->wq_init_success = 1;

		INIT_WORK(&proxy_ctrl->proxy_work, get_proxy);
		pr_err("INIT_WORK done!\n");

		queue_work(proxy_ctrl->work_thread, &proxy_ctrl->proxy_work);
		pr_err("queue_work done!\n");
	}
	return 0;
}
uint16_t vl6180_proxy_thread_end(void)
{
	uint16_t ret = 0;
	pr_err("vl6180_proxy_thread_end\n");
	ret = stop_proxy();
	return ret;
}
uint16_t vl6180_proxy_thread_pause(void)
{
	uint16_t ret = 0;
	pr_err("vl6180_proxy_thread_pause\n");
	ret = pause_proxy();
	return ret;
}
uint16_t vl6180_proxy_thread_restart(void)
{
	uint16_t ret = 0;
	pr_err("vl6180_proxy_thread_restart\n");
	proxy_ctrl->i2c_fail_cnt = 0;
	ret = restart_proxy();
	return ret;
}
uint16_t vl6180_proxy_cal(void)
{
	uint16_t ret = 0;
	pr_err("vl6180_proxy_cal\n");
	proxy_ctrl->proxy_cal = 1;
	return ret;
}

int vl6180_init_proxy()
{
	pr_err("%s: called successful!!!\n", __func__);
#if 0 // Liger
	proxy_client = v4l2_get_subdevdata(sd);
#else // MTK
    proxy_ctrl->client = g_pstVL6180_I2Cclient;
#endif

	int rc = 0;
	int i = 0;
	uint8_t byteArray[4] = {0,0,0,0};
	int8_t offsetByte = 0;
	int16_t finVal = 0;
	uint8_t calCount = 0;
	uint16_t modelID = 0;
	uint16_t revID = 0;
	uint16_t chipidRange = 0;
	uint16_t chipidRangeMax = 0;
	uint16_t chipidgpio = 0;
	uint32_t shift = 0;
	uint32_t dataMask = 0;
	uint16_t readI2C = 0x0;
	uint32_t ninepointseven = 0;
	uint16_t crosstalkHeight = 0;
	uint16_t ignoreThreshold = 0;
	uint16_t ignoreThresholdHeight = 0;
	uint16_t proxyStatus = 0;
	uint16_t proxyFatal = 0;
	uint16_t dataByte = 0;
	uint16_t ambpart2partCalib1 = 0;
	uint16_t ambpart2partCalib2 = 0;
	uint16_t moduleId = 0;
	uint8_t shiftModuleId = 0;

	pr_err("vl6180_init_proxy ENTER!\n");

	proxy_i2c_read( RESULT__RANGE_STATUS, &proxyStatus, 1);
	proxy_i2c_read( 0x290, &proxyFatal, 1);

	if((proxyStatus & 0x01) && ((proxyStatus>>4) == 0) && (proxyFatal == 0))
		pr_err("init proxy alive!\n");

	else {
		pr_err("init proxy fail!, no proxy sensor found!\n");
		return -1;
	}

	proxy_i2c_read( IDENTIFICATION__MODEL_ID, &modelID, 1);
	proxy_i2c_read( IDENTIFICATION__REVISION_ID, &revID, 1);
	pr_err("Model ID : 0x%X, REVISION ID : 0x%X\n", modelID, revID);   //if revID == 2;(not calibrated), revID == 3 (calibrated)
	if(revID != REVISION_CALIBRATED) {
		pr_err("not calibrated!\n");
		//return -1;
	}

	//waitForStandby
	for(i = 0; i < 100; i++) {
	proxy_i2c_read( FIRMWARE__BOOTUP, &modelID, 1);
		if( (modelID & 0x01) == 1 ) {
			i = 100;
		}
	}
	//range device ready
	for(i = 0; i < 100; i++) {
		proxy_i2c_read( RESULT__RANGE_STATUS, &modelID, 1);
		if( (modelID & 0x01) == 1) {
			i = 100;
			}
	}
	//performRegisterTuningCut1_1
	proxy_i2c_write( GPIO_HV_PAD01__CONFIG, 0x30, 1);
	proxy_i2c_write( 0x0207, 0x01, 1);
	proxy_i2c_write( 0x0208, 0x01, 1);
	proxy_i2c_write( 0x0133, 0x01, 1);
	proxy_i2c_write( 0x0096, 0x00, 1);
	proxy_i2c_write( 0x0097, 0xFD, 1);
	proxy_i2c_write( 0x00e3, 0x00, 1);
	proxy_i2c_write( 0x00e4, 0x04, 1);
	proxy_i2c_write( 0x00e5, 0x02, 1);
	proxy_i2c_write( 0x00e6, 0x01, 1);
	proxy_i2c_write( 0x00e7, 0x03, 1);
	proxy_i2c_write( 0x00f5, 0x02, 1);
	proxy_i2c_write( 0x00D9, 0x05, 1);

	// AMB P2P calibration
	proxy_i2c_read(SYSTEM__FRESH_OUT_OF_RESET, &dataByte, 1);
	if(dataByte == 0x01) {
		proxy_i2c_read( 0x26, &dataByte, 1);
		ambpart2partCalib1 = dataByte<<8;
		proxy_i2c_read( 0x27, &dataByte, 1);
		ambpart2partCalib1 = ambpart2partCalib1 + dataByte;
		proxy_i2c_read( 0x28, &dataByte, 1);
		ambpart2partCalib2 = dataByte<<8;
		proxy_i2c_read( 0x29, &dataByte, 1);
		ambpart2partCalib2 = ambpart2partCalib2 + dataByte;
		if(ambpart2partCalib1 != 0) {
			// p2p calibrated
			proxy_i2c_write( 0xDA, (ambpart2partCalib1>>8)&0xFF, 1);
			proxy_i2c_write( 0xDB, ambpart2partCalib1&0xFF, 1);
			proxy_i2c_write( 0xDC, (ambpart2partCalib2>>8)&0xFF, 1);
			proxy_i2c_write( 0xDD, ambpart2partCalib2&0xFF, 1);
		}
		else {
			// No p2p Calibration, use default settings
			proxy_i2c_write( 0xDB, 0xCE, 1);
			proxy_i2c_write( 0xDC, 0x03, 1);
			proxy_i2c_write( 0xDD, 0xF8, 1);
		}
	}
	proxy_i2c_write( 0x009f, 0x00, 1);
	proxy_i2c_write( 0x00a3, 0x3c, 1);
	proxy_i2c_write( 0x00b7, 0x00, 1);
	proxy_i2c_write( 0x00bb, 0x3c, 1);
	proxy_i2c_write( 0x00b2, 0x09, 1);
	proxy_i2c_write( 0x00ca, 0x09, 1);
	proxy_i2c_write( 0x0198, 0x01, 1);
	proxy_i2c_write( 0x01b0, 0x17, 1);
	proxy_i2c_write( 0x01ad, 0x00, 1);
	proxy_i2c_write( 0x00FF, 0x05, 1);
	proxy_i2c_write( 0x0100, 0x05, 1);
	proxy_i2c_write( 0x0199, 0x05, 1);
	proxy_i2c_write( 0x0109, 0x07, 1);
	proxy_i2c_write( 0x010a, 0x30, 1);
	proxy_i2c_write( 0x003f, 0x46, 1);
	proxy_i2c_write( 0x01a6, 0x1b, 1);
	proxy_i2c_write( 0x01ac, 0x3e, 1);
	proxy_i2c_write( 0x01a7, 0x1f, 1);
	proxy_i2c_write( 0x0103, 0x01, 1);
	proxy_i2c_write( 0x0030, 0x00, 1);
	proxy_i2c_write( 0x001b, 0x0A, 1);
	proxy_i2c_write( 0x003e, 0x0A, 1);
	proxy_i2c_write( 0x0131, 0x04, 1);
	proxy_i2c_write( 0x0011, 0x10, 1);
	proxy_i2c_write( 0x0014, 0x24, 1);
	proxy_i2c_write( 0x0031, 0xFF, 1);
	proxy_i2c_write( 0x00d2, 0x01, 1);
	proxy_i2c_write( 0x00f2, 0x01, 1);

	// RangeSetMaxConvergenceTime
	proxy_i2c_write( SYSRANGE__MAX_CONVERGENCE_TIME, 0x3F, 1);
	proxy_i2c_read( SYSRANGE__RANGE_CHECK_ENABLES, &chipidRangeMax, 1);
	chipidRangeMax = chipidRangeMax & 0xFE; // off ECE
	chipidRangeMax = chipidRangeMax | 0x02; // on ignore thr
	proxy_i2c_write( SYSRANGE__RANGE_CHECK_ENABLES, chipidRangeMax, 1);
	proxy_i2c_write( SYSRANGE__MAX_AMBIENT_LEVEL_MULT, 0xFF, 1);//SNR

	// ClearSystemFreshOutofReset
	proxy_i2c_write( SYSTEM__FRESH_OUT_OF_RESET, 0x0, 1);
	ProxyRead16bit(RANGE__RANGE_SCALER, &readI2C);

	//Range_Set_scalar
	for(i = 0; i < sizeof(uint16_t); i++) {
		shift = (sizeof(uint16_t) - i - 1) * 0x08;
		dataMask = (0xFF << shift);
		byteArray[i] = (u8)(((uint16_t)((uint16_t)85 & 0x01ff) & dataMask) >> shift);
		proxy_i2c_write( RANGE__RANGE_SCALER + i, byteArray[i], 1);
	}

	//readRangeOffset
	proxy_i2c_e2p_read(0x700, &moduleId, 2);
	shiftModuleId = moduleId >> 8;
	pr_err("KSY module ID : %d\n", shiftModuleId);

	if((shiftModuleId == 0x01) || (shiftModuleId == 0x02)) {
		proxy_i2c_e2p_read(IT_EEP_REG, &finVal, 2);
		offsetByte = 0x00FF & finVal;
		calCount = (0xFF00 & finVal) >> 8;
		if((offsetByte <= -21) || (offsetByte >= 11) || (calCount >= 100)) {
			proxy_i2c_e2p_write(IT_EEP_REG, 0, 2);
			calCount = 0;
			offsetByte = 0;
		}
		proxy_ctrl->proxy_info->cal_count = calCount;
		pr_err("inot read offset = %d from eeprom\n", offsetByte);
		proxy_i2c_write( SYSRANGE__PART_TO_PART_RANGE_OFFSET, offsetByte, 1);

	}
	else if(shiftModuleId == 0x03)           //fj module
    {
		proxy_i2c_e2p_read(FJ_EEP_REG, &finVal, 2);
		offsetByte = 0x00FF & finVal;
		calCount = (0xFF00 & finVal) >> 8;
		if((offsetByte <= -21) || (offsetByte >= 11) || (calCount >= 100)) {
			proxy_i2c_e2p_write(FJ_EEP_REG, 0, 2);
			calCount = 0;
			offsetByte = 0;
		}
		//	offsetByte -= 255;
		proxy_ctrl->proxy_info->cal_count = calCount;
		pr_err("fj read offset = %d from eeprom\n", offsetByte);
		proxy_i2c_write( SYSRANGE__PART_TO_PART_RANGE_OFFSET, offsetByte, 1);

	}
	else // default(inot) module
	{
		proxy_i2c_e2p_read(IT_EEP_REG, &finVal, 2);
		offsetByte = 0x00FF & finVal;
		calCount = (0xFF00 & finVal) >> 8;
		if((offsetByte <= -21) || (offsetByte >= 11) || (calCount >= 100)) {
			proxy_i2c_e2p_write(IT_EEP_REG, 0, 2);
			calCount = 0;
			offsetByte = 0;
		}
		proxy_ctrl->proxy_info->cal_count = calCount;
		pr_err("inot read offset = %d from eeprom\n", offsetByte);
		proxy_i2c_write( SYSRANGE__PART_TO_PART_RANGE_OFFSET, offsetByte, 1);
	}

	// Babybear_SetStraylight
	ninepointseven=25;
	proxy_i2c_write( SYSRANGE__CROSSTALK_COMPENSATION_RATE,(ninepointseven>>8)&0xFF, 1);
	proxy_i2c_write( SYSRANGE__CROSSTALK_COMPENSATION_RATE+1,ninepointseven&0xFF, 1);

	crosstalkHeight = 40;
	proxy_i2c_write(SYSRANGE__CROSSTALK_VALID_HEIGHT,crosstalkHeight & 0xFF, 1);


	// Will ignore all low distances (<100mm) with a low return rate
	ignoreThreshold = 64; // 64 = 0.5Mcps
	ignoreThresholdHeight = 33; // 33 * scaler3 = 99mm
	proxy_i2c_write(SYSRANGE__RANGE_IGNORE_THRESHOLD, (ignoreThreshold >> 8) & 0xFF, 1);
	proxy_i2c_write(SYSRANGE__RANGE_IGNORE_THRESHOLD+1,ignoreThreshold & 0xFF, 1);
	proxy_i2c_write(SYSRANGE__RANGE_IGNORE_VALID_HEIGHT,ignoreThresholdHeight & 0xFF, 1);

	// Init of Averaging samples : in case of adding glass
	for(i = 0; i < 8; i++){
		lastMeasurements[i] = 65535; // 65535 means no valid data
	}
	currentIndex = 0;

	// SetSystemInterruptConfigGPIORanging
	proxy_i2c_read(SYSTEM__INTERRUPT_CONFIG_GPIO, &chipidgpio, 1);
	proxy_i2c_write(SYSTEM__INTERRUPT_CONFIG_GPIO, (chipidgpio | 0x04), 1);


	//RangeSetSystemMode
	chipidRange = 0x01;
	proxy_i2c_write(SYSRANGE__START, chipidRange, 1);

	#ifdef COMPLEX_FILTER
		VL6180_InitComplexFilter();
	#else
		VL6180_InitLiteFilter();
	#endif

//	proxy_func_tbl.proxy_stat = get_proxy_stat;
//	proxy_ctrl->proxy_func_tbl = &proxy_func_tbl;

	return rc;
}

/*
uint16_t vl6180_get_proxy(struct vl6180_sensor_proxy_info_t* proxy_info)
{
	uint16_t proxy = 0;
	proxy = proxy_ctrl->last_proxy;

	//memcpy(proxy_info, &proxy_ctrl->proxy_stat, sizeof(proxy_ctrl->proxy_stat));

	return proxy;
}
*/
 void BabyBear_ParameterOptimization(uint32_t ambientRate)
{
	uint32_t newCrossTalk = 0;
	uint32_t newIgnoreThreshold = 0;

	// Compute new values
	newCrossTalk = BabyBear_damper(DEFAULT_CROSSTALK, ambientRate, LOW_LIGHT_XTALK_RATIO, HIGH_LIGHT_XTALK_RATIO);
	newIgnoreThreshold = BabyBear_damper(DEFAULT_IGNORETHRES, ambientRate, LOW_LIGHT_IGNORETHRES_RATIO, HIGH_LIGHT_IGNORETHRES_RATIO);
	// Program new values
	proxy_i2c_write( SYSRANGE__CROSSTALK_COMPENSATION_RATE, (newCrossTalk>>8)&0xFF, 1);
	proxy_i2c_write( SYSRANGE__CROSSTALK_COMPENSATION_RATE+1,newCrossTalk&0xFF, 1);

}



 uint32_t BabyBear_damper(uint32_t inData, uint32_t ambientRate, uint32_t LowLightRatio, uint32_t HighLightRatio)
{
	int weight = 0;
	uint32_t newVal = 0;

	if(ambientRate <= LOW_LIGHT_RETURN_RATE) {
		weight = LowLightRatio;
	}
	else {
		if(ambientRate >= HIGH_LIGHT_RETURN_RATE)
			weight = HighLightRatio;
		else
			weight = (int)LowLightRatio + ( ((int)ambientRate - LOW_LIGHT_RETURN_RATE) * ((int)HighLightRatio - (int)LowLightRatio) / (HIGH_LIGHT_RETURN_RATE - LOW_LIGHT_RETURN_RATE) );

	}

	newVal = (inData * weight)/100;

	return newVal;
}


void VL6180_InitLiteFilter(void)
{
    measurementIndex = 0;
    defaultZeroVal = 0;
    defaultAvgVal = 0;
    noDelayZeroVal = 0;
    noDelayAvgVal = 0;
    previousAvgDiff = 0;
}

uint16_t VL6180_LiteFilter(uint16_t m_trueRange_mm, uint16_t rawRange, uint32_t m_rtnSignalRate, uint32_t m_rtnAmbientRate, uint16_t errorCode)
{
    uint16_t m_newTrueRange_mm = 0;
    uint16_t maxOrInvalidDistance = 0;
    uint16_t registerValue = 0;
    uint32_t register32BitsValue1 = 0;
    uint32_t register32BitsValue2 = 0;
    uint16_t bypassFilter = 0;
    uint32_t VAvgDiff = 0;
    uint32_t idealVAvgDiff = 0;
    uint32_t minVAvgDiff = 0;
    uint32_t maxVAvgDiff = 0;
    uint16_t wrapAroundLowRawRangeLimit = 20;
    uint32_t wrapAroundLowReturnRateLimit = 800;
    uint16_t wrapAroundLowRawRangeLimit2 = 55;
    uint32_t wrapAroundLowReturnRateLimit2 = 300;
    uint32_t wrapAroundLowReturnRateFilterLimit = 600;
    uint16_t wrapAroundHighRawRangeFilterLimit = 350;
    uint32_t wrapAroundHighReturnRateFilterLimit = 900;
    uint32_t wrapAroundMaximumAmbientRateFilterLimit = 7500;
    uint32_t maxVAvgDiff2 = 1800;
    uint8_t wrapAroundDetected = 0;

    // Determines max distance
    maxOrInvalidDistance = (uint16_t)(255 * 3);

    // Check if distance is Valid or not
    switch (errorCode) {
        case 0x0C:
            m_trueRange_mm = maxOrInvalidDistance;
            break;
        case 0x0D:
            m_trueRange_mm = maxOrInvalidDistance;
            break;
        default:
            if (rawRange >= maxOrInvalidDistance)
                m_trueRange_mm = maxOrInvalidDistance;
            break;
    }

    if ((rawRange < wrapAroundLowRawRangeLimit) && (m_rtnSignalRate < wrapAroundLowReturnRateLimit))
        m_trueRange_mm = maxOrInvalidDistance;

    if ((rawRange < wrapAroundLowRawRangeLimit2) && (m_rtnSignalRate < wrapAroundLowReturnRateLimit2))
        m_newTrueRange_mm = maxOrInvalidDistance;

    bypassFilter = 0;

    if (m_rtnAmbientRate > wrapAroundMaximumAmbientRateFilterLimit)
		bypassFilter = 1;

    if (!(((rawRange < wrapAroundHighRawRangeFilterLimit) && (m_rtnSignalRate < wrapAroundLowReturnRateFilterLimit)) ||
        ((rawRange >= wrapAroundHighRawRangeFilterLimit) && (m_rtnSignalRate < wrapAroundHighReturnRateFilterLimit))
        ))
        bypassFilter = 1;

    proxy_i2c_read( 0x01AC, &registerValue, 1);
    if (bypassFilter == 1) {
        // Do not go through the filter
        if (registerValue != 0x3E)
			proxy_i2c_write( 0x01AC, 0x3E, 1);
    }
    else {
        // Go through the filter
		ProxyRead32bit(0x010C, &register32BitsValue1);
		ProxyRead32bit(0x0110, &register32BitsValue2);

        if (registerValue == 0x3E) {
            defaultZeroVal = register32BitsValue1;
            defaultAvgVal = register32BitsValue2;
			proxy_i2c_write( 0x01AC, 0x3F, 1);
        }
        else {
            noDelayZeroVal = register32BitsValue1;
            noDelayAvgVal = register32BitsValue2;
			proxy_i2c_write( 0x01AC, 0x3E, 1);
        }

        // Computes current VAvgDiff
        if (defaultAvgVal > noDelayAvgVal)
            VAvgDiff = defaultAvgVal - noDelayAvgVal;
        else
            VAvgDiff = 0;
        previousAvgDiff = VAvgDiff;

        // Check the VAvgDiff
        idealVAvgDiff = defaultZeroVal - noDelayZeroVal;
        if (idealVAvgDiff > maxVAvgDiff2)
            minVAvgDiff = idealVAvgDiff - maxVAvgDiff2;
        else
            minVAvgDiff = 0;
        maxVAvgDiff = idealVAvgDiff + maxVAvgDiff2;
        if (VAvgDiff < minVAvgDiff || VAvgDiff > maxVAvgDiff)
            wrapAroundDetected = 1;
    }
    if (wrapAroundDetected == 1)
        m_newTrueRange_mm = maxOrInvalidDistance;
    else
        m_newTrueRange_mm = m_trueRange_mm;

    measurementIndex = measurementIndex + 1;

    return m_newTrueRange_mm;
}

void VL6180_InitComplexFilter(void)
{
    int i = 0;

    measurementIndex = 0;
    defaultZeroVal = 0;
    defaultAvgVal = 0;
    noDelayZeroVal = 0;
    noDelayAvgVal = 0;
    previousAvgDiff = 0;
    stdFilteredReads = 0;
    previousRangeStdDev = 0;
    previousReturnRateStdDev = 0;

    for (i = 0; i < FILTERNBOFSAMPLES; i++){
        lastTrueRange[i] = FILTERINVALIDDISTANCE;
        lastReturnRates[i] = 0;
    }
}

uint32_t VL6180_StdDevDamper(uint32_t AmbientRate, uint32_t SignalRate, uint32_t StdDevLimitLowLight, uint32_t StdDevLimitLowLightSNR, uint32_t StdDevLimitHighLight, uint32_t StdDevLimitHighLightSNR)
{
    uint32_t newStdDev = 0;
    uint16_t snr = 0;

    if (AmbientRate > 0)
        snr = (uint16_t)((100 * SignalRate) / AmbientRate);
    else
        snr = 9999;

    if (snr >= StdDevLimitLowLightSNR)
        newStdDev = StdDevLimitLowLight;
    else {
        if (snr <= StdDevLimitHighLightSNR)
            newStdDev = StdDevLimitHighLight;
        else
            newStdDev = (uint32_t)(StdDevLimitHighLight + (snr - StdDevLimitHighLightSNR) * (int)(StdDevLimitLowLight - StdDevLimitHighLight) / (StdDevLimitLowLightSNR - StdDevLimitHighLightSNR));
    }

    return newStdDev;
}
uint16_t VL6180_ComplexFilter(uint16_t m_trueRange_mm, uint16_t rawRange, uint32_t m_rtnSignalRate, uint32_t m_rtnAmbientRate, uint16_t errorCode)
{
    uint16_t m_newTrueRange_mm = 0;
    uint16_t i = 0;
    uint16_t bypassFilter = 0;
    uint16_t registerValue = 0;
    uint32_t register32BitsValue1 = 0;
    uint32_t register32BitsValue2 = 0;
    uint16_t validDistance = 0;
    uint16_t maxOrInvalidDistance = 0;
    uint16_t wrapAroundFlag = 0;
    uint16_t noWrapAroundFlag = 0;
    uint16_t noWrapAroundHighConfidenceFlag = 0;
    uint16_t flushFilter = 0;
    uint32_t rateChange = 0;
    uint16_t stdDevSamples = 0;
    uint32_t stdDevDistanceSum = 0;
    uint32_t stdDevDistanceMean = 0;
    uint32_t stdDevDistance = 0;
    uint32_t stdDevRateSum = 0;
    uint32_t stdDevRateMean = 0;
    uint32_t stdDevRate = 0;
    uint32_t stdDevLimitWithTargetMove = 0;
    uint32_t VAvgDiff = 0;
    uint32_t idealVAvgDiff = 0;
    uint32_t minVAvgDiff = 0;
    uint32_t maxVAvgDiff = 0;
    uint16_t wrapAroundLowRawRangeLimit = 20;
    uint32_t wrapAroundLowReturnRateLimit = 800;
    uint16_t wrapAroundLowRawRangeLimit2 = 55;
    uint32_t wrapAroundLowReturnRateLimit2 = 300;
    uint32_t wrapAroundLowReturnRateFilterLimit = 600;
    uint16_t wrapAroundHighRawRangeFilterLimit = 350;
    uint32_t wrapAroundHighReturnRateFilterLimit = 900;
    uint32_t wrapAroundMaximumAmbientRateFilterLimit = 7500;
    uint32_t MinReturnRateFilterFlush = 75;
    uint32_t MaxReturnRateChangeFilterFlush = 50;
    uint32_t stdDevLimit = 300;
    uint32_t stdDevLimitLowLight = 300;
    uint32_t stdDevLimitLowLightSNR = 30; // 0.3
    uint32_t stdDevLimitHighLight = 2500;
    uint32_t stdDevLimitHighLightSNR = 5; //0.05
    uint32_t stdDevHighConfidenceSNRLimit = 8;
    uint32_t stdDevMovingTargetStdDevLimit = 90000;
    uint32_t stdDevMovingTargetReturnRateLimit = 3500;
    uint32_t stdDevMovingTargetStdDevForReturnRateLimit = 5000;
    uint32_t maxVAvgDiff2 = 1800;
    uint16_t wrapAroundNoDelayCheckPeriod = 2;
    uint16_t stdFilteredReadsIncrement = 2;
    uint16_t stdMaxFilteredReads = 4;

    // End Filter Parameters
    maxOrInvalidDistance = (uint16_t)(255 * 3);
    // Check if distance is Valid or not
    switch (errorCode) {
        case 0x0C:
            m_trueRange_mm = maxOrInvalidDistance;
            validDistance = 0;
            break;
        case 0x0D:
            m_trueRange_mm = maxOrInvalidDistance;
            validDistance = 1;
            break;
        default:
            if (rawRange >= maxOrInvalidDistance)
                validDistance = 0;
            else
                validDistance = 1;
            break;
    }
    m_newTrueRange_mm = m_trueRange_mm;

    // Checks on low range data
    if ((rawRange < wrapAroundLowRawRangeLimit) && (m_rtnSignalRate < wrapAroundLowReturnRateLimit)) {
        //Not Valid distance
        m_newTrueRange_mm = maxOrInvalidDistance;
        bypassFilter = 1;
    }
    if ((rawRange < wrapAroundLowRawRangeLimit2) && (m_rtnSignalRate < wrapAroundLowReturnRateLimit2)) {
        //Not Valid distance
        m_newTrueRange_mm = maxOrInvalidDistance;
        bypassFilter = 1;
    }

    // Checks on Ambient rate level
    if (m_rtnAmbientRate > wrapAroundMaximumAmbientRateFilterLimit) {
        // Too high ambient rate
        flushFilter = 1;
        bypassFilter = 1;
    }
    // Checks on Filter flush
    if (m_rtnSignalRate < MinReturnRateFilterFlush) {
        // Completely lost target, so flush the filter
        flushFilter = 1;
        bypassFilter = 1;
    }
    if (lastReturnRates[0] != 0) {
        if (m_rtnSignalRate > lastReturnRates[0])
            rateChange = (100 * (m_rtnSignalRate - lastReturnRates[0])) / lastReturnRates[0];
        else
            rateChange = (100 * (lastReturnRates[0] - m_rtnSignalRate)) / lastReturnRates[0];
    }
    else
        rateChange = 0;
    if (rateChange > MaxReturnRateChangeFilterFlush)
        flushFilter = 1;

    if (flushFilter == 1) {
        measurementIndex = 0;
        for (i = 0; i < FILTERNBOFSAMPLES; i++) {
            lastTrueRange[i] = FILTERINVALIDDISTANCE;
            lastReturnRates[i] = 0;
        }
    }
    else {
        for (i = (uint16_t)(FILTERNBOFSAMPLES - 1); i > 0; i--) {
            lastTrueRange[i] = lastTrueRange[i - 1];
            lastReturnRates[i] = lastReturnRates[i - 1];
        }
    }
    if (validDistance == 1)
        lastTrueRange[0] = m_trueRange_mm;
    else
        lastTrueRange[0] = FILTERINVALIDDISTANCE;
    lastReturnRates[0] = m_rtnSignalRate;

    // Check if we need to go through the filter or not
    if (!(((rawRange < wrapAroundHighRawRangeFilterLimit) && (m_rtnSignalRate < wrapAroundLowReturnRateFilterLimit)) ||
        ((rawRange >= wrapAroundHighRawRangeFilterLimit) && (m_rtnSignalRate < wrapAroundHighReturnRateFilterLimit))
        ))
        bypassFilter = 1;

    // Check which kind of measurement has been made
	proxy_i2c_read( 0x01AC, &registerValue, 1);

    // Read data for filtering
	ProxyRead32bit(0x010C, &register32BitsValue1);
	ProxyRead32bit(0x0110, &register32BitsValue2);

    if (registerValue == 0x3E) {
        defaultZeroVal = register32BitsValue1;
        defaultAvgVal = register32BitsValue2;
    }
    else {
        noDelayZeroVal = register32BitsValue1;
        noDelayAvgVal = register32BitsValue2;
    }

    if (bypassFilter == 1) {
        // Do not go through the filter
        if (registerValue != 0x3E)
			proxy_i2c_write( 0x01AC, 0x3E, 1);
        // Set both Defaut and NoDelay To same value
        defaultZeroVal = register32BitsValue1;
        defaultAvgVal = register32BitsValue2;
        noDelayZeroVal = register32BitsValue1;
        noDelayAvgVal = register32BitsValue2;
        measurementIndex = 0;

        // Return immediately
        return m_newTrueRange_mm;
    }

    if (measurementIndex % wrapAroundNoDelayCheckPeriod == 0)
		proxy_i2c_write( 0x01AC, 0x3F, 1);
    else
		proxy_i2c_write( 0x01AC, 0x3E, 1);
    measurementIndex = (uint16_t)(measurementIndex + 1);

    // Computes current VAvgDiff
    if (defaultAvgVal > noDelayAvgVal)
        VAvgDiff = defaultAvgVal - noDelayAvgVal;
    else
        VAvgDiff = 0;
    previousAvgDiff = VAvgDiff;

    // Check the VAvgDiff
    if(defaultZeroVal>noDelayZeroVal)
        idealVAvgDiff = defaultZeroVal - noDelayZeroVal;
    else
        idealVAvgDiff = noDelayZeroVal - defaultZeroVal;
    if (idealVAvgDiff > maxVAvgDiff2)
        minVAvgDiff = idealVAvgDiff - maxVAvgDiff2;
    else
        minVAvgDiff = 0;
    maxVAvgDiff = idealVAvgDiff  + maxVAvgDiff2;
    if (VAvgDiff < minVAvgDiff || VAvgDiff > maxVAvgDiff)
        wrapAroundFlag = 1;
    else {
        // Go through filtering check

        // stdDevLimit Damper on SNR
        stdDevLimit = VL6180_StdDevDamper(m_rtnAmbientRate, m_rtnSignalRate, stdDevLimitLowLight, stdDevLimitLowLightSNR, stdDevLimitHighLight, stdDevLimitHighLightSNR);

        // Standard deviations computations
        stdDevSamples = 0;
        stdDevDistanceSum = 0;
        stdDevDistanceMean = 0;
        stdDevDistance = 0;
        stdDevRateSum = 0;
        stdDevRateMean = 0;
        stdDevRate = 0;
        for (i = 0; (i < FILTERNBOFSAMPLES) && (stdDevSamples < FILTERSTDDEVSAMPLES); i++) {
            if (lastTrueRange[i] != FILTERINVALIDDISTANCE) {
                stdDevSamples = (uint16_t)(stdDevSamples + 1);
                stdDevDistanceSum = (uint32_t)(stdDevDistanceSum + lastTrueRange[i]);
                stdDevRateSum = (uint32_t)(stdDevRateSum + lastReturnRates[i]);
            }
        }
        if (stdDevSamples > 0) {
            stdDevDistanceMean = (uint32_t)(stdDevDistanceSum / stdDevSamples);
            stdDevRateMean = (uint32_t)(stdDevRateSum / stdDevSamples);
        }
        stdDevSamples = 0;
        stdDevDistanceSum = 0;
        stdDevRateSum = 0;
        for (i = 0; (i < FILTERNBOFSAMPLES) && (stdDevSamples < FILTERSTDDEVSAMPLES); i++) {
            if (lastTrueRange[i] != FILTERINVALIDDISTANCE) {
                stdDevSamples = (uint16_t)(stdDevSamples + 1);
                stdDevDistanceSum = (uint32_t)(stdDevDistanceSum + (int)(lastTrueRange[i] - stdDevDistanceMean) * (int)(lastTrueRange[i] - stdDevDistanceMean));
                stdDevRateSum = (uint32_t)(stdDevRateSum + (int)(lastReturnRates[i] - stdDevRateMean) * (int)(lastReturnRates[i] - stdDevRateMean));
            }
        }
        if (stdDevSamples >= MINFILTERSTDDEVSAMPLES) {
            stdDevDistance = (uint16_t)(stdDevDistanceSum / stdDevSamples);
            stdDevRate = (uint16_t)(stdDevRateSum / stdDevSamples);
        }
        else {
			stdDevDistance = 0;
			stdDevRate = 0;
        }

        // Check Return rate standard deviation
        if (stdDevRate < stdDevMovingTargetStdDevLimit) {
            if (stdDevSamples < MINFILTERVALIDSTDDEVSAMPLES) {
				if(measurementIndex<FILTERSTDDEVSAMPLES)
					m_newTrueRange_mm = 800;
				else
					m_newTrueRange_mm = maxOrInvalidDistance;
            }
            else {
                // Check distance standard deviation
                if (stdDevRate < stdDevMovingTargetReturnRateLimit)
                    stdDevLimitWithTargetMove = stdDevLimit + (((stdDevMovingTargetStdDevForReturnRateLimit - stdDevLimit) * stdDevRate) / stdDevMovingTargetReturnRateLimit);
                else
                    stdDevLimitWithTargetMove = stdDevMovingTargetStdDevForReturnRateLimit;

                if ((stdDevDistance * stdDevHighConfidenceSNRLimit) < stdDevLimitWithTargetMove)
                    noWrapAroundHighConfidenceFlag = 1;
                else {
                    if (stdDevDistance < stdDevLimitWithTargetMove) {
                        if (stdDevSamples >= MINFILTERVALIDSTDDEVSAMPLES)
                            noWrapAroundFlag = 1;
                        else
                            m_newTrueRange_mm = maxOrInvalidDistance;
					}
                    else
                        wrapAroundFlag = 1;
                }
            }
        }
        else
            wrapAroundFlag = 1;
    }

    if (m_newTrueRange_mm == maxOrInvalidDistance){
        if (stdFilteredReads > 0)
            stdFilteredReads = (uint16_t)(stdFilteredReads - 1);
    }
    else
    {
        if (wrapAroundFlag == 1) {
            m_newTrueRange_mm = maxOrInvalidDistance;
            stdFilteredReads = (uint16_t)(stdFilteredReads + stdFilteredReadsIncrement);
            if (stdFilteredReads > stdMaxFilteredReads)
                stdFilteredReads = stdMaxFilteredReads;
        }
        else {
            if (noWrapAroundFlag == 1) {
                if (stdFilteredReads > 0) {
                    m_newTrueRange_mm = maxOrInvalidDistance;
                    if (stdFilteredReads > stdFilteredReadsIncrement)
                        stdFilteredReads = (uint16_t)(stdFilteredReads - stdFilteredReadsIncrement);
                    else
                        stdFilteredReads = 0;
                }
            }
            else {
                if (noWrapAroundHighConfidenceFlag == 1)
                    stdFilteredReads = 0;
            }
        }

    }
    previousRangeStdDev = stdDevDistance;
    previousReturnRateStdDev = stdDevRate;
    previousStdDevLimit = stdDevLimitWithTargetMove;

    return m_newTrueRange_mm;
}


#define VL6180_NAME "VL6180"

static struct i2c_board_info vl6180_info[] = {
	{
		I2C_BOARD_INFO("VL6180", 0x52),
	},
};

/* Main jobs: */
/* 1.check for device-specified errors, device not ready. */
/* 2.Initialize the device if it is opened for the first time. */
/* 3.Update f_op pointer. */
/* 4.Fill data structures into private_data */
/* CAM_RESET */
static int VL6180_Open(struct inode *a_pstInode, struct file *a_pstFile)
{
	VL6180ADB("[VL6180] VL6180_Open - Start\n");


	if (g_VL6180_Opened) {
		VL6180ADB("[VL6180] the device is opened\n");
		return -EBUSY;
	}

	spin_lock(&g_VL6180_SpinLock);
	g_VL6180_Opened = 1;
	spin_unlock(&g_VL6180_SpinLock);


	VL6180ADB("[VL6180] VL6180_Open - End\n");

	return 0;
}

/* Main jobs: */
/* 1.Deallocate anything that "open" allocated in private_data. */
/* 2.Shut down the device on last close. */
/* 3.Only called once on last time. */
/* Q1 : Try release multiple times. */
static int VL6180_Release(struct inode *a_pstInode, struct file *a_pstFile)
{
	VL6180ADB("[VL6180] VL6180_Release - Start\n");

	if (g_VL6180_Opened) {
		VL6180ADB("[DW9714A] feee\n");
		spin_lock(&g_VL6180_SpinLock);
		g_VL6180_Opened = 0;
		spin_unlock(&g_VL6180_SpinLock);

	}
	stop_proxy();
	VL6180ADB("[VL6180] VL6180_Release - End\n");

	return 0;
}


static int VL6180_Ioctl(
struct file * a_pstFile,
unsigned int a_u4Command,
unsigned long a_u4Param)
{
	int rc = 0;

	VL6180ADB("[VL6180] %s:%d cmd(%d) Param(%d)\n", __func__, __LINE__, (int)(a_u4Command), (int)(a_u4Param));

    switch(a_u4Command)
    {
        case VL6180_T_SETPROXYCFG :
			VL6180ADB("[VL6180] VL6180_T_SETPROXYCFG \n");
			{
				#if 0
				enum vl6180_ldaf_cmd_mode proxy_cmd;
				if(copy_from_user(&proxy_cmd, (void *)a_u4Param, sizeof(enum vl6180_ldaf_cmd_mode)) < 0) {
					pr_err("copy_from_user failed\n");
					break;
				}
				#endif
				int proxy_cmd;
				proxy_cmd = (int)(a_u4Param);

				VL6180ADB("[VL6180] %s:%d:proxy_cmd(%d) \n", __func__, __LINE__, proxy_cmd);
				switch(proxy_cmd) {
					case PROXY_ON:
					case PROXY_THREAD_ON:
					case PROXY_THREAD_RESTART:
						VL6180ADB("[VL6180] %s:%d:PROXY_ON \n", __func__, __LINE__);
						rc = vl6180_enable_gpio(true);
						if(rc) {
							pr_err("%s:%d:failed to enable gpio rc(%d)\n", __func__, __LINE__, rc);
							goto gpio_enable_failed;
						}
						rc = vl6180_init_proxy();
						if(rc) {
							pr_err("failed to init proxy");
							goto proxy_init_failed;
						}
						rc = vl6180_proxy_thread_start();
						if(rc) {
							pr_err("failed to start proxy thread");
							goto proxy_thread_start_failed;
						}
						break;
					case PROXY_THREAD_PAUSE:
					case PROXY_THREAD_OFF:
						VL6180ADB("[VL6180] %s:%d:PROXY_THREAD_PAUSE \n", __func__, __LINE__);
						rc = stop_proxy();
						if(rc) {
							pr_err("failed to stop proxy");
						}

						rc = vl6180_enable_gpio(false);
						if(rc) {
							pr_err("failed to disable gpio");
						}
						break;
					case PROXY_CAL:
						VL6180ADB("[VL6180] %s:%d:PROXY_CAL \n", __func__, __LINE__);
						rc = vl6180_proxy_cal();
						if(rc) {
							pr_err("failed to calibrate proxy");
						}
						break;
					default:
						VL6180ADB("[VL6180] %s:%d:default \n", __func__, __LINE__);
						break;
					}
			}
	        break;

        case VL6180_G_GETPROXYDATA :
			{
				VL6180ADB("[VL6180] VL6180_G_GETPROXYDATA laser_value(%d)\n", (int)proxy_ctrl->proxy_info->proxy_val[0]);
				mutex_lock(&proxy_ctrl->proxy_lock);
				rc = copy_to_user((void __user *)a_u4Param, (void *)proxy_ctrl->proxy_info, sizeof(struct vl6180_proxy_info));
				mutex_unlock(&proxy_ctrl->proxy_lock);
				if(rc  != 0) {
					pr_err("%s:%d:copy_to_user failed!! rc = %d\n", __func__, __LINE__, rc);
					//rc = -EFAULT;
				}
			}
	        break;
        default :
			VL6180ADB("[VL6180] No CMD \n");
            rc = -EPERM;
        break;
    }

    return rc;

proxy_thread_start_failed:
	stop_proxy();
proxy_init_failed:
	vl6180_enable_gpio(false);
gpio_enable_failed:
	return rc;
}

static int
VL6180_ldaf_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long start = vma->vm_start;
	unsigned long size = (vma->vm_end - vma->vm_start);
	unsigned long pfn = (virt_to_phys(proxy_ctrl->proxy_info)) >> PAGE_SHIFT;

	VL6180ADB("E, start = 0x%lx, size = %ld, pfn = %ld\n", start, size, pfn);

	if(remap_pfn_range(vma, start, pfn, PAGE_SIZE, PAGE_SHARED)) {
		VL6180ADB("failed to remap_pfn_range\n");
		return -EAGAIN;
	}

	return 0;
}

static const struct file_operations g_stVL6180_fops = {
	.owner = THIS_MODULE,
	.open = VL6180_Open,
	.release = VL6180_Release,
	.unlocked_ioctl = VL6180_Ioctl,
	.mmap = VL6180_ldaf_mmap,
};

inline static int Register_VL6180_CharDrv(void)
{
	struct device *laser_device = NULL;

	VL6180ADB("[VL6180] Register_VL6180_CharDrv - Start\n");

	/* Allocate char driver no. */
	if (alloc_chrdev_region(&g_VL6180_devno, 0, 1, VL6180_DRVNAME)) {
		VL6180ADB("[VL6180] Allocate device no failed\n");

		return -EAGAIN;
	}
	/* Allocate driver */
	g_pVL6180_CharDrv = cdev_alloc();

	if (NULL == g_pVL6180_CharDrv) {
		unregister_chrdev_region(g_VL6180_devno, 1);

		VL6180ADB("[VL6180] Allocate mem for kobject failed\n");

		return -ENOMEM;
	}
	/* Attatch file operation. */
	cdev_init(g_pVL6180_CharDrv, &g_stVL6180_fops);

	g_pVL6180_CharDrv->owner = THIS_MODULE;

	/* Add to system */
	if (cdev_add(g_pVL6180_CharDrv, g_VL6180_devno, 1)) {
		VL6180ADB("[VL6180] Attatch file operation failed\n");

		unregister_chrdev_region(g_VL6180_devno, 1);

		return -EAGAIN;
	}

	laser_class = class_create(THIS_MODULE, "laserdrv");
	if (IS_ERR(laser_class)) {
		int ret = PTR_ERR(laser_class);
		VL6180ADB("Unable to create class, err = %d\n", ret);
		return ret;
	}

	laser_device = device_create(laser_class, NULL, g_VL6180_devno, NULL, VL6180_DRVNAME);

	if (NULL == laser_device) {
		return -EIO;
	}

	VL6180ADB("[VL6180] Register_VL6180_CharDrv - End\n");
	return 0;
}

inline static void Unregister_VL6180_CharDrv(void)
{
	VL6180ADB("[VL6180] Unregister_VL6180_CharDrv - Start\n");

	/* Release char driver */
	cdev_del(g_pVL6180_CharDrv);

	unregister_chrdev_region(g_VL6180_devno, 1);

	device_destroy(laser_class, g_VL6180_devno);

	class_destroy(laser_class);

	VL6180ADB("[VL6180] Unregister_VL6180_CharDrv - End\n");
}

static int VL6180_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int VL6180_i2c_remove(struct i2c_client *client);
static const struct i2c_device_id VL6180_i2c_id[] = { {VL6180_DRVNAME, 0}, {} };

struct i2c_driver VL6180_i2c_driver = {
	.probe = VL6180_i2c_probe,
	.remove = VL6180_i2c_remove,
	.driver.name = VL6180_DRVNAME,
	.id_table = VL6180_i2c_id,
};

static int VL6180_i2c_remove(struct i2c_client *client)
{
	return 0;
}

/* Kirby: add new-style driver {*/
static int VL6180_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int i4RetValue = 0;

	VL6180ADB("[VL6180] VL6180_i2c_probe\n");
	VL6180ADB("[6180]%s:%d\n", __func__, __LINE__);

	/* Kirby: add new-style driver { */
	g_pstVL6180_I2Cclient = client;

	g_pstVL6180_I2Cclient->addr = g_pstVL6180_I2Cclient->addr >> 1;
	/* Register char driver */
	i4RetValue = Register_VL6180_CharDrv();

	if (i4RetValue) {

		VL6180ADB("[VL6180] register char device failed!\n");

		return i4RetValue;
	}

	spin_lock_init(&g_VL6180_SpinLock);

	VL6180ADB("[VL6180] Attached!!\n");

	return 0;
}

static int VL6180_probe(struct platform_device *pdev)
{
	VL6180ADB("[6180]%s:%d\n", __func__, __LINE__);
	///////////////////////////////////////////////////////////////
	proxy_ctrl = devm_kzalloc(&pdev->dev, sizeof(struct vl6180_proxy_ctrl), GFP_KERNEL);
	if(!proxy_ctrl) {
		VL6180ADB("Cannot allocate memory for proxy_ctrl!!\n");
		return -ENOMEM;
	}

	proxy_ctrl->proxy_info = (struct vl6180_proxy_info *)get_zeroed_page(GFP_KERNEL);
	if(!proxy_ctrl->proxy_info) {
		pr_err("Cannot allocate memory for proxy_info!!\n");
		return -ENOMEM;
	}

	proxy_ctrl->client = NULL;
	proxy_ctrl->proxy_power.gpio_enable = 97;
	proxy_ctrl->proxy_power.gpio_interrupt = 96;
	proxy_ctrl->max_i2c_fail_thres = 5;
	proxy_ctrl->exit_workqueue = 1;
	mutex_init(&proxy_ctrl->proxy_lock);
	///////////////////////////////////////////////////////////////
	return i2c_add_driver(&VL6180_i2c_driver);
}

static int VL6180_remove(struct platform_device *pdev)
{
	i2c_del_driver(&VL6180_i2c_driver);
	if((proxy_ctrl != NULL) && !proxy_ctrl->proxy_info) {
		pr_err("Free memory for proxy_info\n");
		free_pages(proxy_ctrl->proxy_info, 0);
		proxy_ctrl->proxy_info = NULL;
	}
	return 0;
}

static int VL6180_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	return 0;
}

static int VL6180_resume(struct platform_device *pdev)
{
	return 0;
}

/* platform structure */
static struct platform_driver g_stVL6180_Driver = {
	.probe = VL6180_probe,
	.remove = VL6180_remove,
	.suspend = VL6180_suspend,
	.resume = VL6180_resume,
	.driver = {
		   .name = "laser_sensor",
		   .owner = THIS_MODULE,
		   }
};

static int __init VL6180_i2C_init(void)
{
	i2c_register_board_info(LASER_I2C_BUSNUM, &kd_laser_dev, 1);

	VL6180ADB("[6180]%s:%d\n", __func__, __LINE__);

	if (platform_driver_register(&g_stVL6180_Driver)) {
		VL6180ADB("failed to register VL6180 driver\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit VL6180_i2C_exit(void)
{
	VL6180ADB("[6180]%s:%d\n", __func__, __LINE__);
	platform_driver_unregister(&g_stVL6180_Driver);
}
module_init(VL6180_i2C_init);
module_exit(VL6180_i2C_exit);

MODULE_DESCRIPTION("VL6180 laser module driver");
MODULE_LICENSE("GPL");
