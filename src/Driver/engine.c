#include <ntifs.h>
#include <ntddk.h>
#include <wdm.h>
#include <ntimage.h>
#include <windef.h>
#include <ntstrsafe.h>
#include "engine.h"
#include "define.h"
#include "undocumented.h"
#include "listentry.h"

// Build 19041
#define EPROCESS_ACTIVE_PROCESS_LINKS	0x448 
// Build 18362
//#define EPROCESS_ACTIVE_PROCESS_LINKS	0x2F0 

#define EPROCESS_IMAGE_FILE_NAME		0x5A8		
#define EPROCESS_UNIQUE_PROCESS_ID		0x440 // PVOID64


extern PDRIVER_OBJECT g_pDriverObject;
extern POBJECT_TYPE* IoDriverObjectType;
PLDR_DATA_TABLE_ENTRY PsLoadedModuleList;

VOID EnumerateEPROCESS(PUCHAR pTargetFileName)
{
	// system(pid:4)�� EPROCESS �� ��ȯ��
	PEPROCESS pCurrentProcess = PsInitialSystemProcess; /* PsGetCurrentProcess() �� ���� */
	PLIST_ENTRY pCurrentListEntry = NULL;

	while (TRUE)
	{
		// next EPROCESS
		pCurrentListEntry = (PLIST_ENTRY)((uintptr_t)pCurrentProcess + EPROCESS_ACTIVE_PROCESS_LINKS);
		PEPROCESS pNextProcess = (PEPROCESS)((uintptr_t)pCurrentListEntry->Flink - EPROCESS_ACTIVE_PROCESS_LINKS);

		/* ����ü���� ���ϱ�
		size_t dwPid = *((uintptr_t*)((uintptr_t)pCurrentProcess + EPROCESS_UNIQUE_PROCESS_ID));
		char* pImageFileName = (char*)((uintptr_t)pCurrentProcess + EPROCESS_IMAGE_FILE_NAME);
		*/

		// build �������� �������� �ٸ��⶧���� ������ api�� ���
		size_t dwPid = (size_t)PsGetProcessId(pCurrentProcess);
		dwPid = 0;
		UCHAR* pImageFileName = PsGetProcessImageFileName(pCurrentProcess); // ����� ��������.

		kprintf("%p %s %lld", pCurrentProcess, PsGetProcessImageFileName(pCurrentProcess), dwPid);

		// process �����
		// �� �� �̳��� patch guard�� ���� BSOD �߻� (CRITICAL STRUCTURE CORRUPTION)
		// ���� : https://www.sysnet.pe.kr/2/0/12110
		if (0 == strcmp((char*)pTargetFileName, (char*)pImageFileName))
		{
			kprintf("hide process\n");
			PLIST_ENTRY pPrevListEntry = pCurrentListEntry->Blink;
			PLIST_ENTRY pNextListEntry = pCurrentListEntry->Flink;
			pPrevListEntry->Flink = pCurrentListEntry->Flink;  // ���� ����� ������ �� �������� �ٲ�
			pNextListEntry->Blink = pCurrentListEntry->Blink; // ���� ����� ������ �� ������ �ٲ� 

			pCurrentListEntry->Flink = pCurrentListEntry;
			pCurrentListEntry->Blink = pCurrentListEntry;
		}

		/* ring3 ���μ������� �˻��ϴ� ��ƾ �ʿ�
		if (PsGetProcessPeb(pCurrentProcess))
		{
			PPEB ppp = PsGetProcessPeb(pCurrentProcess);

			kprintf("have peb\n");
		}
		*/

		// ���� ������ break���� ������ �������� ���ư�
		if (pNextProcess == PsInitialSystemProcess)
		{
			break;
		}
		pCurrentProcess = pNextProcess;
	}

}

