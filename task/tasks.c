/* 
	对FreeRTOS中动态任务创建中的xTaskCreate()和vTaskDelete()内部实现进行详解
*/	

/*
	xTaskCreate内部实现步骤：
		1.申请堆栈内存
		2.申请任务控制快内存
		3.把前面申请的堆栈地址，赋值给控制块的堆栈成员
		4.调用prvInitialiseNewTask初始化任务块的堆栈成员
			4.1记录栈顶，保存在pxTopofStack
			4.2保存任务名字到pxNewTCB->pcTaskName[ x ]中
			4.3保存任务优先级到pxNewTCB->uxPriority
			4.5设置状态列表项的所属控制块，设置事件列表项的值
			4.6列表项的插入是从小到大插入，所以这里将越高优先级的任务他的事件列表项值设置越小，这样就可以拍到前面
			4.7调用pxPortInitialiseStack初始化任务堆栈，用于保存当前任务上下文寄存器信息，已备后续任务切换使用
			4.8将任务句柄等于任务控制块
		5.调用prvAddNewTaskReadyList添加新创建任务到就绪列表中
			5.1记录任务数量uxCurrentNumberOfTasks++
			5.2判断新创建的任务是否为第一个任务 -5.2.1 如果创建的是第一个任务，初始化任务列表prvInitialiseTaskLists
			                                 -5.2.2 如果创建的不是第一个任务，并且调度器还未开始启动，比较新任务与正在执行的任务优先级大小，
											 新任务优先级大的话，将当前控制块重新指向新的控制块
		    5.3将新的任务控制块添加到就绪列表中，使用函数prvAddTaskToReadyList
			5.4如果调度器已经开始运行，并且新任务的优先级更大的话，进行一次任务切换
*/

    BaseType_t xTaskCreate( TaskFunction_t pxTaskCode,                   /* 任务函数 */
                            const char * const pcName,					 /* 任务名字 */
                            const configSTACK_DEPTH_TYPE usStackDepth,   /* 任务堆栈空间大小 */
                            void * const pvParameters,                   /* 传给任务函数的参数 */
                            UBaseType_t uxPriority,                      /* 任务优先级 */
                            TaskHandle_t * const pxCreatedTask )         /* 任务句柄 */
    {
        TCB_t * pxNewTCB;
        BaseType_t xReturn;

        /* If the stack grows down then allocate the stack then the TCB so the stack
         * does not grow into the TCB.  Likewise if the stack grows up then allocate
         * the TCB then the stack. */
        #if ( portSTACK_GROWTH > 0 )
            {
                /* Allocate space for the TCB.  Where the memory comes from depends on
                 * the implementation of the port malloc function and whether or not static
                 * allocation is being used. */
			   
                pxNewTCB = ( TCB_t * ) pvPortMalloc( sizeof( TCB_t ) );

                if( pxNewTCB != NULL )
                {
                    /* Allocate space for the stack used by the task being created.
                     * The base of the stack memory stored in the TCB so the task can
                     * be deleted later if required. */
                    pxNewTCB->pxStack = ( StackType_t * ) pvPortMallocStack( ( ( ( size_t ) usStackDepth ) * sizeof( StackType_t ) ) ); /*lint !e961 MISRA exception as the casts are only redundant for some ports. */

                    if( pxNewTCB->pxStack == NULL )
                    {
                        /* Could not allocate the stack.  Delete the allocated TCB. */
                        vPortFree( pxNewTCB );
                        pxNewTCB = NULL;
                    }
                }
            }
        #else /* portSTACK_GROWTH */
            {
                StackType_t * pxStack;

                /* Allocate space for the stack used by the task being created. */
                pxStack = pvPortMallocStack( ( ( ( size_t ) usStackDepth ) * sizeof( StackType_t ) ) ); /*lint !e9079 All values returned by pvPortMalloc() have at least the alignment required by the MCU's stack and this allocation is the stack. */

                if( pxStack != NULL )
                {
                    /* Allocate space for the TCB. */
                    pxNewTCB = ( TCB_t * ) pvPortMalloc( sizeof( TCB_t ) ); /*lint !e9087 !e9079 All values returned by pvPortMalloc() have at least the alignment required by the MCU's stack, and the first member of TCB_t is always a pointer to the task's stack. */

                    if( pxNewTCB != NULL )
                    {
                        /* Store the stack location in the TCB. */
                        pxNewTCB->pxStack = pxStack;
                    }
                    else
                    {
                        /* The stack cannot be used as the TCB was not created.  Free
                         * it again. */
                        vPortFreeStack( pxStack );
                    }
                }
                else
                {
                    pxNewTCB = NULL;
                }
            }
        #endif /* portSTACK_GROWTH */

        if( pxNewTCB != NULL )
        {
            #if ( tskSTATIC_AND_DYNAMIC_ALLOCATION_POSSIBLE != 0 ) /*lint !e9029 !e731 Macro has been consolidated for readability reasons. */
                {
                    /* Tasks can be created statically or dynamically, so note this
                     * task was created dynamically in case it is later deleted. */
                    pxNewTCB->ucStaticallyAllocated = tskDYNAMICALLY_ALLOCATED_STACK_AND_TCB;
                }
            #endif /* tskSTATIC_AND_DYNAMIC_ALLOCATION_POSSIBLE */

            prvInitialiseNewTask( pxTaskCode, pcName, ( uint32_t ) usStackDepth, pvParameters, uxPriority, pxCreatedTask, pxNewTCB, NULL );
            prvAddNewTaskToReadyList( pxNewTCB );
            xReturn = pdPASS;
        }
        else
        {
            xReturn = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
        }

        return xReturn;
    }

