/* 
	对FreeRTOS中动态任务创建中的xTaskStartScheduler()、xPortStartScheduler()内部实现进行详解。
*/	

/*
	vTaskStartScheduler（）：用于启动任务调度器，任务调度器启动后，FreeRTOS便会开始进行任务调度。
	vTaskStartScheduler内部实现步骤：
		1.创建空闲任务: prvIdleTask
		2.创建软件定时器任务：xTimerCreateTimerTask
		3.关中断（在启动第一个任务时开启）
		4.初始化一些全局变量
		5.调用函数xPortStartScheduler()
		
*/
void vTaskStartScheduler( void )
{
    BaseType_t xReturn;

    /* Add the idle task at the lowest priority. */
    #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
        {
            StaticTask_t * pxIdleTaskTCBBuffer = NULL;
            StackType_t * pxIdleTaskStackBuffer = NULL;
            uint32_t ulIdleTaskStackSize;

            /* The Idle task is created using user provided RAM - obtain the
             * address of the RAM then create the idle task. */
            vApplicationGetIdleTaskMemory( &pxIdleTaskTCBBuffer, &pxIdleTaskStackBuffer, &ulIdleTaskStackSize );
            xIdleTaskHandle = xTaskCreateStatic( prvIdleTask,
                                                 configIDLE_TASK_NAME,
                                                 ulIdleTaskStackSize,
                                                 ( void * ) NULL,       /*lint !e961.  The cast is not redundant for all compilers. */
                                                 portPRIVILEGE_BIT,     /* In effect ( tskIDLE_PRIORITY | portPRIVILEGE_BIT ), but tskIDLE_PRIORITY is zero. */
                                                 pxIdleTaskStackBuffer,
                                                 pxIdleTaskTCBBuffer ); /*lint !e961 MISRA exception, justified as it is not a redundant explicit cast to all supported compilers. */

            if( xIdleTaskHandle != NULL )
            {
                xReturn = pdPASS;
            }
            else
            {
                xReturn = pdFAIL;
            }
        }
    #else /* if ( configSUPPORT_STATIC_ALLOCATION == 1 ) */
        {
            /* The Idle task is being created using dynamically allocated RAM. */
            xReturn = xTaskCreate( prvIdleTask,
                                   configIDLE_TASK_NAME,
                                   configMINIMAL_STACK_SIZE,
                                   ( void * ) NULL,
                                   portPRIVILEGE_BIT,  /* In effect ( tskIDLE_PRIORITY | portPRIVILEGE_BIT ), but tskIDLE_PRIORITY is zero. */
                                   &xIdleTaskHandle ); /*lint !e961 MISRA exception, justified as it is not a redundant explicit cast to all supported compilers. */
        }
    #endif /* configSUPPORT_STATIC_ALLOCATION */

    #if ( configUSE_TIMERS == 1 )
        {
            if( xReturn == pdPASS )
            {
                xReturn = xTimerCreateTimerTask();
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }
        }
    #endif /* configUSE_TIMERS */

    if( xReturn == pdPASS )
    {
        /* freertos_tasks_c_additions_init() should only be called if the user
         * definable macro FREERTOS_TASKS_C_ADDITIONS_INIT() is defined, as that is
         * the only macro called by the function. */
        #ifdef FREERTOS_TASKS_C_ADDITIONS_INIT
            {
                freertos_tasks_c_additions_init();
            }
        #endif

        /* Interrupts are turned off here, to ensure a tick does not occur
         * before or during the call to xPortStartScheduler().  The stacks of
         * the created tasks contain a status word with interrupts switched on
         * so interrupts will automatically get re-enabled when the first task
         * starts to run. */
        portDISABLE_INTERRUPTS();

        #if ( configUSE_NEWLIB_REENTRANT == 1 )
            {
                /* Switch Newlib's _impure_ptr variable to point to the _reent
                 * structure specific to the task that will run first.
                 * See the third party link http://www.nadler.com/embedded/newlibAndFreeRTOS.html
                 * for additional information. */
                _impure_ptr = &( pxCurrentTCB->xNewLib_reent );
            }
        #endif /* configUSE_NEWLIB_REENTRANT */

        xNextTaskUnblockTime = portMAX_DELAY;
        xSchedulerRunning = pdTRUE;
        xTickCount = ( TickType_t ) configINITIAL_TICK_COUNT;

        /* If configGENERATE_RUN_TIME_STATS is defined then the following
         * macro must be defined to configure the timer/counter used to generate
         * the run time counter time base.   NOTE:  If configGENERATE_RUN_TIME_STATS
         * is set to 0 and the following line fails to build then ensure you do not
         * have portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() defined in your
         * FreeRTOSConfig.h file. */
        portCONFIGURE_TIMER_FOR_RUN_TIME_STATS();

        traceTASK_SWITCHED_IN();

        /* Setting up the timer tick is hardware specific and thus in the
         * portable interface. */
        if( xPortStartScheduler() != pdFALSE )
        {
            /* Should not reach here as if the scheduler is running the
             * function will not return. */
        }
        else
        {
            /* Should only reach here if a task calls xTaskEndScheduler(). */
        }
    }
    else
    {
        /* This line will only be reached if the kernel could not be started,
         * because there was not enough FreeRTOS heap to create the idle task
         * or the timer task. */
        configASSERT( xReturn != errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY );
    }

    /* Prevent compiler warnings if INCLUDE_xTaskGetIdleTaskHandle is set to 0,
     * meaning xIdleTaskHandle is not used anywhere else. */
    ( void ) xIdleTaskHandle;

    /* OpenOCD makes use of uxTopUsedPriority for thread debugging. Prevent uxTopUsedPriority
     * from getting optimized out as it is no longer used by the kernel. */
    ( void ) uxTopUsedPriority;
}

