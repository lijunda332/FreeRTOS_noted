/* 
	FreeRTOS列表相关API内部分析:	
              (1)列表初始化函数：vListInitialise()
              (2)列表项初始化函数：vListInitialiseItem()
              (3)列表项插入函数：vListInsert()
              (4)末尾列表插入函数：vListInsertEnd()
              (5)列表项移除函数：uxListRemove(）

/*
//列表结构体声明
typedef struct xLIST
{
    listFIRST_LIST_INTEGRITY_CHECK_VALUE      /*< Set to a known value if configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES is set to 1. */
    volatile UBaseType_t uxNumberOfItems;
    ListItem_t * configLIST_VOLATILE pxIndex; /*< Used to walk through the list.  Points to the last item returned by a call to listGET_OWNER_OF_NEXT_ENTRY (). */
    MiniListItem_t xListEnd;                  /*< List item that contains the maximum possible item value meaning it is always at the end of the list and is therefore used as a marker. */
    listSECOND_LIST_INTEGRITY_CHECK_VALUE     /*< Set to a known value if configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES is set to 1. */
} List_t;
//列表项结构体声明
struct xLIST_ITEM
{
    listFIRST_LIST_ITEM_INTEGRITY_CHECK_VALUE           /*< Set to a known value if configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES is set to 1. */
    configLIST_VOLATILE TickType_t xItemValue;          /*< The value being listed.  In most cases this is used to sort the list in ascending order. */
    struct xLIST_ITEM * configLIST_VOLATILE pxNext;     /*< Pointer to the next ListItem_t in the list. */
    struct xLIST_ITEM * configLIST_VOLATILE pxPrevious; /*< Pointer to the previous ListItem_t in the list. */
    void * pvOwner;                                     /*< Pointer to the object (normally a TCB) that contains the list item.  There is therefore a two way link between the object containing the list item and the list item itself. */
    struct xLIST * configLIST_VOLATILE pxContainer;     /*< Pointer to the list in which this list item is placed (if any). */
    listSECOND_LIST_ITEM_INTEGRITY_CHECK_VALUE          /*< Set to a known value if configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES is set to 1. */
};
typedef struct xLIST_ITEM ListItem_t;                   /* For some reason lint wants this as two separate definitions. */


/*
	vListInitialise()内部实现步骤：
		1.初始化时，列表中只有 xListEnd，因此 pxIndex 指向 xListEnd
		2.xListEnd 的值初始化为最大值，用于列表项升序排序时，排在最后
		3.初始化时，列表中只有 xListEnd，因此上一个和下一个列表项都为 xListEnd 本身
		4.初始化时，列表中的列表项数量为 0（不包含 xListEnd）
*/
void vListInitialise( List_t * const pxList )
{
    /* The list structure contains a list item which is used to mark the
     * end of the list.  To initialise the list the list end is inserted
     * as the only list entry. */
    pxList->pxIndex = ( ListItem_t * ) &( pxList->xListEnd ); /*lint !e826 !e740 !e9087 The mini list structure is used as the list end to save RAM.  This is checked and valid. */

    /* The list end value is the highest possible value in the list to
     * ensure it remains at the end of the list. */
    pxList->xListEnd.xItemValue = portMAX_DELAY;

    /* The list end next and previous pointers point to itself so we know
     * when the list is empty. */
    pxList->xListEnd.pxNext = ( ListItem_t * ) &( pxList->xListEnd );     /*lint !e826 !e740 !e9087 The mini list structure is used as the list end to save RAM.  This is checked and valid. */
    pxList->xListEnd.pxPrevious = ( ListItem_t * ) &( pxList->xListEnd ); /*lint !e826 !e740 !e9087 The mini list structure is used as the list end to save RAM.  This is checked and valid. */

    pxList->uxNumberOfItems = ( UBaseType_t ) 0U;

    /* Write known values into the list if
     * configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES is set to 1. */
    listSET_LIST_INTEGRITY_CHECK_1_VALUE( pxList );
    listSET_LIST_INTEGRITY_CHECK_2_VALUE( pxList );
}

/*
	vListInitialiseItem()内部实现步骤：
		初始化时，列表项不属于任何一个列表，所以为空。
*/
void vListInitialiseItem( ListItem_t * const pxItem )
{
    /* Make sure the list item is not recorded as being on a list. */
    pxItem->pxContainer = NULL;

    /* Write known values into the list item if
     * configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES is set to 1. */
    listSET_FIRST_LIST_ITEM_INTEGRITY_CHECK_VALUE( pxItem );
    listSET_SECOND_LIST_ITEM_INTEGRITY_CHECK_VALUE( pxItem );
}

