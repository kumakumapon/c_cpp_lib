#include "embed_test/embed_test.hpp"
#include <iostream>
#include <string>
#include <vector>

#include "embed_fsm/embed_fsm.h"
#include "embed_fsm/embed_fsm.hpp"
#include "embed_fsm/fsm_types.h"

enum StateId : fsm_state_id_t {
    STATE_ACTIVE = 1,
    STATE_IDLE = 2,
    STATE_RUNNING = 3,
    STATE_MOVING = 4,
    STATE_PAUSED = 5,
    STATE_ERROR = 6
};

enum EventId : fsm_event_id_t {
    EVT_START = 10,
    EVT_PAUSE = 11,
    EVT_RESUME = 12,
    EVT_FAULT = 99,
    EVT_RESET = 100
};

struct TestContext {
    std::vector<std::string> log;
};

static void log_action(fsm_machine_t *, void *user_data, const char *msg) {
    if (user_data) {
        auto *ctx = static_cast<TestContext *>(user_data);
        ctx->log.push_back(msg);
    }
}

static void on_enter_active(fsm_machine_t *fsm, void *ud) { log_action(fsm, ud, "ENTER:Active"); }
static void on_exit_active(fsm_machine_t *fsm, void *ud) { log_action(fsm, ud, "EXIT:Active"); }
static void on_enter_idle(fsm_machine_t *fsm, void *ud) { log_action(fsm, ud, "ENTER:Idle"); }
static void on_exit_idle(fsm_machine_t *fsm, void *ud) { log_action(fsm, ud, "EXIT:Idle"); }
static void on_enter_moving(fsm_machine_t *fsm, void *ud) { log_action(fsm, ud, "ENTER:Moving"); }
static void on_exit_moving(fsm_machine_t *fsm, void *ud) { log_action(fsm, ud, "EXIT:Moving"); }
static void on_enter_error(fsm_machine_t *fsm, void *ud) { log_action(fsm, ud, "ENTER:Error"); }
static void on_exit_error(fsm_machine_t *fsm, void *ud) { log_action(fsm, ud, "EXIT:Error"); }

static fsm_result_t on_event_active(fsm_machine_t *fsm, const fsm_event_t *event, void *) {
    if (event->id == EVT_FAULT) {
        fsm_transition_to(fsm, STATE_ERROR);
        return FSM_RET_TRANSITION;
    }
    return FSM_RET_UNHANDLED;
}

static fsm_result_t on_event_idle(fsm_machine_t *fsm, const fsm_event_t *event, void *) {
    if (event->id == EVT_START) {
        fsm_transition_to(fsm, STATE_MOVING);
        return FSM_RET_TRANSITION;
    }
    return FSM_RET_UNHANDLED;
}

static fsm_result_t on_event_moving(fsm_machine_t *fsm, const fsm_event_t *event, void *) {
    if (event->id == EVT_PAUSE) {
        fsm_transition_to(fsm, STATE_PAUSED);
        return FSM_RET_TRANSITION;
    }
    return FSM_RET_UNHANDLED; // EVT_FAULT will bubble up to Active!
}

static fsm_result_t on_event_error(fsm_machine_t *fsm, const fsm_event_t *event, void *) {
    if (event->id == EVT_RESET) {
        fsm_transition_to(fsm, STATE_IDLE);
        return FSM_RET_TRANSITION;
    }
    return FSM_RET_UNHANDLED;
}

TEST_CASE(test_hierarchical_fsm) {
    std::cout << "# [Test] Running Hierarchical FSM tests..." << std::endl;
    TestContext ctx;
    embed_fsm::StateMachine fsm(&ctx);

    // 状態階層の構築:
    // Root -> Active -> Idle
    //      -> Active -> Running -> Moving
    //      -> Active -> Running -> Paused
    //      -> Error
    fsm.add_state(STATE_ACTIVE, FSM_STATE_NONE, "Active", on_enter_active, on_exit_active,
                  on_event_active);
    fsm.add_state(STATE_IDLE, STATE_ACTIVE, "Idle", on_enter_idle, on_exit_idle, on_event_idle);
    fsm.add_state(STATE_RUNNING, STATE_ACTIVE, "Running", nullptr, nullptr, nullptr);
    fsm.add_state(STATE_MOVING, STATE_RUNNING, "Moving", on_enter_moving, on_exit_moving,
                  on_event_moving);
    fsm.add_state(STATE_PAUSED, STATE_RUNNING, "Paused", nullptr, nullptr, nullptr);
    fsm.add_state(STATE_ERROR, FSM_STATE_NONE, "Error", on_enter_error, on_exit_error,
                  on_event_error);

    // 1. 起動 -> Idle (Active -> Idle の Enter が実行される)
    fsm.start(STATE_IDLE);
    CHECK(fsm.current_state() == STATE_IDLE);
    CHECK(fsm.is_in_state(STATE_ACTIVE));
    CHECK(fsm.is_in_state(STATE_IDLE));
    CHECK(!fsm.is_in_state(STATE_ERROR));

    // 2. EVT_START: Idle -> Moving (Active は維持され、Idle Exit -> Moving Enter)
    CHECK(fsm.dispatch(EVT_START));
    CHECK(fsm.current_state() == STATE_MOVING);
    CHECK(fsm.is_in_state(STATE_ACTIVE));
    CHECK(fsm.is_in_state(STATE_RUNNING));
    CHECK(fsm.is_in_state(STATE_MOVING));

    // 3. EVT_FAULT: Moving で未処理のため親の Active にバブルアップ -> Error へ遷移
    // Moving Exit -> Active Exit -> Error Enter
    CHECK(fsm.dispatch(EVT_FAULT));
    CHECK(fsm.current_state() == STATE_ERROR);
    CHECK(!fsm.is_in_state(STATE_ACTIVE));
    CHECK(fsm.is_in_state(STATE_ERROR));

    // 4. EVT_RESET: Error -> Idle
    CHECK(fsm.dispatch(EVT_RESET));
    CHECK(fsm.current_state() == STATE_IDLE);

    std::cout << "# [Test] Hierarchical FSM passed! Action trace count: " << ctx.log.size()
              << std::endl;
}

EMBED_TEST_MAIN()
