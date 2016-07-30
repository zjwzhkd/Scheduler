/*******************************************************************************
* 文 件 名: sched_fsm.c
* 创 建 者: Keda Huang
* 版    本: V1.0
* 创建日期: 2016-07-29
* 文件说明: 实现事件驱动调度器的核心框架 - 调度核心
*******************************************************************************/

#include "sched_framework.h"

/*******************************************************************************

                                    全局变量

*******************************************************************************/
/*调度器核心当前状态*/
SchedStatus_t framework_CoreStatus = SCHED_CORE_UNKNOWN;
/*内核时间管理*/
static SchedList_t delayedObjectList1;
static SchedList_t delayedObjectList2;
static SchedList_t * volatile pDelayedObjectList;
static SchedList_t * volatile pOverflowDelayedObjectList;
static SchedTick_t volatile coreTickCount;
static SchedTick_t volatile nextTimeArrival;

static void prvCoreEnvirInit(void);
/*******************************************************************************

                                    操作函数

*******************************************************************************/
/*调度器初始化*/
void framework_CoreInit(void)
{
    /*内核环境初始化*/
    prvCoreEnvirInit();
    /*调度器组件初始化*/
    /*调度器底层初始化*/
    sched_PortInit();
}

/*启动调度器*/
void framework_CoreStart(void)
{
    framework_CoreStatus = SCHED_CORE_RUNNING;
    while (1)
    {

    }
}

/*调度器节拍中断*/
void framework_CoreTickHandler(void)
{
    /*调度器启动后开始处理节拍中断*/
    if (SCHED_CORE_RUNNING == framework_CoreStatus)
    {
    SchedCPU_t cpu_sr;

        cpu_sr = SCHED_EnterCriticalFromISR();  /*进入临界区*/
        {
        const SchedTick_t currentTick = coreTickCount + 1;
        SchedList_t *pListItem;
        SchedTick_t listItemValue;
        SchedTick_t delay;
        SchedTick_t arrival;

            coreTickCount = currentTick;
            /*当节拍溢出时(计数到0),交换延时对象链表*/
            if (0 == currentTick)
            {
            SchedList_t *pTmpList;

                pTmpList = pDelayedObjectList;
                pDelayedObjectList = pOverflowDelayedObjectList;
                pOverflowDelayedObjectList = pTmpList;
                __framework_CoreTimeManagerUpdate();
            }
            /*当前可能存在对象延时结束*/
            if (currentTick >= nextTimeArrival)
            {
                for ( ;; )
                {
                    if (SCHED_FALSE != internal_ListIsEmpty(pDelayedObjectList))
                    {
                        nextTimeArrival = SCHED_MAX_TICK;
                        break;
                    }
                    else
                    {
                        /*获取将最先结束延时的链表项*/
                        pListItem       = internal_ListNext(pDelayedObjectList);
                        listItemValue   = internal_ListGetValue(pListItem);
                        if ( listItemValue > currentTick )
                        {
                            nextTimeArrival = listItemValue;
                            break;
                        }
                        /*延时结束,使用回调函数处理结束延时的对象*/
                        internal_ListRemove(pListItem);
                        delay = 0;
                        {
                        #if SCHED_TASK_EN
                        #if SCHED_TASK_CYCLE_EN
                            if (SCHED_LIST_CYCLE == pListItem->type)
                            {
                                delay = 0;
                            } else
                        #endif  /* SCHED_TASK_CYCLE_EN */
                        #if SCHED_TASK_ALARM_EN
                            if (SCHED_LIST_ALARM == pListItem->type)
                            {
                                delay = 0;
                            } else
                        #endif  /* SCHED_TASK_ALARM_EN */
                        #endif  /* SCHED_TASK_EN */
                        #if SCHED_DAEMON_EN
                            if (SCHED_LIST_DAEMON == pListItem->type)
                            {
                                delay = 0;
                            } else
                        #endif  /* SCHED_DAEMON_EN */
                            {
                                delay = 0;
                            }
                        }
                        /*将需要继续延时的对象添加到延时链表*/
                        if (0 != delay)
                        {
                            arrival = currentTick + delay;
                            internal_ListSetValue(pListItem, arrival);
                            if (arrival < currentTick)
                            {
                                internal_ListInsert(pOverflowDelayedObjectList, pListItem);
                            }
                            else
                            {
                                internal_ListInsert(pDelayedObjectList, pListItem);
                            }
                        }
                    }
                }
            }
        }
        SCHED_ExitCriticalFromISR(cpu_sr);      /*退出临界区*/
    }
}

/*******************************************************************************

                                    内部函数

*******************************************************************************/
/**
 * 说明: 1.向时间管理器添加延时对象
 *       2.确保添加的延时对象链表项是孤立的!
 *       3.若延时时间为0,则不执行任何操作
 *
 * 参数: 1.pListItem - 待添加的延时对象链表项指针
 *       2.delay     - 延时时间
 *
 * 返回: 无返回
 */
void __framework_CoreTimeManagerAddDelay(SchedList_t *pListItem, SchedTick_t delay)
{
const SchedTick_t currentTick = coreTickCount;
SchedTick_t arrival;

    if (0 != delay)
    {
        arrival = currentTick + delay;
        internal_ListSetValue(pListItem, arrival);
        if (arrival < currentTick)
        {
            internal_ListInsert(pOverflowDelayedObjectList, pListItem);
        }
        else
        {
            internal_ListInsert(pDelayedObjectList, pListItem);
            if (arrival < nextTimeArrival)
            {
                nextTimeArrival = arrival;
            }
        }
    }
}

/**
 * 说明: 1.更新时间管理器,用于优化节拍中断执行效率
 *       2.当对象链表项可能从延时链表中删除时,建议调用本函数
 *
 * 参数: 无参数
 *
 * 返回: 无返回
 */
void __framework_CoreTimeManagerUpdate(void)
{
SchedList_t *pListItem;

    if (SCHED_FALSE != internal_ListIsEmpty(pDelayedObjectList))
    {
        nextTimeArrival = SCHED_MAX_TICK;
    }
    else
    {
        pListItem = internal_ListNext(pDelayedObjectList);
        nextTimeArrival = internal_ListGetValue(pListItem);
    }
}

/*******************************************************************************

                                    私有函数

*******************************************************************************/
/*调度器内核环境初始化*/
static void prvCoreEnvirInit(void)
{
    internal_ListInit(&delayedObjectList1, SCHED_LIST_HEAD);
    internal_ListInit(&delayedObjectList2, SCHED_LIST_HEAD);
    pDelayedObjectList          = &delayedObjectList1;
    pOverflowDelayedObjectList  = &delayedObjectList2;
    coreTickCount               = 0;
    nextTimeArrival             = SCHED_MAX_TICK;
    framework_CoreStatus        = SCHED_CORE_STOP;
}