/*
	vListInsert()内部实现步骤：
		1.获取新插入的列表项的值
		2.判断新插入的列表项数值大小
			2.1 如果数值等于末尾列表项的数值。就插入到末尾列表项前面
			2.2 否则遍历列表中的列表项，找到插入的位置
		3.将列表项插入前面所找到的位置
		4.更新待插入列表项所在列表
		5.更新列表中列表项的数量
*/
void vListInsert( List_t * const pxList,
                  ListItem_t * const pxNewListItem )
{
    ListItem_t * pxIterator;
    const TickType_t xValueOfInsertion = pxNewListItem->xItemValue;

    /* Only effective when configASSERT() is also defined, these tests may catch
     * the list data structures being overwritten in memory.  They will not catch
     * data errors caused by incorrect configuration or use of FreeRTOS. */
    listTEST_LIST_INTEGRITY( pxList );
    listTEST_LIST_ITEM_INTEGRITY( pxNewListItem );

    /* Insert the new list item into the list, sorted in xItemValue order.
     *
     * If the list already contains a list item with the same item value then the
     * new list item should be placed after it.  This ensures that TCBs which are
     * stored in ready lists (all of which have the same xItemValue value) get a
     * share of the CPU.  However, if the xItemValue is the same as the back marker
     * the iteration loop below will not end.  Therefore the value is checked
     * first, and the algorithm slightly modified if necessary. */
    if( xValueOfInsertion == portMAX_DELAY )
    {
        pxIterator = pxList->xListEnd.pxPrevious;
    }
    else
    {
        /* *** NOTE ***********************************************************
        *  If you find your application is crashing here then likely causes are
        *  listed below.  In addition see https://www.FreeRTOS.org/FAQHelp.html for
        *  more tips, and ensure configASSERT() is defined!
        *  https://www.FreeRTOS.org/a00110.html#configASSERT
        *
        *   1) Stack overflow -
        *      see https://www.FreeRTOS.org/Stacks-and-stack-overflow-checking.html
        *   2) Incorrect interrupt priority assignment, especially on Cortex-M
        *      parts where numerically high priority values denote low actual
        *      interrupt priorities, which can seem counter intuitive.  See
        *      https://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html and the definition
        *      of configMAX_SYSCALL_INTERRUPT_PRIORITY on
        *      https://www.FreeRTOS.org/a00110.html
        *   3) Calling an API function from within a critical section or when
        *      the scheduler is suspended, or calling an API function that does
        *      not end in "FromISR" from an interrupt.
        *   4) Using a queue or semaphore before it has been initialised or
        *      before the scheduler has been started (are interrupts firing
        *      before vTaskStartScheduler() has been called?).
        *   5) If the FreeRTOS port supports interrupt nesting then ensure that
        *      the priority of the tick interrupt is at or below
        *      configMAX_SYSCALL_INTERRUPT_PRIORITY.
        **********************************************************************/

        for( pxIterator = ( ListItem_t * ) &( pxList->xListEnd ); pxIterator->pxNext->xItemValue <= xValueOfInsertion; pxIterator = pxIterator->pxNext ) /*lint !e826 !e740 !e9087 The mini list structure is used as the list end to save RAM.  This is checked and valid. *//*lint !e440 The iterator moves to a different value, not xValueOfInsertion. */
        {
            /* There is nothing to do here, just iterating to the wanted
             * insertion position. */
        }
    }

    pxNewListItem->pxNext = pxIterator->pxNext;
    pxNewListItem->pxNext->pxPrevious = pxNewListItem;
    pxNewListItem->pxPrevious = pxIterator;
    pxIterator->pxNext = pxNewListItem;

    /* Remember which list the item is in.  This allows fast removal of the
     * item later. */
    pxNewListItem->pxContainer = pxList;

    ( pxList->uxNumberOfItems )++;
}

/*
	uxListRemove()内部实现步骤：
		1.获取所要移除的列表项的所在列表
		2.从列表中移除列表项
		3.如果 pxIndex 正指向待移除的列表项，将其指向带移除的上一个列表项
		4.将待移除列表项的所在列表指针清空
		5.列表的列表项数目减一
		6.返回列表项移除后列表中列表项的数量
*/
UBaseType_t uxListRemove( ListItem_t * const pxItemToRemove )
{
/* The list item knows which list it is in.  Obtain the list from the list
 * item. */
    List_t * const pxList = pxItemToRemove->pxContainer;

    pxItemToRemove->pxNext->pxPrevious = pxItemToRemove->pxPrevious;
    pxItemToRemove->pxPrevious->pxNext = pxItemToRemove->pxNext;

    /* Only used during decision coverage testing. */
    mtCOVERAGE_TEST_DELAY();

    /* Make sure the index is left pointing to a valid item. */
    if( pxList->pxIndex == pxItemToRemove )
    {
        pxList->pxIndex = pxItemToRemove->pxPrevious;
    }
    else
    {
        mtCOVERAGE_TEST_MARKER();
    }

    pxItemToRemove->pxContainer = NULL;
    ( pxList->uxNumberOfItems )--;

    return pxList->uxNumberOfItems;
}
