// 階層状態機械の enter / exit の呼び出し順序、イベントのバブルアップ、戻り値、
// 再遷移、不正入力を、記録したログの完全一致で固定するテスト。
#include "embed_test/embed_test.hpp"
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "embed_fsm/embed_fsm.h"
#include "embed_fsm/embed_fsm.hpp"

namespace {
//   A(1) ── B(2) ── C(3) ── D(4)
//     └──── E(5)
//   F(6) ── G(7)               (別の木)
enum : fsm_state_id_t { A = 1, B = 2, C = 3, D = 4, E = 5, F = 6, G = 7 };
constexpr int kMaxId = 40;

struct Ctx {
    std::vector<std::string> log;
    std::function<void(fsm_machine_t *)> enter_hook[kMaxId];
    std::function<void(fsm_machine_t *)> exit_hook[kMaxId];
    std::function<fsm_result_t(fsm_machine_t *, const fsm_event_t *)> handler[kMaxId];
    fsm_event_t last_event{};
    void *last_user_data = nullptr;
};

template <int N> void on_enter(fsm_machine_t *f, void *ud) {
    Ctx *c = static_cast<Ctx *>(ud);
    c->log.push_back("+" + std::to_string(N));
    if (c->enter_hook[N]) c->enter_hook[N](f);
}
template <int N> void on_exit(fsm_machine_t *f, void *ud) {
    Ctx *c = static_cast<Ctx *>(ud);
    c->log.push_back("-" + std::to_string(N));
    if (c->exit_hook[N]) c->exit_hook[N](f);
}
template <int N> fsm_result_t on_event(fsm_machine_t *f, const fsm_event_t *e, void *ud) {
    Ctx *c = static_cast<Ctx *>(ud);
    c->log.push_back("ev" + std::to_string(N));
    c->last_event = *e;
    c->last_user_data = ud;
    return c->handler[N] ? c->handler[N](f, e) : FSM_RET_UNHANDLED;
}

template <int N>
bool add(embed_fsm::StateMachine &m, fsm_state_id_t parent, const char *name = "s") {
    return m.add_state((fsm_state_id_t)N, parent, name, on_enter<N>, on_exit<N>, on_event<N>);
}

struct Tree {
    Ctx ctx;
    embed_fsm::StateMachine m{&ctx};
    Tree() {
        add<1>(m, FSM_STATE_NONE);
        add<2>(m, A);
        add<3>(m, B);
        add<4>(m, C);
        add<5>(m, A);
        add<6>(m, FSM_STATE_NONE);
        add<7>(m, F);
    }
    // ログを取り出して空にする
    std::string take() {
        std::string s;
        for (const std::string &e : ctx.log) s += (s.empty() ? "" : " ") + e;
        ctx.log.clear();
        return s;
    }
};

bool same(const std::string &a, const std::string &e, const char *file, int line) {
    const bool ok = embed_test::check(a == e, "log equality", file, line);
    if (!ok) {
        std::printf("# expected: [%s]\n# actual:   [%s]\n", e.c_str(), a.c_str());
    }
    return ok;
}
#define CHECK_LOG(actual, expected) same((actual), (expected), __FILE__, __LINE__)

fsm_event_t ev(fsm_event_id_t id, uint32_t p = 0) { return fsm_event_t{id, p, nullptr}; }
} // namespace

TEST_CASE(start_enters_from_root_to_leaf) {
    Tree t;
    CHECK(t.m.start(D));
    CHECK_LOG(t.take(), "+1 +2 +3 +4");
    CHECK_EQ(t.m.current_state(), D);
    for (fsm_state_id_t s : {A, B, C, D}) CHECK(t.m.is_in_state(s));
    for (fsm_state_id_t s : {E, F, G}) CHECK(!t.m.is_in_state(s));

    Tree r;
    CHECK(r.m.start(A));
    CHECK_LOG(r.take(), "+1");
    CHECK_EQ(r.m.current_state(), A);
    CHECK(r.m.is_in_state(A));
    CHECK(!r.m.is_in_state(B));
}

TEST_CASE(start_rejects_unknown_states) {
    Tree t;
    CHECK(!t.m.start(99));
    CHECK(!t.m.start(FSM_STATE_NONE));
    CHECK_LOG(t.take(), "");
    CHECK_EQ(t.m.current_state(), FSM_STATE_NONE);
    CHECK(!t.m.is_in_state(A));
    CHECK(!t.m.is_in_state(FSM_STATE_NONE));
    CHECK(!fsm_start(nullptr, A));
}

