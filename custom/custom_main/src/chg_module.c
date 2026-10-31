/*
 * @Author: tn
 * @Description: 封装串口1函数,MQTT通信代码（后期将mqtt代码移植到其他地方）
 * @Date: 2025-03-10 19:54:35
 * @LastEditors: tn
 * @LastEditTime: 2025-06-29 22:59:58
 */
#include <stdarg.h>
#include <stdio.h>

#include "chg_module.h"
#include "cm_os.h"
#include "cm_mem.h"
#include "cm_sys.h"
#include "cm_virt_at.h"
#include "cm_rtc.h"
#include "cm_modem.h"
#include "cm_pm.h"
#include "cm_fs.h"
#include "custom_main.h"
#include "chg_struct.h"
#include "cm_gpio.h"
#include "cm_iomux.h"
#include "cm_uart.h"
#include "uart.h"

#include "cm_i2c.h"

#include <stdlib.h>
#include <string.h>
#include "cm_uart.h"
#include "cm_mqtt.h"
#include "cm_ssl.h"
#include "chg_dev.h"

#include "cJSON.h"

#define EPROM_I2C_ID	    CM_I2C_DEV_0
#define EPROM_DEV_ADDR   	0x50
#define PAGE_LEN  128

static osMessageQueueId_t OSUartMess = 0;
static osMessageQueueId_t OSMqttMess = 0;
static osMessageQueueId_t OSmeterMess = 0;
uartStruct uart0={0};//定义串口结构体变量
static char Mqtt_buf[255];//mqtt缓冲区;
static char meter_buf[255];//mqtt缓冲区;
uint16_t eprom_addr = 0x0000; //选取所支持的任意E2PROM地址进行测试,可修改

#define MAX_TOPICS 1
// const char* target_topics[MAX_TOPICS] = {"device/commands", "sensor/data", "status/update"};
const char* target_topics[MAX_TOPICS] = {"/sys/a1zukTvYu5S/Vra8xiisfbZM6Cl5Qsal/thing/service/property/set"};

/**
 * @author: tn
 * @description: 封装串口打印函数,支持格式化输出，串口0打印
 * @param {char} *data
 * @return 0传入的data数组为空，或者格式化失败
 *         1正常发送
 */
uint8_t sysPrintf(const char *data, ...)
{
  if(data == NULL)
  {
    return 0;
  }
  // 定义缓冲区（根据需求调整大小）
  char buffer[128];
  va_list args;

  // 1. 初始化可变参数
  va_start(args, data);

  // 2. 将格式化字符串写入缓冲区
  int len = vsnprintf(buffer, sizeof(buffer), data, args);

  // 3. 结束可变参数
  va_end(args);

  // 检查格式化是否成功
  if (len < 0) {
    return 0;  // 格式化失败
  } else if (len >= (int)sizeof(buffer)) {
    len = sizeof(buffer) - 1;  // 防止缓冲区溢出
  }

  // 4. 通过串口发送数据
  uartSendData(&uart0, (uint8_t*)buffer, len);

  return 1;
}

/**
 * @Author: TN
 * @description: 创建消息队列
 * @return {*}
 */
void CreateQueueTask(void)
{
    if (OSUartMess == NULL)
    {
        OSUartMess = osMessageQueueNew(30, SHELL_CMD_LENTH, NULL); // 创建消息队列
    }   
    if(OSUartMess==NULL)
    {
        sysPrintf("OSUartMess Creat fail"); 
    }
    if (OSMqttMess == NULL)
    {
        OSMqttMess = osMessageQueueNew(255, SHELL_CMD_LENTH, NULL); // 创建MQTT消息队列
    }   
    if(OSMqttMess==NULL)
    {
        sysPrintf("OSMqttMess Creat fail"); 
    }
    if (OSmeterMess == NULL)
    {
        OSmeterMess = osMessageQueueNew(255, SHELL_CMD_LENTH, NULL); // 创建meter消息队列
    }   
    if(OSmeterMess==NULL)
    {
        sysPrintf("OSmeterMess Creat fail"); 
    }
}

/**
 * @author: tn
 * @description: 向OSmeterMess队列推送消息
 * @param {char} *data
 * @return {*}
 */
char SendmeterMessQueue(char *data)
{
    if (OSmeterMess == NULL)
      return 1;
    if (osMessageQueuePut(OSmeterMess, data, 0, 0) == osOK)
    {
        return 0;
    } 
    sysPrintf("MQTT recv put queue fail"); 
    return 1;
}

/**
 * @author: tn
 * @description: 获取OSmeterMess队列消息
 * @param {char} *data
 * @return {*}
 */
char GetmeterMessQueue(char *data)
{
    if (OSmeterMess == NULL)
      return 1;  
    if (osMessageQueueGet(OSmeterMess, data, 0, 0) == osOK)
    {
        return 0;
    } 
    return 1;
}

