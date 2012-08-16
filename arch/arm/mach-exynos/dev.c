/* linux/arch/arm/mach-exynos/dev.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * EXYNOS4 Device List support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <mach/dev.h>
#ifdef CONFIG_ARCH_EXYNOS4
#include <mach/busfreq_exynos4.h>
#else
#include <mach/busfreq_exynos5.h>
#endif

static LIST_HEAD(domains_list);
static DEFINE_MUTEX(domains_mutex);

static struct device_domain *find_device_domain(struct device *dev)
{
	struct device_domain *tmp_domain, *domain = ERR_PTR(-ENODEV);

	mutex_lock(&domains_mutex);
	list_for_each_entry(tmp_domain, &domains_list, node) {
		if (tmp_domain->device == dev) {
			domain = tmp_domain;
			break;
		}
	}

	mutex_unlock(&domains_mutex);
	return domain;
}

int dev_add(struct device_domain *dev, struct device *device)
{
	if (!dev || !device)
		return -EINVAL;

	mutex_lock(&domains_mutex);
	INIT_LIST_HEAD(&dev->domain_list);
	INIT_LIST_HEAD(&dev->max_domain_list);
	dev->device = device;
	list_add(&dev->node, &domains_list);
	mutex_unlock(&domains_mutex);

	return 0;
}

struct device *dev_get(const char *name)
{
	struct device_domain *domain;

	mutex_lock(&domains_mutex);
	list_for_each_entry(domain, &domains_list, node)
		if (strcmp(name, dev_name(domain->device)) == 0)
			goto found;

	mutex_unlock(&domains_mutex);
	return ERR_PTR(-ENODEV);
found:
	mutex_unlock(&domains_mutex);
	return domain->device;
}

void dev_put(const char *name)
{
	return;
}
static void dev_timeout_work_fn(struct work_struct *work)
{
	struct domain_lock *lock = container_of(to_delayed_work(work),
						  struct domain_lock,
						  work);
	lock->freq = 0;
}
int dev_lock_timeout(struct device *device, struct device *dev, unsigned long freq, unsigned long timeout_ms)
{
	struct device_domain *domain;
	struct domain_lock *lock;
	int ret = 0;

	if(timeout_ms <= 0) {
		return -EINVAL;
	}
	domain = find_device_domain(device);

	if (IS_ERR(domain)) {
		dev_err(dev, "Can't find device domain.\n");
		return -EINVAL;
	}

	mutex_lock(&domains_mutex);
	list_for_each_entry(lock, &domain->domain_list, node) {
		if (lock->device == dev) {
			/* If the lock already exist, only update the freq */
			lock->freq = freq;
			lock->timeout_ms = timeout_ms;
			goto out;
		}
	}

	lock = kzalloc(sizeof(struct domain_lock), GFP_KERNEL);
	if (!lock) {
		dev_err(device, "Unable to create domain_lock");
		ret = -ENOMEM;
		goto out;
	}
	lock->domain = domain;
	lock->device = dev;
	lock->freq = freq;
	lock->timeout_ms = timeout_ms;
	INIT_DELAYED_WORK(&lock->work, dev_timeout_work_fn);
	list_add(&lock->node, &domain->domain_list);

out:
	if (delayed_work_pending(&lock->work))
		cancel_delayed_work_sync(&lock->work);	
	queue_delayed_work(system_freezable_wq, &lock->work, msecs_to_jiffies(lock->timeout_ms));

	mutex_unlock(&domains_mutex);
	exynos_request_apply(freq, dev);
	return ret;
}

int dev_lock(struct device *device, struct device *dev,
		unsigned long freq)
{
	struct device_domain *domain;
	struct domain_lock *lock;
	int ret = 0;

	domain = find_device_domain(device);

	if (IS_ERR(domain)) {
		dev_err(dev, "Can't find device domain.\n");
		return -EINVAL;
	}

	mutex_lock(&domains_mutex);
	list_for_each_entry(lock, &domain->domain_list, node) {
		if (lock->device == dev) {
			/* If the lock already exist, only update the freq */
			lock->freq = freq;
			goto out;
		}
	}

	lock = kzalloc(sizeof(struct domain_lock), GFP_KERNEL);
	if (!lock) {
		dev_err(device, "Unable to create domain_lock");
		ret = -ENOMEM;
		goto out;
	}
	lock->domain = domain;
	lock->device = dev;
	lock->freq = freq;
	lock->timeout_ms = 0;
	INIT_DELAYED_WORK(&lock->work, dev_timeout_work_fn);
	list_add(&lock->node, &domain->domain_list);

out:
	mutex_unlock(&domains_mutex);
	exynos_request_apply(freq, dev);
	return ret;
}

