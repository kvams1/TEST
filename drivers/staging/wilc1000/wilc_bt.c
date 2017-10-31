#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/firmware.h>

#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/host.h>

#include "wilc_wfi_netdevice.h"

static struct wilc *wilc_bt;
static dev_t chc_dev_no; /* Global variable for the first device number */
static struct cdev str_chc_dev; /* Global variable for the character */
struct device *dev;
static struct class *chc_dev_class; /* Global variable for the device class */
static bool device_created;
int bt_init_done=0;

typedef void (wilc_cmd_handler)(char*);

static void wilc_cmd_handle_bt_enable(char* param);
static void wilc_cmd_handle_bt_power_up(char* param);
static void wilc_cmd_handle_bt_power_down(char* param);
static void wilc_cmd_handle_bt_fw_chip_wake_up(char* param);
static void wilc_cmd_handle_bt_fw_chip_allow_sleep(char* param);
static void wilc_cmd_handle_bt_download_fw(char* param);
static void wilc_cmd_handle_wilc_cca_threshold(char* param);

int wilc_bt_power_up(struct wilc *, int source);
int wilc_bt_power_down(struct wilc *, int source);
static void wilc_bt_firmware_download(struct wilc *);
static void wilc_bt_start(struct wilc *);
static int wilc_bt_dev_open(struct inode *i, struct file *f);
static int wilc_bt_dev_close(struct inode *i, struct file *f);
static ssize_t wilc_bt_dev_read(struct file *f, char __user *buf, size_t len,
				loff_t *off);
static ssize_t wilc_bt_dev_write(struct file *f, const char __user *buff,
				 size_t len, loff_t *off);

static const struct file_operations pugs_fops = {
	.owner = THIS_MODULE,
	.open = wilc_bt_dev_open,
	.release = wilc_bt_dev_close,
	.read = wilc_bt_dev_read,
	.write = wilc_bt_dev_write
};

struct wilc_cmd_handle_entry {
	const char *wilc_cmd_str;
	wilc_cmd_handler *wilc_handle_cmd;
};

static const struct wilc_cmd_handle_entry wilc_cmd_table[] = {
	{"BT_DOWNLOAD_FW", wilc_cmd_handle_bt_download_fw},
	{"BT_POWER_UP", wilc_cmd_handle_bt_power_up},
	{"BT_POWER_DOWN", wilc_cmd_handle_bt_power_down},
	{"BT_FW_CHIP_WAKEUP", wilc_cmd_handle_bt_fw_chip_wake_up},
	{"BT_FW_CHIP_ALLOW_SLEEP", wilc_cmd_handle_bt_fw_chip_allow_sleep},
	{"BT_ENABLE", wilc_cmd_handle_bt_enable},
	{"CCA_THRESHOLD", wilc_cmd_handle_wilc_cca_threshold},
	/* Keep the NULL handler at the end of the table */
	{(const char *)NULL, NULL},
};

static int wilc_bt_dev_open(struct inode *i, struct file *f)
{
	return 0;
}

static int wilc_bt_dev_close(struct inode *i, struct file *f)
{
	return 0;
}

static ssize_t wilc_bt_dev_read(struct file *f, char __user *buf, size_t len,
				loff_t *off)
{
	return 0;
}

static ssize_t wilc_bt_dev_write(struct file *f, const char __user *buff,
				 size_t len, loff_t *off)
{
	struct wilc_cmd_handle_entry *wilc_cmd_entry;
	char *cmd_buff;

	cmd_buff = kmalloc(sizeof(char) * len, GFP_KERNEL);

	if(copy_from_user(cmd_buff, buff, len))
		return -EIO;

	if (len > 0) {
		// call the appropriate command handler
		wilc_cmd_entry = (struct wilc_cmd_handle_entry *)wilc_cmd_table;
		while (wilc_cmd_entry->wilc_handle_cmd != NULL) {
			if (strncmp(wilc_cmd_entry->wilc_cmd_str, cmd_buff,
				    strlen(wilc_cmd_entry->wilc_cmd_str)) == 0) {
				wilc_cmd_entry->wilc_handle_cmd(cmd_buff + strlen(wilc_cmd_entry->wilc_cmd_str));
				break;
			}
			wilc_cmd_entry++;
		}
	}
	kfree(cmd_buff);
	return len;
}

