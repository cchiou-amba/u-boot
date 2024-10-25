#ifndef __MACH_SOC_H__
#define __MACH_SOC_H__

#define REF_CLK		24000000

#if defined(CONFIG_ARCH_AMBARELLA_S6LM)
#include "s6lm.h"
#elif defined(CONFIG_ARCH_AMBARELLA_CV22)
#include "cv22.h"
#elif defined(CONFIG_ARCH_AMBARELLA_CV25)
#include "cv25.h"
#elif defined(CONFIG_ARCH_AMBARELLA_CV28)
#include "cv28.h"
#elif defined(CONFIG_ARCH_AMBARELLA_CV5)
#include "cv5.h"
#elif defined(CONFIG_ARCH_AMBARELLA_CV72)
#include "cv72.h"
#elif defined(CONFIG_ARCH_AMBARELLA_CV3)
#include "cv3.h"
#elif defined(CONFIG_ARCH_AMBARELLA_CV75)
#include "cv75.h"
#elif defined(CONFIG_ARCH_AMBARELLA_N1_655)
#include "n1_655.h"
#else
#error("No specified Ambarella Soc")
#endif


#endif
