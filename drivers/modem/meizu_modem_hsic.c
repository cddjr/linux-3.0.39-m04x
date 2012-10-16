/* drivers/modem/modem_link_device_hsic.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2010 Samsung Electronics.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_data/modem.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/suspend.h>
#include <linux/usb/cdc.h>
#include <linux/usb.h>
#include <linux/version.h>
#include <linux/wakelock.h>
#include <mach/modem.h>
#include <mach/usb-detect.h>
#include "modem_prj.h"
#include "modem_utils.h"
#include "meizu_modem_hsic.h"

#define HSIC_MAX_PIPE_ORDER_NR 3

static struct modem_ctl *if_usb_get_modemctl(struct link_pm_data *pm_data);
static int link_pm_runtime_get_active(struct link_pm_data *pm_data);
static int usb_tx_urb_with_skb(struct usb_device *usbdev, struct sk_buff *skb,
					struct if_usb_devdata *pipe_data);
static void usb_rx_complete(struct urb *urb);

static int link_pm_set_slave_wakeup(unsigned int gpio, int val)
{
	gpio_set_value(gpio, val);
	mif_trace("\n");
	mif_debug("[SWK]=>[%d]:[%d]\n", val, gpio_get_value(gpio));

	return 0;
}

static int pm_data_get_device(struct link_pm_data *pm_data, int new_state)
{
	DECLARE_WAITQUEUE(wait, current);

	while (1) {
		spin_lock(&pm_data->pm_data_lock);
		if (pm_data->state == ACM_READY) {
			pm_data->state = new_state;
			spin_unlock(&pm_data->pm_data_lock);
			break;
		}
		if (new_state == ACM_SUSPEND) {
			spin_unlock(&pm_data->pm_data_lock);
			return -EAGAIN;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&pm_data->waitqueue, &wait);
		spin_unlock(&pm_data->pm_data_lock);
		schedule();
		remove_wait_queue(&pm_data->waitqueue, &wait);
	}

	return 0;
}

static void pm_data_release_device(struct link_pm_data *pm_data)
{
	spin_lock(&pm_data->pm_data_lock);
	pm_data->state = ACM_READY;
	wake_up(&pm_data->waitqueue);
	spin_unlock(&pm_data->pm_data_lock);
}

static void usb_set_autosuspend_delay(struct usb_device *usbdev, int delay)
{
	pm_runtime_set_autosuspend_delay(&usbdev->dev, delay);
}

static int start_ipc(struct link_device *ld, struct io_device *iod)
{
	int err = 0;
	struct usb_link_device *usb_ld = to_usb_link_device(ld);
	struct link_pm_data *pm_data = usb_ld->link_pm_data;
	struct device *dev = &usb_ld->usbdev->dev;

	if (!usb_ld->if_usb_connected) {
		mif_debug("HSIC not connected, skip start ipc\n");
		err = -ENODEV;
		goto exit;
	}

retry:
	if (ld->mc->cp_flag != MODEM_CONNECT_FLAG) {
		mif_debug("MODEM is not online, skip start ipc\n");
		err = -ENODEV;
		goto exit;
	}

	/* check usb runtime pm first */
	if (dev->power.runtime_status != RPM_ACTIVE) {
		if (!pm_data->resume_requested) {
			mif_debug("QW PM\n");
			INIT_COMPLETION(pm_data->active_done);
			queue_delayed_work(pm_data->wq,
					&pm_data->link_pm_work, 0);
		}
		mif_debug("Wait pm\n");
		err = wait_for_completion_timeout(&pm_data->active_done,
						msecs_to_jiffies(1000));
		/* timeout or -ERESTARTSYS */
		if (err <= 0)
			goto retry;
	}
	usb_mark_last_busy(usb_ld->usbdev);
exit:
	return err;
}

static int usb_init_communication(struct link_device *ld,
			struct io_device *iod)
{
	struct task_struct *task = get_current();
	char str[TASK_COMM_LEN];

	mif_debug("%d:%s\n", task->pid, get_task_comm(str, task));

	start_ipc(ld, iod);

	return 0;
}

static void usb_terminate_communication(struct link_device *ld,
			struct io_device *iod)
{
	/*ld->com_state = COM_NONE;*/
	pr_info("%s iod id:%d\n", __func__, iod->id);
}

static int usb_rx_submit(struct usb_link_device *usb_ld,
					struct if_usb_devdata *pipe_data,
					gfp_t gfp_flags)
{
	int ret;
	struct urb *urb;

	if (pipe_data->disconnected)
		return -ENOENT;

	urb = pipe_data->urb;
	if (urb == NULL) {
		printk("%s urb is NULL!!\n", __func__);
		return -EPIPE;
	}
	usb_mark_last_busy(usb_ld->usbdev);

	/*urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;*/
	urb->transfer_flags = 0;
	usb_fill_bulk_urb(urb, pipe_data->usbdev,
				pipe_data->rx_pipe, pipe_data->rx_buf,
				pipe_data->rx_buf_size, usb_rx_complete,
				(void *)pipe_data);

	if (!usb_ld->if_usb_connected || !usb_ld->usbdev)
		return -ENOENT;

	ret = usb_submit_urb(urb, gfp_flags);
	if (ret)
		mif_err("submit urb fail with ret (%d)\n", ret);
	usb_mark_last_busy(usb_ld->usbdev);

	return ret;
}