static void wilc_bt_create_device(void)
{
	int ret = 0;

	if (device_created)
		return;

	ret = alloc_chrdev_region(&chc_dev_no, 0, 1, "atmel");
	if (ret < 0)
		return;
	chc_dev_class = class_create(THIS_MODULE, "atmel");
	if (IS_ERR(chc_dev_class)) {
		unregister_chrdev_region(chc_dev_no, 1);
		return;
	}
	dev = device_create(chc_dev_class, NULL, chc_dev_no, NULL,
			    "wilc_bt");
	if (IS_ERR(dev)) {
		class_destroy(chc_dev_class);
		unregister_chrdev_region(chc_dev_no, 1);
		return;
	}

	cdev_init(&str_chc_dev, &pugs_fops);
	ret = cdev_add(&str_chc_dev, chc_dev_no, 1);
	if (ret < 0) {
		device_destroy(chc_dev_class, chc_dev_no);
		class_destroy(chc_dev_class);
		unregister_chrdev_region(chc_dev_no, 1);
		return;
	}
	mutex_init(&wilc_bt->cs);	
	device_created = 1;
}

static void wilc_cmd_handle_wilc_cca_threshold(char* param)
{
	int carrier_thrshold, noise_thrshold;
	unsigned int carr_thrshold_frac, noise_thrshold_frac, carr_thrshold_int, 
		noise_thrshold_int, reg;
	
	if(param == NULL)
		return;


	if(sscanf(param, " %d %d", &noise_thrshold, &carrier_thrshold) != 2) {
		return;
	}
	

	carr_thrshold_int = carrier_thrshold/10;
	if(carrier_thrshold < 0)
		carr_thrshold_frac = (carr_thrshold_int * 10) - carrier_thrshold;
	else
		carr_thrshold_frac = carrier_thrshold - (carr_thrshold_int * 10);

	noise_thrshold_int = noise_thrshold/10;
	if(noise_thrshold < 0)
		noise_thrshold_frac = (noise_thrshold_int * 10) - noise_thrshold;
	else
		noise_thrshold_frac = noise_thrshold - (noise_thrshold_int * 10);

	wilc_bt->hif_func->hif_read_reg(wilc_bt, CCA_CTL_2, &reg);
	reg &= ~(0x7FF0000);
	reg |= ((noise_thrshold_frac & 0x7) | ((noise_thrshold_int & 0x1FF) << 3)) << 16;
	wilc_bt->hif_func->hif_write_reg(wilc_bt, CCA_CTL_2, reg);
	
	wilc_bt->hif_func->hif_read_reg(wilc_bt, CCA_CTL_7, &reg);
	reg &= ~(0x7FF0000);
	reg |= ((carr_thrshold_frac & 0x7) | ((carr_thrshold_int & 0x1FF) << 3)) << 16;
	wilc_bt->hif_func->hif_write_reg(wilc_bt, CCA_CTL_7, reg);
	
	return;
}

