/**
 *-----------------------------------------------------------*
 *  版权所有：  (c), 2010 - 2999, 北京融信恒通科技有限公司
 *    文件名：  FileLog.cpp
 *      说明：  驱动日志处理函数接口库实现文件。
 *    版本号：  1.0.0
 * 
 *  版本历史：
 *	版本号		日期	作者	说明
 *	1.0.0	2010.08.06	范振兴	初始版本

 *  开发环境：
 *  Visual Studio 2008+WinDDK\7600.16385.1
 *	 Include库:WinDDK\7600.16385.1\inc\ddk;D:\WinDDK\7600.16385.1\inc\crt;WinDDK\7600.16385.1\inc\api
 *	 Lib库:WinDDK\7600.16385.1\lib\wxp\i386\
 *			hal.lib int64.lib ntoskrnl.lib ntstrsafe.lib
 *
 *-----------------------------------------------------------*
 */

#include "FileLog.h"
#define MAX_PATH 260						//日志文件的最大长度
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>
#include "LogConst.h"
#define CHECK_TIME 20						//到达该次数后文件进行大小判断

UNICODE_STRING  g_logFileName = {0};		//记录日志文件名称
ULONG			g_logLevel = LOG_TYPE_ALL;	//记录日志文件最大信息等级
HANDLE			g_logFile = NULL;			//日志文件句柄
LONGLONG		g_logFileMaxSize = 0;		//记录日志文件最大允许大小
ULONG			g_logCurrentFileNumber = 0;	//用0-1两个文件存储日志，要与当前打开文件名同步！！
USHORT			g_logWriteTime = 0;			//记录写入次数，到达CHECK_TIME后清零
USHORT			g_logStrategy = 1;			//记录日志文件策略,1为单文件，2为双文件

KMUTEX			g_logMutex;					//互斥体，防止多线程调用写日志

/**
* 设置系统内日志记录等级
* @param loglevel:ULONG,日志能够记录信息的最大等级。
*/
void SetLogLevel(ULONG loglevel)
{
	g_logLevel = loglevel;
}

/**
* 获得系统内日志记录等级
* @return ULONG 函数返回能够记录信息的最大等级。
*/
ULONG GetLogLevel()
{
	return g_logLevel;
}

/**
* 清除日志参数
* 无参数
*/
void LogUninitialize()
{
	//关闭文件
	ZwClose(g_logFile);
	g_logFile = 0;
	g_logLevel = LOG_TYPE_ALL;

	::RtlFreeUnicodeString(&g_logFileName);
	g_logCurrentFileNumber = 0;
	g_logFileMaxSize = 0;
	g_logWriteTime = 0;
	g_logStrategy = 1;
	KdPrint( ("Log uninitialize success!\n") );

}

/**
* 获取当前系统时间
* @return TIME_FIELDS 返回当前系统的时间。
*/
TIME_FIELDS GetLocalTime()
{
	LARGE_INTEGER  current_stime;
	KeQuerySystemTime(&current_stime);

	LARGE_INTEGER current_ltime;
	ExSystemTimeToLocalTime(&current_stime,&current_ltime);

	TIME_FIELDS current_tinfo;
	RtlTimeToTimeFields(&current_ltime,&current_tinfo);
    
	return current_tinfo;
}

