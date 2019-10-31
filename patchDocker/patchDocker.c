#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#define printerr(e) (fprintf(stderr, e))

/*
* Shoutout to https://github.com/giniyat202 for the original idea for this
*
* This patches in-memory vmcompute.dll in the dockerd process
* to fix arguments to MountVhd that in this version of vmcompute.dll throws an
* "incorrect function" error from hcsshim like this:
* hcsshim::PrepareLayer failed in Win32: Incorrect function.
* Patching this in dockerd fixes the problem leading me to believe
* Microsoft can fix this DLL to add the missing arguments, or docker
* can fix this by changing how the function it called.
*
* This only happens on certain Windows 10 1903 machines, not always
* reproducible - this does not happen in Server 2016.
*
* Original code modified to build with Visual Studio VC++ compiler and work with
* 64 bit machines.
*
* Call like this: patchDocker.exe p (ps dockerd).id
/*

/*
$ ==>            | 45 33 C0                | xor r8d,r8d                          |
$+3              | BA 76 03 00 00          | mov edx,376                          |
$+8              | 48 8B 4D A8             | mov rcx,qword ptr ss:[rbp-58]        |
$+C              | E8 22 99 02 00          | call vmcompute.MountVhd              |
$+11             | 90                      | nop                                  |
$+12             | 48 8B 4D A8             | mov rcx,qword ptr ss:[rbp-58]        |
*/
#define check_integrity_offset (0x25C79)
#define patch_offset (check_integrity_offset + 4)
/*
static const char check_integrity_bytes[] = {
	'\x45', '\x33', '\xC0', '\xBA', '\x76', '\x03', '\x00', '\x00',
	'\x48', '\x8B', '\x4D', '\xA8', '\xE8', '\x22', '\x99', '\x02',
	'\x00', '\x90', '\x48', '\x8B', '\x4D', '\xA8'
};
*/
static const char check_integrity_bytes[] = {
	'\x45', '\x33', '\xC0', '\xBA', '\x76', '\x03', '\x00', '\x00',
	'\x48', '\x8B', '\x4D', '\xA8', '\xE8', '\x02', '\x9A', '\x02',
	'\x00', '\x90', '\x48', '\x8B', '\x4D', '\xA8'
};

#define check_integrity_byte_count (sizeof(check_integrity_bytes) / sizeof(char))


void usage(const char *program)
{
	printf("usage: %s <p|u> <pid>", program);
}

BOOL check_integrity()
{
	BOOL result, res;
	HMODULE vmcompute;
	LPCVOID address;
	char buffer[check_integrity_byte_count];
	size_t bytesread;

	result = FALSE;
	vmcompute = LoadLibrary(TEXT("vmcompute.dll"));
	if (vmcompute == NULL)
	{
		printerr("LoadLibrary failed\n");
	}
	else
	{
		address = (LPCVOID)((INT_PTR)vmcompute + check_integrity_offset);
		res = ReadProcessMemory(GetCurrentProcess(), address, buffer, sizeof(buffer), &bytesread);
		if (!res || bytesread != sizeof(buffer))
		{
			printerr("ReadProcessMemory failed\n");
		}
		else if (memcmp(buffer, check_integrity_bytes, sizeof(buffer)) != 0)
		{
			printerr("check_integrity failed\n");
		}
		else
		{
			result = TRUE;
		}
		FreeLibrary(vmcompute);
	}
	return result;
}


BYTE *findvmcompute(int pid)
{
	BYTE *result;
	HANDLE snapshot;
	MODULEENTRY32 entry;
	BOOL res;

	result = NULL;
	snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
	if (snapshot == INVALID_HANDLE_VALUE)
	{
		printerr("CreateToolhelp32Snapshot failed\n");
	}
	else
	{
		entry.dwSize = sizeof(entry);
		res = Module32First(snapshot, &entry);
		if (!res)
		{
			printerr("Module32First failed\n");
		}
		else
		{
			do
			{
				if (_wcsicmp(entry.szModule, TEXT("vmcompute")) == 0 || _wcsicmp(entry.szModule, TEXT("vmcompute.dll")) == 0)
				{
					result = entry.modBaseAddr;
					break;
				}
			} while (Module32Next(snapshot, &entry));

			if (result == NULL)
			{
				printerr("cannot find vmcompute.dll in the target process\n");
			}
		}
		CloseHandle(snapshot);
	}
	return result;
}

BOOL writemem(int pid, BYTE *vmcomputeaddr, char mode)
{
	BOOL result, res;
	HANDLE process;
	DWORD buffer;
	size_t byteswritten;

	result = FALSE;
	buffer = mode == 'p' ? 0x36 : 0x376;
	process = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid);
	if (process == NULL)
	{
		printerr("OpenProcess failed\n");
	}
	else
	{
		res = WriteProcessMemory(process, vmcomputeaddr + patch_offset, &buffer, sizeof(buffer), &byteswritten);
		if (!res || byteswritten != sizeof(buffer))
		{
			printerr("WriteProcessMemory failed\n");
		}
		else
		{
			result = TRUE;
		}
		CloseHandle(process);
	}
	return result;
}

int main(int argc, char **argv)
{
	char mode;
	int pid;
	BYTE *vmcomputeaddr;
	BOOL bresult;

	if (argc != 3)
	{
		usage(argv[0]);
		return 0;
	}

	mode = tolower(argv[1][0]);
	pid = atoi(argv[2]);

	if (pid <= 0 || (mode != 'p' && mode != 'u'))
	{
		usage(argv[0]);
		return 0;
	}

	bresult = check_integrity();

	if (bresult)
	{
		vmcomputeaddr = findvmcompute(pid);

		if (vmcomputeaddr != NULL)
		{
			bresult = writemem(pid, vmcomputeaddr, mode);
			if (bresult)
			{
				printf("patch applied\nonly required to build images, will be unloaded upon restart of docker process");
			}
		}
	}

	return 0;
}