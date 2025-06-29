#include <iostream>
#include <conio.h>
#include "HWIDTool.h"

int main()
{
    auto hwid = HWIDTool::GetHWID();
    std::cout << "HWID: " << hwid << std::endl;
    std::cout << "Press any key to continue...";
    _getch();
    return 0;
}
