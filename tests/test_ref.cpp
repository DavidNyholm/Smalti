// Ref: checked handles, deferred removal, thread checks.
#include <thread>

#include "check.hpp"
#include "support.hpp"

using namespace smalti;

TEST(ref_is_live_until_removed_then_stale) {
  App app(headless());
  auto win = app.open_window();
  auto col = win->add<Column>();
  auto label = col->add<Label>("hello");
  CHECK(static_cast<bool>(label));
  CHECK_EQ(label->text(), std::string("hello"));
  label->remove();
  // Removal waits for the end of the current event; the test driver's frame is one.
  testing::Driver(win).frame();
  CHECK(!label);
  CHECK(label.try_get() == nullptr);
  ExpectContracts expect;
  CHECK(throws_contract([&] { (void)label->text(); }));
}

TEST(ref_removing_a_parent_makes_children_stale) {
  App app(headless());
  auto win = app.open_window();
  auto col = win->add<Column>();
  auto a = col->add<Button>("a");
  auto b = col->add<Button>("b");
  col->remove();
  testing::Driver(win).frame();
  CHECK(!col);
  CHECK(!a);
  CHECK(!b);
}

TEST(ref_slot_reuse_does_not_revive_old_refs) {
  App app(headless());
  auto win = app.open_window();
  auto col = win->add<Column>();
  auto first = col->add<Label>("first");
  first->remove();
  testing::Driver(win).frame();
  auto second = col->add<Label>("second");  // may reuse the same slot
  CHECK(!first);
  CHECK(static_cast<bool>(second));
  if (first.id().slot == second.id().slot) CHECK(first.id().generation != second.id().generation);
}

TEST(ref_converts_up_and_casts_down_checked) {
  App app(headless());
  auto win = app.open_window();
  auto button = win->add<Button>("go");
  Ref<Widget> w = button;
  CHECK(ref_cast<Button>(w) == button);
  CHECK(!ref_cast<Label>(w));
}

TEST(ref_use_from_another_thread_is_a_contract_violation) {
  App app(headless());
  auto win = app.open_window();
  auto label = win->add<Label>("x");
  ExpectContracts expect;
  bool violated = false;
  std::thread t([&] { violated = throws_contract([&] { (void)label.try_get(); }); });
  t.join();
  CHECK(violated);
}

TEST(ref_adding_to_a_leaf_is_a_contract_violation) {
  App app(headless());
  auto win = app.open_window();
  auto label = win->add<Label>("leaf");
  ExpectContracts expect;
  CHECK(throws_contract([&] { label->add<Label>("child"); }));
}

TEST(ref_callback_may_remove_its_own_widget) {
  App app(headless());
  auto win = app.open_window();
  auto col = win->add<Column>();
  Ref<Button> button = col->add<Button>("close me");
  int clicks = 0;
  button->on_click([&clicks, button] {
    ++clicks;
    button->remove();  // the widget whose callback is running
  });
  testing::Driver d(win);
  d.click(button);
  d.frame();
  CHECK_EQ(clicks, 1);
  CHECK(!button);
}

TEST(app_post_from_another_thread_runs_on_the_ui_thread) {
  App app(headless());
  auto win = app.open_window();
  auto label = win->add<Label>("before");
  const std::thread::id ui = std::this_thread::get_id();
  bool on_ui_thread = false;
  std::string seen;
  std::thread t([&] {
    app.post([&, label, win] {
      on_ui_thread = std::this_thread::get_id() == ui;
      label->set_text("after");
      seen = label->text();
      win->close();  // the last window: run() returns
    });
  });
  t.join();
  app.run();
  CHECK(on_ui_thread);
  CHECK_EQ(seen, std::string("after"));
  CHECK(!win);
  CHECK(!label);
}

TEST(app_rethrows_a_callback_exception_from_run) {
  App app(headless());
  app.open_window();  // an open window keeps run() running until the exception stops it
  app.post([] { throw std::runtime_error("from a callback"); });
  bool caught = false;
  try {
    app.run();
  } catch (const std::runtime_error& e) {
    caught = std::string(e.what()) == "from a callback";
  }
  CHECK(caught);
}