/**
 * @author: tn+
 * @description: 向OSMqttMess队列推送消息
 * @param {char} *data
 * @return {*}
 */
char SendMqttMessQueue(char *data)
{
    if (OSMqttMess == NULL)
      return 1;
    if (osMessageQueuePut(OSMqttMess, data, 0, 0) == osOK)
    {
        return 0;
    } 
    sysPrintf("MQTT recv put queue fail"); 
    return 1;
}

/**
 * @author: tn
 * @description: 获取OSMqttMess队列消息
 * @param {char} *data
 * @return {*}
 */
char GetMqttMessQueue(char *data)
{
    if (OSMqttMess == NULL)
      return 1;  
    if (osMessageQueueGet(OSMqttMess, data, 0, 0) == osOK)
    {
        return 0;
    } 
    return 1;
}

/**
 * @Author: TN
 * @description: 向OSUartMess队列推送数据
 * @param {char} *data
 * @return {*}
 */
char SendUartMessQueue(char *data)
{
    if (OSUartMess == NULL)
      return 1;
    if (osMessageQueuePut(OSUartMess, data, 0, 0) == osOK)
    {
        return 0;
    } 
    sysPrintf("uart0 recv put queue fail"); 
    return 1;
}

/**
 * @Author: TN
 * @description: 获取消息队列
 * @param {char} *data
 * @return {*}
 */
char GetUartMessQueue(char *data)
{
    if (OSUartMess == NULL)
      return 1;  
    if (osMessageQueueGet(OSUartMess, data, 0, 0) == osOK)
    {
        return 0;
    } 
    return 1;
}

/**
 * @author: tn
 * @description: 数据接收函数，后续升级
 * @param {uint32_t} flags
 * @param {char} *data
 * @param {int} len
 * @return {*}
 */
void uart0RecvData(uint32_t flags, char *data, int len)
{
    SendUartMessQueue((char *)data);//推送数据到消息队列
    //uartSendData(&uart0, (uint8_t *)data, len);//把接收的数据返回，调试暂时屏蔽改回传
}

/**
 * @author: tn
 * @description: 用于初始化配置串口
 * @return {*}
 */
uint8_t uart0Recv_init()
{  
  //配置串口
  uart0.uartId = 0;//配置串口号
  uart0.uartRecvCb = uart0RecvData;//设置接收数据函数
  uart0.config.baudrate = 115200;//波特率
  uart0.config.byte_size = CM_UART_BYTE_SIZE_8;//数据位数
  uart0.config.flow_ctrl = CM_UART_FLOW_CTRL_NONE;//硬件流控
  uart0.config.parity = CM_UART_PARITY_NONE;//奇偶校验
  uart0.config.stop_bit = CM_UART_STOP_BIT_ONE;//停止位
  uart0.config.is_lpuart = 0;//若要配置为低功耗模式可改为1
  if (uart_init(&uart0) !=0)//初始化串口
  {
      return -1;
  }
  return 0;
}
/*****************eeprom********************************/
/**
 * @Author: TN
 * @description: IIC操作eeprom初始化
 * @param {int32_t} iic_id
 * @return {*}
 */
int32_t hal_iic_init_driver(int32_t iic_id)
{
	cm_i2c_cfg_t config = 
	{
		CM_I2C_ADDR_TYPE_7BIT,
		CM_I2C_MODE_MASTER, //目前仅支持模式
		CM_I2C_CLK_100KHZ
	};//master模式,(100KHZ)

	int32_t ret;

	sysPrintf("i2c test start,i2c num:%d!!\n",iic_id);

	cm_gpio_cfg_t cfg = {0};
    cfg.direction = CM_GPIO_DIRECTION_OUTPUT;
    cfg.pull = CM_GPIO_PULL_NONE;

	if(iic_id == CM_I2C_DEV_0)
	{
		cm_iomux_set_pin_func(CM_IOMUX_PIN_68, CM_IOMUX_FUNC_FUNCTION2);
    	cm_gpio_init(CM_GPIO_NUM_10, &cfg);
		cm_gpio_set_level(CM_GPIO_NUM_10, CM_GPIO_LEVEL_HIGH);
	}
    else
    {}

    //开启之前一定要先设置引脚复用
	if(iic_id == CM_I2C_DEV_0)
	{
		cm_iomux_set_pin_func(CM_IOMUX_PIN_66, CM_IOMUX_FUNC_FUNCTION1); //IIC0_SDA
		cm_iomux_set_pin_func(CM_IOMUX_PIN_67, CM_IOMUX_FUNC_FUNCTION1); //IIC0_SCL	
	}
	else if(iic_id == CM_I2C_DEV_1)
	{
		cm_iomux_set_pin_func(CM_IOMUX_PIN_52, CM_IOMUX_FUNC_FUNCTION2); //IIC1_SDA
		cm_iomux_set_pin_func(CM_IOMUX_PIN_53, CM_IOMUX_FUNC_FUNCTION2); //IIC1_SCL
	}
	else
	{
		cm_iomux_set_pin_func(CM_IOMUX_PIN_74, CM_IOMUX_FUNC_FUNCTION2); //IIC2_SDA
		cm_iomux_set_pin_func(CM_IOMUX_PIN_75, CM_IOMUX_FUNC_FUNCTION2); //IIC2_SCL
	}

	ret = cm_i2c_open(iic_id, &config);
	if(ret != 0)
	{
		sysPrintf("eeprom i2c init err,ret=%d\n", ret);
		return -1;
	}
	sysPrintf("i2c init ok\n");

	return 0;
}

