#ifndef __MACH_SOC_H__
#define __MACH_SOC_H__

#define REF_CLK		24000000

#define S6LM		(17000)
#define CV2		(21000)
#define CV22		(22000)
#define CV25		(25000)
#define CV28		(27000)
#define CV5		(30000)
#define N1		(31000)
#define CV3		(31000)
#define CV72		(32000)
#define CV3AD685	(33000)
#define CV75            (34000)
#define N1_655		(35000)

#if defined(CONFIG_ARCH_AMBARELLA_S6LM)
#include "s6lm.h"
#define CHIP_REV	S6LM
#elif defined(CONFIG_ARCH_AMBARELLA_CV22)
#include "cv22.h"
#define CHIP_REV	CV22
#elif defined(CONFIG_ARCH_AMBARELLA_CV25)
#include "cv25.h"
#define CHIP_REV	CV25
#elif defined(CONFIG_ARCH_AMBARELLA_CV28)
#include "cv28.h"
#define CHIP_REV	CV28
#elif defined(CONFIG_ARCH_AMBARELLA_CV5)
#include "cv5.h"
#define CHIP_REV	CV5
#elif defined(CONFIG_ARCH_AMBARELLA_CV72)
#include "cv72.h"
#define CHIP_REV	CV72
#elif defined(CONFIG_ARCH_AMBARELLA_CV3)
#include "cv3.h"
#define CHIP_REV	CV3
#elif defined(CONFIG_ARCH_AMBARELLA_CV75)
#include "cv75.h"
#define CHIP_REV	CV75
#elif defined(CONFIG_ARCH_AMBARELLA_N1_655)
#include "n1_655.h"
#define CHIP_REV	N1_655
#elif defined(CONFIG_ARCH_AMBARELLA_CV7)
#include "cv7.h"
#define CHIP_REV	CV7
#else
#error("No specified Ambarella Soc")
#endif

#endif
