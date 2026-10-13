#include "pd_console.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::pd_console {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

// ---- shared helpers ----

static std::string fixed(float v, int places) {
  char b[24];
  snprintf(b, sizeof b, "%.*f", places, v);
  return b;
}
static std::string watts(float v) {
  const long w = static_cast<long>(v);
  if (std::labs(w) >= 1000) {
    std::string s = fixed(w / 1000.0f, 2);
    while (s.size() > 1 && s.back() == '0') s.pop_back();  // 1.50 -> 1.5, like the card's rounding
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s + " kW";
  }
  return std::to_string(w) + " W";
}
static Tone tone_of(const char *name) {
  const std::string t = name ? name : "";
  if (t == "accent") return Tone::ACCENT;
  if (t == "busy") return Tone::BUSY;
  if (t == "alert") return Tone::ALERT;
  return Tone::NORMAL;
}
static bool is_good(const char *name) { return name && std::string(name) == "good"; }
// A pill's look by tone: a fill role and a text tone. Alert has no fill of its own (a screen has no red role): a plain
// pill with red words. Good is a green fill.
struct Look { theme::Role fill; Tone text; theme::Role ink; };
static Look look_of(Tone tone, bool good) {
  if (good) return {theme::MARK_GREEN, Tone::NORMAL, theme::ON_ACCENT};
  switch (tone) {
    case Tone::ACCENT: return {theme::ACCENT, Tone::NORMAL, theme::ON_ACCENT};
    case Tone::BUSY: return {theme::MARK_AMBER, Tone::NORMAL, theme::ON_ACCENT};
    case Tone::ALERT: return {theme::TRACK, Tone::ALERT, theme::INK};
    default: return {theme::TRACK, Tone::NORMAL, theme::INK};
  }
}
static const Look OK{theme::MARK_GREEN, Tone::NORMAL, theme::ON_ACCENT};
static const Look BAD{theme::TRACK, Tone::ALERT, theme::INK};

// A pill: a rounded block with one label inside. place() sizes it to its text and puts it at x, y.
struct Pill {
  lv_obj_t *box{}, *text{};
  Font font = Font::BODY;
  void make(lv_obj_t *parent, Font f) {
    font = f;
    box = ui::block(parent, theme::TRACK);
    lv_obj_set_style_radius(box, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    text = ui::label(box, f, theme::INK);
  }
  int width_for(const std::string &s) const { return ui::text_width(s, font) + 2 * ui::px(10); }
  void set(const std::string &s, const Look &look) {
    const int w = width_for(s), h = ui::line_height(font) + ui::px(6);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, ui::color(look.fill), 0);
    ui::set_text(text, s);
    ui::set_tone(text, look.text, look.ink);
    lv_obj_set_size(text, w - 2 * ui::px(10), ui::line_height(font));
    lv_obj_set_pos(text, ui::px(10), ui::px(3));
  }
  void hide(bool h) {
    if (h) lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(box, LV_OBJ_FLAG_HIDDEN);
  }
};

// ---- the header tile: Power Distribution Console ----
class HeaderTile : public tessera::Tile {
 public:
  void create(const tessera::TileContext &c) override {
    w_ = c.width;
    h_ = c.height;
    icon_l_ = ui::label(c.parent, Font::ICON, theme::INK);
    icon_r_ = ui::label(c.parent, Font::ICON, theme::INK);
    title_ = ui::label(c.parent, Font::TITLE, theme::INK);
    grid_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
    target_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
    total_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
    mode_.make(c.parent, Font::BODY_LARGE);
    burst_.make(c.parent, Font::BODY_LARGE);
    story1_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
    story2_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
    glance_ = ui::label(c.parent, Font::BODY, theme::INK_SOFT);
    for (auto *l : {story1_, story2_, glance_, title_}) {
      lv_obj_set_width(l, w_);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }
  }

  void on_state(JsonObjectConst d) override {
    const char *state = d["state"] | "";
    waiting_ = !*state || !strcmp(state, "unknown") || !strcmp(state, "unavailable") || !strcmp(state, "none");
    JsonObjectConst a = d["attributes"];
    mode_text_ = a["mode"] | "Controller Disabled";
    mode_tone_ = tone_of(a["tone"] | "");
    mode_good_ = is_good(a["tone"] | "");
    ctrl_ = a["ctrl"].as<bool>();
    grid_w_ = a["grid_w"] | 0.0f;
    total_w_ = a["total_w"] | 0.0f;
    target_w_ = a["tgt_w"] | -100.0f;
    burst_on_ = a["burst"].as<bool>();
    story1_t_ = a["story1"] | "";
    story2_t_ = a["story2"] | "";
    dp3_ = a["dp3_w"] | 0.0f;
    ux_ = a["ux_w"] | 0.0f;
    ac_ = a["ac_w"] | 0.0f;
  }