static void usb_rx_retry_work(struct work_struct *work)
{
	int ret = 0;
	struct usb_link_device *usb_ld =
		container_of(work, struct usb_link_device, rx_retry_work.work);
	struct urb *urb = usb_ld->retry_urb;
	struct if_usb_devdata *pipe_data = urb->context;
	struct io_device *iod;

	if (!usb_ld->if_usb_connected || !usb_ld->usbdev)
		return;

	if (usb_ld->usbdev)
		usb_mark_last_busy(usb_ld->usbdev);

	iod = link_get_iod_with_channel(&usb_ld->ld, pipe_data->channel_id);
	if (iod) {
		ret = iod->recv(iod, &usb_ld->ld, (char *)urb->transfer_buffer,
			urb->actual_length);
		if (ret == -ENOMEM) {
			/* TODO: check the retry count */
			/* retry the delay work after 20ms and resubit*/
			mif_err("ENOMEM, +retry 20ms\n");
			if (usb_ld->usbdev)
				usb_mark_last_busy(usb_ld->usbdev);
			usb_ld->retry_urb = urb;
			if (usb_ld->rx_retry_cnt++ < 10)
				queue_delayed_work(usb_ld->ld.tx_wq,
					&usb_ld->rx_retry_work,	10);
			return;
		}
		if (ret < 0)
			mif_err("io device recv error (%d)\n", ret);
		usb_ld->rx_retry_cnt = 0;
	}

	if (usb_ld->usbdev)
		usb_mark_last_busy(usb_ld->usbdev);
	usb_rx_submit(usb_ld, pipe_data, GFP_ATOMIC);
}


static void usb_rx_complete(struct urb *urb)
{
	struct if_usb_devdata *pipe_data = urb->context;
	struct usb_link_device *usb_ld = pipe_data->usb_ld;
	struct io_device *iod;
	int ret;

	if (usb_ld->usbdev)
		usb_mark_last_busy(usb_ld->usbdev);

	switch (urb->status) {
	case -ENOENT:
		/* case for 'link pm suspended but rx data had remained' */
	case 0:
		pipe_data->hsic_channel_rx_count ++;
		if (!urb->actual_length)
			goto rx_submit;
		usb_ld->link_pm_data->rx_cnt++;

		iod = link_get_iod_with_channel(&usb_ld->ld,
						pipe_data->channel_id);
		if (iod) {
			if (iod->atdebug)
				iod->atdebugfunc(iod, urb->transfer_buffer,
							urb->actual_length);

			ret = iod->recv(iod, &usb_ld->ld, urb->transfer_buffer,
					urb->actual_length);
			if (ret == -ENOMEM) {
				/* retry the delay work and resubit*/
				mif_err("ENOMEM, retry\n");
				if (usb_ld->usbdev)
					usb_mark_last_busy(usb_ld->usbdev);
				usb_ld->retry_urb = urb;
				queue_delayed_work(usb_ld->ld.tx_wq,
					&usb_ld->rx_retry_work, 0);
				return;
			} else {
				if (ret < 0)
					pr_err("io device recv err:%d\n", ret);
				else
					pipe_data->hsic_channel_rx_count --;
			}
		}
rx_submit:
		if (urb->status == 0) {
			if (usb_ld->usbdev)
				usb_mark_last_busy(usb_ld->usbdev);
			usb_rx_submit(usb_ld, pipe_data, GFP_ATOMIC);
		}
		break;
	default:
		mif_err("urb err status = %d\n", urb->status);
		break;
	}
}

static int usb_send(struct link_device *ld, struct io_device *iod,
			struct sk_buff *skb)
{
	struct sk_buff_head *txq;
	size_t tx_size;
	struct usb_link_device *usb_ld = to_usb_link_device(ld);
	struct link_pm_data *pm_data = usb_ld->link_pm_data;

	if (usb_ld->ld.com_state != COM_ONLINE)
		return 0;

	txq = &ld->sk_raw_tx_q;
	tx_size = skb->len;

	skb_queue_tail(txq, skb);

	usb_ld->devdata[iod->id].hsic_channel_tx_count ++;

	/* Hold wake_lock for getting schedule the tx_work */
	wake_lock(&pm_data->tx_async_wake);

	if (!work_pending(&ld->tx_delayed_work.work))
		queue_delayed_work(ld->tx_wq, &ld->tx_delayed_work, 0);

	return tx_size;
}

static void usb_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct io_device *iod = skbpriv(skb)->iod;
	struct link_device *linkdev = get_current_link(iod);
	struct usb_link_device *usb_ld = to_usb_link_device(linkdev);

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
		mif_debug("iod %d TX error (%d)\n", iod->id, urb->status);
		break;
	default:
		mif_info("iod %d TX error (%d)\n", iod->id, urb->status);
	}

	if (iod->atdebug)
		iod->atdebugfunc(iod, skb->data, skb->len);

	usb_ld->devdata[iod->id].hsic_channel_tx_count --;
	dev_kfree_skb_any(skb);
	if (urb->dev)
		usb_mark_last_busy(urb->dev);
	usb_free_urb(urb);
}

/* Even if usb_tx_urb_with_skb is failed, does not release the skb to retry */
static int usb_tx_urb_with_skb(struct usb_device *usbdev, struct sk_buff *skb,
					struct if_usb_devdata *pipe_data)
{
	int ret;
	struct urb *urb;

	if (pipe_data->disconnected)
		return -ENOENT;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		mif_err("alloc urb error\n");
		return -ENOMEM;
	}
	urb->transfer_flags = URB_ZERO_PACKET;
	pm_data_get_device(pipe_data->usb_ld->link_pm_data, ACM_WRITE);
	usb_fill_bulk_urb(urb, pipe_data->usbdev, pipe_data->tx_pipe, skb->data,
			skb->len, usb_tx_complete, (void *)skb);
	usb_mark_last_busy(usbdev);
	ret = usb_submit_urb(urb, GFP_KERNEL);
	pm_data_release_device(pipe_data->usb_ld->link_pm_data);
	if (ret < 0) {
		mif_err("usb_submit_urb with ret(%d)\n", ret);
		usb_free_urb(urb);
		return ret;
	}

	return 0;
}


