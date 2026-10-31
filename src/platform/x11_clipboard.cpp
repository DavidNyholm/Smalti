// The X11 clipboard (see x11_clipboard.hpp). The protocol is ICCCM section 2, with
// freedesktop.org's UTF8_STRING target and its clipboard manager specification (SAVE_TARGETS).
#include "platform/x11_clipboard.hpp"

#include <poll.h>

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <stdexcept>

#include <X11/Xatom.h>

#include "platform/text_input.hpp"

namespace smalti::platform {

namespace {

// A program that holds the clipboard and stays silent this long has failed the paste.
constexpr int64_t kAnswerWaitNs = 500'000'000;
// Every transfer ends within this however steadily it moves, since the App handles nothing
// else while it waits; and a requestor that asks for no piece for this long is given up on.
constexpr int64_t kTransferLimitNs = 5'000'000'000;
// Text larger than one piece goes in pieces (INCR). Other toolkits use this size; it is cut to
// what the server takes in one request where that is less.
constexpr size_t kPieceBytes = 256 * 1024;
// A ChangeProperty request's own fields, which count toward the server's request size limit.
constexpr size_t kChangePropertyHeaderBytes = 24;
// Target and property pairs a MULTIPLE request may list: far more than any requestor uses.
constexpr long kMaxMultiplePairs = 64;

int64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

struct XFreer {
  void operator()(unsigned char* data) const { XFree(data); }
};
using XData = std::unique_ptr<unsigned char, XFreer>;

const unsigned char* bytes_of(const void* p) { return static_cast<const unsigned char*>(p); }

// Requests on other programs' windows fail if the window is gone by the time they arrive, and
// Xlib's default error handler ends the process. While a trap lives, errors are only counted.
// It syncs at both ends, so the errors it sees are exactly those of the requests made under it.
class ErrorTrap {
 public:
  explicit ErrorTrap(Display* display) : display_(display) {
    XSync(display_, False);
    errors_ = 0;
    previous_ = XSetErrorHandler(&ErrorTrap::count);
  }
  ~ErrorTrap() {
    XSync(display_, False);
    XSetErrorHandler(previous_);
  }
  ErrorTrap(const ErrorTrap&) = delete;
  ErrorTrap& operator=(const ErrorTrap&) = delete;
  // Whether a request made so far under the trap failed.
  bool failed() {
    XSync(display_, False);
    return errors_ != 0;
  }

