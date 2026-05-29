// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Ambarella International LP
 */

#include <common.h>
#include <dm.h>
#include <wdt.h>

static int ambarella_wdt_start(struct udevice *dev, u64 timeout, ulong flags)
{
	pr_debug("debug: %s %d\n", __func__,__LINE__);

	return 0;
}

static int ambarella_wdt_stop(struct udevice *dev)
{
	pr_debug("debug: %s %d\n", __func__,__LINE__);

	return 0;
}

static int ambarella_wdt_reset(struct udevice *dev)
{
	pr_debug("debug: %s %d\n", __func__,__LINE__);

	return 0;
}

static int ambarella_wdt_expire_now(struct udevice *dev, ulong flags)
{
	pr_debug("debug: %s %d\n", __func__,__LINE__);

	return 0;
}

static const struct wdt_ops ambarella_wdt_ops = {
	.start = ambarella_wdt_start,
	.reset = ambarella_wdt_reset,
	.stop = ambarella_wdt_stop,
	.expire_now = ambarella_wdt_expire_now,
};

static const struct udevice_id ambarella_wdt_ids[] = {
	{ .compatible = "ambarella,wdt" },
	{}
};

U_BOOT_DRIVER(wdt_ambarella) = {
	.name = "wdt_ambarella",
	.id = UCLASS_WDT,
	.of_match = ambarella_wdt_ids,
	.ops = &ambarella_wdt_ops,
};