/*
	vTaskDelete()内部实现步骤：
		1.获取所要删除任务的控制块
		2.将被删除任务，移除所在列表
		3.判断所需要删除的任务
			3.1删除任务自身，需先添加到等待删除列表，内存释放将在空闲任务执行
			3.2删除其他任务，当前任务数量--
		4.删除的任务为其他任务则直接释放内存prvDeleteTCB( )
		5.调度器正在运行且删除任务自身，则需要进行一次任务切换

*/
 void vTaskDelete( TaskHandle_t xTaskToDelete )
    {
        TCB_t * pxTCB;

        taskENTER_CRITICAL();
        {
            /* If null is passed in here then it is the calling task that is
             * being deleted. */
            pxTCB = prvGetTCBFromHandle( xTaskToDelete );

            /* Remove task from the ready/delayed list. */
            if( uxListRemove( &( pxTCB->xStateListItem ) ) == ( UBaseType_t ) 0 )
            {
                taskRESET_READY_PRIORITY( pxTCB->uxPriority );
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }

            /* Is the task waiting on an event also? */
            if( listLIST_ITEM_CONTAINER( &( pxTCB->xEventListItem ) ) != NULL )
            {
                ( void ) uxListRemove( &( pxTCB->xEventListItem ) );
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }

            /* Increment the uxTaskNumber also so kernel aware debuggers can
             * detect that the task lists need re-generating.  This is done before
             * portPRE_TASK_DELETE_HOOK() as in the Windows port that macro will
             * not return. */
            uxTaskNumber++;

            if( pxTCB == pxCurrentTCB )
            {
                /* A task is deleting itself.  This cannot complete within the
                 * task itself, as a context switch to another task is required.
                 * Place the task in the termination list.  The idle task will
                 * check the termination list and free up any memory allocated by
                 * the scheduler for the TCB and stack of the deleted task. */
                vListInsertEnd( &xTasksWaitingTermination, &( pxTCB->xStateListItem ) );

                /* Increment the ucTasksDeleted variable so the idle task knows
                 * there is a task that has been deleted and that it should therefore
                 * check the xTasksWaitingTermination list. */
                ++uxDeletedTasksWaitingCleanUp;

                /* Call the delete hook before portPRE_TASK_DELETE_HOOK() as
                 * portPRE_TASK_DELETE_HOOK() does not return in the Win32 port. */
                traceTASK_DELETE( pxTCB );

                /* The pre-delete hook is primarily for the Windows simulator,
                 * in which Windows specific clean up operations are performed,
                 * after which it is not possible to yield away from this task -
                 * hence xYieldPending is used to latch that a context switch is
                 * required. */
                portPRE_TASK_DELETE_HOOK( pxTCB, &xYieldPending );
            }
            else
            {
                --uxCurrentNumberOfTasks;
                traceTASK_DELETE( pxTCB );

                /* Reset the next expected unblock time in case it referred to
                 * the task that has just been deleted. */
                prvResetNextTaskUnblockTime();
            }
        }
        taskEXIT_CRITICAL();

        /* If the task is not deleting itself, call prvDeleteTCB from outside of
         * critical section. If a task deletes itself, prvDeleteTCB is called
         * from prvCheckTasksWaitingTermination which is called from Idle task. */
        if( pxTCB != pxCurrentTCB )
        {
            prvDeleteTCB( pxTCB );
        }

        /* Force a reschedule if it is the currently running task that has just
         * been deleted. */
        if( xSchedulerRunning != pdFALSE )
        {
            if( pxTCB == pxCurrentTCB )
            {
                configASSERT( uxSchedulerSuspended == 0 );
                portYIELD_WITHIN_API();
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }
        }
