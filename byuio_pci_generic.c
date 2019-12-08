/* byuio_pci_generic - generic UIO driver for PCI 2.3 devices
 *
 * Copyright (C) 2009 Red Hat, Inc.
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * Since the driver does not declare any device ids, you must allocate
 * id and bind the device to the driver yourself.  For example:
 *
 * # echo "8086 10f5" > /sys/bus/pci/drivers/byuio_pci_generic/new_id
 * # echo -n 0000:00:19.0 > /sys/bus/pci/drivers/e1000e/unbind
 * # echo -n 0000:00:19.0 > /sys/bus/pci/drivers/byuio_pci_generic/bind
 * # ls -l /sys/bus/pci/devices/0000:00:19.0/driver
 * .../0000:00:19.0/driver -> ../../../bus/pci/drivers/byuio_pci_generic
 *
 * Driver won't bind to devices which do not support the Interrupt Disable Bit
 * in the command register. All devices compliant to PCI 2.3 (circa 2002) and
 * all compliant PCI Express devices should support this bit.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>

#define DRIVER_VERSION "0.01.0"
#define DRIVER_AUTHOR "Binyao Jiang <binyaoj2@illinois.edu>"
#define DRIVER_DESC "Revised generic UIO driver for PCI 2.3 devices"
#define IRQ_VECTOR_MAX_NUM (32)

extern int byuio_register_device(struct module *owner, struct device *parent, struct uio_info *info);
extern void byuio_unregister_device(struct uio_info *info);
extern void byuio_event_notify(struct uio_info *info);
struct uio_pci_generic_dev
{
	struct uio_info info;
	struct pci_dev *pdev;
};

static inline struct uio_pci_generic_dev *
to_uio_pci_generic_dev(struct uio_info *info)
{
	return container_of(info, struct uio_pci_generic_dev, info);
}

/* Interrupt handler. Read/modify/write the command register to disable
 * the interrupt. */
static irqreturn_t irqhandler(int irq, void *dev_id)
{
	struct uio_info *info = (struct uio_info *)dev_id;

	byuio_event_notify(info);
	/* UIO core will signal the user process. */
	return IRQ_HANDLED;
}

static int probe(struct pci_dev *pdev,
				 const struct pci_device_id *id)
{
	struct uio_pci_generic_dev *gdev;
	int err;

	err = pci_enable_device(pdev);
	if (err)
	{
		dev_err(&pdev->dev, "%s: pci_enable_device failed: %d\n",
				__func__, err);
		return err;
	}

	gdev = kzalloc(sizeof(struct uio_pci_generic_dev), GFP_KERNEL);
	if (!gdev)
	{
		err = -ENOMEM;
		goto err_alloc;
	}

	gdev->info.mem[0].addr = pci_resource_start(pdev, 0);
	if (!gdev->info.mem[0].addr)
		goto out_release;
	gdev->info.mem[0].internal_addr = pci_ioremap_bar(pdev, 0);
	if (!gdev->info.mem[0].internal_addr)
		goto out_release;

	gdev->info.mem[0].size = pci_resource_len(pdev, 0);
	gdev->info.mem[0].memtype = UIO_MEM_PHYS;

	gdev->info.name = "byuio_pci_generic";
	gdev->info.version = DRIVER_VERSION;
	gdev->pdev = pdev;
	if (pdev->irq)
	{
		gdev->info.irq = UIO_IRQ_CUSTOM;
		// only support interrupt for completion queue id = 1
		if ((err = pci_alloc_irq_vectors(pdev, 1, IRQ_VECTOR_MAX_NUM, PCI_IRQ_ALL_TYPES)) < 0)
		{
			dev_err(&pdev->dev, "%s: pci_alloc_irq_vectors failed: %d\n",
					__func__, err);
			goto err_alloc_irq;
		}

		if ((err = pci_request_irq(pdev, 1, irqhandler, NULL, &gdev->info, "nvme_intr")) < 0)
		{
			dev_err(&pdev->dev, "%s: pci_alloc_irq_vectors_affinity failed: %d\n",
					__func__, err);
			goto err_req_irq;
		}
	}
	else
	{
		dev_warn(&pdev->dev, "No IRQ assigned to device: "
							 "no support for interrupts?\n");
	}

	err = byuio_register_device(THIS_MODULE, &pdev->dev, &gdev->info);
	if (err)
		goto err_register;
	pci_set_drvdata(pdev, gdev);

	return 0;
err_register:
	pci_free_irq(pdev, 1, &gdev->info);
err_req_irq:
	pci_free_irq_vectors(pdev);
err_alloc_irq:
	iounmap(gdev->info.mem[0].internal_addr);
out_release:
	kfree(gdev);
err_alloc:
	pci_disable_device(pdev);
	return err;
}

static void remove(struct pci_dev *pdev)
{
	struct uio_pci_generic_dev *gdev = pci_get_drvdata(pdev);

	pci_free_irq(pdev, 1, &gdev->info);
	pci_free_irq_vectors(pdev);
	iounmap(gdev->info.mem[0].internal_addr);
	byuio_unregister_device(&gdev->info);
	pci_disable_device(pdev);
	kfree(gdev);
}

static struct pci_driver uio_pci_driver = {
	.name = "byuio_pci_generic",
	.id_table = NULL, /* only dynamic id's */
	.probe = probe,
	.remove = remove,
};

module_pci_driver(uio_pci_driver);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
