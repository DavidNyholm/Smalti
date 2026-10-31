// The X11 clipboard: the CLIPBOARD selection (ICCCM section 2). Copying makes the App the
// selection's owner, which then answers every program that pastes, sending large text in pieces
// (INCR); pasting asks the owner and waits for its answer. Created on first use, so an App that
// never copies or pastes makes no window for it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <X11/Xlib.h>

namespace smalti::platform {

class X11Clipboard {
 public:
  explicit X11Clipboard(Display* display);
  // What the App owns would be gone with it: a clipboard manager, where one runs, takes a copy.
  ~X11Clipboard();
  X11Clipboard(const X11Clipboard&) = delete;
  X11Clipboard& operator=(const X11Clipboard&) = delete;

  // See PlatformBackend::clipboard_text: the owner's answer, read while other events wait.
  std::optional<std::string> text();
  bool set_text(std::string_view plain);
  // Handles an event that belongs to the clipboard (a program asking for the text, a transfer
  // moving on) and returns true; false for any other event.
  bool handle(const XEvent& e);

 private:
  struct Atoms {
    Atom clipboard, targets, multiple, timestamp, utf8_string, text, incr, atom_pair,
        clipboard_manager, save_targets;
    Atom transfer;  // the property on our window that answers arrive in
    Atom time;      // the property touched to learn the server's time
  };
  struct Property {
    Atom type = None;
    int format = 0;
    std::string bytes;  // format 8 only
  };
  // Text sent in pieces: the requestor deletes the property to ask for the next one.
  struct Outgoing {
    ::Window requestor;
    Atom property;
    Atom type;
    std::string data;
    size_t sent = 0;
    int64_t last_ns = 0;  // when it last asked: a requestor that stops is not waited for
  };
  enum class Got { text, refused, failed };

  // Waits for the clipboard event `match` accepts, handling other clipboard events meanwhile;
  // false if the other side stays silent for the answer wait, or `give_up` passes.
  template <typename Match>
  bool wait_for(XEvent& out, int64_t give_up, Match match);
  static Bool is_clipboard_event(Display*, XEvent* e, XPointer self);
  Time server_time();
  Got request(Atom target, int64_t give_up, std::string& out);
  Got receive_pieces(Atom property, int64_t give_up, std::string& out);
  std::optional<Property> take_property(Atom property, size_t limit);
  Got decode(const Property& p, std::string& out) const;
  void answer(const XSelectionRequestEvent& request);
  bool serve(::Window requestor, Atom target, Atom property);
  bool serve_multiple(::Window requestor, Atom property);
  void send(::Window requestor, Atom property, Atom type, std::string data);
  bool send_next_piece(const XPropertyEvent& e);
  bool sending_to(::Window requestor) const;
  void stop_sending(std::vector<Outgoing>::iterator transfer);
  void drop_stalled_transfers();
  void hand_over();

  Display* display_;
  Atoms atoms_{};
  ::Window window_ = None;  // never mapped: it owns the selection and receives answers
  size_t max_piece_ = 0;
  std::optional<std::string> owned_;  // the text while the App owns the selection
  Time owned_time_ = CurrentTime;
  std::vector<Outgoing> outgoing_;
};

}  // namespace smalti::platform
