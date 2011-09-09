/************************************************************************
* �ļ�����:Driver.cpp                                                 
* 
* 
*************************************************************************/

#include "Driver.h"
#include "HardCode.h"
#include "../common/DriverDefine.h"


#include "ServiceTable.h"
#include "MapViewOfSection.h"

#include "ProcessFilter.h"
#include "SecuHash.h"
#include "HardCode.h"
#include "FilterCache.h"
#include "Utils/comm.h"
#include "RegOperation.h"
#include "LogSystem.h"
#include "SendDataToGUI.h"
#include "BlackHash.h"
#include "MyDefine.h"

extern LONG g_HookCounter;
extern KMUTEX	g_DispatchMutex;
__int64    g_globalTime     = 0;
ULONG  g_processNameOffset  = 0;

/**
* ��ý�������ƫ��
*/
ULONG getProcessNameOffset()
{
	PEPROCESS curproc;

	DWORD procNameOffset;

	curproc = PsGetCurrentProcess();

	for(int i=0; i< 4096; i++)
	{
		//tolower();
		if( !strncmp( SYSTEMNAME, (PCHAR) curproc + i, strlen(SYSTEMNAME) ))
		{  
			g_processNameOffset = i;
		}
	}
	return g_processNameOffset;
}
/************************************************************************
* ��������:DriverEntry
* ��������:��ʼ���������򣬶�λ������Ӳ����Դ�������ں˶���
* �����б�:
      pDriverObject:��I/O�������д���������������
      pRegistryPath:����������ע������е�·��
* ���� ֵ:���س�ʼ������״̬
*************************************************************************/
#pragma INITCODE
extern "C" NTSTATUS DriverEntry (
			IN PDRIVER_OBJECT pDriverObject,
			IN PUNICODE_STRING pRegistryPath	) 
{
 	NTSTATUS status ;
	KdPrint(("==> DriverEntry\n"));

	// ע����̻ص�����
	PsSetCreateProcessNotifyRoutine(OnProcessQuit, FALSE);

	pDriverObject->MajorFunction[IRP_MJ_CREATE] = DDKDispatchRoutine;
	pDriverObject->MajorFunction[IRP_MJ_CLOSE] = DDKDispatchRoutine;
	pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;

	pDriverObject->MajorFunction[IRP_MJ_POWER] = DDKPower;
	pDriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = DDKPower;
	
	status = CreateDevice(pDriverObject);
	::KeInitializeMutex(&g_DispatchMutex,0);
	
	getProcessNameOffset();
	InitializeFilterGoEvent();

	if(false == initializeLog())
		return STATUS_UNSUCCESSFUL;

	if(false == InitHardCode())
	{
		KdPrint(("init OS is error!\n"));
		return STATUS_UNSUCCESSFUL;
	}

	HookSSDT();

	initialMutex();//whiteTable
	initialMutexB();//blackTable
	initialMutexAddPID();

	KdPrint(("<== DriverEntry\n"));
	return status;
}

