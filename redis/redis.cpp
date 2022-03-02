#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <hiredis/hiredis.h>
 
void doTest()
{
	//redis默认监听端口为6387 可以再配置文件中修改
	redisContext* redis = redisConnect("47.106.133.195", 6380);
	if ( NULL == redis || redis->err)
	{       // redis为NULL与redis->err是两种不同的错误，若redis->err为true，可使用redis->errstr查看错误信息
		redisFree(redis);
		printf("Connect to redisServer faile\n");
		return ;
	}
	printf("Connect to redisServer Success\n");
	const char* command1 = "set stest1 value1";
	redisReply* reply = (redisReply*)redisCommand(redis, command1);    // 执行命令，结果强转成redisReply*类型
	if( NULL == reply)
	{
		printf("Execut command1 failure\n");
		redisFree(redis);     // 命令执行失败，释放内存
		return;
	}
	if( !(reply->type == REDIS_REPLY_STATUS && strcasecmp(reply->str,"OK")==0))
	{       // 判断命令执行的返回值
		printf("Failed to execute command[%s]\n",command1);
		freeReplyObject(reply);
		redisFree(redis);
		return;
	}	
	freeReplyObject(reply);
	printf("Succeed to execute command[%s]\n", command1);
	// 一切正常，则对返回值进行处理
}
int main()
{
	doTest();
	return 0;
}