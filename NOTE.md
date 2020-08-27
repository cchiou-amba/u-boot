` CONFIG_SYS_MALLOC_F_LEN=0x100000
	Heap for board_f.
` specify the u-boot stdout
	chosen {
		stdout-path = "serial0";
	};
` CONFIG_DEBUG_UART

 #if defined(CONFIG_PPC) || defined(CONFIG_SYS_FSL_CLK) || defined(CONFIG_M68K)
        /* get CPU and bus clocks according to the environment variable */
@@ -1009,6 +1009,8 @@ void board_init_f(ulong boot_flags)
        gd->flags = boot_flags;
        gd->have_console = 0;
 
+       board_early_init_f();
+
        if (initcall_run_list(init_sequence_f))
                hang();

` set uart pinconfig pre-relocated
	# pinctrl_ambarella.c
	U_BOOT_DRIVER(pinctrl_ambarella) = {
		.name		= "pinctrl_ambarella",
		.id		= UCLASS_PINCTRL,
		.of_match	= ambarella_pinctrl_ids,
		.priv_auto_alloc_size = sizeof(struct ambarella_pinctrl_priv),
		.ops		= &ambarella_pinctrl_ops,
		.probe		= ambarella_pinctrl_probe,
		.flags		= DM_FLAG_PRE_RELOC, *
	};

	# .dts
	uart0_pins: uart0@0 {
			    reg = <0>;
			    amb,pinmux-ids = <0x100a 0x100b>;
			    u-boot,dm-pre-reloc; *
		    };

  fastboot erase bootloader; fastboot flash bootloader ~/share/u-boot.bin
  fastboot erase kernel; fastboot flash kernel /home/qtu/work/stable/ambarella/out/s6lm_pineapple/kernel/Image
  fastboot erase rootfs; fastboot flash rootfs /home/qtu/work/stable/ambarella/out/s6lm_pineapple/rootfs/ubifs



  - board_f

          ^ ----------------------^ --> CONFIG_SYS_INIT_SP_ADDR		# 0x10000000
          |   SYS_MALLOC_F_LEN    |
           -----------------------  --> sp register pointer
                                    \
                                     \
                                      -  malloc_base		* Heap for malloc simple
                                      -  struct global_data	* x18 - crt0_64.S

 - board_r

          ^ ----------------------^ --> RAM TOP
          |     ~~~~~~~~~~        |
          |     ~~~~~~~~~~        |
          |     ~~~~~~~~~~        |
          |     ~~~~~~~~~~        |
          |     ~~~~~~~~~~        |
          * ----------------------* --> CONFIG_SYS_LOAD_ADDR		# 0x18000000
          |     FASTBOOT_SIZE     |					# 0x08000000
           -----------------------  --> CONFIG_FASTBOOT_ADDR		# 0x10000000

 - CONFIG_CMD_MTD & CONFIG_MTD_PARTITIONS
 	set `mtdparts` after executing `mtd read xxx`