/*
	vPortStartScheduler():该函数用于完成启动任务调度器中与硬件架构相关的配置部分，以及启动第一个任务
	vPortStartScheduler内部实现步骤：
		1.检测用户在FreeRTOSConfig.h文件中对中断的相关配置是否有误
		2.配置PendSV和SysTick的中断优先级为最低优先级
		3.调用函数vPortSetupTimerInterrupt()配置SysTick
		4.初始化临界去嵌套计数器为0
		5.调用函数prvEnableVFP()使能FPU
		6.调用函数prvStartFirstTask()启动第一个任务
		
*/
BaseType_t xPortStartScheduler( void )
{
    #if ( configASSERT_DEFINED == 1 )
        {
            volatile uint32_t ulOriginalPriority;
            volatile uint8_t * const pucFirstUserPriorityRegister = ( uint8_t * ) ( portNVIC_IP_REGISTERS_OFFSET_16 + portFIRST_USER_INTERRUPT_NUMBER );
            volatile uint8_t ucMaxPriorityValue;

            /* Determine the maximum priority from which ISR safe FreeRTOS API
             * functions can be called.  ISR safe functions are those that end in
             * "FromISR".  FreeRTOS maintains separate thread and ISR API functions to
             * ensure interrupt entry is as fast and simple as possible.
             *
             * Save the interrupt priority value that is about to be clobbered. */
            ulOriginalPriority = *pucFirstUserPriorityRegister;

            /* Determine the number of priority bits available.  First write to all
             * possible bits. */
            *pucFirstUserPriorityRegister = portMAX_8_BIT_VALUE;

            /* Read the value back to see how many bits stuck. */
            ucMaxPriorityValue = *pucFirstUserPriorityRegister;

            /* The kernel interrupt priority should be set to the lowest
             * priority. */
            configASSERT( ucMaxPriorityValue == ( configKERNEL_INTERRUPT_PRIORITY & ucMaxPriorityValue ) );

            /* Use the same mask on the maximum system call priority. */
            ucMaxSysCallPriority = configMAX_SYSCALL_INTERRUPT_PRIORITY & ucMaxPriorityValue;

            /* Calculate the maximum acceptable priority group value for the number
             * of bits read back. */
            ulMaxPRIGROUPValue = portMAX_PRIGROUP_BITS;

            while( ( ucMaxPriorityValue & portTOP_BIT_OF_BYTE ) == portTOP_BIT_OF_BYTE )
            {
                ulMaxPRIGROUPValue--;
                ucMaxPriorityValue <<= ( uint8_t ) 0x01;
            }

            #ifdef __NVIC_PRIO_BITS
                {
                    /* Check the CMSIS configuration that defines the number of
                     * priority bits matches the number of priority bits actually queried
                     * from the hardware. */
                    configASSERT( ( portMAX_PRIGROUP_BITS - ulMaxPRIGROUPValue ) == __NVIC_PRIO_BITS );
                }
            #endif

            #ifdef configPRIO_BITS
                {
                    /* Check the FreeRTOS configuration that defines the number of
                     * priority bits matches the number of priority bits actually queried
                     * from the hardware. */
                    configASSERT( ( portMAX_PRIGROUP_BITS - ulMaxPRIGROUPValue ) == configPRIO_BITS );
                }
            #endif

            /* Shift the priority group value back to its position within the AIRCR
             * register. */
            ulMaxPRIGROUPValue <<= portPRIGROUP_SHIFT;
            ulMaxPRIGROUPValue &= portPRIORITY_GROUP_MASK;

            /* Restore the clobbered interrupt priority register to its original
             * value. */
            *pucFirstUserPriorityRegister = ulOriginalPriority;
        }
    #endif /* configASSERT_DEFINED */

    /* Make PendSV and SysTick the lowest priority interrupts. */
    portNVIC_SHPR3_REG |= portNVIC_PENDSV_PRI;

    portNVIC_SHPR3_REG |= portNVIC_SYSTICK_PRI;

    /* Start the timer that generates the tick ISR.  Interrupts are disabled
     * here already. */
    vPortSetupTimerInterrupt();

    /* Initialise the critical nesting count ready for the first task. */
    uxCriticalNesting = 0;

    /* Start the first task. */
    prvStartFirstTask();

    /* Should not get here! */
    return 0;
}
