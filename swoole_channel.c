/*
 +----------------------------------------------------------------------+
 | Swoole                                                               |
 +----------------------------------------------------------------------+
 | Copyright (c) 2012-2015 The Swoole Group                             |
 +----------------------------------------------------------------------+
 | This source file is subject to version 2.0 of the Apache license,    |
 | that is bundled with this package in the file LICENSE, and is        |
 | available through the world-wide-web at the following url:           |
 | http://www.apache.org/licenses/LICENSE-2.0.html                      |
 | If you did not receive a copy of the Apache2.0 license and are unable|
 | to obtain it through the world-wide-web, please send a note to       |
 | license@swoole.com so we can mail you a copy immediately.            |
 +----------------------------------------------------------------------+
 | Author: Tianfeng Han  <mikan.tenny@gmail.com>                        |
 +----------------------------------------------------------------------+
 */

//底层基于共享内存+Mutex互斥锁实现，可实现用户态的高性能内存队列。
//初始化时需要把申请的内存大小想好，因为空间不足的话不会扩容。
//channel 用链表实现是否更好了呢？？
//先进先出队列
#include "php_swoole.h"
#include "swoole_coroutine.h"

static PHP_METHOD(swoole_channel, __construct);
static PHP_METHOD(swoole_channel, __destruct);
static PHP_METHOD(swoole_channel, push);
static PHP_METHOD(swoole_channel, pop);
static PHP_METHOD(swoole_channel, peek);
static PHP_METHOD(swoole_channel, stats);

static zend_class_entry swoole_channel_ce;
zend_class_entry *swoole_channel_class_entry_ptr;

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_channel_construct, 0, 0, 1)
    ZEND_ARG_INFO(0, size)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_channel_push, 0, 0, 1)
    ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_swoole_void, 0, 0, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry swoole_channel_methods[] =
{
    PHP_ME(swoole_channel, __construct, arginfo_swoole_channel_construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
    PHP_ME(swoole_channel, __destruct, arginfo_swoole_void, ZEND_ACC_PUBLIC | ZEND_ACC_DTOR)
    PHP_ME(swoole_channel, push, arginfo_swoole_channel_push, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_channel, pop, arginfo_swoole_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_channel, peek, arginfo_swoole_void, ZEND_ACC_PUBLIC)
    PHP_ME(swoole_channel, stats, arginfo_swoole_void, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

//初期化
void swoole_channel_init(int module_number TSRMLS_DC)
{
    SWOOLE_INIT_CLASS_ENTRY(swoole_channel_ce, "swoole_channel", "Swoole\\Channel", swoole_channel_methods);
    swoole_channel_class_entry_ptr = zend_register_internal_class(&swoole_channel_ce TSRMLS_CC);
    SWOOLE_CLASS_ALIAS(swoole_channel, "Swoole\\Channel");
}

//实例化 传递参数 size 通道占用的内存的尺寸，单位为字节
static PHP_METHOD(swoole_channel, __construct)
{
    long size;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &size) == FAILURE)
    {
        RETURN_FALSE;
    }

    if (size < SW_BUFFER_SIZE_STD)//size 不能小于8129
    {
        size = SW_BUFFER_SIZE_STD;//8192
    }

    swChannel *chan = swChannel_new(size, SW_BUFFER_SIZE_STD, SW_CHAN_LOCK | SW_CHAN_SHM);
    if (chan == NULL)
    {
        zend_throw_exception(swoole_exception_class_entry_ptr, "failed to create channel.", SW_ERROR_MALLOC_FAIL TSRMLS_CC);
        RETURN_FALSE;
    }
    swoole_set_object(getThis(), chan);//保存chan 内存指针
}

//channel 释放
static PHP_METHOD(swoole_channel, __destruct)
{
    SW_PREVENT_USER_DESTRUCT;

    swoole_set_object(getThis(), NULL);
}

//向channel 中push
static PHP_METHOD(swoole_channel, push)
{
    swChannel *chan = swoole_get_object(getThis());
    zval *zdata;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &zdata) == FAILURE)
    {
        RETURN_FALSE;
    }

    swEventData buf;//channel 放入存放这个结构体，数据放在buf.data中
    if (php_swoole_task_pack(&buf, zdata TSRMLS_CC) < 0) //存入的数据pack
    {
        RETURN_FALSE;
    }
    SW_CHECK_RETURN(swChannel_push(chan, &buf, sizeof(buf.info) + buf.info.len));//把数据压入
}

//弹出数据
static PHP_METHOD(swoole_channel, pop)
{
    swChannel *chan = swoole_get_object(getThis());
    swEventData buf;

    int n = swChannel_pop(chan, &buf, sizeof(buf));
    if (n < 0)
    {
        RETURN_FALSE;
    }

    zval *ret_data = php_swoole_task_unpack(&buf TSRMLS_CC);//unpack
    if (ret_data == NULL)
    {
        RETURN_FALSE;
    }

    RETVAL_ZVAL(ret_data, 0, NULL);
    efree(ret_data);
}

static PHP_METHOD(swoole_channel, peek)
{
    swChannel *chan = swoole_get_object(getThis());
    swEventData buf;

    int n = swChannel_peek(chan, &buf, sizeof(buf));
    if (n < 0)
    {
        RETURN_FALSE;
    }

    swTask_type(&buf) |= SW_TASK_PEEK;
    zval *ret_data = php_swoole_task_unpack(&buf TSRMLS_CC);
    if (ret_data == NULL)
    {
        RETURN_FALSE;
    }

    RETVAL_ZVAL(ret_data, 0, NULL);
    efree(ret_data);
}

//数据统计
static PHP_METHOD(swoole_channel, stats)
{
    swChannel *chan = swoole_get_object(getThis());
    array_init(return_value);

    sw_add_assoc_long_ex(return_value, ZEND_STRS("queue_num"), chan->num);
    sw_add_assoc_long_ex(return_value, ZEND_STRS("queue_bytes"), chan->bytes);
}
