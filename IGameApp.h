#pragma once

class IGameApp {
public:
    virtual void Initialize() = 0;
    virtual void Update(float dt) = 0;
    virtual void Render() = 0;
    virtual ~IGameApp() = default;
};