TEST_CASE(transition_order_between_siblings) {
    Tree t;
    t.m.start(D);
    t.take();
    CHECK(t.m.transition_to(E)); // 共通の祖先 A: D, C, B の順に exit、E を enter
    CHECK_LOG(t.take(), "-4 -3 -2 +5");
    CHECK_EQ(t.m.current_state(), E);
    CHECK(t.m.is_in_state(A));
    CHECK(!t.m.is_in_state(B));

    CHECK(t.m.transition_to(D)); // E を exit、B, C, D の順に enter
    CHECK_LOG(t.take(), "-5 +2 +3 +4");
    CHECK_EQ(t.m.current_state(), D);

    CHECK(t.m.transition_to(C)); // D -> C は親への遷移: C 自身は exit も enter もしない
    CHECK_LOG(t.take(), "-4");
    CHECK_EQ(t.m.current_state(), C);
    CHECK(t.m.transition_to(E));
    CHECK_LOG(t.take(), "-3 -2 +5");
    CHECK(t.m.transition_to(B)); // 兄弟の子へ: E を exit、B を enter
    CHECK_LOG(t.take(), "-5 +2");
}

TEST_CASE(transition_to_ancestor_and_descendant) {
    Tree t;
    t.m.start(D);
    t.take();
    CHECK(t.m.transition_to(B)); // 祖先へ: 途中の子孫だけ exit、B は維持
    CHECK_LOG(t.take(), "-4 -3");
    CHECK_EQ(t.m.current_state(), B);
    CHECK(t.m.transition_to(D)); // 子孫へ: 途中の子孫だけ enter
    CHECK_LOG(t.take(), "+3 +4");
    CHECK(t.m.transition_to(A)); // 最上位の祖先へ
    CHECK_LOG(t.take(), "-4 -3 -2");
    CHECK_EQ(t.m.current_state(), A);
}

TEST_CASE(transition_across_separate_trees) {
    Tree t;
    t.m.start(D);
    t.take();
    CHECK(t.m.transition_to(G)); // 共通の祖先なし: すべて exit してから、ルートから enter
    CHECK_LOG(t.take(), "-4 -3 -2 -1 +6 +7");
    CHECK_EQ(t.m.current_state(), G);
    CHECK(t.m.is_in_state(F));
    CHECK(!t.m.is_in_state(A));
    CHECK(t.m.transition_to(C));
    CHECK_LOG(t.take(), "-7 -6 +1 +2 +3");
}

TEST_CASE(self_transition_exits_and_reenters) {
    Tree t;
    t.m.start(D);
    t.take();
    CHECK(t.m.transition_to(D));
    CHECK_LOG(t.take(), "-4 +4"); // 親は触らない
    CHECK_EQ(t.m.current_state(), D);
    t.m.transition_to(B);
    t.take();
    CHECK(t.m.transition_to(B));
    CHECK_LOG(t.take(), "-2 +2");
    t.m.transition_to(A);
    t.take();
    CHECK(t.m.transition_to(A)); // ルートの自己遷移
    CHECK_LOG(t.take(), "-1 +1");
}

TEST_CASE(transition_rejects_invalid_targets) {
    Tree t;
    t.m.start(D);
    t.take();
    CHECK(!t.m.transition_to(99));
    CHECK(!t.m.transition_to(FSM_STATE_NONE));
    CHECK(!fsm_transition_to(nullptr, A));
    CHECK_LOG(t.take(), "");
    CHECK_EQ(t.m.current_state(), D); // 失敗しても状態は変わらない
}

// ---- イベント ----
TEST_CASE(dispatch_bubbles_up_until_handled) {
    Tree t;
    t.m.start(D);
    t.take();
    fsm_event_t e = ev(7, 1234);
    t.ctx.handler[C] = [](fsm_machine_t *, const fsm_event_t *) { return FSM_RET_HANDLED; };
    CHECK(fsm_dispatch(t.m.raw_handle(), &e));
    CHECK_LOG(t.take(), "ev4 ev3"); // D は未処理、C で止まる (B, A は呼ばれない)
    CHECK_EQ(t.ctx.last_event.id, 7);
    CHECK_EQ(t.ctx.last_event.param, 1234u);
    CHECK(t.ctx.last_user_data == &t.ctx);

    t.ctx.handler[C] = nullptr;
    t.ctx.handler[A] = [](fsm_machine_t *, const fsm_event_t *) { return FSM_RET_HANDLED; };
    CHECK(t.m.dispatch(7));
    CHECK_LOG(t.take(), "ev4 ev3 ev2 ev1");

    t.ctx.handler[A] = nullptr;
    CHECK(!t.m.dispatch(7)); // どこでも未処理
    CHECK_LOG(t.take(), "ev4 ev3 ev2 ev1");

    // 兄弟の木のハンドラは呼ばれない
    t.ctx.handler[E] = [](fsm_machine_t *, const fsm_event_t *) { return FSM_RET_HANDLED; };
    t.ctx.handler[F] = [](fsm_machine_t *, const fsm_event_t *) { return FSM_RET_HANDLED; };
    CHECK(!t.m.dispatch(7));
    CHECK_LOG(t.take(), "ev4 ev3 ev2 ev1");
}

