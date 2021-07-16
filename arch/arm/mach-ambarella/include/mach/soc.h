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
#else
#error("No specified Ambarella Soc")
#endif


#endif
