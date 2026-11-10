#include <windows.h>
#include <iostream>
#include <cstring>
#include <cstdlib>

using namespace std;

// Такая же структура, как в родителе
struct SharedData {
    char message[256];
    volatile LONG parentDone;
    volatile LONG childDone;
};

int main(int argc, char* argv[])
{
    // Проверяем, что адрес передан
    if (argc < 2) {
        cout << "Child: No address passed!" << endl;
        system("pause");
        return 1;
    }

    // Получаем адрес из командной строки
    LPVOID remoteBlock = (LPVOID)strtoull(argv[1], NULL, 16);
    cout << "Child: Received memory address: " << remoteBlock << endl;

    // Открываем СВОЙ процесс, чтобы работать через ReadProcessMemory/WriteProcessMemory
    HANDLE hSelf = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, GetCurrentProcessId());
    if (!hSelf) {
        cout << "Child: OpenProcess failed. Error: " << GetLastError() << endl;
        system("pause");
        return 1;
    }

    SharedData localData;
    SIZE_T bytesRead = 0;
    bool messageReceived = false;

    // Ждем, пока родитель запишет сообщение и установит parentDone = 1
    while (!messageReceived)
    {
        // Читаем память в локальную структуру
        ReadProcessMemory(hSelf, remoteBlock, &localData, sizeof(localData), &bytesRead);

        if (localData.parentDone == 1)
        {
            cout << "Child: Received message: " << localData.message << endl;
            messageReceived = true;
        }
        Sleep(100);
    }

    // Формируем ответ
    strcpy_s(localData.message, "Hello from Child! I got your message.");

    // Синхронизация: ставим флаг childDone = 1
    localData.childDone = 1;

    // Записываем ответ обратно в память
    WriteProcessMemory(hSelf, remoteBlock, &localData, sizeof(localData), &bytesRead);

    cout << "Child: Reply sent." << endl;

    CloseHandle(hSelf);

    // Пауза, чтобы вы успели прочитать вывод в консоли ребенка
    system("pause");
    return 0;
}