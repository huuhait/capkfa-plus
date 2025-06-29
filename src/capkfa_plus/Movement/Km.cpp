#include "Km.h"

Km::Km(std::shared_ptr<CommanderClient> commanderClient): commanderClient_(commanderClient) {
    if (!commanderClient_) {
        throw std::runtime_error("Failed to create Km");
    }
}

void Km::Move(int16_t x, int16_t y) {
    commanderClient_->Move(x, y);
}

void Km::Click() {
    commanderClient_->Click();
}

std::string Km::Version() {
    return commanderClient_->Version();
}