int wilc_bt_power_up(struct wilc *wilc, int source)
{
	int count=0;
	int ret;
	int reg;
	
	mutex_lock(&wilc->cs);

	PRINT_D(PWRDEV_DBG, "source: %s, current bus status Wifi: %d, BT: %d\n",
		 (source == PWR_DEV_SRC_WIFI ? "Wifi" : "BT"),
		 wilc->power_status[PWR_DEV_SRC_WIFI],
		 wilc->power_status[PWR_DEV_SRC_BT]);

	if (wilc->power_status[source] == true) {
		PRINT_ER("power up request for already powered up source %s\n",
			 (source == PWR_DEV_SRC_WIFI ? "Wifi" : "BT"));
		}
	else
	{
		/*Bug 215*/
		/*Avoid overlapping between BT and Wifi intialization*/
		if((wilc->power_status[PWR_DEV_SRC_WIFI]==true))
		{
			while(!wilc->initialized)
			{
				msleep(100);
				if(++count>20)
				{
					PRINT_D(GENERIC_DBG,"Error: Wifi has taken too much time to initialize \n");
					break;
				}
			}
		}
		else if((wilc->power_status[PWR_DEV_SRC_BT]==true))
		{
			while(!bt_init_done)
			{
				msleep(200);
				if(++count>30)
				{
					PRINT_D(GENERIC_DBG,"Error: BT has taken too much time to initialize \n");
					break;
				}
			}
			/*An additional wait to give BT firmware time to do CPLL update as the time 
			measured since the start of BT Fw till the end of function "rf_nmi_init_tuner" was 71.2 ms */	
			msleep(100);
		}
	}

	if ((wilc->power_status[PWR_DEV_SRC_WIFI] == true) ||
		   (wilc->power_status[PWR_DEV_SRC_BT] == true)) {
		PRINT_WRN(PWRDEV_DBG, "Device already up. request source is %s\n",
			 (source == PWR_DEV_SRC_WIFI ? "Wifi" : "BT"));
	} else {
		PRINT_D(PWRDEV_DBG, "WILC POWER UP\n");
	}
	wilc->power_status[source] = true;
	mutex_unlock(&wilc->cs);

	if(source == PWR_DEV_SRC_BT)
	{
		/*TicketId1092*/
		/*If WiFi is off, force BT*/
		if(wilc->power_status[PWR_DEV_SRC_WIFI] == false)
		{
			acquire_bus(wilc, ACQUIRE_AND_WAKEUP,PWR_DEV_SRC_BT);
		
			ret = wilc->hif_func->hif_read_reg(wilc, WILC_COEXIST_CTL, &reg);
			if (!ret) {
				PRINT_ER("[wilc start]: fail read reg %x ...\n", WILC_COEXIST_CTL);
				goto _fail_;
			}
			/*Force BT*/
			reg |= BIT(0) | BIT(9);
			reg &= ~BIT(11);
			ret = wilc->hif_func->hif_write_reg(wilc, WILC_COEXIST_CTL, reg);
			if (!ret) {
				PRINT_ER( "[wilc start]: fail write reg %x ...\n", WILC_COEXIST_CTL);
				goto _fail_;
			}

			/*TicketId1115*/
			/*Disable awake coex null frames*/
			ret = wilc->hif_func->hif_read_reg(wilc, WILC_COE_AUTO_PS_ON_NULL_PKT, &reg);
			if (!ret) {
				PRINT_ER("[wilc start]: fail read reg %x ...\n", WILC_COE_AUTO_PS_ON_NULL_PKT);
				goto _fail_;
			}
			reg &= ~BIT(30);
			ret = wilc->hif_func->hif_write_reg(wilc, WILC_COE_AUTO_PS_ON_NULL_PKT, reg);
			if (!ret) {
				PRINT_ER( "[wilc start]: fail write reg %x ...\n", WILC_COE_AUTO_PS_ON_NULL_PKT);
				goto _fail_;
			}

			/*TicketId1115*/
			/*Disable doze coex null frames*/
			ret = wilc->hif_func->hif_read_reg(wilc, WILC_COE_AUTO_PS_OFF_NULL_PKT, &reg);
			if (!ret) {
				PRINT_ER("[wilc start]: fail read reg %x ...\n", WILC_COE_AUTO_PS_OFF_NULL_PKT);
				goto _fail_;
			}
			reg &= ~BIT(30);
			ret = wilc->hif_func->hif_write_reg(wilc, WILC_COE_AUTO_PS_OFF_NULL_PKT, reg);
			if (!ret) {
				PRINT_ER( "[wilc start]: fail write reg %x ...\n", WILC_COE_AUTO_PS_OFF_NULL_PKT);
				goto _fail_;
			}
			
			release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);	
		}

		// Enable BT wakeup
		acquire_bus(wilc, ACQUIRE_AND_WAKEUP, PWR_DEV_SRC_BT);

		ret = wilc->hif_func->hif_read_reg(wilc, WILC_PWR_SEQ_MISC_CTRL, &reg);
		if (!ret) {
			PRINT_ER( "[wilc start]: fail read reg %x ...\n", WILC_PWR_SEQ_MISC_CTRL);
			goto _fail_;
		}
		reg |= BIT(29);
		ret = wilc->hif_func->hif_write_reg(wilc, WILC_PWR_SEQ_MISC_CTRL, reg);
		if (!ret) {
			PRINT_ER( "[wilc start]: fail write reg %x ...\n", WILC_PWR_SEQ_MISC_CTRL);
			goto _fail_;
		}

		release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
	}

	return 0;

