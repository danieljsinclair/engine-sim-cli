#ifndef IKEYBOARD_INPUT_H
#define IKEYBOARD_INPUT_H

class IKeyboardInput {
public:
    virtual ~IKeyboardInput() = default;
    virtual int getKey() = 0;
};

#endif
