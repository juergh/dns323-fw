/*******************************************************************************
 *
 * Name:        mwl_hotplug.c
 * Project:     Linux SoftAP for Marvell CB32/CB35 cardbus cards
 * Version:     $Revision: 1.1.1.1 $
 * Date:        $Date: 2009/08/19 08:02:21 $
 * Author:      Ralph Roesler (rroesler@syskonnect.de)
 * Purpose:     hotplug functions
 *
 ******************************************************************************/

/*******************************************************************************
 *
 *	(C)Copyright 2004-2005 SysKonnect GmbH.
 *	(C)Copyright 2004-2005 Marvell.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

#include "mwl_version.h"
#include "mwl_main.h"
#include "mwl_debug.h"

/*******************************************************************************
 *
 * Definitions (defines, typedefs, prototypes etc.)
 *
 ******************************************************************************/

static const struct pci_device_id mwl_id_tbl[] __devinitdata = {
    { 0x11ab, 0x1faa, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
        (unsigned long) "Marvell Libertas MB/CB35P Wireless LAN adapter"},
    { 0 }
};

MODULE_AUTHOR("Ralph Roesler <rroesler@syskonnect.de");
MODULE_DESCRIPTION("Marvell Libertas MB/CB35P Wireless LAN driver");
MODULE_SUPPORTED_DEVICE("Marvell Libertas MB/CB35P WLAN adapter");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, mwl_id_tbl);

static int __init mwl_module_init(void);
static void __exit mwl_module_exit(void);
static int mwl_probe(struct pci_dev *, const struct pci_device_id *);
static void mwl_remove(struct pci_dev *);
static int mwl_suspend(struct pci_dev *, u32);
static int mwl_resume(struct pci_dev *);
static const char * mwl_getAdapterDescription(u_int32_t, u_int32_t);

static struct pci_driver mwl_driver = {
   .name     = DRV_NAME,
   .id_table = mwl_id_tbl,
   .probe    = mwl_probe,
   .remove   = mwl_remove,
   .suspend  = mwl_suspend,
   .resume   = mwl_resume,
};

module_init(mwl_module_init);
module_exit(mwl_module_exit);

/*******************************************************************************
 *
 * Local functions
 *
 ******************************************************************************/

static int __init
mwl_module_init(void)
{
    printk(KERN_INFO "Loaded %s driver, version %s\n", DRV_NAME, DRV_VERSION);
    return pci_module_init(&mwl_driver);
}

static void __exit
mwl_module_exit(void)
{
    pci_unregister_driver(&mwl_driver);
    printk(KERN_INFO "Unloaded %s driver\n", DRV_NAME);
}

static int 
mwl_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct mwl_private *mwlp = NULL;
    unsigned long physAddr = 0;

    MWL_DBG_ENTER(DBG_CLASS_HPLUG);

    if (pci_enable_device(pdev)) {
        return -EIO;
    }

    if (pci_set_dma_mask(pdev, 0xffffffff)) {
        printk(KERN_ERR "%s: 32-bit PCI DMA not supported", DRV_NAME);
        goto err_pci_disable_device;
    }

    pci_set_master(pdev);

    mwlp = kmalloc(sizeof(struct mwl_private), GFP_KERNEL);
    if (mwlp == NULL) {
        printk(KERN_ERR "%s: no mem for private driver context\n", DRV_NAME);
        goto err_pci_disable_device;
    }
    memset(mwlp, 0, sizeof(struct mwl_private));

    physAddr = pci_resource_start(pdev, 0);
    if (!request_mem_region(physAddr, pci_resource_len(pdev, 0), DRV_NAME)) {
        printk(KERN_ERR "%s: cannot reserve PCI memory region 0\n", DRV_NAME);
        goto err_kfree;
    }
    mwlp->ioBase0 = ioremap(physAddr,pci_resource_len(pdev,0));
    if (!mwlp->ioBase0) {
        printk(KERN_ERR "%s: cannot remap PCI memory region 0\n", DRV_NAME);
        goto err_release_mem_region_bar0;
    }

    physAddr = pci_resource_start(pdev, 1);
    if (!request_mem_region(physAddr, pci_resource_len(pdev, 1), DRV_NAME)) {
        printk(KERN_ERR "%s: cannot reserve PCI memory region 1\n", DRV_NAME);
        goto err_iounmap_ioBase0;
    }
    mwlp->ioBase1 = ioremap(physAddr,pci_resource_len(pdev,1));
    if (!mwlp->ioBase1) {
        printk(KERN_ERR "%s: cannot remap PCI memory region 1\n", DRV_NAME);
        goto err_release_mem_region_bar1;
    }

    memcpy(mwlp->netDev.name, "mwl%d", sizeof("mwl%d"));
    mwlp->netDev.irq       = pdev->irq;
    mwlp->netDev.mem_start = pci_resource_start(pdev, 0);
    mwlp->netDev.mem_end   = physAddr + pci_resource_len(pdev, 1);
    mwlp->netDev.priv      = mwlp;
    mwlp->pPciDev          = pdev;