_fail_:
	release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
	wilc_bt_power_down(wilc, PWR_DEV_SRC_BT);
	return ret;
}

int wilc_bt_power_down(struct wilc *wilc, int source)
{
	if(source == PWR_DEV_SRC_BT)
	{
		int ret;
		u32 reg;

		PRINT_D(PWRDEV_DBG, "AT PWR: bt_power_down\n");

		/* Adjust coexistence module. This should be done from the FW in the future*/
		acquire_bus(wilc, ACQUIRE_AND_WAKEUP, PWR_DEV_SRC_BT);

		ret = wilc->hif_func->hif_read_reg(wilc, WILC_GLOBAL_MODE_CONTROL, &reg);
		if (!ret) {
			PRINT_ER("[wilc start]: fail read reg %x ...\n",
			       WILC_GLOBAL_MODE_CONTROL);
			release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
			return ret;
		}
		/* Clear BT mode*/
		reg &= ~BIT(1);
		ret = wilc->hif_func->hif_write_reg(wilc, WILC_GLOBAL_MODE_CONTROL, reg);
		if (!ret) {
			PRINT_ER("[wilc start]: fail write reg %x ...\n",
			       WILC_GLOBAL_MODE_CONTROL);
			release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
			return ret;
		}

		ret = wilc->hif_func->hif_read_reg(wilc, WILC_COEXIST_CTL, &reg);
		if (!ret) {
			PRINT_ER("[wilc start]: fail read reg %x ...\n",
			       WILC_COEXIST_CTL);
			release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
			return ret;
		}
		/* Stop forcing BT and force Wifi */
		reg &= ~BIT(9);
		reg |= BIT(11);
		ret = wilc->hif_func->hif_write_reg(wilc, WILC_COEXIST_CTL, reg);
		if (!ret) {
			PRINT_ER("[wilc start]: fail write reg %x ...\n",
			       WILC_COEXIST_CTL);
			release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
			return ret;
		}

		/*TicketId1115*/
		/*Disable awake coex null frames*/
		ret = wilc->hif_func->hif_read_reg(wilc, WILC_COE_AUTO_PS_ON_NULL_PKT, &reg);
		if (!ret) {
			PRINT_ER("[wilc start]: fail read reg %x ...\n", WILC_COE_AUTO_PS_ON_NULL_PKT);
			release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
			return ret;
		}
		reg &= ~BIT(30);
		ret = wilc->hif_func->hif_write_reg(wilc, WILC_COE_AUTO_PS_ON_NULL_PKT, reg);
		if (!ret) {
			PRINT_ER( "[wilc start]: fail write reg %x ...\n", WILC_COE_AUTO_PS_ON_NULL_PKT);
			release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
			return ret;
		}

		/*TicketId1115*/
		/*Disable doze coex null frames*/
		ret = wilc->hif_func->hif_read_reg(wilc, WILC_COE_AUTO_PS_OFF_NULL_PKT, &reg);
		if (!ret) {
			PRINT_ER("[wilc start]: fail read reg %x ...\n", WILC_COE_AUTO_PS_OFF_NULL_PKT);
			release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
			return ret;
		}
		reg &= ~BIT(30);
		ret = wilc->hif_func->hif_write_reg(wilc, WILC_COE_AUTO_PS_OFF_NULL_PKT, reg);
		if (!ret) {
			PRINT_ER( "[wilc start]: fail write reg %x ...\n", WILC_COE_AUTO_PS_OFF_NULL_PKT);
			release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
			return ret;
		}
		// Disable BT wakeup
		ret = wilc->hif_func->hif_read_reg(wilc, WILC_PWR_SEQ_MISC_CTRL, &reg);
		if (!ret) {
			PRINT_ER( "[wilc start]: fail read reg %x ...\n", WILC_PWR_SEQ_MISC_CTRL);
			release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
			return ret;
		}
		reg &= ~ BIT(29);
		ret = wilc->hif_func->hif_write_reg(wilc, WILC_PWR_SEQ_MISC_CTRL, reg);
		if (!ret) {
			PRINT_ER( "[wilc start]: fail write reg %x ...\n", WILC_PWR_SEQ_MISC_CTRL);
			release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
			return ret;
		}

		release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);

		bt_init_done=0;
	}
	
	mutex_lock(&wilc->cs);

	PRINT_D(PWRDEV_DBG, "source: %s, current bus status Wifi: %d, BT: %d\n",
		 (source == PWR_DEV_SRC_WIFI ? "Wifi" : "BT"),
		 wilc->power_status[PWR_DEV_SRC_WIFI],
		 wilc->power_status[PWR_DEV_SRC_BT]);

	if (wilc->power_status[source] == false) {
		PRINT_ER("power down request for already powered down source %s\n",
		       (source == PWR_DEV_SRC_WIFI ? "Wifi" : "BT"));
	} else if (((source == PWR_DEV_SRC_WIFI) &&
		  (wilc->power_status[PWR_DEV_SRC_BT] == true)) ||
		  ((source == PWR_DEV_SRC_BT) &&
		  (wilc->power_status[PWR_DEV_SRC_WIFI] == true))) {
		PRINT_WRN(PWRDEV_DBG, "Another device is preventing power down. request source is %s\n",
			(source == PWR_DEV_SRC_WIFI ? "Wifi" : "BT"));
	} else {
		wilc_wlan_power_off_sequence();
	}
	wilc->power_status[source] = false;

	mutex_unlock(&wilc->cs);

	return 0;
}

