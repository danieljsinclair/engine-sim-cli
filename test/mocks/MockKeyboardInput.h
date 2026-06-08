#ifndef MOCK_KEYBOARD_INPUT_H
#define MOCK_KEYBOARD_INPUT_H

#include "input/IKeyboardInput.h"
#include <queue>

class MockKeyboardInput : public IKeyboardInput {
public:
    void enqueue(int key) { keys_.push(key); }
    int getKey() override {
        if (keys_.empty()) return -1;
        int k = keys_.front();
        keys_.pop();
        return k;
    }
private:
    std::queue<int> keys_;
};

#endif