TEST_CASE(dispatch_result_codes) {
    Tree t;
    t.m.start(C);
    t.take();
    // TRANSITION を返しただけでは状態は変わらないが、処理済みとして true
    t.ctx.handler[C] = [](fsm_machine_t *, const fsm_event_t *) { return FSM_RET_TRANSITION; };
    CHECK(t.m.dispatch(1));
    CHECK_EQ(t.m.current_state(), C);
    CHECK_LOG(t.take(), "ev3");

    // 親のハンドラが遷移を実行する: バブルアップした先で exit / enter が走る
    t.ctx.handler[C] = nullptr;
    t.ctx.handler[A] = [](fsm_machine_t *f, const fsm_event_t *) {
        fsm_transition_to(f, G);
        return FSM_RET_TRANSITION;
    };
    CHECK(t.m.dispatch(2));
    CHECK_LOG(t.take(), "ev3 ev2 ev1 -3 -2 -1 +6 +7");
    CHECK_EQ(t.m.current_state(), G);
}

TEST_CASE(dispatch_skips_states_without_handler_and_checks_arguments) {
    Ctx ctx;
    embed_fsm::StateMachine m(&ctx);
    m.add_state(A, FSM_STATE_NONE, "A", nullptr, nullptr, on_event<1>);
    m.add_state(B, A, "B", nullptr, nullptr, nullptr); // ハンドラなし: 親へ素通し
    ctx.handler[A] = [](fsm_machine_t *, const fsm_event_t *) { return FSM_RET_HANDLED; };
    CHECK(!m.dispatch(1)); // 未起動
    m.start(B);
    CHECK(m.dispatch(1));
    CHECK_EQ(ctx.log.size(), 1u);

    fsm_event_t e = ev(1);
    CHECK(!fsm_dispatch(nullptr, &e));
    CHECK(!fsm_dispatch(m.raw_handle(), nullptr));
    CHECK_EQ(fsm_get_current_state(nullptr), (fsm_state_id_t)FSM_STATE_NONE);
    CHECK(!fsm_is_in_state(nullptr, A));
}

// ---- 遷移中のコールバックからの再遷移 ----
TEST_CASE(transition_from_on_enter_redirects) {
    Tree t;
    t.m.start(F);
    t.take();
    // B に入った時点で E へ遷移し直す: 外側の遷移は C, D を enter せず中断する
    t.ctx.enter_hook[B] = [](fsm_machine_t *f) { fsm_transition_to(f, E); };
    CHECK(t.m.transition_to(D));
    CHECK_LOG(t.take(), "-6 +1 +2 -2 +5");
    CHECK_EQ(t.m.current_state(), E);
    CHECK(t.m.is_in_state(A));
    CHECK(!t.m.is_in_state(B));
}

TEST_CASE(initial_substate_pattern) {
    // 複合状態の enter から初期の子状態へ入る
    Tree t;
    t.ctx.enter_hook[A] = [](fsm_machine_t *f) { fsm_transition_to(f, B); };
    t.ctx.enter_hook[B] = [](fsm_machine_t *f) { fsm_transition_to(f, C); };
    CHECK(t.m.start(A));
    CHECK_LOG(t.take(), "+1 +2 +3");
    CHECK_EQ(t.m.current_state(), C);
    CHECK(t.m.is_in_state(A));
    CHECK(t.m.is_in_state(B));
}

TEST_CASE(transition_from_on_exit_is_rejected) {
    Tree t;
    t.m.start(D);
    t.take();
    bool nested_result = true;
    t.ctx.exit_hook[C] = [&nested_result](fsm_machine_t *f) { nested_result = fsm_transition_to(f, F); };
    CHECK(t.m.transition_to(E));
    CHECK(!nested_result); // exit 中の遷移は受け付けず、元の遷移がそのまま完了する
    CHECK_LOG(t.take(), "-4 -3 -2 +5");
    CHECK_EQ(t.m.current_state(), E);
    // 完了後は通常どおり遷移できる
    CHECK(t.m.transition_to(D));
    CHECK_LOG(t.take(), "-5 +2 +3 +4");
}

