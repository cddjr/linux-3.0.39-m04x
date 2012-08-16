/*
 * Touch key & led driver for meizu m040
 *
 * Copyright (C) 2012 Meizu Technology Co.Ltd, Zhuhai, China
 * Author:		
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <plat/gpio-cfg.h>
#include <mach/gpio-m040.h>
#include <linux/firmware.h>
#include	<linux/mx_qm.h>


#define QM_MX_FW "qm_mx_led.bin"

#define 	RESET_COLD	1
#define	RESET_SOFT	0

//#define	VERIFY_CRC

struct mx_qm_reg_data {
	unsigned char addr;
	unsigned char value;
};

const struct mx_qm_reg_data init_regs[] = {
	{LED_REG_LEDMAUTO,1},
	{},			
};

	
static struct mfd_cell mx_qm_devs[] = {
	{ .name = "mx-qm-touch", .id = 0 },
	{ .name = "mx-qm-led", .id = 1 },
	{ .name = "mx-qm-led", .id = 2 },
	{ .name = "mx-qm-led", .id = 3 },
	{ .name = "mx-qm-led", .id = 4 },
};

static int mx_qm_update(struct mx_qm_data *mx);
static void mx_qm_reset(struct mx_qm_data *mx,int m);
static void mx_qm_wakeup(struct mx_qm_data *mx,int bEn);

static int mx_qm_read(struct i2c_client *client,
				  int bytes, void *dest)
{
	struct i2c_msg xfer;
	int ret;

	xfer.addr =  client->addr;
	xfer.flags = I2C_M_RD;
	xfer.len = bytes;
	xfer.buf = (char *)dest;

	ret = i2c_transfer(client->adapter, &xfer, 1);

	if (ret < 0)
		goto err_read;
	if (ret != 1) {
		ret = -EIO;
		goto err_read;
	}

	return 0;

err_read:
	return ret;
}

static int mx_qm_write(struct i2c_client *client,
				int bytes, const void *src)
{
	struct i2c_msg xfer;
	int ret;

	xfer.addr = client->addr;
	xfer.flags = 0;
	xfer.len = bytes;
	xfer.buf = (char *)src;

	ret = i2c_transfer(client->adapter, &xfer, 1);
	if (ret < 0)
		goto err_write;
	if (ret != 1) {
		ret = -EIO;
		goto err_write;
	}

	return 0;

err_write:
	return ret;
}

static int mx_qm_readbyte(struct i2c_client *client, u8 reg)
{
	struct mx_qm_data *mx = i2c_get_clientdata(client);
	int ret;
	
	mutex_lock(&mx->iolock);

	ret = mx_qm_write(client,1,&reg);
	if (ret < 0)
		dev_err(&client->dev,
			"can not write register, returned %d at line %d\n", ret,__LINE__);
	
	ret = mx_qm_read(client, 1,&reg);
	if (ret < 0)
		dev_err(&client->dev,
			"can not read register, returned %d at line %d\n", ret,__LINE__);
	else
		ret = reg;

	mutex_unlock(&mx->iolock);

	return ret;
}

static int mx_qm_writebyte(struct i2c_client *client, u8 reg, u8 data)
{
	struct mx_qm_data *mx = i2c_get_clientdata(client);
	int ret;	
	unsigned char buf[2];

	mutex_lock(&mx->iolock);

	buf[0] = reg;
	buf[1] = data;
	ret = mx_qm_write(client,2,buf);
	if (ret < 0)
		dev_err(&client->dev,
			"can not write register, returned %d at line %d\n", ret,__LINE__);

	mutex_unlock(&mx->iolock);

	return ret;
}


static int mx_qm_readword(struct i2c_client *client, u8 reg)
{
	struct mx_qm_data *mx = i2c_get_clientdata(client);
	int ret;	
	u16 val;

	mutex_lock(&mx->iolock);

	ret = mx_qm_write(client,1,&reg);
	if (ret < 0)
		dev_err(&client->dev,
			"can not read register, returned %d at line %d\n", ret,__LINE__);
	msleep(10);
	ret = mx_qm_read(client, 2,&val);
	if (ret < 0)
		dev_err(&client->dev,
			"can not read register, returned %d\n", ret);
	else
		ret = val;


	mutex_unlock(&mx->iolock);

	return ret;
}

static int mx_qm_writeword(struct i2c_client *client, u8 reg, u16 data)
{
	struct mx_qm_data *mx = i2c_get_clientdata(client);
	int ret;	
	unsigned char buf[3];

	mutex_lock(&mx->iolock);

	buf[0] = reg;
	buf[1] = data & 0xFF;
	buf[2] = (data >>8 ) & 0xFF;
	ret = mx_qm_write(client,3,buf);
	if (ret < 0)
		dev_err(&client->dev,
			"can not write register, returned %d\n", ret);


	mutex_unlock(&mx->iolock);

	return ret;
}

static void __devinit mx_qm_init_registers(struct mx_qm_data *mx)
{
	int i, ret;

	for (i=0; i<ARRAY_SIZE(init_regs); i++) {
		ret = mx_qm_writebyte(mx->client, init_regs[i].addr, init_regs[i].value);
		if (ret) {
			dev_err(mx->dev, "failed to init reg[%d], ret = %d\n", i, ret);
		}
	}
}


static bool __devinit mx_qm_identify(struct mx_qm_data *mx)
{
	int id, ver;
	struct i2c_client *client = mx->client;

	/* Read Chip ID */
	id = mx_qm_readbyte(client, QM_REG_DEVICE_ID);
	if (id != MX_QM_DEVICE_ID) {
		dev_err(&client->dev, "ID %d not supported\n", id);
		return false;
	}

	/* Read firmware version */
	ver = mx_qm_readbyte(client, QM_REG_VERSION);
	if (ver < 0) {
		dev_err(&client->dev, "could not read the firmware version\n");
		return false;
	}
	
	if( ver < FW_VERSION)
	{
		dev_info(&client->dev, "Old firmware version %x ,need be update\n", ver);
		mx_qm_update(mx);
		mx_qm_wakeup(mx,true);
	}

	dev_info(&client->dev, "MX_QM id 0x%.2X firmware version %x \n", id,ver);

	return true;
}