/************************************************************************
* ��������:CreateDevice
* ��������:��ʼ���豸����
* �����б�:
      pDriverObject:��I/O�������д���������������
* ���� ֵ:���س�ʼ��״̬
*************************************************************************/
#pragma INITCODE
NTSTATUS CreateDevice (
		IN PDRIVER_OBJECT	pDriverObject) 
{
	NTSTATUS status;
	PDEVICE_OBJECT pDevObj;
	PDEVICE_EXTENSION pDevExt;
	
	UNICODE_STRING devName;
	RtlInitUnicodeString(&devName,DEVICE_NAME);
	
	UNICODE_STRING symLinkName;
	RtlInitUnicodeString(&symLinkName,DOS_NAME);
	
	
	status = IoCreateDevice( pDriverObject,
						sizeof(DEVICE_EXTENSION),
						&(UNICODE_STRING)devName,
						FILE_DEVICE_UNKNOWN,
						FILE_DEVICE_SECURE_OPEN, FALSE,
						&pDevObj );
	if (!NT_SUCCESS(status))
	{
		KdPrint( ("create device is error!\n") );
		return status;
	}

	IoRegisterShutdownNotification(pDevObj);

	pDevObj->Flags |= DO_BUFFERED_IO;
	pDevObj->Flags &= (~DO_DEVICE_INITIALIZING);//wdm used

	pDevExt = (PDEVICE_EXTENSION)pDevObj->DeviceExtension;
	pDevExt->pDevice = pDevObj;
	pDevExt->ustrDeviceName = devName;//
	pDevExt->ustrSymLinkName = symLinkName;//

	status = IoCreateSymbolicLink( &symLinkName,&devName );
	if (!NT_SUCCESS(status)) 
	{
		IoDeleteDevice( pDevObj );
		KdPrint( ("create symbolicLink is error!\n") );
		return status;
	}

	//fanzhenxing add
	InitBlackCache();
	InitFilterCache();

	return STATUS_SUCCESS;
}

/************************************************************************
* ��������:DDKUnload
* ��������:�������������ж�ز���
* �����б�:
      pDriverObject:��������
* ���� ֵ:����״̬
*************************************************************************/
#pragma PAGEDCODE
VOID DDKUnload (IN PDRIVER_OBJECT pDriverObject) 
{
	::KeWaitForSingleObject(&g_DispatchMutex,Executive,KernelMode,FALSE,NULL);
	KdPrint(("==> DriverUnload\n"));
	
	// ȡ�����̻ص�
	PsSetCreateProcessNotifyRoutine(OnProcessQuit, TRUE);
	UnHookSSDT();

	WriteSysLog(LOG_TYPE_DEBUG,L" FinishHookSSDT 2");

	
	if(pEvent)
		ObDereferenceObject(pEvent);

	ClearFilterCache();
	ClearBlackCache();
	WriteSysLog(LOG_TYPE_DEBUG,L" ClearBlackCache");

	LogUninitialize();
	::KeReleaseMutex(&g_DispatchMutex,FALSE);

	while(g_HookCounter>0)
	{
		LARGE_INTEGER interval;
		interval.QuadPart = -10 * 1000 * 1000;
		KeDelayExecutionThread(KernelMode, FALSE, &interval);
	}
	//

	PDEVICE_OBJECT	pNextObj;
	pNextObj = pDriverObject->DeviceObject;
	
	while (pNextObj != NULL) 
	{
		PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)
			pNextObj->DeviceExtension;

		//ɾ����������
		UNICODE_STRING pLinkName = pDevExt->ustrSymLinkName;
		IoDeleteSymbolicLink(&pLinkName);
		pNextObj = pNextObj->NextDevice;
		IoDeleteDevice( pDevExt->pDevice );

		IoUnregisterShutdownNotification(pDevExt->pDevice);
	}

	KdPrint(("<== DriverUnload\n"));
}

/************************************************************************
* ��������:HelloDDKDispatchRoutine
* ��������:�Զ�IRP���д���
* �����б�:
      pDevObj:�����豸����
      pIrp:��IO�����
* ���� ֵ:����״̬
*************************************************************************/
#pragma PAGEDCODE
NTSTATUS DDKDispatchRoutine(IN PDEVICE_OBJECT pDevObj,
								 IN PIRP pIrp) 
{
	NTSTATUS status = STATUS_SUCCESS;

	pIrp->IoStatus.Status = status;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest( pIrp, IO_NO_INCREMENT );

	return status;
}

