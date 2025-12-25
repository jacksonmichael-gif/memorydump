#include <iostream>
#include <Windows.h>
#include <vector>
#include <map>
using namespace std;

void read(HANDLE hprocess) {
	static vector<uintptr_t> foundAddresses;
	static int searchednumber = 0;
	int sousa;
	while(1){
		size_t size;
		int searchnumber;
		uintptr_t addr = 0;
		bool found = false;
		cout << "1:search int 2:different 3:different and is n 4:exit" << endl;
		cin >> sousa;
		cout << "enter search number:";
		cin >> searchnumber;
		if(sousa == 4) {
			return;
		}
		MEMORY_BASIC_INFORMATION mbi;
		switch(sousa) {
			case 1:
				while (VirtualQueryEx(hprocess, (LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
    				if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_NOACCESS)) {
						BYTE* buffer = new BYTE[mbi.RegionSize];
						ReadProcessMemory(hprocess, (LPCVOID)addr, buffer, mbi.RegionSize, NULL);
						for(int i = 0; i < mbi.RegionSize - sizeof(int); i++) {
							int pInt = *(int*)(buffer + i);
							if(pInt == searchnumber) {
								cout << "found at address: " << hex << (addr + i) << dec << endl;
								found = true;
								foundAddresses.push_back(addr + i);
							}
						}
					delete[] buffer;
    				}
    				addr += mbi.RegionSize;
				}
				searchednumber = searchnumber;
				break;
			case 2:
				for(auto iter = foundAddresses.begin(); iter != foundAddresses.end();) {
					int value;
					//printf("\rchecking address: %p\n", (LPCVOID)(*iter));
					ReadProcessMemory(hprocess, (LPCVOID)(*iter), &value, sizeof(int), NULL);
					if(value != searchednumber) {
						cout << "address that changed: " << hex << (*iter) << dec << endl;
						cout << searchednumber << "=>" << value << endl;
						iter = foundAddresses.erase(iter);
					}else {
						iter++;
					}
				}
				break;
			case 3:
				for(auto iter = foundAddresses.begin(); iter != foundAddresses.end();) {
					int value;
					//printf("\rchecking address: %p\n", (LPCVOID)(*iter));
					ReadProcessMemory(hprocess, (LPCVOID)(*iter), &value, sizeof(int), NULL);
					if(value == searchnumber) {
						cout << "\raddress still valid: " << hex << (*iter) << dec << endl;
						iter++;
					}else {
						iter = foundAddresses.erase(iter);
					}
				}
				break;
			default:
				printf("invalid option/n");
				
		}
	}
}
void write(HANDLE hprocess) {
	uintptr_t address;
	int newvalue;
	cout << "enter address to write to (hex):";
	cin >> hex >> address >> dec;
	cout << "enter new int value:";
	cin >> newvalue;
	SIZE_T bytesWritten;
	if(WriteProcessMemory(hprocess, (LPVOID)address, &newvalue, sizeof(int), &bytesWritten)) {
		cout << "wrote " << bytesWritten << " bytes to address " << hex << address << dec << endl;
	}else {
		cout << "failed to write memory" << endl;
	}
	return;
}
int main() {
	HANDLE hprocess;
	int PID;
	cout << "enter target process ID:";
	cin >> PID;
	hprocess = OpenProcess(
    PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION,
    FALSE,
    PID
	);
	if (hprocess == NULL) {
		printf("failed to open process\n");
		return 1;
	}else {
		TCHAR name[MAX_PATH];
		DWORD size = MAX_PATH;
		QueryFullProcessImageName(hprocess, 0, name, &size);
		wcout << L"opened process: " << name << L" (PID: " << PID << L")" << endl;
	}
	vector<uintptr_t> foundAddresses;
	int searchednumber = 0;
	while(1) {
		int sousa;
		cout << "1:read memory 2:write memory 3:exit" << endl;
		cin >> sousa;
		if(sousa == 3) {
			break;
		}
		switch(sousa) {
			case 1:
				read(hprocess);
				break;
			case 2:
				write(hprocess);
				break;
			default:
				printf("invalid option/n");
		}
	}
	CloseHandle(hprocess);
	return 0;
}