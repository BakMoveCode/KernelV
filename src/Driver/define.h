#pragma once

#define kprintf(...) KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, __VA_ARGS__)) // DbgPrint()�� ����� ��� �ε��� ���� �ʾҴ� ���� �߻�
#define POOL_TAG 'coTr'

