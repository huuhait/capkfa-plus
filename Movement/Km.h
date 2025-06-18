#ifndef KM_H
#define KM_H


#include <memory>

#include "CommanderClient.h"

class Km {
public:
    Km(std::shared_ptr<CommanderClient> commanderClient);

    void Move(int16_t x, int16_t y);
    void Click();
    std::string Version();

private:
    std::shared_ptr<CommanderClient> commanderClient_;
};

#endif