static void mx_qm_reset(struct mx_qm_data *mx,int m)
{
	if(m)
	{	
		s3c_gpio_setpull(mx->gpio_reset, S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(mx->gpio_reset, S3C_GPIO_OUTPUT);
		gpio_set_value(mx->gpio_reset,  0);
		msleep(100);
		gpio_set_value(mx->gpio_reset,  1);
		s3c_gpio_cfgpin(mx->gpio_reset, S3C_GPIO_INPUT);
		
	}
	else
	{

	}
	dev_info(&mx->client->dev, "%s reset. \n", m?"cold":"soft");
}


static void mx_qm_wakeup(struct mx_qm_data *mx,int bEn)
{
	if( bEn )
	{
		s3c_gpio_setpull(mx->gpio_wake, S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(mx->gpio_wake, S3C_GPIO_OUTPUT);
		gpio_set_value(mx->gpio_wake,  0);
	}
	else
	{	
		s3c_gpio_setpull(mx->gpio_wake, S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(mx->gpio_wake, S3C_GPIO_INPUT);
	}	

	msleep(50);

	dev_dbg(&mx->client->dev, "mx_qm_wakeup %s\n",bEn?"enable":"disable");
}


#define TWI_CMD_BOOT      'D'
#define TWI_CMD_UPD  	 'U'
#define TWI_CMD_END  	 'E'
#define TWI_CMD_CRC	'C'//get the flash crc
#define TWI_CMD_BVER	'B'//get the bootloader revision
#define	PAGESIZE		64
static void mx_qm_firmware_handler(const struct firmware *fw,
        void *context)
{
	struct mx_qm_data * mx = context;

	int ret = 0;

	unsigned char cmd;
	int i,cnt,size,try_cnt;
	unsigned char buf[PAGESIZE+3];
	const unsigned char * ptr;
	unsigned short crc1;
	unsigned short crc2;


	try_cnt = 0;
	size = fw->size;	// 1024*6
	ptr = fw->data;

	mx_qm_reset(mx,RESET_COLD);
	mx_qm_wakeup(mx,true);
	
	msleep(50);

	cmd = TWI_CMD_BOOT;
	ret = mx_qm_write(mx->client,1,&cmd);
	if(ret < 0 )
	{
		dev_err(&mx->client->dev,"can not write register, returned %d at line %d\n", ret,__LINE__);
		goto err_exit;

	}
	dev_info(&mx->client->dev,"mx qmatrix sensor updating ... \n");
	msleep(10);

	cmd = TWI_CMD_BVER; //
	ret = 0;
	ret = mx_qm_readbyte(mx->client,cmd);
	if(ret < 0 )
	{
		dev_err(&mx->client->dev,"can not read the bootloader revision, returned %d at line %d\n", ret,__LINE__);
		goto err_exit;
	}
	else
	{
		dev_info(&mx->client->dev,"bootloader revision %d\n", ret);
		if( ret > 2 )
			try_cnt = 3;
	}
	
	
	do
	{
		buf[0] = TWI_CMD_UPD;
		for(i=0;i<size;i+=PAGESIZE)
		{
			buf[1] = i & 0xFF;
			buf[2] = (i>>8) & 0xFF;

			memcpy(&buf[3],ptr+i,PAGESIZE);

			cnt = 3;

			do
			{
				ret = mx_qm_write(mx->client,sizeof(buf),buf);
			}while(ret < 0 && cnt--);
			
			if(ret < 0 )
				dev_err(&mx->client->dev,"can not write register, returned %d at addres 0x%.4X(page %d)\n", ret,i,(i/PAGESIZE+1));
		}	

		cmd = TWI_CMD_CRC;
		ret = mx_qm_write(mx->client,1,&cmd);
		ret = mx_qm_read(mx->client,1,&crc1);
		crc1 <<= 8;
		ret = mx_qm_read(mx->client,1,&crc1);
		crc2  = *(unsigned short *)(fw->data+fw->size-2);
		if( crc1 != crc2)
		{
			dev_err(&mx->client->dev,"crc check 0x%.4X (0x%.4X) failed !!!\n", crc1,crc2);
		}
		else
		{
			dev_info(&mx->client->dev,"Verifying Flash OK! CRC 0x%.4X\n", crc1);
			break;
		}		
	}while(try_cnt--);
	
	cmd = TWI_CMD_END;
	ret = mx_qm_write(mx->client,1,&cmd);
	if(ret < 0 )
	{
		dev_err(&mx->client->dev,"can not write register, returned %d at line %d\n", ret,__LINE__);
		goto err_exit;

	}
	
ok_exit:
	dev_info(&mx->client->dev, "Update completed. \n");
	goto exit;
	
err_exit:	
	dev_info(&mx->client->dev, "Update failed !! \n");
	
exit:
	mx_qm_wakeup(mx,false);
	mx_qm_reset(mx,RESET_COLD); 
	msleep(250);	
	mx_qm_wakeup(mx,true);
	release_firmware(fw);
	return;
}

static int mx_qm_update_async(struct mx_qm_data *mx)
{
	int ret = 0;
	
#ifndef	CONFIG_M040_USB_PCB1
		disable_irq(mx->irq);	
		msleep(100);
#endif	

	ret = request_firmware_nowait(THIS_MODULE,
	        FW_ACTION_HOTPLUG,
	        QM_MX_FW,
	        &mx->client->dev,
	        GFP_KERNEL | __GFP_ZERO,
	        mx,
	        mx_qm_firmware_handler);
	
#ifndef	CONFIG_M040_USB_PCB1
		enable_irq(mx->irq);
#endif	
	
	return ret;
}

//����CRC 
uint16_t calcrc16(char *ptr, int count) 
{ 
    int  j = 0; 
    uint16_t crc = 0; 
    uint8_t data;
    char i; 
  
    while (j < count )
  {
      data = *ptr++;
    
        crc = crc ^ (uint16_t) data << 8; 
        i = 8; 
        do 
        { 
        if (crc & 0x8000) 
            crc = crc << 1 ^ 0x1021; 
        else 
            crc = crc << 1; 
        } while(--i); 
          
    j++;
  }
  
  return (crc); 
} 

static int mx_qm_update(struct mx_qm_data *mx)
{
	int err = 0;
	const struct firmware *fw;
	const char *fw_name;
	
#ifndef	CONFIG_M040_USB_PCB1
	disable_irq(mx->irq);	
	msleep(100);
#endif	

	fw_name = QM_MX_FW;

	err = request_firmware(&fw, fw_name,  &mx->client->dev);
	if (err) {
		printk(KERN_ERR "Failed to load firmware \"%s\"\n", fw_name);
		return err;
	}
	
	mx_qm_firmware_handler(fw,(void *)mx);
	pr_info("%s:load firmware %s\n", dev_name(&mx->client->dev), fw_name);
	
#ifndef	CONFIG_M040_USB_PCB1
	enable_irq(mx->irq);
#endif	

	return err;
}

static ssize_t qm_show_property(struct device *dev,
                                      struct device_attribute *attr,
                                      char *buf);
static ssize_t qm_store(struct device *dev, 
			     struct device_attribute *attr,
			     const char *buf, size_t count);

#define QM_ATTR(_name)\
{\
    .attr = { .name = #_name, .mode = S_IRUGO | S_IWUSR},\
    .show = qm_show_property,\
    .store = qm_store,\
}

static struct device_attribute qm_attrs[] = {
    QM_ATTR(status),
    QM_ATTR(position),
    QM_ATTR(cmd),
    QM_ATTR(reset),
    QM_ATTR(version),
    QM_ATTR(led),
    QM_ATTR(update),
};
enum {
	QM_STATUS,
	QM_POS,
	QM_CMD,
	QM_RESET,
	QM_FWR_VER,
	QM_LED,
	QM_UPD,
};
static ssize_t qm_show_property(struct device *dev,
                                      struct device_attribute *attr,
                                      char *buf)
{
	int i = 0;
	ptrdiff_t off;
	struct mx_qm_data *qm = (struct mx_qm_data*)dev_get_drvdata(dev);
	
	if(!qm)
	{
		pr_err("%s(): failed!!!\n", __func__);
		return -ENODEV;
	}

	off = attr - qm_attrs;

	switch(off){
	case QM_POS:
		i += scnprintf(buf+i, PAGE_SIZE-i, "%d\n",mx_qm_readbyte(qm->client,QM_REG_POSITION));
		break;
	case QM_STATUS:
		i += scnprintf(buf+i, PAGE_SIZE-i, "%d\n",mx_qm_readbyte(qm->client,QM_REG_STATUS));
		break;
	case QM_CMD:
		{
			int ret,val;

			ret = mx_qm_read(qm->client,1,&val);
			if (ret < 0)
				pr_err("mx_qm_readbyte error at %d line\n", __LINE__);
			i += scnprintf(buf+i, PAGE_SIZE-i, "0x%.8X\n",val);
		}
		break;
	case QM_FWR_VER:
		i += scnprintf(buf+i, PAGE_SIZE-i, "ID = %d ,Ver.%d\n",mx_qm_readbyte(qm->client,QM_REG_DEVICE_ID),mx_qm_readbyte(qm->client,QM_REG_VERSION));
		break;
	case QM_RESET:
		i += scnprintf(buf+i, PAGE_SIZE-i, "\n");
		break;
	case QM_LED:
		i += scnprintf(buf+i, PAGE_SIZE-i, "\n");
		break;
	case QM_UPD:
		mx_qm_update(qm);
		i += scnprintf(buf+i, PAGE_SIZE-i, "\n");
		break;
	default:
		i += scnprintf(buf+i, PAGE_SIZE-i, "Error\n");
		break;	
	}
	return i;
}

static ssize_t qm_store(struct device *dev, 
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	unsigned int reg,value;
	int ret = 0;
	ptrdiff_t off;
	struct mx_qm_data *qm = (struct mx_qm_data*)dev_get_drvdata(dev);
	
	if(!qm)
	{
		pr_err("%s(): failed!!!\n", __func__);
		return -ENODEV;
	}

	off = attr - qm_attrs;

	switch(off){
	case QM_POS:
		if (sscanf(buf, "%d\n", &value) == 1) {	
			
		}
		ret = count;
		break;
	case QM_STATUS:
		if (sscanf(buf, "%d\n", &value) == 1) {	
			
		}
		ret = count;
		break;
	case QM_CMD:
		if (sscanf(buf, "%x %x", &reg, &value) == 2) {
			int ret;
			unsigned char msg[2];
			msg[0] = reg;
			msg[1] = value;			
			dev_info(dev, "R:0x%.2X V:0x%.2X \n", reg,value);
			ret = mx_qm_write(qm->client,2,msg);
			if (ret < 0)
				pr_err("mx_qm_writebyte error at %d line\n", __LINE__);
		}
		else if (sscanf(buf, "%x\n", &value) == 1) {	
			int ret;
			unsigned char msg[2];
			msg[0] = value;			
			dev_info(dev, "V:0x%.2X \n", value);
			ret = mx_qm_write(qm->client,1,msg);
			if (ret < 0)
				pr_err("mx_qm_writebyte error at %d line\n", __LINE__);
			
		}
		else {			
			pr_err("%s(): failed !!!\n", __func__);
		}
		ret = count;
		break;
	case QM_RESET:
		if (sscanf(buf, "%x\n", &value) == 1) {	

			mx_qm_reset(qm,value);
			pr_info("Sensor %s reset. \n", value?"cold":"soft");
		}
		ret = count;
		break;
	case QM_FWR_VER:
		ret = count;
		break;
	case QM_LED:
		ret = count;
		if (sscanf(buf, "%x %x", &reg, &value) == 2) {
			reg = reg+LED_REG_LEDM0;
			
			if(reg <= LED_REG_LEDM6 && value < 7)
			{				
				ret = mx_qm_writebyte(qm->client,reg,value);
				if (ret < 0)
				{
					pr_err("mx_qm_writebyte error at %d line\n", __LINE__);
					ret = -EINVAL;
				}
				else
				{
					ret = count;
				}
				
			}
			else
			{
				ret = -EINVAL;
			}
		}
		break;
	case QM_UPD:
		mx_qm_update(qm);
		ret = count;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;	
}

static int qm_create_attrs(struct device * dev)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(qm_attrs); i++) {
		rc = device_create_file(dev, &qm_attrs[i]);
		if (rc)
			goto qm_attrs_failed;
	}
	goto succeed;

qm_attrs_failed:
	printk(KERN_INFO "%s(): failed!!!\n", __func__);	
	while (i--)
		device_remove_file(dev, &qm_attrs[i]);
succeed:		
	return rc;

}

static void qm_destroy_atts(struct device * dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(qm_attrs); i++)
		device_remove_file(dev, &qm_attrs[i]);
}

static int __devinit mx_qm_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct mx_qm_platform_data *pdata = client->dev.platform_data;
	struct mx_qm_data *data;
	int err = 0;
	pr_debug("%s:++\n",__func__);
	
	s3c_gpio_setpull(pdata->gpio_irq, S3C_GPIO_PULL_UP);
	s3c_gpio_cfgpin(pdata->gpio_irq, S3C_GPIO_INPUT);
	
	s3c_gpio_setpull(pdata->gpio_wake, S3C_GPIO_PULL_UP);
	s3c_gpio_cfgpin(pdata->gpio_wake, S3C_GPIO_INPUT);
	
	s3c_gpio_setpull(pdata->gpio_reset, S3C_GPIO_PULL_UP);
	s3c_gpio_cfgpin(pdata->gpio_reset, S3C_GPIO_OUTPUT);
	gpio_set_value(pdata->gpio_reset,  0);
	msleep(50);
	gpio_set_value(pdata->gpio_reset,  1);
	s3c_gpio_cfgpin(pdata->gpio_reset, S3C_GPIO_INPUT);

	msleep(250);
	

	data = kzalloc(sizeof(struct mx_qm_data), GFP_KERNEL);
	data->dev = &client->dev;
	
#ifdef	CONFIG_M040_USB_PCB1
		data->gpio_wake = pdata->gpio_irq;
		data->gpio_reset = pdata->gpio_reset;
		data->gpio_irq = pdata->gpio_wake;
#else
		data->gpio_wake = pdata->gpio_wake;
		data->gpio_reset = pdata->gpio_reset;
		data->gpio_irq = pdata->gpio_irq;
#endif	

	data->client = client;
	data->irq = client->irq;//__gpio_to_irq(data->gpio_irq);
	data->i2c_readbyte = mx_qm_readbyte;
	data->i2c_writebyte= mx_qm_writebyte;
	data->reset = mx_qm_reset;
	data->wakeup= mx_qm_wakeup;
	data->update= mx_qm_update;	
		
	i2c_set_clientdata(client, data);
	mutex_init(&data->iolock);

	mx_qm_wakeup(data,true);
	/* Identify the mx_qm chip */
	if (!mx_qm_identify(data))
	{
		mx_qm_update(data);		

		mx_qm_wakeup(data,true);
		if (!mx_qm_identify(data))
		{
			err = -ENODEV;
			goto err_free_mem;
		}
	}

	qm_create_attrs(&client->dev);
	
	/*initial registers*/
	mx_qm_init_registers(data);

	err = mfd_add_devices(data->dev, -1, mx_qm_devs,
			ARRAY_SIZE(mx_qm_devs), NULL, 0);
	if (err) {
		dev_err(&client->dev,"MX QMatrix Sensor mfd add devices failed\n");
		goto err_free_mem;
	}

	pr_debug("%s:--\n",__func__);
	return 0;

err_free_mem:
	i2c_set_clientdata(client, NULL);
	kfree(data);
	return err;
}

#ifdef CONFIG_PM
static int mx_qm_suspend(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct mx_qm_data *mx = i2c_get_clientdata(i2c);
		
	mx->wakeup(mx,false);
	
#ifndef	CONFIG_M040_USB_PCB1
	disable_irq(mx->irq);
	enable_irq_wake(mx->irq);
#endif	

	return 0;
}

static int mx_qm_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct mx_qm_data *mx = i2c_get_clientdata(i2c);
	
#ifndef	CONFIG_M040_USB_PCB1
	disable_irq_wake(mx->irq);
	enable_irq(mx->irq);
#endif	

	mx->wakeup(mx,true);
	return 0;
}
#else
#define mx_qm_suspend	NULL
#define mx_qm_resume		NULL
#endif /* CONFIG_PM */