/**
* 检测文件大小并做后处理，如果文件超过规定大小重新创建文件：2个参数
* 根据需要进行重建方针的选择，在日志系统初始化时一般采用1或2，当写入日志文件中判断大小采用1.
*/
NTSTATUS CheckLogFileSizeAndReCreateLogFile(PUNICODE_STRING pRecreateFilename,USHORT strategy)
{
	OBJECT_ATTRIBUTES    objectAttributes;
	IO_STATUS_BLOCK		ioStatus;
	//获取文件大小
	FILE_STANDARD_INFORMATION  fsi;
	NTSTATUS ntStatus = ::ZwQueryInformationFile(g_logFile,&ioStatus,&fsi,sizeof(FILE_STANDARD_INFORMATION),FileStandardInformation);

	
	if(!NT_SUCCESS(ntStatus) )
	{
		KdPrint( ("Strategy:%d:Get file's length error!\n",g_logStrategy) );
		return ntStatus;
	}

	if(fsi.EndOfFile.QuadPart >= g_logFileMaxSize)//文件超过大小,关闭该文件,重建
	{
		ZwClose(g_logFile);
		InitializeObjectAttributes(&objectAttributes,pRecreateFilename,OBJ_CASE_INSENSITIVE,NULL, NULL);

		if(strategy == 1)
		{
			KdPrint( ("Recreate File\n") );
			ntStatus = ZwCreateFile(&g_logFile,FILE_READ_ATTRIBUTES | FILE_APPEND_DATA | SYNCHRONIZE,
						&objectAttributes,&ioStatus,NULL,FILE_ATTRIBUTE_NORMAL,
						0,FILE_SUPERSEDE,FILE_SYNCHRONOUS_IO_NONALERT,NULL,0);

			if(!NT_SUCCESS(ntStatus))
			{
				//对于策略1无影响，如果为策略2，g_logCurrentFileNumber返回改为原有值
				g_logCurrentFileNumber = (g_logCurrentFileNumber > 0) ? 0 : 1;
				KdPrint( ("Strategy:%d:ReCreate File Error!\n",g_logStrategy));
				return ntStatus;
			}
		}
		else
		{
			KdPrint( ("Open Another File\n") );
			ntStatus = ZwCreateFile(&g_logFile,FILE_READ_ATTRIBUTES | FILE_APPEND_DATA | SYNCHRONIZE,
								&objectAttributes,&ioStatus,NULL,FILE_ATTRIBUTE_NORMAL,
								0,FILE_OPEN_IF,FILE_SYNCHRONOUS_IO_NONALERT,NULL,0);

			if(!NT_SUCCESS(ntStatus))
			{
				//对于策略1无影响，如果为策略2，g_logCurrentFileNumber返回改为原有值
				g_logCurrentFileNumber = (g_logCurrentFileNumber > 0) ? 0 : 1;
				KdPrint( ("Strategy:%d:ReCreate File Error!\n",g_logStrategy));
				return ntStatus;
			}

		}
	}
	else
		g_logCurrentFileNumber = (g_logCurrentFileNumber > 0) ? 0 : 1;//对于策略1无影响，如果为策略2，g_logCurrentFileNumber返回改为原有值

	return STATUS_SUCCESS;
}