 private:
  static int count(Display*, XErrorEvent*) {
    ++errors_;
    return 0;
  }
  // Only the UI thread talks to the display, and Xlib calls the handler on the reading thread.
  static inline int errors_ = 0;
  Display* display_;
  XErrorHandler previous_ = nullptr;
};

}  // namespace

X11Clipboard::X11Clipboard(Display* display) : display_(display) {
  const auto intern = [&](const char* name) { return XInternAtom(display_, name, False); };
  atoms_.clipboard = intern("CLIPBOARD");
  atoms_.targets = intern("TARGETS");
  atoms_.multiple = intern("MULTIPLE");
  atoms_.timestamp = intern("TIMESTAMP");
  atoms_.utf8_string = intern("UTF8_STRING");
  atoms_.text = intern("TEXT");
  atoms_.incr = intern("INCR");
  atoms_.atom_pair = intern("ATOM_PAIR");
  atoms_.clipboard_manager = intern("CLIPBOARD_MANAGER");
  atoms_.save_targets = intern("SAVE_TARGETS");
  atoms_.transfer = intern("SMALTI_CLIPBOARD");
  atoms_.time = intern("SMALTI_TIME");
  XSetWindowAttributes attributes{};
  attributes.event_mask = PropertyChangeMask;  // answers in pieces, and the server's time
  window_ = XCreateWindow(display_, DefaultRootWindow(display_), 0, 0, 1, 1, 0, CopyFromParent,
                          InputOnly, CopyFromParent, CWEventMask, &attributes);
  if (!window_) throw std::runtime_error("smalti: cannot create the clipboard's X window");
  // The server's request limit is in 4-byte units, and is at least 4096 of them.
  const auto max_request = static_cast<size_t>(XMaxRequestSize(display_)) * 4;
  max_piece_ = std::min(kPieceBytes, max_request - kChangePropertyHeaderBytes);
}

X11Clipboard::~X11Clipboard() {
  hand_over();
  XDestroyWindow(display_, window_);
  XFlush(display_);
}

std::optional<std::string> X11Clipboard::text() {
  const ::Window owner = XGetSelectionOwner(display_, atoms_.clipboard);
  if (owner == None) return std::nullopt;
  if (owner == window_) return owned_;
  const int64_t give_up = now_ns() + kTransferLimitNs;
  // UTF-8 first; a program that predates it may still offer Latin-1.
  for (const Atom target : {atoms_.utf8_string, Atom{XA_STRING}}) {
    std::string out;
    const Got got = request(target, give_up, out);
    if (got == Got::text) return out;
    if (got == Got::failed) return std::nullopt;
  }
  return std::nullopt;
}

bool X11Clipboard::set_text(std::string_view plain) {
  std::string text(plain);  // first: running out of memory leaves the clipboard as it was
  // Ownership is taken with the server's time, not CurrentTime (ICCCM 2.1), and TIMESTAMP
  // reports it.
  const Time time = server_time();
  XSetSelectionOwner(display_, atoms_.clipboard, window_, time);
  if (XGetSelectionOwner(display_, atoms_.clipboard) != window_) {
    owned_.reset();
    return false;
  }
  owned_ = std::move(text);
  owned_time_ = time;
  return true;
}

bool X11Clipboard::handle(const XEvent& e) {
  switch (e.type) {
    case SelectionRequest:
      if (e.xselectionrequest.owner != window_) return false;
      answer(e.xselectionrequest);
      return true;
    case SelectionClear:
      if (e.xselectionclear.window != window_) return false;
      if (e.xselectionclear.selection == atoms_.clipboard) owned_.reset();
      return true;
    case SelectionNotify:
      return e.xselection.requestor == window_;  // a late answer to a paste that gave up
    case PropertyNotify:
      // On our window: the traces of pastes, which read their answers as they come.
      if (e.xproperty.window == window_) return true;
      return send_next_piece(e.xproperty);
    default:
      return false;
  }
}

template <typename Match>
bool X11Clipboard::wait_for(XEvent& out, int64_t give_up, Match match) {
  const int fd = ConnectionNumber(display_);
  int64_t deadline = std::min(now_ns() + kAnswerWaitNs, give_up);
  for (;;) {
    // Reads what the server has sent, without blocking; other events stay queued, in order.
    while (XCheckIfEvent(display_, &out, &X11Clipboard::is_clipboard_event,
                         reinterpret_cast<XPointer>(this))) {
      if (match(out)) return true;
      handle(out);
      deadline = std::min(now_ns() + kAnswerWaitNs, give_up);  // the other side is still there
    }
    const int64_t left = deadline - now_ns();
    if (left <= 0) return false;
    XFlush(display_);
    pollfd connection{fd, POLLIN, 0};
    poll(&connection, 1, static_cast<int>((left + 999'999) / 1'000'000));
  }
}

Bool X11Clipboard::is_clipboard_event(Display*, XEvent* e, XPointer self) {
  const auto* clipboard = reinterpret_cast<const X11Clipboard*>(self);
  // Selection events name our window in the field xany reads (owner, or requestor).
  if (e->xany.window == clipboard->window_) return True;
  return e->type == PropertyNotify && clipboard->sending_to(e->xproperty.window);
}

Time X11Clipboard::server_time() {
  // Appending nothing to a property changes nothing, but the server reports the change with
  // its time.
  const unsigned char nothing = 0;
  XChangeProperty(display_, window_, atoms_.time, XA_STRING, 8, PropModeAppend, &nothing, 0);
  XEvent e;
  const bool reported = wait_for(e, now_ns() + kTransferLimitNs, [&](const XEvent& ev) {
    return ev.type == PropertyNotify && ev.xproperty.window == window_ &&
           ev.xproperty.atom == atoms_.time;
  });
  return reported ? e.xproperty.time : CurrentTime;  // the server always reports it
}

// Asks the owner for the clipboard as `target`, and reads its answer into `out` as UTF-8.
X11Clipboard::Got X11Clipboard::request(Atom target, int64_t give_up, std::string& out) {
  XDeleteProperty(display_, window_, atoms_.transfer);
  XConvertSelection(display_, atoms_.clipboard, target, atoms_.transfer, window_, CurrentTime);
  XEvent e;
  const bool answered = wait_for(e, give_up, [&](const XEvent& ev) {
    return ev.type == SelectionNotify && ev.xselection.selection == atoms_.clipboard &&
           ev.xselection.target == target;
  });
  if (!answered) return Got::failed;
  if (e.xselection.property == None) return Got::refused;
  // The property the owner names: ours, unless it answers an obsolete way.
  const Atom property = e.xselection.property;
  const std::optional<Property> p = take_property(property, detail::kMaxClipboardBytes);
  if (!p) return Got::failed;
  if (p->type == atoms_.incr) return receive_pieces(property, give_up, out);
  return decode(*p, out);
}

// An answer in pieces (INCR). Reading the INCR property deleted it, which asks for the first
// piece; reading each piece asks for the next, and an empty piece ends the transfer.
X11Clipboard::Got X11Clipboard::receive_pieces(Atom property, int64_t give_up, std::string& out) {
  Property whole{None, 8, {}};
  for (;;) {
    XEvent e;
    const bool arrived = wait_for(e, give_up, [&](const XEvent& ev) {
      return ev.type == PropertyNotify && ev.xproperty.window == window_ &&
             ev.xproperty.atom == property && ev.xproperty.state == PropertyNewValue;
    });
    if (!arrived) return Got::failed;
    const std::optional<Property> piece =
        take_property(property, detail::kMaxClipboardBytes - whole.bytes.size());
    if (!piece || piece->format != 8) return Got::failed;
    whole.type = piece->type;
    if (piece->bytes.empty()) return decode(whole, out);
    whole.bytes += piece->bytes;
  }
}

// Reads a property of our window whole, and deletes it; nullopt if there is none,
// or if it holds more than `limit` bytes.
std::optional<X11Clipboard::Property> X11Clipboard::take_property(Atom property, size_t limit) {
  Atom type = None;
  int format = 0;
  unsigned long count = 0;
  unsigned long after = 0;
  unsigned char* raw = nullptr;
  const auto words = static_cast<long>(limit / 4 + 1);  // the length is in 4-byte units
  const int status = XGetWindowProperty(display_, window_, property, 0, words, True,
                                        AnyPropertyType, &type, &format, &count, &after, &raw);
  const XData data(raw);
  if (status != Success || type == None) return std::nullopt;
  if (after != 0) {
    XDeleteProperty(display_, window_, property);  // only a property read whole is
    return std::nullopt;
  }
  Property p{type, format, {}};
  if (format == 8) {
    if (count > limit) return std::nullopt;
    p.bytes.assign(reinterpret_cast<const char*>(raw), count);
  }
  return p;
}

X11Clipboard::Got X11Clipboard::decode(const Property& p, std::string& out) const {
  if (p.format != 8) return Got::refused;
  if (p.type == atoms_.utf8_string) {
    out = p.bytes;
  } else if (p.type == XA_STRING) {
    out = detail::utf8_of_latin1(p.bytes);
  } else {
    return Got::refused;  // another encoding (COMPOUND_TEXT): the next target may do
  }
  return Got::text;
}

void X11Clipboard::answer(const XSelectionRequestEvent& request) {
  ErrorTrap trap(display_);
  drop_stalled_transfers();
  // An obsolete requestor names no property: the target is the property then (ICCCM 2.2).
  const Atom property = request.property != None ? request.property : request.target;
  bool served = false;
  if (owned_ && request.selection == atoms_.clipboard) {
    try {
      served = serve(request.requestor, request.target, property);
    } catch (const std::bad_alloc&) {
      served = false;  // refused now, rather than leaving the requestor waiting for nothing
    }
  }
  XEvent reply{};
  XSelectionEvent& r = reply.xselection;
  r.type = SelectionNotify;
  r.display = display_;
  r.requestor = request.requestor;
  r.selection = request.selection;
  r.target = request.target;
  r.property = served ? property : None;
  r.time = request.time;
  XSendEvent(display_, request.requestor, False, NoEventMask, &reply);
  if (trap.failed()) {  // the requestor is gone: it will ask for no piece
    std::erase_if(outgoing_, [&](const Outgoing& o) { return o.requestor == request.requestor; });
  }
}

// Puts the owned text on the requestor's property as `target`; false for a target not offered.
bool X11Clipboard::serve(::Window requestor, Atom target, Atom property) {
  if (target == atoms_.targets) {
    const Atom offered[] = {atoms_.targets,     atoms_.multiple, atoms_.timestamp,
                            atoms_.utf8_string, atoms_.text,     XA_STRING};
    XChangeProperty(display_, requestor, property, XA_ATOM, 32, PropModeReplace, bytes_of(offered),
                    static_cast<int>(std::size(offered)));
    return true;
  }
  if (target == atoms_.timestamp) {
    const long time = static_cast<long>(owned_time_);  // format 32 data is passed as longs
    XChangeProperty(display_, requestor, property, XA_INTEGER, 32, PropModeReplace, bytes_of(&time),
                    1);
    return true;
  }
  // TEXT lets the owner choose the encoding (ICCCM 2.6.2): UTF-8, as other toolkits answer.
  if (target == atoms_.utf8_string || target == atoms_.text) {
    send(requestor, property, atoms_.utf8_string, *owned_);
    return true;
  }
  if (target == XA_STRING) {
    send(requestor, property, XA_STRING, detail::latin1_of(*owned_));
    return true;
  }
  if (target == atoms_.multiple) return serve_multiple(requestor, property);
  return false;
}

// MULTIPLE (ICCCM 2.6.2): the property lists target and property pairs. Each is served in
// turn, and a pair that cannot be has its property replaced by None.
bool X11Clipboard::serve_multiple(::Window requestor, Atom property) {
  Atom type = None;
  int format = 0;
  unsigned long count = 0;
  unsigned long after = 0;
  unsigned char* raw = nullptr;
  const int status =
      XGetWindowProperty(display_, requestor, property, 0, kMaxMultiplePairs * 2, False,
                         AnyPropertyType, &type, &format, &count, &after, &raw);
  const XData data(raw);
  if (status != Success || !raw || format != 32 || count % 2 != 0 || after != 0) return false;
  // Format 32 data arrives as longs, the size of an Atom.
  const auto* listed = reinterpret_cast<const Atom*>(raw);
  std::vector<Atom> pairs(listed, listed + count);
  for (size_t i = 0; i < pairs.size(); i += 2) {
    const bool done = pairs[i] != atoms_.multiple && pairs[i + 1] != None &&
                      serve(requestor, pairs[i], pairs[i + 1]);
    if (!done) pairs[i + 1] = None;
  }
  XChangeProperty(display_, requestor, property, atoms_.atom_pair, 32, PropModeReplace,
                  bytes_of(pairs.data()), static_cast<int>(pairs.size()));
  return true;
}

// Text that fits in one piece goes at once; larger text in pieces, after an INCR property that
// announces its size.
void X11Clipboard::send(::Window requestor, Atom property, Atom type, std::string data) {
  if (data.size() <= max_piece_) {
    XChangeProperty(display_, requestor, property, type, 8, PropModeReplace, bytes_of(data.data()),
                    static_cast<int>(data.size()));
    return;
  }
  // A new request for the same property replaces one still in progress.
  std::erase_if(outgoing_, [&](const Outgoing& o) {
    return o.requestor == requestor && o.property == property;
  });
  outgoing_.push_back({requestor, property, type, std::move(data), 0, now_ns()});
  XSelectInput(display_, requestor, PropertyChangeMask);  // its deletions ask for pieces
  const long size = static_cast<long>(outgoing_.back().data.size());
  XChangeProperty(display_, requestor, property, atoms_.incr, 32, PropModeReplace, bytes_of(&size),
                  1);
}

bool X11Clipboard::send_next_piece(const XPropertyEvent& e) {
  const auto t = std::find_if(outgoing_.begin(), outgoing_.end(), [&](const Outgoing& o) {
    return o.requestor == e.window && o.property == e.atom;
  });
  if (t == outgoing_.end()) return sending_to(e.window);  // another property of a requestor's
  if (e.state != PropertyDelete) return true;             // the piece we just wrote
  ErrorTrap trap(display_);
  const size_t n = std::min(max_piece_, t->data.size() - t->sent);
  XChangeProperty(display_, t->requestor, t->property, t->type, 8, PropModeReplace,
                  bytes_of(t->data.data() + t->sent), static_cast<int>(n));
  t->sent += n;
  t->last_ns = now_ns();
  // The empty piece ends the transfer; a piece that failed means the requestor is gone.
  if (n == 0 || trap.failed()) stop_sending(t);
  return true;
}

bool X11Clipboard::sending_to(::Window requestor) const {
  return std::any_of(outgoing_.begin(), outgoing_.end(),
                     [&](const Outgoing& o) { return o.requestor == requestor; });
}

// Under an ErrorTrap: the requestor may be gone.
void X11Clipboard::stop_sending(std::vector<Outgoing>::iterator transfer) {
  const ::Window requestor = transfer->requestor;
  outgoing_.erase(transfer);
  if (!sending_to(requestor)) XSelectInput(display_, requestor, NoEventMask);
}

// Under an ErrorTrap, like stop_sending.
void X11Clipboard::drop_stalled_transfers() {
  const int64_t now = now_ns();
  const auto stalled = [&](const Outgoing& o) { return now - o.last_ns > kTransferLimitNs; };
  for (auto t = std::find_if(outgoing_.begin(), outgoing_.end(), stalled); t != outgoing_.end();
       t = std::find_if(outgoing_.begin(), outgoing_.end(), stalled)) {
    stop_sending(t);
  }
}

// freedesktop.org's clipboard manager specification: SAVE_TARGETS asks the manager to take the
// listed targets, which it asks for as any program would, answering when it has them.
void X11Clipboard::hand_over() {
  if (!owned_ || XGetSelectionOwner(display_, atoms_.clipboard) != window_) return;
  if (XGetSelectionOwner(display_, atoms_.clipboard_manager) == None) return;
  const Atom saved[] = {atoms_.utf8_string, XA_STRING};
  XChangeProperty(display_, window_, atoms_.transfer, XA_ATOM, 32, PropModeReplace, bytes_of(saved),
                  static_cast<int>(std::size(saved)));
  XConvertSelection(display_, atoms_.clipboard_manager, atoms_.save_targets, atoms_.transfer,
                    window_, CurrentTime);
  XEvent e;
  wait_for(e, now_ns() + kTransferLimitNs, [&](const XEvent& ev) {
    return ev.type == SelectionNotify && ev.xselection.selection == atoms_.clipboard_manager;
  });
}

}  // namespace smalti::platform
