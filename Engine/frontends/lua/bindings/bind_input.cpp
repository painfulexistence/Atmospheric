#include "../lua_application.hpp"

void BindInputAPI(sol::state& lua, Input* input)
{
    sol::table atmos = lua["atmos"];
    sol::table inp = atmos.create("input");

    // Key state queries
    inp["isKeyDown"] = [input](int key) {
        return input->IsKeyDown(static_cast<Key>(key));
    };

    inp["isKeyUp"] = [input](int key) {
        return input->IsKeyUp(static_cast<Key>(key));
    };

    inp["isKeyPressed"] = [input](int key) {
        return input->IsKeyPressed(static_cast<Key>(key));
    };

    inp["isKeyReleased"] = [input](int key) {
        return input->IsKeyReleased(static_cast<Key>(key));
    };

    // Mouse position
    inp["getMousePosition"] = [input]() -> std::tuple<float, float> {
        auto pos = input->GetMousePosition();
        return std::make_tuple(pos.x, pos.y);
    };

    // ===== Key constants =====
    // Match the actual Key enum from window.hpp
    sol::table keys = atmos.create("keys");

    // Special keys
    keys["SPACE"] = static_cast<int>(Key::SPACE);
    keys["ENTER"] = static_cast<int>(Key::ENTER);
    keys["ESCAPE"] = static_cast<int>(Key::ESCAPE);

    // Arrow keys
    keys["UP"] = static_cast<int>(Key::UP);
    keys["DOWN"] = static_cast<int>(Key::DOWN);
    keys["LEFT"] = static_cast<int>(Key::LEFT);
    keys["RIGHT"] = static_cast<int>(Key::RIGHT);

    // Numbers
    keys["NUM_0"] = static_cast<int>(Key::Num0);
    keys["NUM_1"] = static_cast<int>(Key::Num1);
    keys["NUM_2"] = static_cast<int>(Key::Num2);
    keys["NUM_3"] = static_cast<int>(Key::Num3);
    keys["NUM_4"] = static_cast<int>(Key::Num4);
    keys["NUM_5"] = static_cast<int>(Key::Num5);
    keys["NUM_6"] = static_cast<int>(Key::Num6);
    keys["NUM_7"] = static_cast<int>(Key::Num7);
    keys["NUM_8"] = static_cast<int>(Key::Num8);
    keys["NUM_9"] = static_cast<int>(Key::Num9);

    // Letters
    keys["Q"] = static_cast<int>(Key::Q);
    keys["W"] = static_cast<int>(Key::W);
    keys["E"] = static_cast<int>(Key::E);
    keys["R"] = static_cast<int>(Key::R);
    keys["T"] = static_cast<int>(Key::T);
    keys["Y"] = static_cast<int>(Key::Y);
    keys["U"] = static_cast<int>(Key::U);
    keys["I"] = static_cast<int>(Key::I);
    keys["O"] = static_cast<int>(Key::O);
    keys["P"] = static_cast<int>(Key::P);
    keys["A"] = static_cast<int>(Key::A);
    keys["S"] = static_cast<int>(Key::S);
    keys["D"] = static_cast<int>(Key::D);
    keys["F"] = static_cast<int>(Key::F);
    keys["G"] = static_cast<int>(Key::G);
    keys["H"] = static_cast<int>(Key::H);
    keys["J"] = static_cast<int>(Key::J);
    keys["K"] = static_cast<int>(Key::K);
    keys["L"] = static_cast<int>(Key::L);
    keys["Z"] = static_cast<int>(Key::Z);
    keys["X"] = static_cast<int>(Key::X);
    keys["C"] = static_cast<int>(Key::C);
    keys["V"] = static_cast<int>(Key::V);
    keys["B"] = static_cast<int>(Key::B);
    keys["N"] = static_cast<int>(Key::N);
    keys["M"] = static_cast<int>(Key::M);
}
