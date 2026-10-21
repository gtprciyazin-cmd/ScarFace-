#pragma once
#include <functional>
#include <string>

#include "../command.hpp"

namespace command {
// "/<name>": runs one action, ignoring any arguments.
class ActionCommand final : public ICommand {
public:
    ActionCommand(std::string name, std::string description, std::function<void()> action)
        : name_{ std::move(name) }
        , description_{ std::move(description) }
        , action_{ std::move(action) }
    {

    }

    [[nodiscard]] std::string_view name() const override { return name_; }
    [[nodiscard]] std::string description() const override { return description_; }

    Result execute(const Context&) override
    {
        action_();
        return Result::Success;
    }

private:
    std::string name_;
    std::string description_;
    std::function<void()> action_;
};
}