static int _usb_tx_work(struct sk_buff *skb)
{
	struct sk_buff_head *txq;
	struct io_device *iod = skbpriv(skb)->iod;
	struct link_device *ld = skbpriv(skb)->ld;
	struct usb_link_device *usb_ld = to_usb_link_device(ld);
	struct if_usb_devdata *pipe_data;

	pipe_data = &usb_ld->devdata[iod->id];
	txq = &ld->sk_raw_tx_q;

	if (!pipe_data)
		return -ENOENT;

	return usb_tx_urb_with_skb(usb_ld->usbdev, skb,	pipe_data);
}


static void usb_tx_work(struct work_struct *work)
{
	int ret = 0;
	struct link_device *ld =
		container_of(work, struct link_device, tx_delayed_work.work);
	struct usb_link_device *usb_ld = to_usb_link_device(ld);
	struct sk_buff *skb;
	struct link_pm_data *pm_data = usb_ld->link_pm_data;

	if (!usb_ld->usbdev) {
		mif_debug("usbdev is invalid\n");
		return;
	}

	pm_data->tx_cnt++;

	while (ld->sk_raw_tx_q.qlen) {
		ret = link_pm_runtime_get_active(pm_data);
		if (ret < 0) {
			mif_err("link not avail. ret:%d\n", ret);
			if (ret == -ENODEV)
				goto exit;
			else
				goto retry_tx_work;
		}

		/* If AP try to tx when interface disconnect->reconnect probe,
		 * usbdev was created but one of interface channel device are
		 * probing, _usb_tx_work return to -ENOENT then runtime usage
		 * count allways positive and never enter to L2
		 */
		if (!usb_ld->if_usb_connected) {
			mif_debug("link is available, but it was not readey!\n");
			goto retry_tx_work;
		}
		pm_runtime_get_sync(&usb_ld->usbdev->dev);

		ret = 0;
		/* send skb from raw_txq*/

		skb = skb_dequeue(&ld->sk_raw_tx_q);
		if (skb)
			ret = _usb_tx_work(skb);

		if (ret) {
			mif_err("usb_tx_urb_with_skb for raw_q %d\n", ret);
			skb_queue_head(&ld->sk_raw_tx_q, skb);

			if (ret == -ENODEV || ret == -ENOENT)
				goto exit;

			pm_runtime_put(&usb_ld->usbdev->dev);
			goto retry_tx_work;
		}

		pm_runtime_put(&usb_ld->usbdev->dev);
	}
	wake_unlock(&pm_data->tx_async_wake);
exit:
	return;

retry_tx_work:
	queue_delayed_work(ld->tx_wq, &ld->tx_delayed_work,
		msecs_to_jiffies(20));
	return;
}

/*
#ifdef CONFIG_LINK_PM
*/
static int link_pm_runtime_get_active(struct link_pm_data *pm_data)
{
	int ret;
	struct usb_link_device *usb_ld = pm_data->usb_ld;
	struct device *dev = &usb_ld->usbdev->dev;

	if (!usb_ld->if_usb_connected || usb_ld->ld.com_state == COM_NONE) {
		pr_err("%s if_usb_connected:%d, com_state:%d\n", __func__,
				usb_ld->if_usb_connected, usb_ld->ld.com_state);
		return -ENODEV;
	}

	if (pm_data->dpm_suspending) {
		mif_debug("Kernel in suspending try get_active later\n");
		/* during dpm_suspending...if AP get tx data, wake up. */
		wake_lock(&pm_data->l2_wake);
		return -EAGAIN;
	}

	if (dev->power.runtime_status == RPM_ACTIVE) {
		pm_data->resume_retry_cnt = 0;
		return 0;
	}

	if (!pm_data->resume_requested) {
		mif_debug("QW PM\n");
		queue_delayed_work(pm_data->wq, &pm_data->link_pm_work, 0);
	}
	mif_debug("Wait pm\n");
	INIT_COMPLETION(pm_data->active_done);
	ret = wait_for_completion_timeout(&pm_data->active_done,
						msecs_to_jiffies(2000));

	/* If usb link was disconnected while waiting ACTIVE State, usb device
	 * was removed, usb_ld->usbdev->dev is invalid and below
	 * dev->power.runtime_status is also invalid address.
	 * It will be occured LPA L3 -> AP iniated L0 -> disconnect -> link
	 * timeout
	 */
	if (!usb_ld->if_usb_connected || usb_ld->ld.com_state == COM_NONE) {
		mif_info("link is not connected!\n");
		return -ENODEV;
	}

	if (dev->power.runtime_status != RPM_ACTIVE) {
		mif_debug("link_active (%d) retry\n",
				dev->power.runtime_status);
		return -EAGAIN;
	} else
		mif_debug("link_active success(%d)\n", ret);

	return 0;
}

static void link_pm_runtime_start(struct work_struct *work)
{
	struct link_pm_data *pm_data =
		container_of(work, struct link_pm_data, link_pm_start.work);
	struct usb_device *usbdev = pm_data->usb_ld->usbdev;
	struct device *dev, *ppdev;
	struct link_device *ld = &pm_data->usb_ld->ld;

	if (!pm_data->usb_ld->if_usb_connected) {
		mif_debug("disconnect status, ignore\n");
		return;
	}

	dev = &pm_data->usb_ld->usbdev->dev;

	/* wait interface driver resumming */
	if (dev->power.runtime_status == RPM_SUSPENDED) {
		mif_debug("suspended yet, delayed work\n");
		queue_delayed_work(pm_data->wq, &pm_data->link_pm_start,
			msecs_to_jiffies(10));
		return;
	}

	if (pm_data->usb_ld->usbdev && dev->parent) {
		mif_debug("rpm_status: %d\n", dev->power.runtime_status);
		ppdev = dev->parent->parent;
		usb_set_autosuspend_delay(usbdev, 200);
		pm_runtime_forbid(dev);
		pm_runtime_allow(dev);
		pm_runtime_forbid(ppdev);
		pm_runtime_allow(ppdev);
		pm_data->resume_requested = false;
		pm_data->resume_retry_cnt = 0;
		/* retry prvious link tx q */
		queue_delayed_work(ld->tx_wq, &ld->tx_delayed_work, 0);
	}
}