// ---- 構築 ----
TEST_CASE(add_state_validation_and_limits) {
    embed_fsm::StateMachine m;
    CHECK(m.add_state(1, FSM_STATE_NONE, "one"));
    CHECK(!m.add_state(1, FSM_STATE_NONE, "dup"));          // 重複 ID
    CHECK(!m.add_state(FSM_STATE_NONE, FSM_STATE_NONE, "none")); // 予約 ID
    CHECK(!m.add_state(2, 2, "self"));                      // 自分自身が親
    CHECK(!fsm_add_state(nullptr, 3, FSM_STATE_NONE, "x", nullptr, nullptr, nullptr));
    CHECK(m.add_state(2, 1, "two"));
    CHECK_EQ(m.raw_handle()->state_count, 2u);

    // 状態数の上限
    embed_fsm::StateMachine full;
    for (int i = 0; i < FSM_MAX_STATES; ++i) CHECK(full.add_state((fsm_state_id_t)(100 + i), FSM_STATE_NONE, "s"));
    CHECK(!full.add_state(999, FSM_STATE_NONE, "over"));
    CHECK_EQ(full.raw_handle()->state_count, (size_t)FSM_MAX_STATES);
    CHECK(full.start(100 + FSM_MAX_STATES - 1)); // 最後の状態にも到達できる
    CHECK_EQ(full.current_state(), 100 + FSM_MAX_STATES - 1);
}

TEST_CASE(state_name_is_copied_and_truncated) {
    embed_fsm::StateMachine m;
    const std::string longname(100, 'n');
    m.add_state(1, FSM_STATE_NONE, longname);
    m.add_state(2, FSM_STATE_NONE, "ok");
    const fsm_state_t &s1 = m.raw_handle()->states[0];
    CHECK_EQ(std::string(s1.name).size(), (size_t)(FSM_STATE_NAME_MAX - 1));
    CHECK_EQ((int)s1.name[FSM_STATE_NAME_MAX - 1], 0);
    CHECK(std::string(m.raw_handle()->states[1].name) == "ok");
    fsm_machine_t raw;
    fsm_init(&raw, nullptr);
    fsm_add_state(&raw, 3, FSM_STATE_NONE, nullptr, nullptr, nullptr, nullptr);
    CHECK(std::string(raw.states[0].name).empty());
}

TEST_CASE(init_state) {
    fsm_machine_t raw;
    std::memset(&raw, 0xFF, sizeof(raw));
    int user = 0;
    fsm_init(&raw, &user);
    CHECK_EQ(raw.state_count, 0u);
    CHECK_EQ(raw.current_state, (fsm_state_id_t)FSM_STATE_NONE);
    CHECK_EQ(raw.target_state, (fsm_state_id_t)FSM_STATE_NONE);
    CHECK(raw.user_data == &user);
    fsm_init(nullptr, nullptr);
}

TEST_CASE(parent_cycles_do_not_hang) {
    // 親の指定が循環していても (前方参照で作れてしまう)、イベント配送や所属判定は止まる
    Ctx ctx;
    embed_fsm::StateMachine m(&ctx);
    m.add_state(1, 2, "a", nullptr, nullptr, on_event<1>);
    m.add_state(2, 1, "b", nullptr, nullptr, on_event<2>);
    CHECK(m.start(1));
    CHECK(!m.dispatch(5)); // 無限ループにならず、未処理で戻る
    CHECK(!m.is_in_state(3));
}

TEST_CASE(unregistered_parent_is_tolerated) {
    // 未登録の親 ID を指定しても落ちず、その子自身は到達できる
    Ctx ctx;
    embed_fsm::StateMachine m(&ctx);
    m.add_state(1, 50, "orphan", on_enter<1>, on_exit<1>, on_event<1>);
    m.add_state(2, FSM_STATE_NONE, "other", on_enter<2>, on_exit<2>, nullptr);
    CHECK(m.start(1));
    CHECK(m.is_in_state(1));
    CHECK(!m.dispatch(1));
    CHECK(m.transition_to(2));
    CHECK_EQ(ctx.log.size(), 4u); // +1 ev1 -1 +2
}

EMBED_TEST_MAIN()
