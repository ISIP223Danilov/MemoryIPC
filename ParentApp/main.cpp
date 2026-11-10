#include <windows.h>
#include <iostream>
#include <cstring>

using namespace std;

// Структура для обмена данными
struct SharedData {
    char message[256];        // Буфер для сообщения
    volatile LONG parentDone; // 1 - родитель записал, 0 - еще нет
    volatile LONG childDone;  // 1 - ребенок записал ответ, 0 - еще нет
};

int main()
{
    // 1. Выделяем память в дочернем процессе по конкретному адресу (как в лекции)
    LPVOID remoteBlock = (LPVOID)0x00900000;
    SIZE_T blockSize = sizeof(SharedData);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // Создаем дочерний процесс приостановленным (CREATE_SUSPENDED)
    // Передаем в командной строке: "ChildApp.exe <адрес_блока>"
    char cmdLine[256];
    sprintf_s(cmdLine, "\"ChildApp.exe\" %p", remoteBlock);

    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE,
        CREATE_SUSPENDED, NULL, NULL, &si, &pi))
    {
        cout << "CreateProcess failed. Error: " << GetLastError() << endl;
        system("pause");
        return 1;
    }

    // Распределяем виртуальную память в адресном пространстве ДОЧЕРНЕГО процесса
    // по конкретному адресу
    LPVOID lpChildMem = VirtualAllocEx(
        pi.hProcess,
        remoteBlock,
        blockSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);

    if (!lpChildMem)
    {
        cout << "VirtualAllocEx failed. Error: " << GetLastError() << endl;
        TerminateProcess(pi.hProcess, 1);
        system("pause");
        return 1;
    }

    cout << "Virtual memory allocated in child process at: " << lpChildMem << endl;

    // Инициализируем структуру и обнуляем её
    SharedData localData;
    memset(&localData, 0, sizeof(localData)); // Обязательно обнуляем перед записью!
    strcpy_s(localData.message, "Hello from Parent!");
    localData.parentDone = 1; // Ставим флаг, что родитель записал сообщение

    SIZE_T bytesWritten = 0;

    // Пишем данные в память ребенка
    WriteProcessMemory(pi.hProcess, lpChildMem, &localData, sizeof(localData), &bytesWritten);

    cout << "Parent: Message sent. Waiting for reply..." << endl;

    // Запускаем дочерний процесс (снимаем с паузы)
    ResumeThread(pi.hThread);

    // Ждем ответа от ребенка
    bool gotReply = false;
    while (!gotReply)
    {
        // ВАЖНО: читаем данные ИЗ ПАМЯТИ ребенка В нашу локальную структуру localData
        ReadProcessMemory(pi.hProcess, lpChildMem, &localData, sizeof(localData), &bytesWritten);

        // Теперь проверяем то, что только что прочитали
        if (localData.childDone == 1)
        {
            cout << "Parent: Received reply: " << localData.message << endl;
            gotReply = true;
        }
        Sleep(100);
    }

    // Освобождаем память и закрываем дескрипторы
    VirtualFreeEx(pi.hProcess, lpChildMem, 0, MEM_RELEASE);
    WaitForSingleObject(pi.hProcess, 1000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    cout << "\nParent process finished successfully." << endl;
    system("pause");
    return 0;
}//