//FreeRTOS列表相关API:
//                 (1)列表初始化函数：vListInitialise()
//                 (2)列表项初始化函数：vListInitialiseItem()
//                 (3)列表项插入函数：vListInsert()
//                 (4)末尾列表插入函数：vListInsertEnd()
//                 (5)列表项移除函数：uxListRemove()

//简单使用demo演示
//0.定义
List_t                  TestList;           /* 定义测试列表 */
ListItem_t              ListItem1;          /* 定义测试列表项1 */
ListItem_t              ListItem2;          /* 定义测试列表项2 */
ListItem_t              ListItem3;          /* 定义测试列表项3 */

//1.初始化
vListInitialise(&TestList);                 /* 初始化列表*/
vListInitialiseItem(&ListItem1);            /* 初始化列表项1 */
vListInitialiseItem(&ListItem2);            /* 初始化列表项2 */
vListInitialiseItem(&ListItem3);            /* 初始化列表项3 */

//2.打印列表信息进行测试
printf("项目\t\t\t地址\r\n");
printf("TestList\t\t0x%p\t\r\n", &TestList);
printf("TestList->pxIndex\t0x%p\t\r\n", TestList.pxIndex);
printf("TestList->xListEnd\t0x%p\t\r\n", (&TestList.xListEnd));
printf("ListItem1\t\t0x%p\t\r\n", &ListItem1);
printf("ListItem2\t\t0x%p\t\r\n", &ListItem2);
printf("ListItem3\t\t0x%p\t\r\n", &ListItem3);

//3.将列表项1插入列表
vListInsert((List_t*    )&TestList,         /* 列表 */
            (ListItem_t*)&ListItem1);       /* 列表项 */

//3.将列表项2插入列表
vListInsert((List_t*    )&TestList,         /* 列表 */
            (ListItem_t*)&ListItem2);       /* 列表项 */

//3.将列表项3插入列表
vListInsert((List_t*    )&TestList,         /* 列表 */
            (ListItem_t*)&ListItem3);       /* 列表项 */
            
//4.移除列表项2
uxListRemove((ListItem_t*   )&ListItem2);   /* 移除列表项 */
printf("项目\t\t\t\t地址\r\n");
printf("TestList->xListEnd->pxNext\t0x%p\r\n", (TestList.xListEnd.pxNext));
printf("ListItem1->pxNext\t\t0x%p\r\n", (ListItem1.pxNext));
printf("ListItem3->pxNext\t\t0x%p\r\n", (ListItem3.pxNext));
printf("TestList->xListEnd->pxPrevious\t0x%p\r\n", (TestList.xListEnd.pxPrevious));
printf("ListItem1->pxPrevious\t\t0x%p\r\n", (ListItem1.pxPrevious));
printf("ListItem3->pxPrevious\t\t0x%p\r\n", (ListItem3.pxPrevious));

//5.列表末尾添加列表项2
vListInsertEnd((List_t*     )&TestList,     /* 列表 */
               (ListItem_t* )&ListItem2);   /* 列表项 */
printf("项目\t\t\t\t地址\r\n");
printf("TestList->pxIndex\t\t0x%p\r\n", TestList.pxIndex);
printf("TestList->xListEnd->pxNext\t0x%p\r\n", (TestList.xListEnd.pxNext));
printf("ListItem1->pxNext\t\t0x%p\r\n", (ListItem1.pxNext));
printf("ListItem2->pxNext\t\t0x%p\r\n", (ListItem2.pxNext));
printf("ListItem3->pxNext\t\t0x%p\r\n", (ListItem3.pxNext));
printf("TestList->xListEnd->pxPrevious\t0x%p\r\n", (TestList.xListEnd.pxPrevious));
printf("ListItem1->pxPrevious\t\t0x%p\r\n", (ListItem1.pxPrevious));
printf("ListItem2->pxPrevious\t\t0x%p\r\n", (ListItem2.pxPrevious));
printf("ListItem3->pxPrevious\t\t0x%p\r\n", (ListItem3.pxPrevious));