int32_t i2c_writepage_bytes(uint16_t addr, int8_t * data, uint32_t len)
{
	int8_t * tmp = cm_malloc(sizeof(int8_t) * (len+2));
	if (tmp == NULL)
	{
		return -1;
	}

	int32_t ret = -1;

	tmp[0] = (addr >> 8) & 0xff;
	tmp[1] = addr & 0xff;
	for(int idx = 0; idx < len; idx++){
		tmp[2+idx] = data[idx];
	}
	
	ret = cm_i2c_write(EPROM_I2C_ID , EPROM_DEV_ADDR, tmp, 2+len);
	if(ret < 0)
	{
		sysPrintf("i2c write e2prom err\n" );
		return -1;
	}
	cm_free(tmp);
	return 0;
}


int32_t driver_eeprom_write(uint16_t MemoryAddr,int8_t * pRxData,int DataSize)
{
	int8_t ret = i2c_writepage_bytes(MemoryAddr, pRxData,DataSize);
	osDelay(5);
	if(ret < 0)
	{
		//CM_DEMO_I2C_LOG("driver_eeprom_write err\n");
		sysPrintf("add:%x, size:%d\r\n", MemoryAddr, DataSize);
		return -1;
	}
	return 0;
}

int32_t i2c_read_bytes(uint16_t addr, int8_t* data, uint32_t len)
{
	
	int8_t tmp[2] = {0};
	int32_t ret;

	if(data == NULL)
	{
		sysPrintf("is24c256_read_byte data ptr err\r\n");
		return -1;
	}

	tmp[0] = (addr >> 8) & 0xff;
	tmp[1] = addr & 0xff;

	ret = cm_i2c_write(EPROM_I2C_ID, EPROM_DEV_ADDR, tmp, 2);
	if(ret < 0)
	{
		sysPrintf("i2c read addr err(w)\n");
		return ret;
	}

	ret = cm_i2c_read(EPROM_I2C_ID, EPROM_DEV_ADDR, data, len);
	if(ret < 0)
	{
		sysPrintf("i2c read addr err(r)\n");
		return ret;
	}

	return 0;	
}

int32_t driver_eeprom_read(uint16_t MemoryAddr,int8_t * pTxData,int DataSize)
{
	int8_t ret =  i2c_read_bytes( MemoryAddr,pTxData, DataSize);
	if(ret < 0)
	{
		sysPrintf("driver_eeprom_read err\n");
		return -1;
	}
	return 0;
}
/*****************MQTT代码*****************************/

/* 主题 */
static char *cmmqtt_glob_subtopic = "/sys/a1zukTvYu5S/Vra8xiisfbZM6Cl5Qsal/thing/service/property/set";                         //订阅主题
static char *cmmqtt_glob_propertytopic = "/sys/a1zukTvYu5S/Vra8xiisfbZM6Cl5Qsal/thing/event/property/post";                     //属性上报

/* message */
// static char *cmmqtt_glob_pubmessage_str = "{\"params\":{\"Humidity\":20}}";                   /*测试信息，请添加转义字符\*/

/* 服务器地址 */
static char cmqtt_glob_cfgserver[129] = "a1zukTvYu5S.iot-as-mqtt.cn-shanghai.aliyuncs.com";                     //用户自行写入MQTT测试服务器地址

/* port号，非加密 */
static int cmqtt_glob_cfgport = 1883;                           //用户自行修改MQTT测试端口号

/* port号，加密 */
// static int cmqtt_glob_cfgport_ssl = 8883;                       //用户自行修改MQTTS测试端口号，屏蔽

/* client_id */
static char cmqtt_glob_cfgclient_id[129] = "a1zukTvYu5S.Vra8xiisfbZM6Cl5Qsal|securemode=2,signmethod=hmacsha256,timestamp=1744201557663|";                  //用户自行写入MQTT测试1号client_id

/* keepAlive参数 */
static int cmqtt_glob_cfgkeepAlive = 60;                        //用户自行修改连接保活时间

/* 用户名 */
static char cmqtt_glob_cfguser[129] = "**************";                       //用户自行写入MQTT测试用户名      