/**
* 初始化文件日志。4个参数
* 在使用该日志模块时在写日志之前要调用该函数进行初始化，在中间任何地方都可以对日志文件进行写入日志信息，当写完日志后，不再调用该函数写日志，那么要调用LogUninitialize进行清理。
*/
void LogInitialize(ULONG loglevel,WCHAR* logfilename,LONGLONG filesize,USHORT strategy)
{
	size_t flength;
	NTSTATUS ntStatus = RtlStringCchLengthW(logfilename,(MAX_PATH-2),&flength);
	if(!NT_SUCCESS(ntStatus) )
	{
		KdPrint( ("Filename too long!\n") );
		return;
	}

	KdPrint( ("Filename length:%d\n",flength) );

	//将文件名记录在g_LogFileName中,注意Unicode_String不是以0为结束，以Length作为计算标准
	g_logFileName.Buffer = (PWSTR)ExAllocatePool(NonPagedPool,MAX_PATH*sizeof(WCHAR));//申请双倍内存
	RtlZeroMemory(g_logFileName.Buffer,MAX_PATH*sizeof(WCHAR));//清0，为了下面的操作顺利
	g_logFileName.MaximumLength = MAX_PATH*sizeof(WCHAR);
	RtlCopyMemory(g_logFileName.Buffer,logfilename,flength*sizeof(WCHAR));
	g_logFileName.Length = flength*sizeof(WCHAR);

	KdPrint( ("Filename:%wZ\n",&g_logFileName) );

	g_logLevel = loglevel;
	g_logFileMaxSize = filesize;
	g_logWriteTime = 0;

	g_logStrategy = strategy;

	if(g_logStrategy == 1)
	{	
		OBJECT_ATTRIBUTES   objectAttributes;
		IO_STATUS_BLOCK		ioStatus;
		InitializeObjectAttributes(&objectAttributes,&g_logFileName,OBJ_CASE_INSENSITIVE,NULL, NULL);
		NTSTATUS ntStatus = ZwCreateFile(&g_logFile,FILE_READ_ATTRIBUTES | FILE_APPEND_DATA | SYNCHRONIZE,
					&objectAttributes,&ioStatus,NULL,FILE_ATTRIBUTE_NORMAL,
					0,FILE_OPEN_IF,FILE_SYNCHRONOUS_IO_NONALERT,NULL,0);

		if(!NT_SUCCESS(ntStatus) )
		{
			KdPrint( ("Strategy:%d:Init Open file error!\n",g_logStrategy) );
			return;
		}

		ntStatus = CheckLogFileSizeAndReCreateLogFile(&g_logFileName,1);

		if(!NT_SUCCESS(ntStatus) )
		{
			KdPrint( ("Strategy:%d:Init Open file error!\n",g_logStrategy) );
			return;
		}
	}
	else
	{//开始打开文件
		OBJECT_ATTRIBUTES   objectAttributes;
		IO_STATUS_BLOCK		ioStatus;
		g_logCurrentFileNumber = 0;
		UNICODE_STRING factfilename0;
		USHORT re = GetLogFileName(&factfilename0);
		KdPrint( ("filename:%wZ\n",&factfilename0) );
		if(re == 1)
		{
			//打开第一个文件			
			InitializeObjectAttributes(&objectAttributes,&factfilename0,OBJ_CASE_INSENSITIVE,NULL, NULL);
			NTSTATUS ntStatus = ZwCreateFile(&g_logFile,FILE_READ_ATTRIBUTES | FILE_APPEND_DATA | SYNCHRONIZE,
							&objectAttributes,&ioStatus,NULL,FILE_ATTRIBUTE_NORMAL,
							0,FILE_OPEN_IF,FILE_SYNCHRONOUS_IO_NONALERT,NULL,0);

			if(!NT_SUCCESS(ntStatus))
			{
				KdPrint( ("Strategy:%d:Init Open file error!\n",g_logStrategy) );
				return;
			}
			g_logCurrentFileNumber = 1;
			UNICODE_STRING factfilename1;
			GetLogFileName(&factfilename1);
			KdPrint( ("CurrentFileNumber:%d:Init Open !\n",g_logCurrentFileNumber) );
			//检查文件00是否满，如果满了，打开文件01
			ntStatus = CheckLogFileSizeAndReCreateLogFile(&factfilename1,2);
			if(!NT_SUCCESS(ntStatus))
			{
				KdPrint( ("Strategy:%d:Init Open file error!\n",g_logStrategy) );
				return;
			}
			KdPrint( ("CurrentFileNumber:%d:Init Open !\n",g_logCurrentFileNumber) );

			//如果文件01满了，直接写入，不再做大小判断，20次后还有检查
			::RtlFreeUnicodeString(&factfilename1);
		}
		::RtlFreeUnicodeString(&factfilename0);
	}
	//初始化处理互斥体
	KeInitializeMutex(&g_logMutex,0);
	KdPrint( ("Log Init Sucsess!\n") );
}

/**
* 获取当前文件名称
* @param pfactfilename:PUNICODE_STRING,需要提前定义，在本函数内进行了空间申请，申请完由调用者释放！！。
* @return USHORT 返回转换的结果:1表示成功，0表示失败。
* 根据g_CurrentFileNumber和g_logFile共同生成了新的文件名。
*/
USHORT GetLogFileName(OUT PUNICODE_STRING pfactfilename)
{
	//此处对factfilename进行了申请，在本函数内未进行销毁，！在外部要销毁
	pfactfilename->Buffer = (PWSTR)ExAllocatePool(PagedPool,MAX_PATH*sizeof(WCHAR));
	RtlZeroMemory(pfactfilename->Buffer,MAX_PATH*sizeof(WCHAR));
	pfactfilename->MaximumLength = MAX_PATH*sizeof(WCHAR);

	NTSTATUS ntStatus = RtlStringCchPrintfW(pfactfilename->Buffer,MAX_PATH*sizeof(WCHAR),L"%s%02d",g_logFileName.Buffer,g_logCurrentFileNumber);
	
	if(!NT_SUCCESS(ntStatus) )
	{
		KdPrint( ("Filename error!\n") );
		return 0;
	}
	pfactfilename->Length = g_logFileName.Length + 2*sizeof(WCHAR);
	return 1;
}