static inline int link_pm_slave_wake(struct link_pm_data *pm_data)
{
	int spin = 20;
	int ret  = 0;
	int val;

	/* when slave device is in sleep, wake up slave cpu first */
	val = gpio_get_value(pm_data->gpio_link_hostwake);
	if (val != HOSTWAKE_TRIGLEVEL) {
		if (gpio_get_value(pm_data->gpio_link_slavewake)) {
			link_pm_set_slave_wakeup(pm_data->gpio_link_slavewake, 0);
			mif_debug("[SWK][1]=>[0]:[%d]\n",
				gpio_get_value(pm_data->gpio_link_slavewake));
			mdelay(5);
		}
		link_pm_set_slave_wakeup(pm_data->gpio_link_slavewake, 1);
		mif_debug("[SWK][0]=>[1]:[%d]\n",
				gpio_get_value(pm_data->gpio_link_slavewake));
		/* wait host wake signal*/
		while (spin-- && gpio_get_value(pm_data->gpio_link_hostwake) !=
							HOSTWAKE_TRIGLEVEL)
			mdelay(5);
		if (spin == 0)
			ret = MC_HOST_TIMEOUT;
		/*
		 *do {
		 *        struct platform_device *dev_modem = pm_data->pdev_modem;
		 *        struct modem_ctl *mc = platform_get_drvdata(dev_modem);
		 *        DECLARE_COMPLETION_ONSTACK(done);
		 *
		 *        mc->l2_done = &done;
		 *        if (!wait_for_completion_timeout(&done, 20*HZ))
		 *                ret = MC_HOST_TIMEOUT;
		 *        mc->l2_done = NULL;
		 *} while(0);
		 */
	} else {
		mif_debug("HOST_WUP:HOSTWAKE_TRIGLEVEL!\n");
		ret = MC_HOST_HIGH;
	}

	return ret;
}

static void link_pm_runtime_work(struct work_struct *work)
{
	int ret;
	struct link_pm_data *pm_data =
		container_of(work, struct link_pm_data, link_pm_work.work);
	struct usb_device *usbdev = pm_data->usb_ld->usbdev;
	struct device *dev = &usbdev->dev;
	int host_wakeup_done = 0;
	int spin1 = 10;
	int spin2 = 20;

	if (!pm_data->usb_ld->if_usb_connected || pm_data->dpm_suspending)
		return;

	if (pm_data->usb_ld->ld.com_state == COM_NONE)
		return;

	mif_debug("for dev 0x%p : current %d\n", dev,
				dev->power.runtime_status);

	usb_mark_last_busy(usbdev);

retry:
	switch (dev->power.runtime_status) {
	case RPM_ACTIVE:
		pm_data->resume_retry_cnt = 0;
		pm_data->resume_requested = false;
		complete(&pm_data->active_done);

		return;
	case RPM_SUSPENDED:
		if (pm_data->resume_requested)
			break;
		if (pm_data->dpm_suspending || host_wakeup_done) {
			mif_debug("DPM Suspending, spin:%d\n", spin2);
			if (spin2-- == 0) {
				mif_err("dpm resume timeout\n");
				break;
			}
			msleep(50);
			goto retry;
		}
		pm_data->resume_requested = true;
		wake_lock(&pm_data->rpm_wake);
		ret = link_pm_slave_wake(pm_data);
		switch (ret) {
		case MC_SUCCESS:
			host_wakeup_done = 1;
			/*wait until RPM_ACTIVE states*/
			goto retry;
		case MC_HOST_TIMEOUT:
			break;
		case MC_HOST_HIGH:
			if (spin2-- == 0) {
				mif_err("MC_HOST_HIGH! spin2==0\n");
				if(!link_pm_runtime_get_active(pm_data)) {
					host_wakeup_done = 1;
					spin2 = 20;
					goto retry;
				}
				mif_err("Modem resume fail\n");
			}
			break;
		}
		if (spin2-- == 0) {
			mif_err("ACM initiated resume, RPM_SUSPEND timeout\n");
			modem_notify_event(MODEM_EVENT_DISCONN);
			break;
		}
		if (!pm_data->usb_ld->if_usb_connected) {
			wake_unlock(&pm_data->rpm_wake);
			return;
		}
		ret = pm_runtime_resume(dev);
		if (ret < 0) {
			mif_err("resume error(%d)\n", ret);
			if (!pm_data->usb_ld->if_usb_connected) {
				wake_unlock(&pm_data->rpm_wake);
				return;
			}
			/* force to go runtime idle before retry resume */
			if (dev->power.timer_expires == 0 &&
						!dev->power.request_pending) {
				mif_debug("run time idle\n");
				pm_runtime_idle(dev);
			}
		} else
			queue_delayed_work(pm_data->wq,
					&pm_data->link_pm_start, 0);

		wake_unlock(&pm_data->rpm_wake);
		break;
	case RPM_SUSPENDING:
		mif_debug("RPM Suspending, spin:%d\n", spin1);
		if (spin1-- == 0) {
			mif_err("Modem suspending timeout\n");
			break;
		}
		msleep(100);
		goto retry;
	case RPM_RESUMING:
		mif_debug("RPM Resuming, spin:%d\n", spin2);
		if (spin2-- == 0) {
			mif_err("Modem resume timeout\n");
			break;
		}
		msleep(50);
		goto retry;
	default:
		break;
	}
	pm_data->resume_requested = false;
	/* check until runtime_status goes to active */
	if (dev->power.runtime_status == RPM_ACTIVE) {
		pm_data->resume_retry_cnt = 0;
		complete(&pm_data->active_done);
	} else if (pm_data->resume_retry_cnt++ > 10) {
		mif_err("runtime_status(%d), retry_cnt(%d)\n",
			dev->power.runtime_status, pm_data->resume_retry_cnt);
		modem_notify_event(MODEM_EVENT_DISCONN);
	} else
		queue_delayed_work(pm_data->wq, &pm_data->link_pm_work,
							msecs_to_jiffies(20));
}