/* 密码 */
static char cmqtt_glob_cfgpasswd[129] = "************";                     //用户自行写入MQTT测试1号密码

/* 是否清除会话 */
static int cmqtt_glob_cfgclean = 1;                             //用户自行修改clean模式，0为不清除，1为清除

/** An enumeration of the PUBLISH flags. */
typedef enum
{
    CM_MQTT_PUBLISH_DUP = 8u,
    CM_MQTT_PUBLISH_QOS_0 = ((0u << 1) & 0x06),
    CM_MQTT_PUBLISH_QOS_1 = ((1u << 1) & 0x06),
    CM_MQTT_PUBLISH_QOS_2 = ((2u << 1) & 0x06),
    CM_MQTT_PUBLISH_QOS_MASK = ((3u << 1) & 0x06),
    CM_MQTT_PUBLISH_RETAIN = 0x01
} cm_mqtt_publish_flags_e;

#define CM_MQTT_CLIENT_MAX          5

static cm_mqtt_client_t *_mqtt_client[CM_MQTT_CLIENT_MAX] = {0};

/**
 *  \brief 连接状态回调
 *  
 *  \param [in] client mqtt客户端
 *  \param [in] session session标志
 *  \param [in] conn_res 连接状态
 *  \return 成功返回0，失败返回-1
 *  
 *  \details More details
 */
static int __mqtt_manager_default_connack_cb(cm_mqtt_client_t* client, int session, cm_mqtt_conn_state_e conn_res)
{

    int i = 0;
    for (; i < CM_MQTT_CLIENT_MAX; i++){
        if(_mqtt_client[i] == client){
            break;
        }
    }
    sysPrintf("\r\n[MQTT]CM MQTT index[%d] , CONNECT: %d\r\n", i, conn_res);
    if(conn_res==0)
    {
      chg_mqtt=1;//设备上线
      audio_play=1;//语音播报
      sysPrintf("device_tn_open\r\n");
    }
           
    return 0;
}

/**
 *  \brief server->client发布消息回调
 *  
 *  \param [in] client mqtt客户端
 *  \param [in] msgid 消息ID
 *  \param [in] topic 主题
 *  \param [in] payload 负载
 *  \param [in] payload_len 负载长度
 *  \param [in] total_len 负载总长度
 *  \return 成功返回0，失败返回-1，未匹配到主题返回1
 *  
 *  \details ToDo：增加消息缓存模式
 */
#define MAX_FIELDS 3
const char* target_fields[MAX_FIELDS] = {"WorkSwitch","power_ctrl","ctrlpower"};

static int __mqtt_manager_default_publish_cb(cm_mqtt_client_t* client, unsigned short msgid, char* topic, int total_len, int payload_len, char* payload)
{
    for (int j = 0; j < MAX_TOPICS; j++) 
    {
        if (strcmp(topic, target_topics[j]) == 0) 
        {//主题过滤
            break;
        }
        else
        {
            return 1;
        }
    }

    int i = 0;
    for (; i < CM_MQTT_CLIENT_MAX; i++){
        if(_mqtt_client[i] == client){
            break;
        }
    }

    sysPrintf("\r\n[MQTT]CM MQTT index[%d] , recv: %d,%s,%d,%d\r\n", i, msgid, topic, total_len, payload_len);//接收数据的函数
    int printf_size = payload_len > 128?128:payload_len;
    sysPrintf("\r\n[MQTT]CM MQTT index[%d] , recv: %.*s\r\n", i, printf_size, payload);

    // 2. 解析JSON负载
    cJSON *root = cJSON_Parse(payload);
    if (!root) {
        sysPrintf("\r\n[MQTT] JSON解析失败: %s\r\n", cJSON_GetErrorPtr());
        return -1;
    }

    // 3. 提取WorkSwitch字段
    cJSON *params = cJSON_GetObjectItem(root, "params");
    if (!params) {
        sysPrintf("\r\n[MQTT] 未找到params字段\r\n");
        cJSON_Delete(root);
        return -1;
    }

  for (int j = 0; j < MAX_FIELDS; j++) {
        const char *field = target_fields[j];
        cJSON *item = cJSON_GetObjectItem(params, field);

        if (item) 
        {
            int value = item->valueint;
            sysPrintf("\r\n[MQTT] %s值: %d\r\n", field, value);

            if (strcmp(field, "WorkSwitch") == 0) 
            {
                handle_work_switch(value); // 自定义处理函数
            }
            else if (strcmp(field, "power_ctrl") == 0) 
            {//功率控制
                handle_work_power(value);
            }
            else if (strcmp(field, "ctrlpower") == 0) 
            {//有序
                handle_work_ctrl(value);
            }
            else 
            {
                sysPrintf("\r\n[MQTT] %s字段类型错误\r\n", field);
            }
        }
    }

    // 5. 释放JSON内存
    cJSON_Delete(root);

    return 0;
}