/**
* 写日志函数
* @param iLevel:ULONG,要写入日志信息的等级，请参考LogConst.h文件内等级。
* @param format:NTSTRSAFE_PSTR*,输入信息的格式，此格式参考printf。
* @param ...:变参,参考printf，要写入的信息。
* 写日志信息接口，写日志信息大小请不要超过规定长度的大小！最大暂时为256字节！！
*/
void _cdecl WriteSysLog(ULONG iLevel, NTSTRSAFE_PSTR format,...)
{
	//判断记录日志级别
	if(iLevel > g_logLevel)
		return;

	//获得互斥体，在下面的调用中，所有的函数退出部分都要进行释放互斥体
	KeWaitForSingleObject(&g_logMutex, Executive, KernelMode, FALSE, NULL);
	//检查文件大小
	if(g_logWriteTime >= CHECK_TIME)
	{
		if(g_logStrategy == 1)
		{
			CheckLogFileSizeAndReCreateLogFile(&g_logFileName,1);	
		}
		else//策略2检测文件大小
		{
			g_logCurrentFileNumber = (g_logCurrentFileNumber > 0) ? 0 : 1;

			UNICODE_STRING factfilename;
			USHORT re = GetLogFileName(&factfilename);
			if(re == 1)
			{
				if(!NT_SUCCESS(CheckLogFileSizeAndReCreateLogFile(&factfilename,1)) )
				{
					//在函数所有的出口释放互斥体
					::RtlFreeUnicodeString(&factfilename);
					KeReleaseMutex(&g_logMutex,FALSE);
					return;
				}
			}
			::RtlFreeUnicodeString(&factfilename);
		}
		g_logWriteTime = 0;
	}
	//获得时间
	TIME_FIELDS ctime;
	ctime = GetLocalTime();
	ULONG pid = (ULONG)PsGetCurrentProcessId();
	PCHAR ptype;
	IO_STATUS_BLOCK		ioStatus;
	//获得信息等级部分的字符串
	switch(iLevel)
	{
		case LOG_TYPE_ERROR:	ptype = "ERROR";break;
		case LOG_TYPE_WARN:		ptype = "WARNING";break;
		case LOG_TYPE_INFO:		ptype = "INFO";break;
		default:				ptype = "DEBUG";break;
	}
    CHAR strTemp[MAX_INFO_LENGTH];
    RtlZeroMemory(strTemp, MAX_INFO_LENGTH);
    NTSTRSAFE_PSTR pinfo = strTemp;
	//合成变参部分的字符串
 	va_list args; 
    va_start(args,format); 
	NTSTATUS ntStatus =::RtlStringCbVPrintfA(pinfo,MAX_INFO_LENGTH*sizeof(CHAR),format,args);
	if( !NT_SUCCESS(ntStatus) )
	{
		KdPrint( ("Log info conversion error1!\n") );
		KeReleaseMutex(&g_logMutex,FALSE);
		//在函数所有的出口释放互斥体
		return;
	}
    va_end(args); 
	//组成写入文件的字符串
	CHAR cinfo[MAX_INFO_LENGTH*2];
	RtlZeroMemory(cinfo,MAX_INFO_LENGTH*2*sizeof(CHAR));//清0，为了下面的操作顺利
	ntStatus = RtlStringCbPrintfA(cinfo,MAX_INFO_LENGTH*2*sizeof(CHAR), "%04d-%02d-%02d %02d:%02d:%02d.%03d\t%d\t%s\t%s\r\n", ctime.Year, ctime.Month, ctime.Day, 
		ctime.Hour, ctime.Minute, ctime.Second, ctime.Milliseconds,pid,ptype,pinfo);
	if( !NT_SUCCESS(ntStatus) )
	{
		KdPrint( ("Log info conversion error2!\n") );
		//在函数所有的出口释放互斥体
		KeReleaseMutex(&g_logMutex,FALSE);
		return;
	}
	//计算该字符串的长度
	size_t length;
	ntStatus = RtlStringCbLengthA(cinfo,MAX_INFO_LENGTH*2,&length);
	if( !NT_SUCCESS(ntStatus) )
	{
		KdPrint( ("Log info conversion error3!\n") );
		//在函数所有的出口释放互斥体
		KeReleaseMutex(&g_logMutex,FALSE);
		return;
	}
	//写入文件
	ntStatus = ZwWriteFile(g_logFile,NULL,NULL,NULL,&ioStatus,cinfo,length,NULL,NULL);
	if( !NT_SUCCESS(ntStatus) )
	{
		KdPrint( ("write log error!\n") );
		//在函数所有的出口释放互斥体
		KeReleaseMutex(&g_logMutex,FALSE);
		return;
	}
	g_logWriteTime ++;//增加写入次数

	KeReleaseMutex(&g_logMutex,FALSE);
	//KdPrint( ("write log successful!\n") );
}