static void wilc_bt_firmware_download(struct wilc *wilc)
{
	u32 offset;
	u32 addr, size, size2, blksz;
	u8 *dma_buffer;
	const struct firmware *wilc_bt_firmware;
	const u8 *buffer;
	size_t buffer_size;
	int ret = 0;
	u32 reg;

	if (request_firmware(&wilc_bt_firmware, FIRMWARE_WILC3000_BT, dev) != 0)

	wilc->firmware = wilc_bt_firmware;

	buffer = wilc_bt_firmware->data;
	buffer_size = (size_t)wilc_bt_firmware->size;
	if (buffer_size <= 0) {
		ret = -1;
		goto _fail_1;
	}

	acquire_bus(wilc, ACQUIRE_AND_WAKEUP, PWR_DEV_SRC_BT);

	ret = wilc->hif_func->hif_write_reg(wilc, 0x4f0000, 0x71);
	if (!ret) {
		release_bus(wilc,RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);	
		goto _fail_1;
	}

	ret = wilc->hif_func->hif_read_reg(wilc, 0x3b0090, &reg);
	if (!ret) {
		release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
		goto _fail_1;
	}

	reg |= (1 << 0);
	ret = wilc->hif_func->hif_write_reg(wilc, 0x3b0090, reg);
	if (!ret) {
		release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
		goto _fail_1;
	}

	wilc->hif_func->hif_read_reg(wilc, 0x3B0400, &reg);

	if (reg & (1ul << 2)) {
		reg &= ~(1ul << 2);
	} else {
		reg |= (1ul << 2);
		wilc->hif_func->hif_write_reg(wilc, 0x3B0400, reg);
		reg &= ~(1ul << 2);
	}
	wilc->hif_func->hif_write_reg(wilc, 0x3B0400, reg);

		release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);

	/* blocks of sizes > 512 causes the wifi to hang! */
	blksz = (1ul << 9);
	/* Allocate a DMA coherent  buffer. */
	dma_buffer = kmalloc(blksz, GFP_KERNEL);
	if (dma_buffer == NULL) {
		ret = -5;
		goto _fail_1;
	}

	offset = 0;
	addr = 0x400000;
	size = buffer_size;
#ifdef BIG_ENDIAN
	addr = BYTE_SWAP(addr);
	size = BYTE_SWAP(size);
