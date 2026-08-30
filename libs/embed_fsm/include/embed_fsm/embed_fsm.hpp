#pragma once
#ifndef EMBED_FSM_HPP
#define EMBED_FSM_HPP

#include "fsm_types.h"
#include "embed_fsm.h"
#include <string>
#include <functional>

namespace embed_fsm {

class StateMachine {
public:
    explicit StateMachine(void *user_data = nullptr) {
        fsm_init(&fsm_, user_data);
    }

    bool add_state(fsm_state_id_t id, fsm_state_id_t parent_id, const std::string &name,
                   fsm_action_fn on_enter = nullptr, fsm_action_fn on_exit = nullptr, fsm_event_fn on_event = nullptr) {
        return fsm_add_state(&fsm_, id, parent_id, name.c_str(), on_enter, on_exit, on_event);
    }

    bool start(fsm_state_id_t initial_state) {
        return fsm_start(&fsm_, initial_state);
    }

    bool dispatch(fsm_event_id_t event_id, uint32_t param = 0, void *payload = nullptr) {
        fsm_event_t evt{event_id, param, payload};
        return fsm_dispatch(&fsm_, &evt);
    }

    bool transition_to(fsm_state_id_t target_state) {
        return fsm_transition_to(&fsm_, target_state);
    }

    fsm_state_id_t current_state() const {
        return fsm_get_current_state(&fsm_);
    }

    bool is_in_state(fsm_state_id_t state_id) const {
        return fsm_is_in_state(&fsm_, state_id);
    }

    fsm_machine_t* raw_handle() { return &fsm_; }
    const fsm_machine_t* raw_handle() const { return &fsm_; }

private:
    fsm_machine_t fsm_{};
};

} // namespace embed_fsm

#endif /* EMBED_FSM_HPP */