int dev_unlock(struct device *device, struct device *dev)
{
	struct device_domain *domain;
	struct domain_lock *lock;

	domain = find_device_domain(device);

	if (IS_ERR(domain)) {
		dev_err(dev, "Can't find device domain.\n");
		return -EINVAL;
	}

	mutex_lock(&domains_mutex);
	list_for_each_entry(lock, &domain->domain_list, node) {
		if (lock->device == dev) {
			list_del(&lock->node);
			kfree(lock);
			break;
		}
	}

	mutex_unlock(&domains_mutex);

	return 0;
}
int dev_lock_max(struct device *device, struct device *dev, unsigned long freq)
{
	struct device_domain *domain;
	struct domain_lock *lock;
	int ret = 0;

	domain = find_device_domain(device);

	if (IS_ERR(domain)) {
		dev_err(dev, "Can't find device domain.\n");
		return -EINVAL;
	}

	mutex_lock(&domains_mutex);
	list_for_each_entry(lock, &domain->max_domain_list, node) {
		if (lock->device == dev) {
			/* If the lock already exist, only update the freq */
			lock->freq = freq;
			goto out;
		}
	}

	lock = kzalloc(sizeof(struct domain_lock), GFP_KERNEL);
	if (!lock) {
		dev_err(device, "Unable to create domain_lock");
		ret = -ENOMEM;
		goto out;
	}
	lock->domain = domain;
	lock->device = dev;
	lock->freq = freq;
	lock->timeout_ms = 0;
	list_add(&lock->node, &domain->max_domain_list);
out:
	mutex_unlock(&domains_mutex);

	return ret;
}
int dev_unlock_max(struct device *device, struct device *dev)
{
	struct device_domain *domain;
	struct domain_lock *lock;

	domain = find_device_domain(device);

	if (IS_ERR(domain)) {
		dev_err(dev, "Can't find device domain.\n");
		return -EINVAL;
	}

	mutex_lock(&domains_mutex);
	list_for_each_entry(lock, &domain->max_domain_list, node) {
		if (lock->device == dev) {
			list_del(&lock->node);
			kfree(lock);
			break;
		}
	}

	mutex_unlock(&domains_mutex);

	return 0;
}
unsigned long dev_uplimit_freq(struct device *device)
{
	struct device_domain *domain;
	struct domain_lock *lock;
	unsigned long freq = 0;

	domain = find_device_domain(device);
	if (IS_ERR(domain)) {
		dev_dbg(device, "Can't find device domain.\n");
		return freq;
	}

	mutex_lock(&domains_mutex);
	list_for_each_entry(lock, &domain->max_domain_list, node)
		if ((lock->freq < freq || freq==0))
			freq = lock->freq;
	mutex_unlock(&domains_mutex);

	return freq;
}
unsigned long dev_max_freq(struct device *device)
{
	struct device_domain *domain;
	struct domain_lock *lock;
	unsigned long freq = 0;

	domain = find_device_domain(device);
	if (IS_ERR(domain)) {
		dev_dbg(device, "Can't find device domain.\n");
		return freq;
	}

	mutex_lock(&domains_mutex);
	list_for_each_entry(lock, &domain->domain_list, node)
		if (lock->freq > freq)
			freq = lock->freq;

	mutex_unlock(&domains_mutex);

	return freq;
}

int dev_lock_list(struct device *device, char *buf)
{
	struct device_domain *domain;
	struct domain_lock *lock;
	int count = 0;

	domain = find_device_domain(device);
	if (IS_ERR(domain)) {
		dev_dbg(device, "Can't find device domain.\n");
		return 0;
	}

	mutex_lock(&domains_mutex);
	count = sprintf(buf, "Min Limit Lock List\n");
	list_for_each_entry(lock, &domain->domain_list, node)
		count += sprintf(buf + count, "%s : %lu\n", dev_name(lock->device), lock->freq);
	count += sprintf(buf + count, "Max Limit Lock List\n");
	list_for_each_entry(lock, &domain->max_domain_list, node)
		count += sprintf(buf + count, "%s : %lu\n", dev_name(lock->device), lock->freq);
	mutex_unlock(&domains_mutex);

	return count;
}