#ifdef MWL_KERNEL_26
    SET_MODULE_OWNER(mwlp->netDev);
#endif

    pci_set_drvdata(pdev, &(mwlp->netDev));

    if (request_irq(mwlp->netDev.irq, mwl_isr, SA_SHIRQ, 
                    mwlp->netDev.name, &(mwlp->netDev))) {
        printk(KERN_ERR "%s: request_irq failed\n", mwlp->netDev.name);
        goto err_iounmap_ioBase1;
    }

    if (mwl_init(&(mwlp->netDev), id->device)) {
        goto err_free_irq;
    }

    printk(KERN_INFO "%s: %s: mem=0x%lx, irq=%d\n",
           mwlp->netDev.name,mwl_getAdapterDescription(id->vendor,id->device), 
           mwlp->netDev.mem_start, mwlp->netDev.irq);

    MWL_DBG_EXIT(DBG_CLASS_HPLUG);
    return 0; /* firmware upload is triggered in mwl_open */

err_free_irq:
    free_irq(mwlp->netDev.irq, &(mwlp->netDev));
err_iounmap_ioBase1:
    iounmap(mwlp->ioBase1);
err_release_mem_region_bar1:
    release_mem_region(pci_resource_start(pdev, 1), pci_resource_len(pdev, 1));
err_iounmap_ioBase0:
    iounmap(mwlp->ioBase0);
err_release_mem_region_bar0:
    release_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
err_kfree:
    kfree(mwlp);
err_pci_disable_device:
    pci_disable_device(pdev);
    MWL_DBG_EXIT_ERROR(DBG_CLASS_DESC, "init error");
    return -EIO;
}

static void 
mwl_remove(struct pci_dev *pdev)
{
    struct net_device *netdev = pci_get_drvdata(pdev);
    struct mwl_private *mwlp = netdev->priv;
    
    MWL_DBG_ENTER(DBG_CLASS_HPLUG);

    if (mwl_deinit(netdev)) {
        printk(KERN_ERR "%s: deinit of device failed\n", netdev->name);
    }
    if (netdev->irq) {
        free_irq(netdev->irq, netdev);
    }
    iounmap(mwlp->ioBase1);
    iounmap(mwlp->ioBase0);
    release_mem_region(pci_resource_start(pdev,1),pci_resource_len(pdev,1));
    release_mem_region(pci_resource_start(pdev,0),pci_resource_len(pdev,0));
    pci_disable_device(pdev);
    free_netdev(netdev);
    MWL_DBG_EXIT(DBG_CLASS_HPLUG);
}

static int 
mwl_suspend(struct pci_dev *pdev, u32 state)
{
    printk(KERN_INFO "%s: suspended device\n", DRV_NAME);
    return 0;
}

static int 
mwl_resume(struct pci_dev *pdev)
{
    printk(KERN_INFO "%s: resumed device\n", DRV_NAME);
    return 0;
}

static const char * 
mwl_getAdapterDescription(u_int32_t vendorid, u_int32_t devid)
{
    int numEntry=((sizeof(mwl_id_tbl)/sizeof(struct pci_device_id))-1);

    while (numEntry) {
        numEntry--;
        if ((mwl_id_tbl[numEntry].vendor == vendorid) &&
            (mwl_id_tbl[numEntry].device == devid)) {
            if ((const char *) mwl_id_tbl[numEntry].driver_data != NULL) {
                return (const char *) mwl_id_tbl[numEntry].driver_data;
            }
            break;
        }
    }
    return "Marvell ???";
}

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