static irqreturn_t host_wakeup_irq_handler(int irq, void *data)
{
	int value;
	struct link_pm_data *pm_data = data;
	struct platform_device *pdev_modem = pm_data->pdev_modem;
	struct modem_ctl *mc = platform_get_drvdata(pdev_modem);

	value = gpio_get_value(pm_data->gpio_link_hostwake);
	mif_debug("\n[HWK]<=[%d]\n", value);

	/*igonore host wakeup interrupt at suspending kernel*/
	if (pm_data->dpm_suspending) {
		mif_info("ignore request by suspending\n");
		/* Ignore HWK but AP got to L2 by suspending fail */
		wake_lock(&pm_data->l2_wake);
		return IRQ_HANDLED;
	}

	if (!mc->enum_done) {
		if (value == HOSTWAKE_TRIGLEVEL) {
			if (mc->l2_done) {
				complete(mc->l2_done);
				mc->l2_done = NULL;
				link_pm_set_slave_wakeup(pm_data->gpio_link_slavewake, 0);
			}
		}
	} else {
		if (value != HOSTWAKE_TRIGLEVEL) {
			if (mc->l2_done) {
				complete(mc->l2_done);
				mc->l2_done = NULL;
			}
			link_pm_set_slave_wakeup(pm_data->gpio_link_slavewake, 0);
		} else {
			queue_delayed_work(pm_data->wq, &pm_data->link_pm_work, 0);
		}
	}

	return IRQ_HANDLED;
}

static int link_pm_open(struct inode *inode, struct file *file)
{
	struct link_pm_data *pm_data =
		(struct link_pm_data *)file->private_data;
	file->private_data = (void *)pm_data;
	return 0;
}

static int link_pm_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static const struct file_operations link_pm_fops = {
	.owner   = THIS_MODULE,
	.open    = link_pm_open,
	.release = link_pm_release,
};

