#include <catch2/catch_test_macros.hpp>
#include "fsm.hpp"

// Pure-logic tests over FSMDefinition/FSMStep — the ported Vapor semantics:
// one transition per step, events before timers, minStateTime guards, and the
// event queue consumed every step.

namespace {

    FSMDefinition MakeFadeDef() {
        return FSMDefinitionBuilder()
            .Transition("Hidden", "FadingIn", "Show")
            .TimedTransition("FadingIn", "Visible", 0.3f)
            .Transition("Visible", "FadingOut", "Hide")
            .TimedTransition("FadingOut", "Hidden", 0.3f)
            .InitialState("Hidden")
            .Build();
    }

}// namespace

TEST_CASE("FSMDefinitionBuilder - states are created on demand", "[fsm]") {
    const FSMDefinition def = MakeFadeDef();
    CHECK(def.stateNames.size() == 4);
    CHECK(def.GetStateIndex("Hidden") == def.initialState);
    CHECK(def.GetStateName(def.GetStateIndex("Visible")) == "Visible");
    CHECK(def.eventTransitions.size() == 2);
    CHECK(def.timedTransitions.size() == 2);
}

TEST_CASE("FSMStep - event transitions", "[fsm]") {
    const FSMDefinition def = MakeFadeDef();
    FSMState state;
    state.currentState = def.initialState;
    std::vector<std::string> events;

    SECTION("no events, no timed match: nothing fires") {
        CHECK(!FSMStep(def, state, events, 0.016f).has_value());
        CHECK(state.currentState == def.GetStateIndex("Hidden"));
        CHECK(state.stateTime > 0.0f);
    }

    SECTION("matching event transitions and resets stateTime") {
        events.push_back("Show");
        auto change = FSMStep(def, state, events, 0.016f);
        REQUIRE(change.has_value());
        CHECK(change->fromState == def.GetStateIndex("Hidden"));
        CHECK(change->toState == def.GetStateIndex("FadingIn"));
        CHECK(state.currentState == def.GetStateIndex("FadingIn"));
        CHECK(events.empty());// queue consumed
    }

    SECTION("non-matching event is consumed without transitioning") {
        events.push_back("Hide");// no rule from Hidden
        CHECK(!FSMStep(def, state, events, 0.016f).has_value());
        CHECK(events.empty());
    }

    SECTION("only one transition per step") {
        events.push_back("Show");
        events.push_back("Hide");// would match Visible, but we land in FadingIn
        auto change = FSMStep(def, state, events, 0.016f);
        REQUIRE(change.has_value());
        CHECK(state.currentState == def.GetStateIndex("FadingIn"));
    }
}

TEST_CASE("FSMStep - timed transitions", "[fsm]") {
    const FSMDefinition def = MakeFadeDef();
    FSMState state;
    state.currentState = def.GetStateIndex("FadingIn");
    std::vector<std::string> events;

    // 0.3s fade: accumulate steps until the timed transition fires.
    CHECK(!FSMStep(def, state, events, 0.2f).has_value());// stateTime now 0.2
    CHECK(!FSMStep(def, state, events, 0.05f).has_value());// 0.25
    auto change = FSMStep(def, state, events, 0.1f);// 0.35 ≥ 0.3 → fires
    REQUIRE(change.has_value());
    CHECK(change->toState == def.GetStateIndex("Visible"));
    CHECK(change->previousStateTime >= 0.3f);
}

TEST_CASE("FSMStep - minStateTime guards event transitions", "[fsm]") {
    const FSMDefinition def = FSMDefinitionBuilder()
                                  .Transition("A", "B", "Go", 0.5f)
                                  .InitialState("A")
                                  .Build();
    FSMState state;
    std::vector<std::string> events;

    events.push_back("Go");
    CHECK(!FSMStep(def, state, events, 0.016f).has_value());// too early, event dropped

    state.stateTime = 0.6f;
    events.push_back("Go");
    CHECK(FSMStep(def, state, events, 0.016f).has_value());
}

TEST_CASE("FSM - full fade round trip", "[fsm]") {
    const FSMDefinition def = MakeFadeDef();
    FSMState state;
    state.currentState = def.initialState;
    std::vector<std::string> events;

    events.push_back("Show");
    REQUIRE(FSMStep(def, state, events, 0.016f).has_value());// Hidden → FadingIn

    for (int i = 0; i < 30 && state.currentState != def.GetStateIndex("Visible"); ++i)
        FSMStep(def, state, events, 0.016f);
    CHECK(state.currentState == def.GetStateIndex("Visible"));

    events.push_back("Hide");
    REQUIRE(FSMStep(def, state, events, 0.016f).has_value());// Visible → FadingOut

    for (int i = 0; i < 30 && state.currentState != def.GetStateIndex("Hidden"); ++i)
        FSMStep(def, state, events, 0.016f);
    CHECK(state.currentState == def.GetStateIndex("Hidden"));
}