const struct dev_pm_ops mx_qm_pm = {
	.suspend = mx_qm_suspend,
	.resume = mx_qm_resume,
};

static int __devexit mx_qm_remove(struct i2c_client *client)
{
	struct mx_qm_data *data = i2c_get_clientdata(client);
	
	qm_destroy_atts(&client->dev);
	
	kfree(data);

	i2c_set_clientdata(client, NULL);

	return 0;
}

static const struct i2c_device_id mx_qm_id[] = {
	{ "mx_qm", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, mx_qm_id);

static struct i2c_driver mx_qm_driver = {
	.driver	= {
		.name	= "mx_qm",
		.owner	= THIS_MODULE,
		.pm = &mx_qm_pm,
	},
	.id_table	= mx_qm_id,
	.probe		= mx_qm_probe,
	.remove		= __devexit_p(mx_qm_remove),
};

static int __init mx_qm_init(void)
{
	return i2c_add_driver(&mx_qm_driver);
}
module_init(mx_qm_init);

static void __exit mx_qm_exit(void)
{
	i2c_del_driver(&mx_qm_driver);
}
module_exit(mx_qm_exit);

MODULE_AUTHOR("Chwei <Chwei@meizu.com>");
MODULE_DESCRIPTION("MX QMatrix Sensor");
MODULE_LICENSE("GPL");