#endif
	offset = 0;

	while (((int)size) && (offset < buffer_size)) {
		if (size <= blksz)
			size2 = size;
		else
			size2 = blksz;

		/* Copy firmware into a DMA coherent buffer */
		memcpy(dma_buffer, &buffer[offset], size2);

		acquire_bus(wilc, ACQUIRE_AND_WAKEUP, PWR_DEV_SRC_BT);

		ret = wilc->hif_func->hif_block_tx(wilc, addr, dma_buffer, size2);

		release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);

		if (!ret)
			break;

		addr += size2;
		offset += size2;
		size -= size2;
	}

	if (!ret) {
		ret = -5;
		goto _fail_;
	}

_fail_:
	kfree(dma_buffer);
_fail_1:
	return;
}

static void wilc_bt_start(struct wilc *wilc)
{
	int ret = 0;
	u32 val32 = 0;

	acquire_bus(wilc, ACQUIRE_AND_WAKEUP, PWR_DEV_SRC_BT);

	if(wilc->power_status[PWR_DEV_SRC_WIFI]) {
		ret = wilc->hif_func->hif_read_reg(wilc, WILC_COEXIST_CTL, &val32);
		if (!ret) {
			goto _fail_1;
		}
		val32 |= (1 << 0);
		val32 &= ~((1 << 9)|(1 << 11));
		ret = wilc->hif_func->hif_write_reg(wilc, WILC_COEXIST_CTL, val32);
		if (!ret) {
			goto _fail_1;
		}
	}

	/*
	 * Write the firmware download complete magic value 0x10ADD09E at
	 * location 0xFFFF000C (Cortus map) or C000C (AHB map).
	 * This will let the boot-rom code execute from RAM.
	 */
	wilc->hif_func->hif_write_reg(wilc, 0x4F000c, 0x10add09e);

	wilc->hif_func->hif_read_reg(wilc, 0x3B0400, &val32);
	val32 &= ~((1ul << 2) | (1ul << 3));
	wilc->hif_func->hif_write_reg(wilc, 0x3B0400, val32);

	msleep(100);

	val32 |= ((1ul << 2) | (1ul << 3));

	wilc->hif_func->hif_write_reg(wilc, 0x3B0400, val32);


_fail_1:
	release_bus(wilc, RELEASE_ALLOW_SLEEP, PWR_DEV_SRC_BT);
}

static void wilc_cmd_handle_bt_power_up(char* param)
{
	bt_init_done=0;
	if (!wilc_bt->hif_func->hif_init(wilc_bt, false)) {
		return;
	}
	wilc_bt_power_up(wilc_bt, PWR_DEV_SRC_BT);
}

static void wilc_cmd_handle_bt_power_down(char* param)
{
	wilc_bt_power_down(wilc_bt, PWR_DEV_SRC_BT);
}

static void wilc_cmd_handle_bt_fw_chip_wake_up(char* param)
{
	chip_wakeup(wilc_bt, PWR_DEV_SRC_BT);
}

static void wilc_cmd_handle_bt_fw_chip_allow_sleep(char* param)
{
	bt_init_done=1;
	chip_allow_sleep(wilc_bt, PWR_DEV_SRC_BT);
}

static void wilc_cmd_handle_bt_download_fw(char* param)
{
	wilc_bt_firmware_download(wilc_bt);
	wilc_bt_start(wilc_bt);
}

static void wilc_cmd_handle_bt_enable(char* param)
{
	wilc_bt_power_up(wilc_bt, PWR_DEV_SRC_BT);
	wilc_bt_firmware_download(wilc_bt);
	wilc_bt_start(wilc_bt);
}

void wilc_bt_init(struct wilc *wilc)
{
	wilc_bt = wilc;
	wilc_bt_create_device();
}
EXPORT_SYMBOL_GPL(wilc_bt_init);

void wilc_bt_deinit(void)
{
	if (&wilc_bt->cs != NULL)
		mutex_destroy(&wilc_bt->cs);

	cdev_del(&str_chc_dev);
	device_created = 0;
	device_destroy(chc_dev_class, chc_dev_no);
	class_destroy(chc_dev_class);
	unregister_chrdev_region(chc_dev_no, 1);
}
EXPORT_SYMBOL_GPL(wilc_bt_deinit);