static int link_pm_notifier_event(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct link_pm_data *pm_data =
			container_of(this, struct link_pm_data,	pm_notifier);

	switch (event) {
	case PM_SUSPEND_PREPARE:
		if(pm_data_get_device(pm_data, ACM_SUSPEND))
			return NOTIFY_BAD;
		pm_data->dpm_suspending = true;
		mif_info("dpm suspending set to true\n");
		return NOTIFY_OK;
	case PM_POST_SUSPEND:
		pm_data_release_device(pm_data);
		pm_data->dpm_suspending = false;
		if (gpio_get_value(pm_data->gpio_link_hostwake)
			== HOSTWAKE_TRIGLEVEL) {
			queue_delayed_work(pm_data->wq, &pm_data->link_pm_work,
				0);
			mif_info("post resume\n");
		}
		mif_info("dpm suspending set to false\n");
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static struct modem_ctl *if_usb_get_modemctl(struct link_pm_data *pm_data)
{
	struct platform_device *pdev_modem = pm_data->pdev_modem;
	struct modem_ctl *mc = platform_get_drvdata(pdev_modem);

	return mc;
}

static int if_usb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct if_usb_devdata *devdata = usb_get_intfdata(intf);
	struct link_pm_data *pm_data = devdata->usb_ld->link_pm_data;

	if (message.event & PM_EVENT_AUTO) {
		if (devdata->hsic_channel_tx_count) {
			pr_debug("%s tx:%d\n", __func__,
					devdata->hsic_channel_tx_count);
			return -EBUSY;
		}
	}

	if (!devdata->disconnected && devdata->state == STATE_RESUMED) {
		usb_kill_urb(devdata->urb);
		devdata->state = STATE_SUSPENDED;
	}

	devdata->usb_ld->suspended++;

	if (devdata->usb_ld->suspended == IF_USB_DEVNUM_MAX*2) {
		mif_debug("[if_usb_suspended]\n");
		wake_lock_timeout(&pm_data->l2_wake, msecs_to_jiffies(500));
		if (!pm_data->rx_cnt && !pm_data->tx_cnt) {
			if (pm_data->ipc_debug_cnt++ > 10) {
				mif_err("No TX/RX after resume 10times\n");
				modem_notify_event(MODEM_EVENT_DISCONN);
			}
		} else {
			pm_data->ipc_debug_cnt = 0;
			pm_data->rx_cnt = 0;
			pm_data->tx_cnt = 0;
		}
	}

	return 0;
}

static int if_usb_resume(struct usb_interface *intf)
{
	int ret;
	struct if_usb_devdata *devdata = usb_get_intfdata(intf);
	struct link_pm_data *pm_data = devdata->usb_ld->link_pm_data;

	if (!devdata->disconnected && devdata->state == STATE_SUSPENDED) {
		ret = usb_rx_submit(devdata->usb_ld, devdata, GFP_ATOMIC);
		if (ret < 0) {
			mif_err("usb_rx_submit error with (%d)\n", ret);
			return ret;
		}
		devdata->state = STATE_RESUMED;
	}

	/* For debugging -  nomal case, never reach below... */
	if (pm_data->resume_retry_cnt > 5) {
		mif_err("retry_cnt=%d, rpm_status=%d",
			pm_data->resume_retry_cnt,
			devdata->usb_ld->usbdev->dev.power.runtime_status);
		pm_data->resume_retry_cnt = 0;
	}

	devdata->usb_ld->suspended--;
	wake_lock(&pm_data->l2_wake);

	if (!devdata->usb_ld->suspended) {
		mif_debug("[if_usb_resumed]\n");
		wake_lock(&pm_data->l2_wake);
		queue_delayed_work(pm_data->wq, &pm_data->link_pm_start, 0);
	}

	return 0;
}

static int if_usb_reset_resume(struct usb_interface *intf)
{
	int ret;
	struct if_usb_devdata *devdata = usb_get_intfdata(intf);
	struct link_pm_data *pm_data = devdata->usb_ld->link_pm_data;

	ret = if_usb_resume(intf);
	pm_data->ipc_debug_cnt = 0;
	/*
	 * for runtime suspend, kick runtime pm at L3 -> L0 reset resume
	*/
	if (!devdata->usb_ld->suspended)
		queue_delayed_work(pm_data->wq, &pm_data->link_pm_start, 0);

	return ret;
}

static void if_usb_disconnect(struct usb_interface *intf)
{
	struct if_usb_devdata *devdata = usb_get_intfdata(intf);
	struct link_pm_data *pm_data = devdata->usb_ld->link_pm_data;
	struct device *dev, *ppdev;
	struct link_device *ld = &devdata->usb_ld->ld;

	if (devdata->disconnected)
		return;

	usb_driver_release_interface(to_usb_driver(intf->dev.driver), intf);

	usb_kill_urb(devdata->urb);

	dev = &devdata->usb_ld->usbdev->dev;
	ppdev = dev->parent->parent;
	/*ehci*/
	pm_runtime_forbid(ppdev);

	mif_debug("dev 0x%p\n", devdata->usbdev);
	usb_put_dev(devdata->usbdev);

	devdata->data_intf = NULL;
	devdata->usbdev = NULL;
	devdata->disconnected = 1;
	devdata->state = STATE_SUSPENDED;

	devdata->usb_ld->ld.com_state = COM_NONE;
	pm_data->ipc_debug_cnt = 0;

	devdata->usb_ld->if_usb_connected = 0;
	devdata->usb_ld->suspended = 0;

	usb_set_intfdata(intf, NULL);

	/* cancel runtime start delayed works */
	cancel_delayed_work_sync(&pm_data->link_pm_start);
	cancel_delayed_work_sync(&ld->tx_delayed_work);

	return;
}

static int if_usb_set_pipe(struct usb_link_device *usb_ld,
			const struct usb_host_interface *desc, int pipe)
{
	if (pipe < 0 || pipe >= IF_USB_DEVNUM_MAX) {
		mif_err("undefined endpoint, exceed max\n");
		return -EINVAL;
	}

	mif_debug("set %d\n", pipe);

	if ((usb_pipein(desc->endpoint[0].desc.bEndpointAddress)) &&
	    (usb_pipeout(desc->endpoint[1].desc.bEndpointAddress))) {
		usb_ld->devdata[pipe].rx_pipe = usb_rcvbulkpipe(usb_ld->usbdev,
				desc->endpoint[0].desc.bEndpointAddress);
		usb_ld->devdata[pipe].tx_pipe = usb_sndbulkpipe(usb_ld->usbdev,
				desc->endpoint[1].desc.bEndpointAddress);
	} else if ((usb_pipeout(desc->endpoint[0].desc.bEndpointAddress)) &&
		   (usb_pipein(desc->endpoint[1].desc.bEndpointAddress))) {
		usb_ld->devdata[pipe].rx_pipe = usb_rcvbulkpipe(usb_ld->usbdev,
				desc->endpoint[1].desc.bEndpointAddress);
		usb_ld->devdata[pipe].tx_pipe = usb_sndbulkpipe(usb_ld->usbdev,
				desc->endpoint[0].desc.bEndpointAddress);
	} else {
		mif_err("undefined endpoint\n");
		return -EINVAL;
	}

	return 0;
}


static struct usb_id_info hsic_channel_info;

static int __devinit if_usb_probe(struct usb_interface *intf,
					const struct usb_device_id *id)
{
	int err;
	int pipe;
	const struct usb_cdc_union_desc *union_hdr;
	const struct usb_host_interface *data_desc;
	unsigned char *buf = intf->altsetting->extra;
	int buflen = intf->altsetting->extralen;
	struct usb_interface *data_intf;
	struct usb_device *usbdev = interface_to_usbdev(intf);
	struct usb_driver *usbdrv = to_usb_driver(intf->dev.driver);
	struct usb_id_info *info = (struct usb_id_info *)id->driver_info;
	struct usb_link_device *usb_ld = info->usb_ld;
	struct usb_interface *control_interface;
	struct usb_device *root_usbdev= to_usb_device(intf->dev.parent->parent);

	mif_debug("usbdev = 0x%p\n", usbdev);

	usb_ld->usbdev = usbdev;
	pm_runtime_forbid(&usbdev->dev);
	usb_ld->link_pm_data->dpm_suspending = false;
	usb_ld->link_pm_data->ipc_debug_cnt = 0;

	union_hdr = NULL;
	/* for WMC-ACM compatibility, WMC-ACM use an end-point for control msg*/
	if (intf->altsetting->desc.bInterfaceSubClass != USB_CDC_SUBCLASS_ACM) {
		mif_err("ignore Non ACM end-point\n");
		return -EINVAL;
	}
	if (!buflen) {
		if (intf->cur_altsetting->endpoint->extralen &&
				    intf->cur_altsetting->endpoint->extra) {
			buflen = intf->cur_altsetting->endpoint->extralen;
			buf = intf->cur_altsetting->endpoint->extra;
		} else {
			mif_err("Zero len descriptor reference\n");
			return -EINVAL;
		}
	}
	while (buflen > 0) {
		if (buf[1] == USB_DT_CS_INTERFACE) {
			switch (buf[2]) {
			case USB_CDC_UNION_TYPE:
				if (union_hdr)
					break;
				union_hdr = (struct usb_cdc_union_desc *)buf;
				break;
			default:
				break;
			}
		}
		buf += buf[0];
		buflen -= buf[0];
	}
	if (!union_hdr) {
		mif_err("USB CDC is not union type\n");
		return -EINVAL;
	}
	control_interface = usb_ifnum_to_if(usbdev, union_hdr->bMasterInterface0);
	control_interface->needs_remote_wakeup = 0;
	pm_runtime_set_autosuspend_delay(&root_usbdev->dev, 200); /*200ms*/

	data_intf = usb_ifnum_to_if(usbdev, union_hdr->bSlaveInterface0);
	if (!data_intf) {
		mif_err("data_inferface is NULL\n");
		return -ENODEV;
	}

	data_desc = data_intf->altsetting;
	if (!data_desc) {
		mif_err("data_desc is NULL\n");
		return -ENODEV;
	}

	pipe = intf->altsetting->desc.bInterfaceNumber / 2;
	if (if_usb_set_pipe(usb_ld, data_desc, pipe) < 0)
		return -EINVAL;

	usb_ld->devdata[pipe].usbdev                = usb_get_dev(usbdev);
	usb_ld->devdata[pipe].state                 = STATE_RESUMED;
	usb_ld->devdata[pipe].usb_ld                = usb_ld;
	usb_ld->devdata[pipe].data_intf             = data_intf;
	usb_ld->devdata[pipe].channel_id            = pipe;
	usb_ld->devdata[pipe].disconnected          = 0;
	usb_ld->devdata[pipe].control_interface     = control_interface;
	usb_ld->devdata[pipe].hsic_channel_rx_count = 0;
	usb_ld->devdata[pipe].hsic_channel_tx_count = 0;

	mif_debug("devdata usbdev = 0x%p\n", usb_ld->devdata[pipe].usbdev);

	usb_ld->suspended = 0;

	err = usb_driver_claim_interface(usbdrv, data_intf,
				(void *)&usb_ld->devdata[pipe]);
	if (err < 0) {
		mif_err("usb_driver_claim() failed\n");
		return err;
	}

	pm_suspend_ignore_children(&usbdev->dev, true);

	usb_set_intfdata(intf, (void *)&usb_ld->devdata[pipe]);

	/* rx start for this endpoint */
	usb_rx_submit(usb_ld, &usb_ld->devdata[pipe], GFP_KERNEL);

	wake_lock(&usb_ld->link_pm_data->l2_wake);

	if (pipe == HSIC_MAX_PIPE_ORDER_NR) {
		pr_info("%s pipe:%d\n", __func__, HSIC_MAX_PIPE_ORDER_NR);
		spin_lock_init(&usb_ld->link_pm_data->pm_data_lock);
		init_waitqueue_head(&usb_ld->link_pm_data->waitqueue);
		usb_ld->link_pm_data->state = ACM_READY;
		usb_ld->if_usb_connected = 1;
		modem_notify_event(MODEM_EVENT_CONN);
		if (!work_pending(&usb_ld->link_pm_data->link_pm_start.work))
			queue_delayed_work(usb_ld->link_pm_data->wq,
				&usb_ld->link_pm_data->link_pm_start,
				msecs_to_jiffies(500));
		skb_queue_purge(&usb_ld->ld.sk_raw_tx_q);
		usb_ld->ld.com_state = COM_ONLINE;
	}

	mif_debug("successfully done\n");

	return 0;
}

static void if_usb_free_pipe_data(struct usb_link_device *usb_ld)
{
	int i;

	for (i = 0; i < IF_USB_DEVNUM_MAX; i++) {
		if (usb_ld->devdata[i].rx_buf)
			kfree(usb_ld->devdata[i].rx_buf);
		if (usb_ld->devdata[i].urb)
			usb_kill_urb(usb_ld->devdata[i].urb);
	}
}

static struct usb_id_info hsic_channel_info = {
	.intf_id = IPC_CHANNEL,
};

static struct usb_device_id if_usb_ids[] = {
	{
          USB_DEVICE(IMC_MAIN_VID, IMC_MAIN_PID),
	  .driver_info = (unsigned long)&hsic_channel_info,
	},
	{}
};
MODULE_DEVICE_TABLE(usb, if_usb_ids);

static struct usb_driver if_usb_driver = {
	.name                 = "cdc_modem",
	.probe                = if_usb_probe,
	.disconnect           = if_usb_disconnect,
	.id_table             = if_usb_ids,
	.suspend              = if_usb_suspend,
	.resume               = if_usb_resume,
	.reset_resume         = if_usb_reset_resume,
	.supports_autosuspend = 1,
};

static int if_usb_init(struct link_device *ld)
{
	int ret;
	int i;
	struct usb_link_device *usb_ld = to_usb_link_device(ld);
	struct if_usb_devdata *pipe_data;
	struct usb_id_info *id_info;

	/* to connect usb link device with usb interface driver */
	for (i = 0; i < ARRAY_SIZE(if_usb_ids); i++) {
		id_info = (struct usb_id_info *)if_usb_ids[i].driver_info;
		if (id_info)
			id_info->usb_ld = usb_ld;
	}

	/* allocate rx buffer for usb receive */
	for (i = 0; i < IF_USB_DEVNUM_MAX; i++) {
		pipe_data = &usb_ld->devdata[i];
		pipe_data->channel_id = i;
		pipe_data->rx_buf_size = 16 * 1024;

		pipe_data->rx_buf = kmalloc(pipe_data->rx_buf_size,
						GFP_DMA | GFP_KERNEL);
		if (!pipe_data->rx_buf) {
			if_usb_free_pipe_data(usb_ld);
			ret = -ENOMEM;
			break;
		}

		pipe_data->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!pipe_data->urb) {
			mif_err("alloc urb fail\n");
			if_usb_free_pipe_data(usb_ld);
			return -ENOMEM;
		}
	}

	ret = usb_register(&if_usb_driver);
	if (ret) {
		mif_err("usb_register_driver() fail : %d\n", ret);
		return ret;
	}

	mif_info("if_usb_init() done : %d, usb_ld (0x%p)\n", ret, usb_ld);

	return ret;
}

