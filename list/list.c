/* 
	对FreeRTOS中动态任务创建中的xTaskCreate()、vTaskDelete()、vTaskSuspend()、vTaskResume()内部实现进行详解。
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
