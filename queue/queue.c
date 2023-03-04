/* 
	FreeRTOS队列相关API内部分析:	
              (1)创建队列：vListInitialise()
              (2)往队列写入数据：vListInitialiseItem()
              (3)从队列读取数据：vListInsert()
/*

/*
	xQueueCreate()内部实现步骤：
		1.计算队列需要多大内存xQueueSizeInBytes = (size_t)(uxQueueLength * uxltemSize)
		2.为队列申请内存，申请大小：sizeof( Queue_t ) + xQueueSizeInBytes,
		3.判断内存是否申请成功，成功即计算出队列项存储器的首地址
		4.调用prvInitialiseNewQueue() 初始化新队列pxNewQueue
*/
 #define xQueueCreate( uxQueueLength, uxItemSize )    xQueueGenericCreate( ( uxQueueLength ), ( uxItemSize ), ( queueQUEUE_TYPE_BASE ) )

QueueHandle_t xQueueGenericCreate( const UBaseType_t uxQueueLength,
								   const UBaseType_t uxItemSize,
								   const uint8_t ucQueueType )
{
	Queue_t * pxNewQueue = NULL;
	size_t xQueueSizeInBytes;
	uint8_t * pucQueueStorage;

	if( ( uxQueueLength > ( UBaseType_t ) 0 ) &&
		/* Check for multiplication overflow. */
		( ( SIZE_MAX / uxQueueLength ) >= uxItemSize ) &&
		/* Check for addition overflow. */
		( ( SIZE_MAX - sizeof( Queue_t ) ) >= ( uxQueueLength * uxItemSize ) ) )
	{
		/* Allocate enough space to hold the maximum number of items that
		 * can be in the queue at any time.  It is valid for uxItemSize to be
		 * zero in the case the queue is used as a semaphore. */
		xQueueSizeInBytes = ( size_t ) ( uxQueueLength * uxItemSize ); /*lint !e961 MISRA exception as the casts are only redundant for some ports. */

		/* Allocate the queue and storage area.  Justification for MISRA
		 * deviation as follows:  pvPortMalloc() always ensures returned memory
		 * blocks are aligned per the requirements of the MCU stack.  In this case
		 * pvPortMalloc() must return a pointer that is guaranteed to meet the
		 * alignment requirements of the Queue_t structure - which in this case
		 * is an int8_t *.  Therefore, whenever the stack alignment requirements
		 * are greater than or equal to the pointer to char requirements the cast
		 * is safe.  In other cases alignment requirements are not strict (one or
		 * two bytes). */
		pxNewQueue = ( Queue_t * ) pvPortMalloc( sizeof( Queue_t ) + xQueueSizeInBytes ); /*lint !e9087 !e9079 see comment above. */

		if( pxNewQueue != NULL )
		{
			/* Jump past the queue structure to find the location of the queue
			 * storage area. */
			pucQueueStorage = ( uint8_t * ) pxNewQueue;
			pucQueueStorage += sizeof( Queue_t ); /*lint !e9016 Pointer arithmetic allowed on char types, especially when it assists conveying intent. */

			#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
				{
					/* Queues can be created either statically or dynamically, so
					 * note this task was created dynamically in case it is later
					 * deleted. */
					pxNewQueue->ucStaticallyAllocated = pdFALSE;
				}
			#endif /* configSUPPORT_STATIC_ALLOCATION */

			prvInitialiseNewQueue( uxQueueLength, uxItemSize, pucQueueStorage, ucQueueType, pxNewQueue );
		}
		else
		{
			traceQUEUE_CREATE_FAILED( ucQueueType );
			mtCOVERAGE_TEST_MARKER();
		}
	}
	else
	{
		configASSERT( pxNewQueue );
		mtCOVERAGE_TEST_MARKER();
	}

	return pxNewQueue;
}