static int usb_link_pm_init(struct usb_link_device *usb_ld, void *data)
{
	int r;
	struct platform_device *pdev = (struct platform_device *)data;
	struct modem_data *pdata =
			(struct modem_data *)pdev->dev.platform_data;
	struct modemlink_pm_data *pm_pdata = pdata->link_pm_data;
	struct link_pm_data *pm_data =
			kzalloc(sizeof(struct link_pm_data), GFP_KERNEL);
	if (!pm_data) {
		mif_err("link_pm_data is NULL\n");
		return -ENOMEM;
	}
	/* get link pm data from modemcontrol's platform data */
	pm_data->pdev_modem = data;

	pm_data->gpio_link_active = pdata->gpio_host_active;

	pm_data->gpio_link_enable = pm_pdata->gpio_link_enable;
	pm_data->gpio_link_hostwake = pm_pdata->gpio_link_hostwake;
	pm_data->gpio_link_slavewake = pm_pdata->gpio_link_slavewake;

	pm_data->irq_link_hostwake = gpio_to_irq(pm_data->gpio_link_hostwake);

	if_usb_get_modemctl(pm_data)->irq_link_hostwake =
						pm_data->irq_link_hostwake;

	pm_data->usb_ld = usb_ld;
	pm_data->ipc_debug_cnt = 0;
	usb_ld->link_pm_data = pm_data;

	pm_data->miscdev.minor = MISC_DYNAMIC_MINOR;
	pm_data->miscdev.name = "link_pm";
	pm_data->miscdev.fops = &link_pm_fops;

	r = misc_register(&pm_data->miscdev);
	if (r < 0) {
		mif_err("fail to register pm device(%d)\n", r);
		goto err_misc_register;
	}

	r = request_threaded_irq(pm_data->irq_link_hostwake,
		NULL, host_wakeup_irq_handler,
		IRQF_NO_SUSPEND | IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
		"hostwake", (void *)pm_data);
	if (r) {
		mif_err("fail to request irq(%d)\n", r);
		goto err_request_irq;
	}

	r = enable_irq_wake(pm_data->irq_link_hostwake);
	if (r) {
		mif_err("failed to enable_irq_wake:%d\n", r);
		goto err_set_wake_irq;
	}

	/* create work queue & init work for runtime pm */
	pm_data->wq = create_singlethread_workqueue("linkpmd");
	if (!pm_data->wq) {
		mif_err("fail to create wq\n");
		goto err_create_wq;
	}

	pm_data->pm_notifier.notifier_call = link_pm_notifier_event;
	register_pm_notifier(&pm_data->pm_notifier);

	init_completion(&pm_data->active_done);
	INIT_DELAYED_WORK(&pm_data->link_pm_work, link_pm_runtime_work);
	INIT_DELAYED_WORK(&pm_data->link_pm_start, link_pm_runtime_start);

	wake_lock_init(&pm_data->l2_wake, WAKE_LOCK_SUSPEND, "l2_hsic");
	wake_lock_init(&pm_data->rpm_wake, WAKE_LOCK_SUSPEND, "rpm_hsic");
	wake_lock_init(&pm_data->tx_async_wake, WAKE_LOCK_SUSPEND, "tx_hsic");

	return 0;

err_create_wq:
	disable_irq_wake(pm_data->irq_link_hostwake);
err_set_wake_irq:
	free_irq(pm_data->irq_link_hostwake, (void *)pm_data);
err_request_irq:
	misc_deregister(&pm_data->miscdev);
err_misc_register:
	kfree(pm_data);
	return r;
}