/**
 * @author: tn
 * @description: 回调函数
 */
void handle_work_switch(int switch_value)
{
    if(switch_value == 1)
    {//启动充电
        chg_ctrl=1;//启动充电
        audio_play=2;//设备启动成功
        eeprom_addr();
    }
    else
    {
        chg_ctrl = 0;
        audio_play=5;
        eeprom_del();
    }
}

uint16_t map_to_resistance(uint16_t input) 
{
    // 输入范围: 0-100
    // 输出范围: 97-127
    // 反转公式: output = 127 - (input * 30 / 100)
    return 127 - (input * 30 / 100);
}

void handle_work_power(int power_value) 
{
    uint16_t resistance_value = map_to_resistance(power_value);
    sysPrintf("test=%d", resistance_value);
    tpl0401a_ctrl(resistance_value); // 调控电阻
}

// void handle_work_power(int power_value) 
// {//pid控制
//     uint16_t resistance_value = PID_calc(power_value);
//     sysPrintf("test=%d", resistance_value);
//     tpl0401a_ctrl(resistance_value); // 调控电阻
// }

void handle_work_ctrl(int power_value)
{
    sysPrintf("test=%d",power_value);
    tpl0401a_ctrl(power_value);//调控电阻
}


/**
 *  \brief client->server发布消息ack回调
 *  
 *  \param [in] client mqtt客户端
 *  \param [in] msgid 消息ID
 *  \param [in] dup dup标志
 *  \return 成功返回0，失败返回-1
 *  
 *  \details More details
 */
static int __mqtt_manager_default_puback_cb(cm_mqtt_client_t* client, unsigned short msgid, char dup)
{
    int i = 0;
    for (; i < CM_MQTT_CLIENT_MAX; i++){
        if(_mqtt_client[i] == client){
            break;
        }
    }
    // sysPrintf("\r\n[MQTT]CM MQTT index[%d] , pub_ack: %d,%d\r\n", i, msgid, dup);
    return 0;
}


/**
 *  \brief client->server发布消息recv回调
 *  
 *  \param [in] client mqtt客户端
 *  \param [in] msgid 消息ID
 *  \param [in] dup dup标志
 *  \return 成功返回0，失败返回-1
 *  
 *  \details More details
 */
static int __mqtt_manager_default_pubrec_cb(cm_mqtt_client_t* client, unsigned short msgid, char dup)
{
    int i = 0;
    for (; i < CM_MQTT_CLIENT_MAX; i++){
        if(_mqtt_client[i] == client){
            break;
        }
    }
    sysPrintf("\r\n[MQTT]CM MQTT index[%d] , pub_rec: %d,%d\r\n", i, msgid, dup);
    return 0;
}


/**
 *  \brief client->server发布消息comp回调
 *  
 *  \param [in] client mqtt客户端
 *  \param [in] msgid 消息ID
 *  \param [in] dup dup标志
 *  \return 成功返回0，失败返回-1
 *  
 *  \details More details
 */
static int __mqtt_manager_default_pubcomp_cb(cm_mqtt_client_t* client, unsigned short msgid, char dup)
{
    int i = 0;
    for (; i < CM_MQTT_CLIENT_MAX; i++){
        if(_mqtt_client[i] == client){
            break;
        }
    }
    sysPrintf("\r\n[MQTT]CM MQTT index[%d] , pub_comp: %d,%d\r\n", i, msgid, dup);
    return 0;
}


/**
 *  \brief 订阅ack回调
 *  
 *  \param [in] client mqtt客户端
 *  \param [in] msgid 消息ID
 *  \return 成功返回0，失败返回-1
 *  
 *  \details More details
 */
static int __mqtt_manager_default_suback_cb(cm_mqtt_client_t* client, unsigned short msgid, int count, int qos[])
{
    int i = 0;
    for (; i < CM_MQTT_CLIENT_MAX; i++){
        if(_mqtt_client[i] == client){
            break;
        }
    }
    // sysPrintf("\r\n[MQTT]CM MQTT index[%d] , sub_ack: %d\r\n", i, msgid);
    return 0;
}


/**
 *  \brief 取消订阅ack回调
 *  
 *  \param [in] client mqtt客户端
 *  \param [in] msgid 消息ID
 *  \return 成功返回0，失败返回-1
 *  
 *  \details More details
 */
static int __mqtt_manager_default_unsuback_cb(cm_mqtt_client_t* client, unsigned short msgid)
{
    int i = 0;
    for (; i < CM_MQTT_CLIENT_MAX; i++){
        if(_mqtt_client[i] == client){
            break;
        }
    }
    sysPrintf("\r\n[MQTT]CM MQTT index[%d] , unsub_ack: %d\r\n", i, msgid);
    return 0;
}