/*
	xQueueSend()内部实现步骤：
		1.进入临界区（关中断）
		2.判断队列是否已满
		3.队列有空闲位置
			3.1当有空闲位置或覆写时：将待写入消息按指定写入方式复制到队列
			3.2判断是否有因为读不到消息而阻塞的任务，有的话，将接触足额太。
			3.3退出临界区
		4.队列已满
			4.1此时不能写入信息，因此要将任务阻塞
			4.2如果阻塞时间为0，代表不阻塞，直接返回队列错误
			4.3如果阻塞时间不为0，任务需要阻塞，记录下此时系统节拍计数器的值和溢出次数，用于下面对阻塞时间进行补偿
			4.4判断阻塞时间补偿后，是否还需要阻塞
				4.41如果需要首先将任务的事件队列添加到等待发送列表中，将任务状态列表添加到阻塞列表中进行阻塞，队列解锁，恢复调度器
				4.42如果不需要的话，那就将队列解锁，恢复调度器返回队列满错误
*/
BaseType_t xQueueGenericSend( QueueHandle_t xQueue,
                              const void * const pvItemToQueue,
                              TickType_t xTicksToWait,
                              const BaseType_t xCopyPosition )
{
    BaseType_t xEntryTimeSet = pdFALSE, xYieldRequired;
    TimeOut_t xTimeOut;
    Queue_t * const pxQueue = xQueue;

    configASSERT( pxQueue );
    configASSERT( !( ( pvItemToQueue == NULL ) && ( pxQueue->uxItemSize != ( UBaseType_t ) 0U ) ) );
    configASSERT( !( ( xCopyPosition == queueOVERWRITE ) && ( pxQueue->uxLength != 1 ) ) );
    #if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
        {
            configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );
        }
    #endif

    /*lint -save -e904 This function relaxes the coding standard somewhat to
     * allow return statements within the function itself.  This is done in the
     * interest of execution time efficiency. */
    for( ; ; )
    {
        taskENTER_CRITICAL();
        {
            /* Is there room on the queue now?  The running task must be the
             * highest priority task wanting to access the queue.  If the head item
             * in the queue is to be overwritten then it does not matter if the
             * queue is full. */
            if( ( pxQueue->uxMessagesWaiting < pxQueue->uxLength ) || ( xCopyPosition == queueOVERWRITE ) )
            {
                traceQUEUE_SEND( pxQueue );

                #if ( configUSE_QUEUE_SETS == 1 )
                    {
                        const UBaseType_t uxPreviousMessagesWaiting = pxQueue->uxMessagesWaiting;

                        xYieldRequired = prvCopyDataToQueue( pxQueue, pvItemToQueue, xCopyPosition );

                        if( pxQueue->pxQueueSetContainer != NULL )
                        {
                            if( ( xCopyPosition == queueOVERWRITE ) && ( uxPreviousMessagesWaiting != ( UBaseType_t ) 0 ) )
                            {
                                /* Do not notify the queue set as an existing item
                                 * was overwritten in the queue so the number of items
                                 * in the queue has not changed. */
                                mtCOVERAGE_TEST_MARKER();
                            }
                            else if( prvNotifyQueueSetContainer( pxQueue ) != pdFALSE )
                            {
                                /* The queue is a member of a queue set, and posting
                                 * to the queue set caused a higher priority task to
                                 * unblock. A context switch is required. */
                                queueYIELD_IF_USING_PREEMPTION();
                            }
                            else
                            {
                                mtCOVERAGE_TEST_MARKER();
                            }
                        }
                        else
                        {
                            /* If there was a task waiting for data to arrive on the
                             * queue then unblock it now. */
                            if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE )
                            {
                                if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != pdFALSE )
                                {
                                    /* The unblocked task has a priority higher than
                                     * our own so yield immediately.  Yes it is ok to
                                     * do this from within the critical section - the
                                     * kernel takes care of that. */
                                    queueYIELD_IF_USING_PREEMPTION();
                                }
                                else
                                {
                                    mtCOVERAGE_TEST_MARKER();
                                }
                            }
                            else if( xYieldRequired != pdFALSE )
                            {
                                /* This path is a special case that will only get
                                 * executed if the task was holding multiple mutexes
                                 * and the mutexes were given back in an order that is
                                 * different to that in which they were taken. */
                                queueYIELD_IF_USING_PREEMPTION();
                            }
                            else
                            {
                                mtCOVERAGE_TEST_MARKER();
                            }
                        }
                    }
                #else /* configUSE_QUEUE_SETS */
                    {
                        xYieldRequired = prvCopyDataToQueue( pxQueue, pvItemToQueue, xCopyPosition );

                        /* If there was a task waiting for data to arrive on the
                         * queue then unblock it now. */
                        if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == pdFALSE )
                        {
                            if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != pdFALSE )
                            {
                                /* The unblocked task has a priority higher than
                                 * our own so yield immediately.  Yes it is ok to do
                                 * this from within the critical section - the kernel
                                 * takes care of that. */
                                queueYIELD_IF_USING_PREEMPTION();
                            }
                            else
                            {
                                mtCOVERAGE_TEST_MARKER();
                            }
                        }
                        else if( xYieldRequired != pdFALSE )
                        {
                            /* This path is a special case that will only get
                             * executed if the task was holding multiple mutexes and
                             * the mutexes were given back in an order that is
                             * different to that in which they were taken. */
                            queueYIELD_IF_USING_PREEMPTION();
                        }
                        else
                        {
                            mtCOVERAGE_TEST_MARKER();
                        }
                    }
                #endif /* configUSE_QUEUE_SETS */

                taskEXIT_CRITICAL();
                return pdPASS;
            }
            else
            {
                if( xTicksToWait == ( TickType_t ) 0 )
                {
                    /* The queue was full and no block time is specified (or
                     * the block time has expired) so leave now. */
                    taskEXIT_CRITICAL();

                    /* Return to the original privilege level before exiting
                     * the function. */
                    traceQUEUE_SEND_FAILED( pxQueue );
                    return errQUEUE_FULL;
                }
                else if( xEntryTimeSet == pdFALSE )
                {
                    /* The queue was full and a block time was specified so
                     * configure the timeout structure. */
                    vTaskInternalSetTimeOutState( &xTimeOut );
                    xEntryTimeSet = pdTRUE;
                }
                else
                {
                    /* Entry time was already set. */
                    mtCOVERAGE_TEST_MARKER();
                }
            }
        }
        taskEXIT_CRITICAL();

        /* Interrupts and other tasks can send to and receive from the queue
         * now the critical section has been exited. */

        vTaskSuspendAll();
        prvLockQueue( pxQueue );

        /* Update the timeout state to see if it has expired yet. */
        if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == pdFALSE )
        {
            if( prvIsQueueFull( pxQueue ) != pdFALSE )
            {
                traceBLOCKING_ON_QUEUE_SEND( pxQueue );
                vTaskPlaceOnEventList( &( pxQueue->xTasksWaitingToSend ), xTicksToWait );

                /* Unlocking the queue means queue events can effect the
                 * event list. It is possible that interrupts occurring now
                 * remove this task from the event list again - but as the
                 * scheduler is suspended the task will go onto the pending
                 * ready list instead of the actual ready list. */
                prvUnlockQueue( pxQueue );

                /* Resuming the scheduler will move tasks from the pending
                 * ready list into the ready list - so it is feasible that this
                 * task is already in the ready list before it yields - in which
                 * case the yield will not cause a context switch unless there
                 * is also a higher priority task in the pending ready list. */
                if( xTaskResumeAll() == pdFALSE )
                {
                    portYIELD_WITHIN_API();
                }
            }
            else
            {
                /* Try again. */
                prvUnlockQueue( pxQueue );
                ( void ) xTaskResumeAll();
            }
        }
        else
        {
            /* The timeout has expired. */
            prvUnlockQueue( pxQueue );
            ( void ) xTaskResumeAll();

            traceQUEUE_SEND_FAILED( pxQueue );
            return errQUEUE_FULL;
        }
    } /*lint -restore */
}