struct link_device *hsic_create_link_device(void *data)
{
	int ret;
	struct usb_link_device *usb_ld;
	struct link_device *ld;

	usb_ld = kzalloc(sizeof(struct usb_link_device), GFP_KERNEL);
	if (!usb_ld)
		return NULL;

	INIT_LIST_HEAD(&usb_ld->ld.list);
	skb_queue_head_init(&usb_ld->ld.sk_raw_tx_q);

	ld = &usb_ld->ld;

	ld->name = "hsic";
	ld->init_comm = usb_init_communication;
	ld->terminate_comm = usb_terminate_communication;
	ld->send = usb_send;
	ld->com_state = COM_NONE;
	ld->raw_tx_suspended = false;
	init_completion(&ld->raw_tx_resumed_by_cp);

	ld->tx_wq = create_singlethread_workqueue("usb_tx_wq");
	if (!ld->tx_wq) {
		mif_err("fail to create work Q.\n");
		goto err;
	}

	INIT_DELAYED_WORK(&ld->tx_delayed_work, usb_tx_work);
	INIT_DELAYED_WORK(&usb_ld->rx_retry_work, usb_rx_retry_work);
	usb_ld->rx_retry_cnt = 0;

	/* create link pm device */
	ret = usb_link_pm_init(usb_ld, data);
	if (ret)
		goto err;

	ret = if_usb_init(ld);
	if (ret)
		goto err;

	mif_info("%s : create_link_device DONE\n", usb_ld->ld.name);

	modem_notify_event(MODEM_EVENT_BOOT_INIT);

	return (void *)ld;
err:
	kfree(usb_ld);
	return NULL;
}

static void __exit if_usb_exit(void)
{
	usb_deregister(&if_usb_driver);
}