VOID ScanMemory(UINT PatternNo, uintptr_t StartAddr, uintptr_t EndAddr, PBYTE Pattern, UINT PatternSize)
{

//	kprintf("pattern size : %d %llx %llx", PatternSize, StartAddr , EndAddr);

	if (Pattern == NULL || PatternSize < 4)
	{
		kprintf("pattern is null or size is too small.");
		return;
	}
	__try
	{
		// page ������ ������ �˻�
		for (uintptr_t Addr = StartAddr; Addr < EndAddr; Addr = Addr + PAGE_SIZE)
		{
			// Ư�� section�� �޸� ������ �Ұ����ϱ� ������ skip��
			BOOLEAN bValid = MmIsAddressValid((PVOID)Addr);
			if (bValid == FALSE)
			{
				//	kprintf("skipAddr %p \n", Addr);
				continue;
			}

			for (int i = 0; i <= PAGE_SIZE - (int)PatternSize; i++)
			{
				size_t j = 0;

				for (j = 0; j < PatternSize; j++)
				{
					PUCHAR pAddr = (PUCHAR)Addr;
					if (pAddr[i + j] != Pattern[j])
					{
						// ��ġ���� ������ j�� ������ ����
						break;
					}
				}
				// ���� ����
				if (j == PatternSize)
				{
					DETECT_LIST_ENTRY DetectListEntry = { 0, };
					DetectListEntry.BaseAddress = StartAddr;		
					uintptr_t Offset = Addr + i - StartAddr;
					DetectListEntry.Offset = (UINT)Offset;
					DetectListEntry.PatternNo = PatternNo;
					AddDetectListEntry(&DetectListEntry);
					kprintf("byte pattern detect : %llx %llx\n", Addr + i, Offset);
				}
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{		
		kprintf("Exception caught in ScanMemory()");
	}
}


VOID EnumerateDriversViaPsLoadedModuleList(int type, uintptr_t* OutStartAddr, uintptr_t* OutEndAddr)
{
	kprintf("===EnumerateDriversViaPsLoadedModuleList (%d)===", type);
	if (!MmIsAddressValid(PsLoadedModuleList))
	{
		return;
	}
	PLDR_DATA_TABLE_ENTRY Entry = (PLDR_DATA_TABLE_ENTRY)PsLoadedModuleList->InLoadOrderLinks.Flink;
	
	int modCount = 0;
	uintptr_t EntryArray[500] = { 0, };

	while (Entry != PsLoadedModuleList)
	{		
		kprintf("[%d] %p %ws %p %llx ", modCount, Entry, Entry->FullDllName.Buffer, Entry->DllBase, Entry->SizeOfImage);	
		EntryArray[modCount] = (uintptr_t)Entry;
		DRIVER_LIST_ENTRY DriverListEntry = { 0, };
		DriverListEntry.DriverObject = 0;
		DriverListEntry.modBaseAddress = (uintptr_t)Entry->DllBase;
		DriverListEntry.modSize = (ULONG)Entry->SizeOfImage;
		RtlStringCchCopyW(DriverListEntry.ServiceName, MAX_PATH, L"");
		RtlStringCchCopyW(DriverListEntry.FilePath, MAX_PATH, (WCHAR*)Entry->FullDllName.Buffer);
		DriverListEntry.isHidden = FALSE;
		AddDriverListEntry(type, &DriverListEntry);

		Entry = (PLDR_DATA_TABLE_ENTRY)Entry->InLoadOrderLinks.Flink;
		modCount++;
	}
	// ����
	uintptr_t temp = 0;
	for (int i = 0; i < modCount - 1; i++) 
	{
		for (int j = i + 1; j < modCount; j++)
		{
			if (EntryArray[i] < EntryArray[j]) 
			{
				temp = EntryArray[j];
				EntryArray[j] = EntryArray[i];
				EntryArray[i] = temp;
			}
		}
	}
	kprintf("min max %p %p", EntryArray[modCount - 1], EntryArray[0]);
	
	// PAGE_SIZE ������ ������
	// ������ entry�� ã�� ���� ������ �ø���.
	// ���� ���� ��ġ�� 0xFFFFDC8A51E50C40���
	// 0x1000000�� align�ؼ� 0xffffdc8a51000000�� �����.
	*OutStartAddr = (EntryArray[modCount-1] & ~(0x1000000 - 1));
	*OutEndAddr = (EntryArray[0] & ~(0x1000000 - 1))+ 0x1000000;
	kprintf("min max %p %p", *OutStartAddr, *OutEndAddr);

}

/* ���� : �޸� Ǯ��ĵ�� �ƴ϶� LDR ��Ʈ���� ���� ���� �ּ� ~ ���� ū �ּ� ���̸� �˻��ϱ� ������
	���� ���� �ּҳ� ���� ū �ּҸ� ����� ã�� �� ���� �ȴ�.
	
	������ ��⸸ ã�´�.
*/
VOID EnumerateDriversViaMmLd(int type, uintptr_t StartAddr, uintptr_t EndAddr)
{
	kprintf("===EnumerateDriversViaMmLd (%d)===", type);
	InitDetectListEntry();

	// "MmLd"
	BYTE Pattern[4] = { 0x4d, 0x6d, 0x4c, 0x64 };
	ScanMemory(0x1938, StartAddr, EndAddr, Pattern, 4); // 0x1938�� �α��� ���ؼ�.. 0x0���� �ص� ����
	
	UINT nIndex = 0;
//	int entryCnt = 0;
	
	while (TRUE)
	{
		PDETECT_LIST_ENTRY pDetectListEntry;
		GetDetectListEntry(nIndex, &pDetectListEntry);
		if (NULL == pDetectListEntry)
		{
			break;
		}
		
		PLDR_DATA_TABLE_ENTRY pEntry = (PLDR_DATA_TABLE_ENTRY)((uintptr_t)pDetectListEntry->BaseAddress + pDetectListEntry->Offset + 0xC);
//		kprintf("PLDR_DATA_TABLE_ENTRY %p", pEntry);
		__try
		{		
			/*
				������ ��⸸ ã�´�.
			*/
			if (pEntry->InLoadOrderLinks.Blink == (PLIST_ENTRY)pEntry // chain�� ������ ���
				&& pEntry->InLoadOrderLinks.Flink == (PLIST_ENTRY)pEntry // chain�� ������ ���
				&& pEntry->InInitializationOrderLinks.Flink == NULL 	// ��ȿ�� ����� Blink�� ���� �Ϻ� ������ Flink�� �˴� null��
				&& pEntry->ObsoleteLoadCount != 0) // ��ε�� ����� ObsoleteLoadCount�� 0
			{
				DRIVER_LIST_ENTRY DriverListEntry = { 0, };
				DriverListEntry.DriverObject = 0;
				DriverListEntry.modBaseAddress = (uintptr_t)pEntry->DllBase;
				DriverListEntry.modSize = (ULONG)pEntry->SizeOfImage;
				RtlStringCchCopyW(DriverListEntry.ServiceName, MAX_PATH, L"");
				RtlStringCchCopyW(DriverListEntry.FilePath, MAX_PATH, (WCHAR*)pEntry->FullDllName.Buffer);
				DriverListEntry.isHidden = FALSE;
				AddDriverListEntry(type, &DriverListEntry);
		//		kprintf("[%d] %ws", entryCnt++, DriverListEntry.FilePath);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{			
			kprintf("Exception caught in EnumerateDriversViaMmLd()");
		}
//	NEXT:
		nIndex++;
	}
	
	FreeDetectListEntry();
}

BOOLEAN ScanKernelDriver(UINT PatternNo, PBYTE Pattern, UINT PatternSize)
{
	kprintf("----- ScanKernelDriver() %d-----", PatternSize);

	int modCount = 0;
	
	PDRIVER_LIST_ENTRY pDriverListEntry = NULL;
		
	__try
	{
		while (TRUE)
		{
			GetDriverListEntry(0, modCount, &pDriverListEntry);
			if (NULL == pDriverListEntry)
			{
				break;
			}
		//	kprintf("[%d] %p %ws %llx %x ", modCount, pDriverListEntry, pDriverListEntry->FilePath, pDriverListEntry->modBaseAddress, pDriverListEntry->modSize);
			BOOLEAN bSuccess = MmIsAddressValid((PVOID)pDriverListEntry->modBaseAddress);
			if (bSuccess == FALSE)
			{
				kprintf("cannot access memory\n");
				goto NEXT;
			}
			ScanMemory(PatternNo, pDriverListEntry->modBaseAddress, pDriverListEntry->modBaseAddress + pDriverListEntry->modSize, Pattern, PatternSize);
		NEXT:
			modCount++;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{	
		kprintf("Exception caught in ScanKernelDriver()");
	}
	if (0 >= modCount)
	{
		kprintf("first init");
		goto EXIT_ERROR;
	}

	
	UINT nIndex = 0;	
	
	while (TRUE)
	{
		PDETECT_LIST_ENTRY pDetectListEntry;
		GetDetectListEntry(nIndex, &pDetectListEntry);
		if (NULL == pDetectListEntry)
		{
			break;
		}
		kprintf("[%d] %llx %llx ", nIndex, pDetectListEntry->Offset , pDetectListEntry->BaseAddress);

		nIndex++;
	}
	
EXIT_ERROR:

	return TRUE;
}

BOOLEAN ScanKernelDriverViaAPI()
{
	NTSTATUS ntSuccess = STATUS_SUCCESS;
	BOOLEAN bSuccess = FALSE;

	// ��Ȯ�� ����� ��� ���� ���
	ULONG neededSize = 0;
	ZwQuerySystemInformation(SystemModuleInformation, &neededSize, 0, &neededSize);

	PSYSTEM_MODULE_INFORMATION pModuleList = (PSYSTEM_MODULE_INFORMATION)ExAllocatePoolWithTag(PagedPool, neededSize, POOL_TAG);
	if (!pModuleList)
	{
		kprintf("error.1\n");
		goto EXIT_ERROR;
	}
	// Ŀ�� ����̹� ����Ʈ ���� ȹ��
	ntSuccess = ZwQuerySystemInformation(SystemModuleInformation, pModuleList, neededSize, 0);
	if (!NT_SUCCESS(ntSuccess))
	{
		kprintf("error.2\n");
		goto EXIT_ERROR;
	}

	uintptr_t modBaseAddress = 0;
	ULONG modSize = 0;
	kprintf("mod Count : %d", (int)pModuleList->ulModuleCount);
	// Ŀ�� ����̹� ����
	for (int i = 0; i < (int)pModuleList->ulModuleCount; i++)
	{
		SYSTEM_MODULE mod = pModuleList->Modules[i];

		modBaseAddress = (uintptr_t)(pModuleList->Modules[i].Base);
		modSize = (ULONG)(pModuleList->Modules[i].Size);
		kprintf("Path : %s Base : %llx Size : %x\n", mod.ImageName, modBaseAddress, modSize);

		// system32\drivers\volmgrx.sys ���� �ƿ� ������ �Ұ����� ����� �ɷ���
		bSuccess = MmIsAddressValid((PVOID)modBaseAddress);
		if (bSuccess == FALSE)
		{
			//			kprintf("Skip.1\n");
			continue;
		}
		
		PIMAGE_NT_HEADERS pHdr = (PIMAGE_NT_HEADERS)RtlImageNtHeader((PVOID)mod.Base);
		if (!pHdr)
		{
			kprintf("Skip.2\n");
			continue;
		}

		// ���� üũ (��ĵ���� �ʿ������� ������ Ȯ�ο뵵)
		PIMAGE_SECTION_HEADER pFirstSection = (PIMAGE_SECTION_HEADER)((uintptr_t)&pHdr->FileHeader + pHdr->FileHeader.SizeOfOptionalHeader + sizeof(IMAGE_FILE_HEADER));
		for (PIMAGE_SECTION_HEADER pSection = pFirstSection; pSection < pFirstSection + pHdr->FileHeader.NumberOfSections; pSection++)
		{
			kprintf("%-10s\t%-8x\t%-6x\n", pSection->Name, pSection->VirtualAddress, pSection->SizeOfRawData);
		}
		

	//	ScanMemory(modBaseAddress, modBaseAddress + modSize);

	}
EXIT_ERROR:
	if (pModuleList) ExFreePoolWithTag(pModuleList, POOL_TAG);

	return TRUE;
}


VOID GetPsLoadedModuleList()
{
	UNICODE_STRING name = RTL_CONSTANT_STRING(L"PsLoadedModuleList");
	PsLoadedModuleList = MmGetSystemRoutineAddress(&name);

}

/*
* ������Ʈ �̸��� enumerate�ϰ� �ش� �̸����� DriverObject�� ���Ѵ�. 
* 
* ���� ����Ʈ
* https://github.com/swatkat/arkitlib/blob/master/ARKitDrv/Drv.c 
* https://github.com/ApexLegendsUC/anti-cheat-emulator/blob/master/Source.cpp
* 
*/
VOID EnumerateDriversViaObjectNameInside(int type, IN PUNICODE_STRING pObjectName)
{
	kprintf("===EnumerateDriversViaObjectNameInside (%wZ)===", pObjectName);
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	OBJECT_ATTRIBUTES objectAttributes;
	HANDLE hDirectoryHandle = NULL;
	ULONG Context = 0;
	ULONG ReturnedLength;
	POBJECT_DIRECTORY_INFORMATION pObjectDirectoryInformation = NULL;
		
	InitializeObjectAttributes(&objectAttributes, pObjectName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
	status = ZwOpenDirectoryObject(&hDirectoryHandle, DIRECTORY_QUERY /*GENERIC_READ | SYNCHRONIZE*/, &objectAttributes);
	if (!NT_SUCCESS(status))
	{
		kprintf("ZwOpenDirectoryObject occurs an error. %x\n", status);
		return;
	}

	// ��Ȯ�� ����ü ����� �𸣴ϱ� �˳��ϰ� ������ ũ�⸸ŭ �Ҵ��Ѵ�.
	pObjectDirectoryInformation = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, POOL_TAG);

	RtlZeroMemory(pObjectDirectoryInformation, PAGE_SIZE);
	//int count = 0;
	while(TRUE)
	{			
		status = ZwQueryDirectoryObject(hDirectoryHandle, pObjectDirectoryInformation, PAGE_SIZE, TRUE, FALSE, &Context, &ReturnedLength);
		if (status == STATUS_NO_MORE_ENTRIES)
		{
			// success
			break;
		}
		else if (status != STATUS_SUCCESS)
		{
			kprintf("ZwQueryDirectoryObject occurs an error. %x", status);
			break;
		}
				
//		kprintf("[%d] %wZ %wZ ", count++, pObjectDirectoryInformation->Name, pObjectDirectoryInformation->TypeName);
		
		// Driver ������ ����. �������� skip. 
		UNICODE_STRING Driver = RTL_CONSTANT_STRING(L"Driver");
		if (!RtlEqualUnicodeString(&Driver, &pObjectDirectoryInformation->TypeName, TRUE))
		{
//			kprintf("non driver skip");
			continue;
		}

		UNICODE_STRING FullName;
		FullName.MaximumLength = MAX_PATH * 2; // ���ڿ� ���� �ִ� ������
		WCHAR szFullName[MAX_PATH] = { 0, };
		FullName.Buffer = szFullName;
			
		RtlCopyUnicodeString(&FullName, pObjectName);
		RtlAppendUnicodeToString(&FullName, L"\\");
		RtlAppendUnicodeStringToString(&FullName, &pObjectDirectoryInformation->Name);
		
	//	kprintf("Name %wZ\n", &FullName);
		
		//PVOID pObject = NULL;
		PDRIVER_OBJECT pDriverObject = NULL;
			
		// �̸����� DriverObject ���ϱ�
		NTSTATUS retVal = ObReferenceObjectByName(&FullName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, &pDriverObject);
		if (STATUS_SUCCESS != retVal)
		{
			kprintf("ObReferenceObjectByName occurs an error. %x", retVal);
			continue;

		}
	//	kprintf("pDriverObject : %p", pDriverObject);

		if (MmIsAddressValid(pDriverObject))
		{
			PLDR_DATA_TABLE_ENTRY pModEntry = (PLDR_DATA_TABLE_ENTRY)pDriverObject->DriverSection;
	//		kprintf("DriverSection %p", pModEntry);
			if (MmIsAddressValid(pModEntry))
			{
				DRIVER_LIST_ENTRY DriverListEntry = { 0, };
				DriverListEntry.DriverObject = (uintptr_t)pDriverObject;
				DriverListEntry.modBaseAddress = (uintptr_t)pModEntry->DllBase;
				DriverListEntry.modSize = (ULONG)pModEntry->SizeOfImage;
				RtlStringCchCopyW(DriverListEntry.ServiceName, MAX_PATH, pObjectDirectoryInformation->Name.Buffer);
				RtlStringCchCopyW(DriverListEntry.FilePath, MAX_PATH, (WCHAR*)pModEntry->FullDllName.Buffer);
				DriverListEntry.isHidden = FALSE;
				AddDriverListEntry(type, &DriverListEntry);

	//			kprintf("final get : %ws %p", pModEntry->BaseDllName.Buffer, pModEntry->DllBase);
			}
			else
			{
	//			kprintf("DriverSection is not valid.");
			}
		}
		else
		{
	//		kprintf("pDriverObject is not vaild.");
		}
	
		ObDereferenceObject(pDriverObject);	
	}

	if (pObjectDirectoryInformation)
	{
		ExFreePoolWithTag(pObjectDirectoryInformation, POOL_TAG);
	}

	if (hDirectoryHandle) ZwClose(hDirectoryHandle);

}


VOID EnumerateDriversViaObjectName(int type)
{	
	UNICODE_STRING directory = RTL_CONSTANT_STRING(L"\\Driver");
	UNICODE_STRING FileSystem = RTL_CONSTANT_STRING(L"\\FileSystem");

	EnumerateDriversViaObjectNameInside(type, &directory);
	EnumerateDriversViaObjectNameInside(type, &FileSystem);
}


void EnumerateDrivers()
{
	UINT nIndex0 = 0;
	UINT nIndex1 = 0;
	UINT nIndex2 = 0;
	InitDriverListEntry(0);
	InitDriverListEntry(1);
	InitDriverListEntry(2);
	uintptr_t ldrEntryStartAddr = 0;
	uintptr_t ldrEntryEndAddr = 0;
	EnumerateDriversViaPsLoadedModuleList(0, &ldrEntryStartAddr, &ldrEntryEndAddr);
	EnumerateDriversViaObjectName(1);
	EnumerateDriversViaMmLd(2, ldrEntryStartAddr, ldrEntryEndAddr);
	
	PDRIVER_LIST_ENTRY pDriverListEntryObjectName = NULL;
	PDRIVER_LIST_ENTRY pDriverListEntry = NULL;
	PDRIVER_LIST_ENTRY pDriverListEntryMmLd = NULL;

	BOOLEAN isHidden = FALSE;

	
	pDriverListEntry = NULL;
	nIndex0 = 0;
	while (TRUE)
	{
		GetDriverListEntry(2, nIndex2, &pDriverListEntryMmLd);
		if (NULL == pDriverListEntryMmLd)
		{
			break;
		}

		pDriverListEntryMmLd->isHidden = TRUE;
		AddDriverListEntry(0, pDriverListEntryMmLd);


		nIndex2++;
	}
	
	pDriverListEntry = NULL;
	nIndex0 = 0;
	while (TRUE)
	{
		GetDriverListEntry(1, nIndex1, &pDriverListEntryObjectName);
		if (NULL == pDriverListEntryObjectName)
		{
			break;
		}
	
		nIndex0 = 0;
		pDriverListEntry = NULL;
		isHidden = TRUE;
		while (TRUE)
		{			
			
			GetDriverListEntry(0, nIndex0, &pDriverListEntry);
			if (NULL == pDriverListEntry)
			{
				break;
			}
			if (0 == wcscmp(pDriverListEntryObjectName->FilePath, pDriverListEntry->FilePath))
			{				
				isHidden = FALSE;
				// ���� �̸� ä���ֱ�
				RtlStringCchCopyW(pDriverListEntry->ServiceName, MAX_PATH, pDriverListEntryObjectName->ServiceName);
				// ����̹� ������Ʈ ä���ֱ�
				pDriverListEntry->DriverObject = pDriverListEntryObjectName->DriverObject;				
			}	
	
			nIndex0++;
		}

		if (isHidden == TRUE) 
		{
		//	kprintf("Hidden Driver:");		
			pDriverListEntryObjectName->isHidden = TRUE;
			AddDriverListEntry(0, pDriverListEntryObjectName);			
		}
		
		nIndex1++;
	}


	/*

	pDriverListEntry = NULL;
	nIndex0 = 0;
	while (TRUE)
	{
		GetDriverListEntry(0, nIndex0, &pDriverListEntry);
		if (NULL == pDriverListEntry)
		{
			break;
		}
	//	kprintf("[%d] %ws Base Address : %llx", nIndex0, pDriverListEntry->FilePath, pDriverListEntry->modBaseAddress);
	//	kprintf("Size : %x Service : %ws DriverObject : %llx", pDriverListEntry->modSize, pDriverListEntry->ServiceName, pDriverListEntry->DriverObject);
	//	kprintf("hidden : %x", pDriverListEntry->isHidden);
		nIndex0++;
	}
	*/

	FreeDriverListEntry(1);
	FreeDriverListEntry(2);
}