  void on_tick(uint32_t) override { paint(); }
  void on_theme() override { paint(); }

  // The burst pill, the card's only action: tapping the tile while Burst is on turns it off.
  void on_tap() override {
    if (burst_on_) tessera::action("input_boolean.turn_off", "input_boolean.pd_predictive_boost_active_boolean");
  }

 private:
  void centre(lv_obj_t *l, int y, int h) {
    lv_obj_set_pos(l, 0, y);
    lv_obj_set_height(l, h);
  }
  void paint() {
    const int th = ui::line_height(Font::TITLE), bh = ui::line_height(Font::BODY_LARGE), sh = ui::line_height(Font::BODY);
    const int ph = bh + ui::px(6), gap = ui::px(8);
    // Five rows, spread over the tile's height.
    const int total = th + bh + ph + bh * 2 + sh;
    const int space = std::max(gap, (h_ - total) / 6);
    int y = std::max(0, (h_ - (total + space * 4)) / 2);

    if (waiting_) {
      ui::set_text(title_, plugin_text("waiting"));
      lv_obj_add_flag(icon_l_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(icon_r_, LV_OBJ_FLAG_HIDDEN);
      centre(title_, (h_ - th) / 2, th);
      for (auto *l : {grid_, target_, total_, story1_, story2_, glance_}) ui::set_text(l, "");
      mode_.hide(true);
      burst_.hide(true);
      return;
    }
    // Title with a cog (controller on) or a cross (off) on both sides.
    const std::string title = "Power Distribution Console";
    ui::set_text(title_, title);
    const std::string icon = ui::icon(ctrl_ ? 0xF08BB : 0xF0156);
    for (auto *l : {icon_l_, icon_r_}) {
      lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
      ui::set_text(l, icon);
      ui::set_tone(l, ctrl_ ? Tone::NORMAL : Tone::ALERT, theme::INK);
    }
    const int tw = ui::text_width(title, Font::TITLE), iw = ui::text_width(icon, Font::ICON);
    const int ih = ui::line_height(Font::ICON);
    centre(title_, y, th);
    lv_obj_set_size(icon_l_, iw + 2, ih);
    lv_obj_set_size(icon_r_, iw + 2, ih);
    lv_obj_set_pos(icon_l_, std::max(0, (w_ - tw) / 2 - iw - gap * 2), y + (th - ih) / 2);
    lv_obj_set_pos(icon_r_, std::min(w_ - iw - 2, (w_ + tw) / 2 + gap * 2), y + (th - ih) / 2);
    y += th + space;

    // Grid | Target | Total on one line, Grid in red while it imports.
    const std::string g = "Grid: " + watts(grid_w_), t = "Target: " + std::to_string(static_cast<long>(target_w_)) + " W",
                      s = "Total: " + watts(total_w_);
    const int gw = ui::text_width(g, Font::BODY_LARGE), tw2 = ui::text_width(t, Font::BODY_LARGE),
              sw = ui::text_width(s, Font::BODY_LARGE);
    const int sep = ui::px(28), line_w = gw + tw2 + sw + 2 * sep;
    int x = std::max(0, (w_ - line_w) / 2);
    ui::set_text(grid_, g);
    ui::set_tone(grid_, grid_w_ > 0 ? Tone::ALERT : Tone::NORMAL, theme::INK);
    lv_obj_set_size(grid_, gw + 2, bh);
    lv_obj_set_pos(grid_, x, y);
    x += gw + sep;
    ui::set_text(target_, t);
    lv_obj_set_size(target_, tw2 + 2, bh);
    lv_obj_set_pos(target_, x, y);
    x += tw2 + sep;
    ui::set_text(total_, s);
    lv_obj_set_size(total_, sw + 2, bh);
    lv_obj_set_pos(total_, x, y);
    y += bh + space;

    // Mode pill, and the Burst pill beside it.
    const std::string burst_text = plugin_text("burst");
    mode_.set(mode_text_, look_of(mode_tone_, mode_good_));
    burst_.set(burst_text, look_of(Tone::BUSY, false));
    burst_.hide(!burst_on_);
    const int mw = mode_.width_for(mode_text_), bw = burst_.width_for(burst_text);
    const int row_w = mw + (burst_on_ ? bw + gap : 0);
    x = std::max(0, (w_ - row_w) / 2);
    lv_obj_set_pos(mode_.box, x, y);
    lv_obj_set_pos(burst_.box, x + mw + gap, y);
    y += ph + space;

    ui::set_text(story1_, story1_t_);
    centre(story1_, y, bh);
    y += bh;
    ui::set_text(story2_, story2_t_);
    centre(story2_, y, bh);
    y += bh + space;
    ui::set_text(glance_, "DP3: " + watts(dp3_) + "  |  Ultra X: " + watts(ux_) + "  |  AC: " + watts(ac_));
    centre(glance_, y, sh);
  }
  const char *plugin_text(const char *key) const;

  int w_ = 0, h_ = 0;
  bool waiting_ = true, ctrl_ = false, burst_on_ = false, mode_good_ = false;
  float grid_w_ = 0, total_w_ = 0, target_w_ = -100, dp3_ = 0, ux_ = 0, ac_ = 0;
  Tone mode_tone_ = Tone::NORMAL;
  std::string mode_text_, story1_t_, story2_t_;
  lv_obj_t *icon_l_{}, *icon_r_{}, *title_{}, *grid_{}, *target_{}, *total_{}, *story1_{}, *story2_{}, *glance_{};
  Pill mode_, burst_;
};

// ---- the battery tile: one device of the Battery Management card ----
struct Device {
  const char *key, *name, *eligibility, *accent_label;
  theme::Role colour;
};
static const Device DEVICES[] = {
    {"dp3", "Delta Pro 3", "input_boolean.pd_batt_dp3_eligible", "dp3", theme::MARK_AMBER},
    {"ultra", "Stream Ultra X", "input_boolean.pd_batt_ultra_x_eligible", "ultra", theme::MARK_GREEN},
    {"ac", "Stream AC Pro", "input_boolean.pd_batt_stream_ac_eligible", "ac", theme::MARK_BLUE},
    {"max", "Stream Max", "input_boolean.pd_batt_stream_max_eligible", "max", theme::MARK_PURPLE},
};
// The order of the numbers in each device's list (README.md).
enum Idx { SOC, KWH, CHARGE, CAP, SET, ACT, ONLINE, ELIGIBLE, BAR_SOC, BAR_SET, BAR_ACT, BAR_SOLAR, GLOW, BLE, IOB, N };

class BatteryTile : public tessera::Tile {
 public:
  void create(const tessera::TileContext &c) override {
    w_ = c.width;
    h_ = c.height;
    const char *which = c.options["device"] | "dp3";
    dev_ = &DEVICES[0];
    for (const auto &d : DEVICES)
      if (!strcmp(d.key, which)) dev_ = &d;
    frame_ = ui::block(c.parent, theme::CARD);
    lv_obj_set_size(frame_, w_, h_);
    lv_obj_set_pos(frame_, 0, 0);
    lv_obj_set_style_bg_opa(frame_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(frame_, 1, 0);
    lv_obj_set_style_border_color(frame_, ui::color(theme::LINE), 0);
    lv_obj_remove_flag(frame_, LV_OBJ_FLAG_CLICKABLE);
    elig_.make(c.parent, Font::BODY);
    conn_.make(c.parent, Font::BODY);
    off_.make(c.parent, Font::BODY);
    name_ = ui::label(c.parent, Font::TITLE, theme::INK);
    line1_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK_SOFT);
    line2_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK_SOFT);
    lv_obj_set_style_text_align(name_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(line1_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(line2_, LV_TEXT_ALIGN_CENTER, 0);
    for (int i = 0; i < 3; i++) {
      track_[i] = ui::block(c.parent, theme::TRACK);
      fill_[i] = ui::block(c.parent, dev_->colour);
    }
    solar_ = ui::block(c.parent, theme::MARK_AMBER);
  }

  void on_state(JsonObjectConst d) override {
    JsonObjectConst a = d["attributes"];
    JsonArrayConst list = a[dev_->key].as<JsonArrayConst>();
    have_ = !list.isNull() && list.size() >= N;
    for (int i = 0; i < N; i++) v_[i] = have_ ? (list[i] | 0.0f) : 0.0f;
    std::string c = std::string("conn_") + dev_->key;
    conn_text_ = a[c.c_str()] | "";
    const char *tone = a["glow_tone"] | "";
    glow_tone_ = tone_of(tone);
    glow_good_ = is_good(tone);
    pending_ = false;  // a new state settles a tap
  }

  void on_tick(uint32_t) override { paint(); }
  void on_theme() override {
    lv_obj_set_style_border_color(frame_, ui::color(theme::LINE), 0);
    paint();
  }

  // Tapping the tile flips the device's eligibility, as its pill does on the card. The pill shows the change at once.
  void on_tap() override {
    if (!have_) return;
    const bool now = pending_ ? pending_value_ : v_[ELIGIBLE] > 0.5f;
    pending_ = true;
    pending_value_ = !now;
    tessera::action(pending_value_ ? "input_boolean.turn_on" : "input_boolean.turn_off", dev_->eligibility);
    paint();
  }

 private:
  void bar(int i, int x, int y, int w, int h, float percent, float solar_percent = -1) {
    const int p = std::min(100, std::max(0, static_cast<int>(percent)));
    lv_obj_set_size(track_[i], w, h);
    lv_obj_set_pos(track_[i], x, y);
    lv_obj_set_size(fill_[i], std::max(1, w * p / 100), h);
    lv_obj_set_pos(fill_[i], x, y);
    lv_obj_set_style_bg_opa(fill_[i], p ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (i == 2) {
      const int sp = solar_percent < 0 ? 0 : std::min(100, std::max(0, static_cast<int>(solar_percent)));
      // The solar share first, the battery share after it: two parts of one bar, as the card draws them.
      lv_obj_set_size(solar_, std::max(1, w * sp / 100), h);
      lv_obj_set_pos(solar_, x, y);
      lv_obj_set_style_bg_opa(solar_, sp ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
      const int bw = std::max(1, w * p / 100);
      lv_obj_set_pos(fill_[i], x + (sp ? w * sp / 100 : 0), y);
      lv_obj_set_width(fill_[i], std::min(bw, w - (sp ? w * sp / 100 : 0)));
    }
  }

  void paint() {
    if (!have_) {
      ui::set_text(name_, dev_->name);
      lv_obj_set_pos(name_, 0, (h_ - ui::line_height(Font::TITLE)) / 2);
      lv_obj_set_size(name_, w_, ui::line_height(Font::TITLE));
      ui::set_text(line1_, "");
      ui::set_text(line2_, "");
      elig_.hide(true);
      conn_.hide(true);
      off_.hide(true);
      for (int i = 0; i < 3; i++) { lv_obj_add_flag(track_[i], LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(fill_[i], LV_OBJ_FLAG_HIDDEN); }
      lv_obj_add_flag(solar_, LV_OBJ_FLAG_HIDDEN);
      return;
    }
    for (int i = 0; i < 3; i++) { lv_obj_remove_flag(track_[i], LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(fill_[i], LV_OBJ_FLAG_HIDDEN); }
    lv_obj_remove_flag(solar_, LV_OBJ_FLAG_HIDDEN);

    const int pad = ui::px(12), nh = ui::line_height(Font::TITLE), bh = ui::line_height(Font::BODY_LARGE);
    const int ph = ui::line_height(Font::BODY) + ui::px(6), barh = std::max(ui::px(6), ui::px(8));
    const int inner = w_ - 2 * pad;

    // Glow: the outline. Charging is green, active/master/equal take the sensor's colour; equal pulses once a second.
    const int glow = static_cast<int>(v_[GLOW]);
    lv_color_t line_col = ui::color(theme::LINE);
    int border = 1;
    lv_opa_t opa = LV_OPA_COVER;
    if (glow == 1) { line_col = ui::color(theme::MARK_GREEN); border = 2; }
    else if (glow >= 2) {
      // The sensor's colour as far as roles go: its tone's role, the accent for red (no red role on a screen).
      line_col = ui::color(glow_good_ ? theme::MARK_GREEN : glow_tone_ == Tone::BUSY ? theme::MARK_AMBER
                           : glow_tone_ == Tone::NORMAL ? theme::MUTED : theme::ACCENT);
      border = glow == 3 ? 3 : 2;
      if (glow == 4) { pulse_ = !pulse_; opa = pulse_ ? LV_OPA_COVER : LV_OPA_40; }
    }
    lv_obj_set_style_border_width(frame_, border, 0);
    lv_obj_set_style_border_color(frame_, line_col, 0);
    lv_obj_set_style_border_opa(frame_, opa, 0);

    // Header: eligible pill left, name centred, online pill right.
    const bool online = v_[ONLINE] > 0.5f;
    const bool eligible = pending_ ? pending_value_ : v_[ELIGIBLE] > 0.5f;
    const std::string es = eligible ? "Eligible" : "Ineligible";
    elig_.set(es, eligible ? OK : BAD);
    const std::string cs = online ? (conn_text_.empty() ? std::string("Online") : conn_text_) : "Offline";
    conn_.set(cs, online ? OK : BAD);
    elig_.hide(false);
    conn_.hide(false);
    int y = pad;
    lv_obj_set_pos(elig_.box, pad, y);
    lv_obj_set_pos(conn_.box, w_ - pad - conn_.width_for(cs), y);
    ui::set_text(name_, dev_->name);
    lv_obj_set_size(name_, w_, nh);
    lv_obj_set_pos(name_, 0, y + (ph - nh) / 2);
    y += std::max(ph, nh) + ui::px(6);

    // The two text lines.
    const bool charging = v_[CHARGE] > 10;
    ui::set_text(line1_, "SoC " + fixed(v_[SOC], 1) + "%   Rem " + fixed(v_[KWH], 2) + " kWh   " +
                             (charging ? "Charging " + std::to_string(static_cast<long>(std::lround(v_[CHARGE]))) + " W"
                                       : "Cap " + std::to_string(static_cast<long>(v_[CAP])) + " W"));
    lv_obj_set_size(line1_, w_, bh);
    lv_obj_set_pos(line1_, 0, y);
    y += bh;
    ui::set_text(line2_, "Set " + std::to_string(static_cast<long>(v_[SET])) + " W   Act " +
                             std::to_string(static_cast<long>(v_[ACT])) + " W");
    lv_obj_set_size(line2_, w_, bh);
    lv_obj_set_pos(line2_, 0, y);
    y += bh + ui::px(8);

    // SoC, Set, Act bars. Set is the SoC bar's lighter twin; Act is blue, with solar (amber) first when the device has it.
    bar(0, pad, y, inner, barh, v_[BAR_SOC]);
    lv_obj_set_style_bg_color(fill_[0], ui::color(dev_->colour), 0);
    y += barh + ui::px(8);
    bar(1, pad, y, inner, barh, v_[BAR_SET]);
    lv_obj_set_style_bg_color(fill_[1], ui::color(dev_->colour), 0);
    lv_obj_set_style_bg_opa(fill_[1], LV_OPA_60, 0);
    y += barh + ui::px(8);
    const bool has_solar = v_[BAR_SOLAR] >= 0;
    bar(2, pad, y, inner, barh, v_[BAR_ACT], has_solar ? v_[BAR_SOLAR] : -1);
    lv_obj_set_style_bg_color(fill_[2], ui::color(theme::ACCENT), 0);
    y += barh + ui::px(8);

    // A link that is down (Bluetooth or the cloud), under the bars, only when it is.
    const bool ble = v_[BLE] > 0.5f, iob = v_[IOB] > 0.5f;
    if (online && ble && iob) {
      off_.hide(true);
    } else if (online) {
      std::string s = !ble && !iob ? "BLE and IoB offline" : !ble ? "BLE offline" : "IoB offline";
      off_.set(s, BAD);
      off_.hide(false);
      lv_obj_set_pos(off_.box, (w_ - off_.width_for(s)) / 2, y);
    } else {
      off_.hide(true);
    }
  }

  int w_ = 0, h_ = 0;
  const Device *dev_{};
  bool have_ = false, pending_ = false, pending_value_ = false, pulse_ = false, glow_good_ = false;
  float v_[N] = {};
  Tone glow_tone_ = Tone::NORMAL;
  std::string conn_text_;
  lv_obj_t *frame_{}, *name_{}, *line1_{}, *line2_{}, *track_[3]{}, *fill_[3]{}, *solar_{};
  Pill elig_, conn_, off_;
};

const char *HeaderTile::plugin_text(const char *key) const {
  static const char *waiting = "Waiting for controller data";
  static const char *burst = "Burst Enabled";
  return !strcmp(key, "waiting") ? waiting : burst;
}

void PdConsole::setup() {
  add_tile("header", []() { return new HeaderTile(); });
  add_tile("battery", []() { return new BatteryTile(); });
}

}  // namespace esphome::pd_console