/************************************************************************
* ��������:DDKPower
* ��������:�Զ�IRP���д���
* �����б�:
      pDevObj:�����豸����
      pIrp:��IO�����
* ���� ֵ:����״̬
*************************************************************************/
#pragma PAGEDCODE
NTSTATUS DDKPower(IN PDEVICE_OBJECT pDevObj,
		 IN PIRP pIrp)
{
	NTSTATUS Status = STATUS_SUCCESS;
 	if(SetReg(AUTORUN,REGNAME) == false)
 		;//WriteSysLog(LOG_TYPE_DEBUG,L" is: %s","error");
	
	return Status;
}
/**
*  �����ַ���������Ӧ�ϲ�Ӧ�õĸ��ֲ�������
*
*/
NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP IRP) 
{
	::KeWaitForSingleObject(&g_DispatchMutex,Executive,KernelMode,FALSE,NULL);

	NTSTATUS ntStatus = STATUS_UNSUCCESSFUL;
	ULONG tmpLen = 0;
	PIO_STACK_LOCATION pIoStackIrp = IoGetCurrentIrpStackLocation(IRP);

	switch (pIoStackIrp->Parameters.DeviceIoControl.IoControlCode) 
	{
		case IOCTL_SET_PROTECT_PID://���ñ���PID
			KdPrint(("IOCTL_SET_PROTECT_PID\n"));

			{
				unsigned char pUnPack[256];
				int unPackLength;

				unPackLength = DownloadUnPack((unsigned char *)IRP->AssociatedIrp.SystemBuffer,pIoStackIrp->Parameters.DeviceIoControl.InputBufferLength,pUnPack);

				if (unPackLength > 0)
				{
					PPID_INFO pInputBuffer = (PPID_INFO)pUnPack;
					int iStringLength = unPackLength;

					if(iStringLength != sizeof(PID_INFO)) 
						break;

					__int64 elapsedTime = __rdtsc() - pInputBuffer->currentTime;

					KdPrint(("IOCTL_SET_PROTECT_PID elapsed time: %I64d.\n", elapsedTime));

					if((elapsedTime > COMMUNICATE_TIME_LIMIT)||(elapsedTime <=COMMUNICATE_TIME_DOWN))
					{
						KdPrint(("IOCTL_SET_PROTECT_PID exceeds time limit.\n"));
					} 
					else 
					{					
					// ������� ID
						AddProtectPID(pInputBuffer->PID[0]);
					}

					ntStatus = STATUS_SUCCESS;
				}
			}
			break;

		case IOCTL_GET_PROTECT_PIDS:///�ж������Ƿ�����������
			KdPrint(("IOCTL_GET_PROTECT_PIDS\n"));

			if (IRP->MdlAddress) 
			{
				PPID_INFO pUserBuffer = (PPID_INFO)MmGetSystemAddressForMdlSafe(IRP->MdlAddress, NormalPagePriority);
				if(pUserBuffer == NULL)
					return ntStatus;
				ULONG OutputLength = pIoStackIrp->Parameters.DeviceIoControl.OutputBufferLength;
	
				ULONG PIDLength = OutputLength - sizeof(PID_INFO) + sizeof(UINT32);

				PPID_INFO tmpBuf=(PPID_INFO)ExAllocatePoolWithTag(PagedPool,OutputLength-sizeof(UINT32),'bak');
				if(!tmpBuf)
					return ntStatus;
				
				
				KdPrint(("entry check hook safe!\n"));
				if(checkHookSafe())
				{
					tmpBuf->count = GetKernelPIDs(tmpBuf->PID, PIDLength / sizeof(UINT32));
					tmpBuf->currentTime = __rdtsc();
					ULONG bufLength = sizeof(PID_INFO) + tmpBuf->count*sizeof(UINT32);
					tmpLen = UploadPack((PUCHAR)tmpBuf , bufLength , (PUCHAR)pUserBuffer);
				}
				else
				{
					RtlZeroMemory(tmpBuf,OutputLength-sizeof(UINT32));
					tmpLen = 0;
				}

				ExFreePoolWithTag(tmpBuf,'bak');

				ntStatus = STATUS_SUCCESS;
			}
			break;

		case IOCTL_SET_SECU_PATHS://��Ч
			KdPrint(("IOCTL_SET_SECU_PATHS\n"));
			{
				ntStatus = STATUS_SUCCESS;
			}
			break;

		case IOCTL_SET_SECU_MD5://���ð�������ϣ�б�
			KdPrint(("IOCTL_SET_SECU_MD5\n"));
			{
				PUCHAR pInputBuffer;
				int iStringLength = pIoStackIrp->Parameters.DeviceIoControl.InputBufferLength;

				unsigned char * pUnPack=(UCHAR *)ExAllocatePoolWithTag(PagedPool,iStringLength,'knab');
				int unPackLength;

				unPackLength = DownloadUnPack((unsigned char *)IRP->AssociatedIrp.SystemBuffer,pIoStackIrp->Parameters.DeviceIoControl.InputBufferLength,pUnPack);
	
				RtlCopyBytes((PVOID)&iStringLength,(PVOID)pUnPack,sizeof(ULONG) );
				pInputBuffer = pUnPack + sizeof(ULONG);
				RtlCopyBytes((PVOID)&g_globalTime,(PVOID)(pInputBuffer + iStringLength),8 );


				__int64 elapseTime = __rdtsc() - g_globalTime;

				if( (elapseTime < COMMUNICATE_TIME_LIMIT) && (elapseTime >= COMMUNICATE_TIME_DOWN)  )
				{
					if (unPackLength > 0)
					{
						for(int i = 0; i <= iStringLength - HASH_SIZE; i += HASH_SIZE)
							AddSecureHash(pInputBuffer + i);

						getSecuTable();
						ClearHash();

						ntStatus = STATUS_SUCCESS;
					}
				}
			
				ExFreePoolWithTag(pUnPack,'knab');
				//
			}
			break;
			/////////////
		case IOCTL_SET_BLACK_MD5://���ú�������ϣ�б�
			KdPrint(("IOCTL_SET_SECU_MD5\n"));
			{
				PUCHAR pInputBuffer;
				int iStringLength = pIoStackIrp->Parameters.DeviceIoControl.InputBufferLength;

				unsigned char * pUnPack=(UCHAR *)ExAllocatePoolWithTag(PagedPool,iStringLength,'knab');
				int unPackLength;

				unPackLength = DownloadUnPack((unsigned char *)IRP->AssociatedIrp.SystemBuffer,pIoStackIrp->Parameters.DeviceIoControl.InputBufferLength,pUnPack);

				RtlCopyBytes((PVOID)&iStringLength,(PVOID)pUnPack,sizeof(ULONG) );
				pInputBuffer = pUnPack + sizeof(ULONG);
				RtlCopyBytes((PVOID)&g_globalTime,(PVOID)(pInputBuffer + iStringLength),8 );

				__int64 elapseTime = __rdtsc() - g_globalTime;

				if( (elapseTime < COMMUNICATE_TIME_LIMIT) && (elapseTime >= COMMUNICATE_TIME_DOWN)  )
				{
					if (unPackLength > 0)
					{
						for(int i = 0; i <= iStringLength - HASH_SIZE; i += HASH_SIZE)
							AddBlackHash(pInputBuffer + i);

						getBlackTable();
						ClearBlackHash();

						ntStatus = STATUS_SUCCESS;
					}
				}

				ExFreePoolWithTag(pUnPack,'knab');
				//
			}
			break;
			/////////////

		case IOCTL_SET_UP_UNLOAD://����ж����������
			KdPrint(("IOCTL_SET_UP_UNLOAD\n"));
			DeviceObject->DriverObject->DriverUnload= DDKUnload;	
			ntStatus = STATUS_SUCCESS;
			break;

		case IOCTL_GET_DRIVER_STATUS://�õ��������ܰ�����״̬
			{
			//////////////////////////////////////////////////////
				unsigned char pUnPack[256];
				int unPackLength;

				unPackLength = DownloadUnPack((unsigned char *)IRP->AssociatedIrp.SystemBuffer,pIoStackIrp->Parameters.DeviceIoControl.InputBufferLength,pUnPack);

				if (unPackLength > 0)
				{
					MYDRIVERSTATUS type = *(MYDRIVERSTATUS *)pUnPack;

					PUCHAR pUserBuffer = (PUCHAR)MmGetSystemAddressForMdlSafe(IRP->MdlAddress, NormalPagePriority);// �������ڴ����ʱ�᷵��NULL
					if(pUserBuffer == NULL)
						return ntStatus;

					ULONG OutputLength = pIoStackIrp->Parameters.DeviceIoControl.OutputBufferLength;

					ULONG * tmpBuf=(ULONG *)ExAllocatePoolWithTag(PagedPool,4,'bnak');
					if(!tmpBuf)
						return ntStatus;

					if(type == DRIVER_RECEIVED)//�Ƿ���ܵ�������
					{
						if(!isRestart())  g_isReceive=false;
						*tmpBuf = g_isReceive ? 1 : 0;
						tmpLen = UploadPack((PUCHAR)tmpBuf , 4 , (PUCHAR)pUserBuffer);
					}
		
					ExFreePoolWithTag(tmpBuf,'bnak');
				}		

				ntStatus = STATUS_SUCCESS;
			//////////////////////////////////////////////////////
			}
			break;

			case IOCTL_SET_HANDLE://�õ�ͬ�����
				{
					if(pEvent)
						ObDereferenceObject(pEvent);

					HANDLE hEvent = NULL; 
					int unPackLength;

					unPackLength=DownloadUnPack((unsigned char *)IRP->AssociatedIrp.SystemBuffer,pIoStackIrp->Parameters.DeviceIoControl.InputBufferLength,(unsigned char *)&hEvent);

					if (unPackLength>0)
					{
						ObReferenceObjectByHandle(hEvent , EVENT_MODIFY_STATE, *ExEventObjectType, KernelMode, (PVOID *)&pEvent, NULL);
	
						ntStatus = STATUS_SUCCESS;
					}
				}
				break;
			case IOCTL_RETURN_DATA://���ػ�����
				{	
					if (IRP->MdlAddress) 
					{
						wchar_t fName[260] = {0};
						unsigned char cHash[16] = {0};
						
						getData(fName,cHash);

						unsigned char dataBuf[260*sizeof(wchar_t) + 16 + 4*4] = {0};
						*(DWORD*)(dataBuf) = 1;
						RtlCopyMemory(dataBuf+4,fName,DATAPATHLEN);
						*(wchar_t*)(dataBuf + DATAPATHLEN -2) = L'\0';
						RtlCopyMemory(dataBuf+4+DATAPATHLEN,cHash,DATAHASHLEN);

						wchar_t * pUserBuffer = (wchar_t *)MmGetSystemAddressForMdlSafe(IRP->MdlAddress, NormalPagePriority);
						ULONG OutputLength = pIoStackIrp->Parameters.DeviceIoControl.OutputBufferLength;
		
						tmpLen = UploadPack((PUCHAR)dataBuf, 260*sizeof(wchar_t) + 16 + 4*4, (PUCHAR)pUserBuffer);
					}

					ntStatus = STATUS_SUCCESS;
				}

				break;

			case IOCTL_SET_FILTERRESULT://��֤���
				{
					bool bResult = false;
					int unPackLength=DownloadUnPack((unsigned char *)IRP->AssociatedIrp.SystemBuffer,pIoStackIrp->Parameters.DeviceIoControl.InputBufferLength,(unsigned char *)&bResult);

					if (unPackLength>0)
					{
						g_bPassFilter = bResult;
					}
					else
					{
						g_bPassFilter = false;
					}

					KeSetEvent(g_pEventFilterGo, IO_NO_INCREMENT, FALSE);
					ntStatus = STATUS_SUCCESS;
				}

				break;
	}

	IRP->IoStatus.Status = 0;
	IRP->IoStatus.Information = tmpLen ;
	IoCompleteRequest(IRP, IO_NO_INCREMENT);

	::KeReleaseMutex(&g_DispatchMutex,FALSE);

	return ntStatus;
}