Обе программы используют одинаковую структуру, которая размещается в памяти дочернего процесса.
struct SharedData {
    char message[256];        // Буфер для передачи текста сообщения (256 байт).
    volatile LONG parentDone; // Флаг-синхронизатор (1 - родитель записал сообщение, 0 - нет).
    volatile LONG childDone;  // Флаг-синхронизатор (1 - ребенок записал ответ, 0 - нет).
};
Ключевое слово volatile запрещает компилятору кэшировать значения этих переменных в регистрах процессора, заставляя каждый раз читать их напрямую из памяти. Это критически важно для синхронизации.



Подробный разбор ParentApp (Родительский процесс)
1. Инициализация адреса и структур
// Задаем жесткий адрес памяти (0x00900000), по которому будет выделена память.
// Жесткий адрес нужен, чтобы передать его ребенку через командную строку ДО запуска.
LPVOID remoteBlock = (LPVOID)0x00900000;
SIZE_T blockSize = sizeof(SharedData);

STARTUPINFOA si;
PROCESS_INFORMATION pi;
ZeroMemory(&si, sizeof(si)); // Обнуляем структуры перед использованием
si.cb = sizeof(si);
ZeroMemory(&pi, sizeof(pi));


2. Формирование командной строки и создание процесса
char cmdLine[256];
// Формируем строку запуска: "ChildApp.exe 0000000000900000"
// %p автоматически форматирует указатель в hex-формат.
sprintf_s(cmdLine, "\"ChildApp.exe\" %p", remoteBlock);

// CREATE_SUSPENDED создает процесс, но останавливает его основной поток (пауза).
// Это нужно, чтобы родитель успел выделить память до того, как ребенок начнет работу.
if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 
                    CREATE_SUSPENDED, NULL, NULL, &si, &pi))
{
    cout << "CreateProcess failed. Error: " << GetLastError() << endl;
    return 1;
}



3. Выделение памяти в ДРУГОМ процессе
// VirtualAllocEx выделяет память в адресном пространстве процесса pi.hProcess (ребенка).
// Мы просим выделить её по конкретному адресу remoteBlock.
LPVOID lpChildMem = VirtualAllocEx(
    pi.hProcess,                          // Дескриптор дочернего процесса
    remoteBlock,                          // Желаемый адрес
    blockSize,                            // Размер
    MEM_COMMIT | MEM_RESERVE,             // Резервируем и сразу выделяем физическую память
    PAGE_READWRITE);                      // Разрешаем чтение и запись

   
4. Подготовка и отправка сообщения
SharedData localData;
memset(&localData, 0, sizeof(localData)); // Обнуляем локальную структуру (чтобы не было мусора)
strcpy_s(localData.message, "Hello from Parent!"); // Записываем текст
localData.parentDone = 1; // Ставим флаг, что данные готовы к чтению

SIZE_T bytesWritten = 0;
// WriteProcessMemory копирует данные из локальной структуры localData 
// в память дочернего процесса по адресу lpChildMem.
WriteProcessMemory(pi.hProcess, lpChildMem, &localData, sizeof(localData), &bytesWritten);


5. Запуск ребенка и ожидание ответа
// ResumeThread снимает процесс с паузы, и ребенок начинает выполнение кода.
ResumeThread(pi.hThread);

bool gotReply = false;
while (!gotReply)
{
    // ReadProcessMemory читает данные ИЗ памяти ребенка (lpChildMem) 
    // В нашу локальную структуру (localData).
    ReadProcessMemory(pi.hProcess, lpChildMem, &localData, sizeof(localData), &bytesWritten);
    
    // Проверяем флаг. Если ребенок его поставил в 1, значит он записал ответ.
    if (localData.childDone == 1)
    {
        cout << "Parent: Received reply: " << localData.message << endl;
        gotReply = true; // Выходим из цикла
    }
    Sleep(100); // Небольшая задержка, чтобы не грузить процессор на 100%
}


6. Очистка ресурсов
// Освобождаем виртуальную память в процессе ребенка
VirtualFreeEx(pi.hProcess, lpChildMem, 0, MEM_RELEASE);
// Ждем завершения ребенка (максимум 1 секунду)
WaitForSingleObject(pi.hProcess, 1000);
// Закрываем дескрипторы (утечка хэндлов недопустима в ОС Windows)
CloseHandle(pi.hProcess);
CloseHandle(pi.hThread);





Подробный разбор ChildApp (Дочерний процесс)
1. Получение адреса из командной строки
// Проверяем, что аргументы были переданы (argc >= 2)
if (argc < 2) {
    cout << "Child: No address passed!" << endl;
    return 1;
}

// Извлекаем адрес из первого аргумента (argv[1]). 
// strtoull конвертирует строку (в hex формате) в 64-битное целое число,
// которое затем приводится к указателю LPVOID.
LPVOID remoteBlock = (LPVOID)strtoull(argv[1], NULL, 16);


2. Открытие собственного процесса
// Чтобы использовать ReadProcessMemory/WriteProcessMemory (как требует задание), 
// ребенку нужен дескриптор процесса. Он открывает сам себя по своему PID.
HANDLE hSelf = OpenProcess(
    PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, 
    FALSE, 
    GetCurrentProcessId());

   
3. Ожидание сообщения от родителя
bool messageReceived = false;
while (!messageReceived)
{
    // Читаем память по переданному адресу в локальную структуру
    ReadProcessMemory(hSelf, remoteBlock, &localData, sizeof(localData), &bytesRead);
    
    // Если родитель записал сообщение и поставил флаг parentDone = 1
    if (localData.parentDone == 1)
    {
        cout << "Child: Received message: " << localData.message << endl;
        messageReceived = true; // Выходим из цикла
    }
    Sleep(100); // Задержка для разгрузки CPU
}

4. Формирование и отправка ответа
// Перезаписываем поле message нашим ответом
strcpy_s(localData.message, "Hello from Child! I got your message.");

// Ставим флаг childDone = 1, сигнализируя родителю, что ответ готов
localData.childDone = 1;

// Записываем обновленную структуру обратно в память
WriteProcessMemory(hSelf, remoteBlock, &localData, sizeof(localData), &bytesRead);






 Как это работает (Логика синхронизации)
Родитель создает ребенка на паузе (CREATE_SUSPENDED).

Родитель выделяет память (VirtualAllocEx) в ребенке по адресу 0x00900000.

Родитель кладет туда структуру с сообщением и ставит parentDone = 1.

Родитель запускает ребенка (ResumeThread).

Ребенок просыпается, читает свой argv[1], видит там 0x00900000.

Ребенок в цикле читает память по этому адресу, пока не увидит parentDone == 1.

Ребенок формирует ответ, ставит childDone = 1 и записывает обратно.

Родитель в своем цикле ждет, пока childDone не станет равен 1.

Как только флаг изменился — оба процесса завершают работу, родитель освобождает память (VirtualFreeEx).

Синхронизация гарантируется тем, что процессы не читают данные, пока не установлен соответствующий флаг (сначала данные, потом флаг). А поскольку тип LONG (4 байта) выровнен в памяти, его запись/чтение происходит атомарно на уровне процессора.