/**
 *  \brief ping回调
 *  
 *  \param [in] client mqtt客户端
 *  \param [in] ret 消息状态，0：ping成功，1：ping超时
 *  \return 成功返回0，失败返回-1
 *  
 *  \details More details
 */
static int __mqtt_manager_default_pingresp_cb(cm_mqtt_client_t* client, int ret)
{
    int i = 0;
    for (; i < CM_MQTT_CLIENT_MAX; i++){
        if(_mqtt_client[i] == client){
            break;
        }
    }
    // sysPrintf("\r\n[MQTT]CM MQTT index[%d] , pingrsp: %d\r\n", i, ret);//将PINGH回调暂时屏蔽

    return 0;
}


/**
 *  \brief 消息超时回调，包括publish/subscribe/unsubscribe等
 *  
 *  \param [in] client mqtt客户端
 *  \param [in] msgid 消息ID
 *  \return 成功返回0，失败返回-1
 *  
 *  \details More details
 */
static int __mqtt_manager_default_timeout_cb(cm_mqtt_client_t* client, unsigned short msgid)
{
    int i = 0;
    for (; i < CM_MQTT_CLIENT_MAX; i++){
        if(_mqtt_client[i] == client){
            break;
        }
    }
    sysPrintf("\r\n[MQTT]CM MQTT index[%d] , timeout: %d\r\n", i, msgid);
    return 0;
}

static void __cm_get_sub_topic(cm_mqtt_client_t* client)
{
    linklist_t* list = cm_mqtt_client_get_sub_topics(client);//获取topic列表

    if(list == NULL || list->count == 0)
    {
        return;
    }

    char topic_tmp[200] = {0};
    int tmp_len = 0;
    int sub_topic_count = 0;
    linklist_element_t* element = NULL;
    cm_mqtt_topic_t* topic_msg = NULL;

    while((element = linklist_next_element(list, &element)) != NULL)    //取出元素
    {
        topic_msg = (cm_mqtt_topic_t*)element->content;

        if(topic_msg->state != CM_MQTT_TOPIC_SUBSCRIBED)
        {
            continue;
        }
        /* 已订阅 */
        sub_topic_count++;
        memcpy(topic_tmp + tmp_len, topic_msg->topic, topic_msg->topic_len);
        tmp_len += topic_msg->topic_len;
        tmp_len += snprintf(topic_tmp + tmp_len, sizeof(topic_tmp) - tmp_len, ",%d,", topic_msg->qos);
    }

    topic_tmp[tmp_len - 1] = '\0';

    if(sub_topic_count > 0)
    {
        sysPrintf("\r\n%s\r\n", topic_tmp);
    }
}

static int __mqtt_client_init(void)
{
    if(_mqtt_client[0]){
        sysPrintf("\r\n[MQTT]CM MQTT CLIENT IS RUN!!!\r\n");
        return -1;
    }
    _mqtt_client[0] = cm_mqtt_client_create();  //client初始化，最大支持五个实例
    if (NULL == _mqtt_client[0]){
        sysPrintf("\r\n[MQTT]CM MQTT CREATE CLIENT ERROR!!!\r\n");
        return -1;
    }
    /* 设置回调函数 */
    cm_mqtt_client_cb_t callback = {0};
    callback.connack_cb = __mqtt_manager_default_connack_cb;
    callback.publish_cb = __mqtt_manager_default_publish_cb;
    callback.puback_cb = __mqtt_manager_default_puback_cb;
    callback.pubrec_cb = __mqtt_manager_default_pubrec_cb;
    callback.pubcomp_cb = __mqtt_manager_default_pubcomp_cb;
    callback.suback_cb = __mqtt_manager_default_suback_cb;
    callback.unsuback_cb = __mqtt_manager_default_unsuback_cb;
    callback.pingresp_cb = __mqtt_manager_default_pingresp_cb;
    callback.timeout_cb = __mqtt_manager_default_timeout_cb;

    /* 设置client参数 */
    int version = 4;//版本3.1.1
    int pkt_timeout = 10;//发送超时10秒
    int reconn_times = 3;//重连三次
    int reconn_cycle = 20;//重连间隔20秒
    int reconn_mode = 0;//以固定间隔尝试重连
    int retry_times = 3;//重传三次
    int ping_cycle = 60;//ping周期60秒
    int dns_priority = 2;//MQTT dns解析ipv6优先

    cm_mqtt_client_set_opt(_mqtt_client[0], CM_MQTT_OPT_EVENT, (void*)&callback);
    cm_mqtt_client_set_opt(_mqtt_client[0], CM_MQTT_OPT_VERSION, (void*)&version);
    cm_mqtt_client_set_opt(_mqtt_client[0], CM_MQTT_OPT_PKT_TIMEOUT, (void*)&pkt_timeout);
    cm_mqtt_client_set_opt(_mqtt_client[0], CM_MQTT_OPT_RETRY_TIMES, (void*)&retry_times);
    cm_mqtt_client_set_opt(_mqtt_client[0], CM_MQTT_OPT_RECONN_MODE, (void*)&reconn_mode);
    cm_mqtt_client_set_opt(_mqtt_client[0], CM_MQTT_OPT_RECONN_TIMES, (void*)&reconn_times);
    cm_mqtt_client_set_opt(_mqtt_client[0], CM_MQTT_OPT_RECONN_CYCLE, (void*)&reconn_cycle);
    cm_mqtt_client_set_opt(_mqtt_client[0], CM_MQTT_OPT_PING_CYCLE, (void*)&ping_cycle);
    cm_mqtt_client_set_opt(_mqtt_client[0], CM_MQTT_OPT_DNS_PRIORITY, (void*)&dns_priority);
    return 0;
}

