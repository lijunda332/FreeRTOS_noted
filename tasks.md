    //FreeRTOS创建动态任务创建使用demo伪代码
	//0.初始化参数配置
	#define TASK1_PRIO      1                   /* 任务优先级*/
	#define TASK1_STK_SIZE  128                 /* 任务堆栈大小 */
	TaskHandle_t            Task1Task_Handler;  /* 任务句柄 */
	void task1(void *pvParameters);             /* 任务函数 */
	
    //1.进入临界区，防止创建任务过程被中断打断
    taskENTER_CRITICAL(); //之后进行详解      
    
    //2.利用xTaskCreate创建动态任务
    xTaskCreate((TaskFunction_t )task1,               /* 任务函数 */
                (const char*    )"task1",             /* 任务名称 */
                (uint16_t       )TASK1_STK_SIZE,      /* 任务堆栈大小 */
                (void*          )NULL,                /* 传入给任务函数的参数 */
                (UBaseType_t    )TASK1_PRIO,          /* 任务优先级 */
                (TaskHandle_t*  )&Task1Task_Handler); /* 任务句柄 */
				
	// 3.进入任务调度
	vTaskStartScheduler(); //之后进行详解
	
	//4.删除任务
    vTaskDelete(Task1Task_Handler); 
	
	//5.退出临界区
	taskEXIT_CRITICAL();  //之后进行详解         
	
	