/*
	xQueueReceive()内部实现步骤：
		1.进入临界区（关中断）
		2.判断队列是否为空
		3.有数据
			3.1使用函数prvCopyDataFromQueue()拷贝数据
			3.2队列项目个数减一
			3.3因为前面已经减了一个队列，所以队列已经有空位，如果xTasksWaitingToSend等待发送列表中，有任务，则接触阻塞态，
			通过这个函数xTaskRemoveFromEventList()
		4.为空
			4.1此时读不到信息消息，因此要将任务阻塞
			4.2如果阻塞时间为0，代表不阻塞，直接返回队列错误
			4.3如果阻塞时间不为0，任务需要阻塞，记录下此时系统节拍计数器的值和溢出次数，用于下面对阻塞时间进行补偿
			4.4判断阻塞时间补偿后，是否还需要阻塞
				4.41如果需要首先将任务的事件队列添加到等待发送列表中，将任务状态列表添加到阻塞列表中进行阻塞，队列解锁，恢复调度器
				4.42如果不需要的话，那就将队列解锁，恢复调度器返回队列满错误
*/
BaseType_t xQueueReceive( QueueHandle_t xQueue,
                          void * const pvBuffer,
                          TickType_t xTicksToWait )
{
    BaseType_t xEntryTimeSet = pdFALSE;
    TimeOut_t xTimeOut;
    Queue_t * const pxQueue = xQueue;

    /* Check the pointer is not NULL. */
    configASSERT( ( pxQueue ) );

    /* The buffer into which data is received can only be NULL if the data size
     * is zero (so no data is copied into the buffer). */
    configASSERT( !( ( ( pvBuffer ) == NULL ) && ( ( pxQueue )->uxItemSize != ( UBaseType_t ) 0U ) ) );

    /* Cannot block if the scheduler is suspended. */
    #if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
        {
            configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );
        }
    #endif

    /*lint -save -e904  This function relaxes the coding standard somewhat to
     * allow return statements within the function itself.  This is done in the
     * interest of execution time efficiency. */
    for( ; ; )
    {
        taskENTER_CRITICAL();
        {
            const UBaseType_t uxMessagesWaiting = pxQueue->uxMessagesWaiting;

            /* Is there data in the queue now?  To be running the calling task
             * must be the highest priority task wanting to access the queue. */
            if( uxMessagesWaiting > ( UBaseType_t ) 0 )
            {
                /* Data available, remove one item. */
                prvCopyDataFromQueue( pxQueue, pvBuffer );
                traceQUEUE_RECEIVE( pxQueue );
                pxQueue->uxMessagesWaiting = uxMessagesWaiting - ( UBaseType_t ) 1;

                /* There is now space in the queue, were any tasks waiting to
                 * post to the queue?  If so, unblock the highest priority waiting
                 * task. */
                if( listLIST_IS_EMPTY( &( pxQueue->xTasksWaitingToSend ) ) == pdFALSE )
                {
                    if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToSend ) ) != pdFALSE )
                    {
                        queueYIELD_IF_USING_PREEMPTION();
                    }
                    else
                    {
                        mtCOVERAGE_TEST_MARKER();
                    }
                }
                else
                {
                    mtCOVERAGE_TEST_MARKER();
                }

                taskEXIT_CRITICAL();
                return pdPASS;
            }
            else
            {
                if( xTicksToWait == ( TickType_t ) 0 )
                {
                    /* The queue was empty and no block time is specified (or
                     * the block time has expired) so leave now. */
                    taskEXIT_CRITICAL();
                    traceQUEUE_RECEIVE_FAILED( pxQueue );
                    return errQUEUE_EMPTY;
                }
                else if( xEntryTimeSet == pdFALSE )
                {
                    /* The queue was empty and a block time was specified so
                     * configure the timeout structure. */
                    vTaskInternalSetTimeOutState( &xTimeOut );
                    xEntryTimeSet = pdTRUE;
                }
                else
                {
                    /* Entry time was already set. */
                    mtCOVERAGE_TEST_MARKER();
                }
            }
        }
        taskEXIT_CRITICAL();

        /* Interrupts and other tasks can send to and receive from the queue
         * now the critical section has been exited. */

        vTaskSuspendAll();
        prvLockQueue( pxQueue );

        /* Update the timeout state to see if it has expired yet. */
        if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == pdFALSE )
        {
            /* The timeout has not expired.  If the queue is still empty place
             * the task on the list of tasks waiting to receive from the queue. */
            if( prvIsQueueEmpty( pxQueue ) != pdFALSE )
            {
                traceBLOCKING_ON_QUEUE_RECEIVE( pxQueue );
                vTaskPlaceOnEventList( &( pxQueue->xTasksWaitingToReceive ), xTicksToWait );
                prvUnlockQueue( pxQueue );

                if( xTaskResumeAll() == pdFALSE )
                {
                    portYIELD_WITHIN_API();
                }
                else
                {
                    mtCOVERAGE_TEST_MARKER();
                }
            }
            else
            {
                /* The queue contains data again.  Loop back to try and read the
                 * data. */
                prvUnlockQueue( pxQueue );
                ( void ) xTaskResumeAll();
            }
        }
        else
        {
            /* Timed out.  If there is no data in the queue exit, otherwise loop
             * back and attempt to read the data. */
            prvUnlockQueue( pxQueue );
            ( void ) xTaskResumeAll();

            if( prvIsQueueEmpty( pxQueue ) != pdFALSE )
            {
                traceQUEUE_RECEIVE_FAILED( pxQueue );
                return errQUEUE_EMPTY;
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }
        }
    } /*lint -restore */
}