// static int __mqtt_ssl_init(void)
// {//屏蔽mqtts
//     int ssl_id = 1;     //SSL通道
//     int verify = 0;     //SSL类型
//     int negotime = 60;  //握手超时(s)
//     int ssl_enable = 1; //开启SSL
//     cm_ssl_setopt(ssl_id, CM_SSL_PARAM_VERIFY, &verify); 
//     cm_ssl_setopt(ssl_id, CM_SSL_PARAM_NEGOTIME, &negotime);
//     cm_mqtt_client_set_opt(_mqtt_client[0], CM_MQTT_OPT_SSL_ID, &ssl_id);
//     cm_mqtt_client_set_opt(_mqtt_client[0], CM_MQTT_OPT_SSL_ENABLE, &ssl_enable);
//     return 0;
// }

/**
 * @author: tn
 * @description: MQTT连接函数，需要修改AT指令的参数
 * @return {*}
 */
void MQTT_connect(void)
{
  int ret = 0;
  /* 配置连接参数，对于字符串参数，内部仅保留指针，不分配空间 */
  cm_mqtt_connect_options_t conn_options = {
      .hostport = (unsigned short)cmqtt_glob_cfgport,
      .hostname = (const char*)cmqtt_glob_cfgserver,
      .clientid = (const char*)cmqtt_glob_cfgclient_id,
      .username = (const char*)cmqtt_glob_cfguser,
      .password = (const char*)cmqtt_glob_cfgpasswd,
      .keepalive = (unsigned short)cmqtt_glob_cfgkeepAlive,
      .will_topic = NULL,
      .will_message = NULL,
      .will_flag = 0, //若要使用遗嘱机制请置1，并补充相关遗嘱信息
      .clean_session = (char)cmqtt_glob_cfgclean,
      };
  //这个使用的系统自带的封装函数，所以需要自己写AT
  ret = cm_mqtt_client_connect(_mqtt_client[0], &conn_options);//连接
  sysPrintf("\r\nCM MQTT connect = %d\r\n", ret);
}

/**
 * @author: tn
 * @description: 订阅mqtt主题，需要修改主题cmmqtt_glob_subtopic
 * 
 * 订阅服务器可发布的主题，当服务器向该主题发送消息时，所有订阅该主题的设备（包括单片机）会自动接收消息
 * 
 * 订阅主题和发布主题在例程是同一个主题，所以在发布主题的时候已经订阅了
 * 都在/sys/a1zukTvYu5S/Vra8xiisfbZM6Cl5Qsal/thing
 * 
 * 
 * 
 * ？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？？
 * 不需要订阅主题就可以接受到消息
 * 
 * @return {*}
 */
void MQTT_subscribe(void)
{
  char *topic_tmp[1] = {0};
  topic_tmp[0] = cmmqtt_glob_subtopic;
  char qos_tmp[1] = {0};
  qos_tmp[0] = 0;
  int ret = cm_mqtt_client_subscribe(_mqtt_client[0], (const char**)topic_tmp, qos_tmp, 1);
  if (0 > ret){
      sysPrintf("\r\n[MQTT]CM MQTT subscribe ERROR!!!, ret = %d\r\n", ret);
  }
  else
  {
    sysPrintf("已订阅主题");
  }
}

/**
 * @author: tn
 * @description: 发布主题，订阅mqtt属性上报，需要使用JSON格式进行打印
 * {"params":{"temp":40}}
 * CM_MQTT_PUBLISH_QOS_1
 * @return {*}
 */
// void MQTT_publish(void)
// {
//     uint8_t publish_flags = 0;
//     publish_flags |= CM_MQTT_PUBLISH_QOS_1;
//     char *pubmessage = cmmqtt_glob_pubmessage_str;
//     int ret = cm_mqtt_client_publish(_mqtt_client[0], cmmqtt_glob_propertytopic, pubmessage, strlen(pubmessage), publish_flags);//发布消息
//     if (0 >= ret){
//         sysPrintf("\r\n[MQTT]CM MQTT publish ERROR!!!, ret = %d\r\n", ret);
//     }
// }

/**
 * @author: tn
 * @description: 发布主题，订阅mqtt属性上报，需要使用JSON格式进行打印
 * {"params":{"temp":40}}
 * CM_MQTT_PUBLISH_QOS_1
 * @return {*}
 */
void MQTT_publish(char *pubmessage,cm_mqtt_publish_flags_e qos_level)
{
    uint8_t publish_flags = 0;

    // 根据qos_level设置QoS标志位
    switch (qos_level) {
        case CM_MQTT_PUBLISH_QOS_1:
            publish_flags |= CM_MQTT_PUBLISH_QOS_1;
            break;
        case CM_MQTT_PUBLISH_QOS_2:
            publish_flags |= CM_MQTT_PUBLISH_QOS_2;
            break;
        // 默认QoS 0
        default:
            publish_flags |= CM_MQTT_PUBLISH_QOS_0;
    }

    int ret = cm_mqtt_client_publish(
        _mqtt_client[0], cmmqtt_glob_propertytopic, pubmessage, strlen(pubmessage), publish_flags
    );
    
    if (ret <= 0) {
        sysPrintf("\r\n[MQTT]CM MQTT publish ERROR!!!, ret = %d\r\n", ret);
    }
}

/**
 * @author: tn
 * @description: 确认设备的状态，发送到云平台
 * @return {*}
 */
void chg_open_ctrl(void)
{
    // static char chg_buf[30] = "{"method":"thing.service.property.set","id":"","params":{"WorkSwitch":1},"version":"1.0.0"}";
    static char chg_buf[50] = "{\"method\":\"thing.service.property.set\",\"id\":\"\",\"params\":{\"WorkSwitch\":1},\"version\":\"1.0.0\"}";
    if(chg_ctrl == 1)
    {//设备启动充电
        MQTT_publish(chg_buf,CM_MQTT_PUBLISH_QOS_2);//qos2
    }
}

/**
 * @author: tn
 * @description: 写入eeprom标志位
 * @return {*}
 */
void eeprom_addr(void)
{
	int8_t  w_data[1] = {0x10};

    driver_eeprom_write(eprom_addr, w_data,1);
    osDelay(10);//延时等待写入完成
    sysPrintf("写入充电标志位");
}

/**
 * @author: tn
 * @description: 清除标志位
 * @return {*}
 */
void eeprom_del(void)
{
    int8_t  w_data[1] = {0x00};

    driver_eeprom_write(eprom_addr, w_data,1);
    osDelay(10);//延时等待写入完成
    sysPrintf("清除充电标志位");
}

/**
 * @author: tn
 * @description: 读标志位
 * @return {*}1 标志位为归零
 * 0 标志位归零
 */
uint8_t eeprom_read(void)
{
    int8_t r_data[5] = {};
    driver_eeprom_read(eprom_addr, r_data,1);
    osDelay(100);
    if(r_data[0] == 0x10)
    {
        sysPrintf("设备异常断电，重启设备！！！");
        return 1;
    }
    return 0;
}

/**
 * @author: tn
 * @description: 通信入口函数
 * 
 * 获取消息队列，推送过快会导致推送失败
 * 
 *  QoS 0	不保证送达	单向发送，无确认	最低
    QoS 1	至少一次（可能重复）	发送后等待确认（PUBACK）	中等
    QoS 2	恰好一次（不重复）	两次握手（PUBREC/PUBCOMP）	最高
 * @return {*}
 */
void module_task_entry(void)
{
    // PID_init();
    uint ret=0;//初始化标志位
    chg_mqtt=-1;

    hal_iic_init_driver(CM_I2C_DEV_0);
    osDelay(100);
    if(eeprom_read()==1)
    {
        chg_ctrl=1;//启动充电
        audio_play=2;//设备启动成功
        eeprom_addr();
    }
    else
    {
        sysPrintf("eepprom_init_ok");
    }

    // MQTT_subscribe();//订阅主题
    while (1)
    {
        /*MQTT连接初始化*/
        if(ret==0)
        {//只初始化一次
            __mqtt_client_init();
            MQTT_connect();
        ret=1;
        }
        if(chg_mqtt==1)
        {
            if(GetMqttMessQueue(Mqtt_buf)==0)
            {//获取消息队列
                MQTT_publish(Mqtt_buf,CM_MQTT_PUBLISH_QOS_0);
                // sysPrintf("MQTT Data: %s\r\n", Mqtt_buf);
            }
            else
            {
                // sysPrintf("MQTT MessQueue err");
            }
            osDelay(20);
            if(GetmeterMessQueue(meter_buf)==0)
            {//获取消息队列
                MQTT_publish(meter_buf,CM_MQTT_PUBLISH_QOS_0);
                // sysPrintf("MQTT Data: %s\r\n", meter_buf);
            }
            else
            {
                // sysPrintf("meter MessQueue err");
            }
        }
        osDelay(500/5); 
    }
}

