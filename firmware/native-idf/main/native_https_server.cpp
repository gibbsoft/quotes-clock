#include "native_https_server.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include <esp_https_server.h>
#include <esp_log.h>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <mbedtls/base64.h>

#include "app_config.hpp"
#include "generated/tls_bootstrap.hpp"
#include "native_display.hpp"
#include "native_sntp.hpp"
#include "native_wifi.hpp"
#include "platform_state.hpp"

namespace quotes_clock {
namespace {
constexpr const char *kTag = "native_https";
constexpr size_t kMaxJsonBody = 1536;

constexpr uint8_t kFaviconIco[] = {
    0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x32, 0x32, 0x00, 0x00, 0x01, 0x00, 0x20, 0x00, 0x59, 0x02,
    0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00,
    0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x32, 0x08, 0x06,
    0x00, 0x00, 0x00, 0x1E, 0x3F, 0x88, 0xB1, 0x00, 0x00, 0x02, 0x20, 0x49, 0x44, 0x41, 0x54, 0x78,
    0xDA, 0xDD, 0x9A, 0x3B, 0x6E, 0xC3, 0x30, 0x0C, 0x86, 0x65, 0x42, 0x5B, 0xD7, 0xA2, 0x05, 0x7C,
    0x0C, 0x0F, 0xD9, 0xE3, 0x25, 0x97, 0xC8, 0x2D, 0x7C, 0x8E, 0xDE, 0x22, 0x87, 0x68, 0x17, 0x7B,
    0xF7, 0x90, 0x5B, 0xB4, 0x43, 0x2E, 0x91, 0x0E, 0x85, 0x1D, 0x57, 0xD0, 0x83, 0xA4, 0x48, 0x29,
    0x89, 0x81, 0x00, 0x09, 0x2C, 0x53, 0xFC, 0xCC, 0x87, 0xE4, 0xDF, 0x69, 0xBE, 0x3F, 0xF7, 0x57,
    0xF3, 0x04, 0x87, 0x95, 0x34, 0xD6, 0x1D, 0x67, 0xD2, 0xF8, 0xF3, 0x69, 0x27, 0x36, 0x77, 0x23,
    0x11, 0x11, 0x2A, 0x80, 0x06, 0x54, 0x16, 0x48, 0x0A, 0xC0, 0x75, 0x8E, 0x3A, 0xBE, 0x08, 0x48,
    0xC8, 0x29, 0xAC, 0x33, 0xB9, 0xD7, 0x8B, 0x80, 0xF8, 0x9C, 0x08, 0x39, 0xB0, 0x8C, 0x4D, 0x9D,
    0xCF, 0x85, 0xB1, 0x39, 0x10, 0x12, 0xC5, 0xBA, 0xB5, 0xB1, 0xD8, 0xEE, 0x8E, 0x33, 0xD9, 0x36,
    0xD4, 0x84, 0xC0, 0x40, 0x89, 0xA7, 0x96, 0x36, 0x44, 0xEE, 0x5C, 0x50, 0xAA, 0xBD, 0x6A, 0xB7,
    0x76, 0xA0, 0x1A, 0xD2, 0x8E, 0x06, 0xA7, 0x6D, 0x93, 0x6B, 0xA4, 0x04, 0x04, 0x77, 0x2E, 0xA8,
    0xB1, 0x35, 0xF1, 0x5D, 0xBF, 0x7C, 0xB8, 0x07, 0x48, 0xDD, 0x21, 0xAE, 0x23, 0xB1, 0xC2, 0xA6,
    0x44, 0x25, 0xDA, 0xB5, 0xA8, 0xDD, 0x23, 0xB5, 0xF8, 0x71, 0x6C, 0x63, 0xC7, 0x81, 0x46, 0x5E,
    0xC7, 0x22, 0xE3, 0x46, 0x4E, 0xAA, 0xEE, 0x40, 0xAB, 0x48, 0x7D, 0x30, 0x2E, 0x80, 0x64, 0xF3,
    0xB0, 0x5A, 0x1D, 0x27, 0x56, 0x33, 0x1A, 0xDD, 0xCF, 0x6A, 0xAD, 0xB8, 0x0B, 0x0C, 0x15, 0x80,
    0x9B, 0x76, 0x60, 0x9E, 0xE4, 0xB0, 0x9A, 0x8B, 0xD4, 0x36, 0x2A, 0xD8, 0x6B, 0xB9, 0x69, 0x67,
    0xCD, 0x9D, 0xAD, 0xD0, 0x77, 0xD3, 0xB5, 0x6A, 0x1D, 0x2B, 0x48, 0x7B, 0x18, 0x4D, 0x7B, 0x18,
    0x1F, 0xC6, 0x71, 0xD7, 0x5F, 0xEB, 0x1B, 0x70, 0x4B, 0x8B, 0xBE, 0xFA, 0x36, 0x7E, 0x9B, 0x9E,
    0xB1, 0x1B, 0x6D, 0x53, 0xD4, 0x5B, 0xA0, 0x5A, 0x10, 0x98, 0x4C, 0xB1, 0xD8, 0x30, 0x1A, 0x63,
    0xCC, 0xCF, 0x57, 0xCF, 0xDE, 0x14, 0x72, 0x8A, 0x9E, 0x92, 0xEA, 0x2B, 0xC8, 0xD6, 0xC9, 0x90,
    0x81, 0xF6, 0x30, 0xB2, 0x60, 0xA4, 0x01, 0x7C, 0x3E, 0x34, 0xEF, 0xAF, 0x2F, 0x57, 0x4C, 0xDB,
    0x74, 0x8D, 0x63, 0x81, 0xA8, 0x8A, 0x48, 0x6A, 0x9E, 0x50, 0xBD, 0x06, 0x41, 0x42, 0x50, 0x39,
    0x69, 0xC6, 0x4D, 0x63, 0x4C, 0xB3, 0x41, 0x81, 0x84, 0x8A, 0xCF, 0x07, 0x33, 0x0D, 0x97, 0xF5,
    0xFB, 0xFE, 0xE3, 0x0D, 0x7D, 0xCE, 0x85, 0xA0, 0x74, 0xCA, 0xF3, 0x69, 0x67, 0x80, 0x5A, 0x84,
    0xDD, 0x71, 0x5E, 0x01, 0xDC, 0x34, 0xA8, 0x01, 0x91, 0xB5, 0x45, 0xF9, 0xCB, 0xFB, 0x9E, 0xE4,
    0x68, 0xEA, 0xDC, 0x72, 0x73, 0xB8, 0x6B, 0x16, 0x68, 0x08, 0x0E, 0xD3, 0x70, 0xF9, 0x07, 0xE6,
    0x7E, 0xDF, 0xFE, 0x96, 0x12, 0x31, 0x40, 0x53, 0x3D, 0x09, 0x39, 0xEC, 0x3B, 0x97, 0xBB, 0x7B,
    0x80, 0x7B, 0x78, 0xC9, 0x23, 0x61, 0x03, 0x24, 0xB6, 0xDA, 0x5C, 0x47, 0xA6, 0xE1, 0x92, 0x0D,
    0xB1, 0xF8, 0x0E, 0xD8, 0xC1, 0x29, 0x58, 0x4E, 0x81, 0xC7, 0xAE, 0xA1, 0x0A, 0x14, 0xAB, 0xAE,
    0x45, 0xED, 0xDB, 0x21, 0x5D, 0x98, 0xD2, 0x82, 0x43, 0xCF, 0xE7, 0x1C, 0x5F, 0x58, 0x20, 0x31,
    0x28, 0xEA, 0xAB, 0x37, 0x8C, 0x16, 0x56, 0x04, 0x84, 0xFA, 0x88, 0xAB, 0x35, 0x8F, 0xD8, 0x3B,
    0x44, 0x4C, 0x74, 0xB0, 0x51, 0xE0, 0x34, 0x1F, 0xF5, 0xF7, 0xEC, 0x98, 0xD4, 0x91, 0x10, 0x28,
    0x6C, 0x29, 0xCD, 0xF7, 0x21, 0xFE, 0xF9, 0xC0, 0xA9, 0x01, 0x69, 0x99, 0x08, 0x6A, 0xE8, 0x58,
    0x1A, 0x5A, 0x17, 0x94, 0x16, 0xE5, 0xB4, 0x04, 0x3B, 0x28, 0xA9, 0x30, 0x6A, 0xAA, 0x8E, 0x50,
    0x4A, 0x2E, 0xD5, 0x96, 0x4E, 0x9B, 0x67, 0xF9, 0xE3, 0xD9, 0x2F, 0x0A, 0x78, 0x80, 0x4C, 0xC3,
    0xA4, 0x35, 0x94, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

constexpr const char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <link rel="icon" href="/favicon.ico" sizes="50x50">
  <title>Quotes Clock Native</title>
  <style>
    :root { color-scheme: dark; --bg: #101114; --fg: #f6f3e8; --muted: #c9c3b1; --panel: #191b21; --field: #1b1d23; --field-border: #50535d; --line: #3b3d44; --tab: #272a32; --accent: #e5b841; --accent-fg: #17140b; --secondary: #31343d; --switch-off: #555965; --switch-on: #34c759; font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
    :root[data-theme="light"] { color-scheme: light; --bg: #f6f3e8; --fg: #1d1b16; --muted: #5f5a4d; --panel: #fffaf0; --field: #ffffff; --field-border: #b9ad92; --line: #d8cfbd; --tab: #ece3d1; --accent: #17140b; --accent-fg: #f6f3e8; --secondary: #d9d1c0; --switch-off: #b9bdc7; --switch-on: #34c759; }
    body { background: var(--bg); color: var(--fg); margin: 0; }
    main { margin: 0 auto; max-width: 48rem; padding: 1.25rem; }
    h1 { font-size: 1.8rem; margin: 0 0 1rem; }
    h2 { font-size: 1.05rem; margin: 0 0 .75rem; }
    .page-header { align-items: center; display: flex; gap: 1rem; justify-content: space-between; }
    .page-logo { background: #e5b841; border-radius: 6px; box-shadow: 0 0 0 1px rgb(255 255 255 / .16); flex: 0 0 auto; height: 50px; image-rendering: pixelated; padding: .35rem; width: 50px; }
    section { border-top: 1px solid var(--line); padding: 1rem 0; }
    footer { border-top: 1px solid var(--line); color: var(--muted); display: flex; flex-wrap: wrap; gap: .35rem 1rem; justify-content: space-between; padding: 1rem 0 0; }
    form { display: grid; gap: .75rem; }
    label, .field-label { color: var(--muted); display: grid; gap: .25rem; font-weight: 650; }
    input, select, button, output { box-sizing: border-box; font: inherit; min-height: 2.6rem; width: 100%; }
    input, select { background: var(--field); border: 1px solid var(--field-border); border-radius: 8px; color: var(--fg); padding: .65rem .75rem; }
    input:disabled { color: var(--muted); opacity: .72; }
    button { background: var(--accent); border: 0; border-radius: 8px; color: var(--accent-fg); cursor: pointer; font-weight: 750; padding: .65rem .8rem; }
    button.secondary { background: var(--secondary); color: var(--fg); }
    a { color: var(--accent); }
    dl { display: grid; gap: .45rem .8rem; grid-template-columns: max-content minmax(0, 1fr); }
    dt { color: var(--muted); font-weight: 750; }
    dd { margin: 0; overflow-wrap: anywhere; }
    .grid { display: grid; gap: .75rem; grid-template-columns: repeat(2, minmax(0, 1fr)); }
    .wide { grid-column: 1 / -1; }
    .tabs { display: flex; gap: .35rem; overflow-x: auto; padding: .25rem 0 .9rem; }
    .tab { background: var(--tab); color: var(--fg); flex: 0 0 auto; min-height: 2.35rem; padding: .55rem .75rem; width: auto; }
    .tab.active { background: var(--accent); color: var(--accent-fg); }
    .nav-button { flex: 0 0 auto; min-height: 2.35rem; padding: .55rem .75rem; width: auto; }
    .theme-button { margin-left: auto; }
    .panel { display: none; }
    .panel.active { display: block; }
    .summary, fieldset { background: var(--panel); border: 1px solid var(--line); border-radius: 8px; color: var(--fg); margin: .75rem 0 0; padding: .75rem; }
    fieldset { display: grid; gap: .75rem; grid-column: 1 / -1; margin: 0; }
    legend { color: var(--muted); font-weight: 750; padding: 0 .35rem; }
    .display-primary { background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: .75rem; }
    .display-group-grid { display: grid; gap: .75rem; grid-template-columns: repeat(2, minmax(0, 1fr)); }
    .password-wrap { display: grid; grid-template-columns: minmax(0, 1fr) 2.6rem; }
    .password-wrap input { border-bottom-right-radius: 0; border-top-right-radius: 0; }
    .icon-button { align-items: center; background: var(--secondary); border: 1px solid var(--field-border); border-left: 0; border-bottom-left-radius: 0; border-top-left-radius: 0; color: var(--fg); display: grid; min-height: 2.6rem; padding: 0; place-items: center; }
    .toggle-row { align-items: center; display: flex; justify-content: space-between; min-height: 2.6rem; }
    .switch { align-items: center; background: var(--switch-off); border: 0; border-radius: 999px; box-shadow: inset 0 0 0 1px rgb(255 255 255 / .12); color: var(--fg); display: inline-flex; min-height: 2rem; padding: .17rem; position: relative; transition: background-color .15s ease; width: 4.2rem; }
    .switch::before { background: #fff; border-radius: 999px; box-shadow: 0 1px 3px rgb(0 0 0 / .35); content: ""; display: block; height: 1.56rem; transition: transform .15s ease; width: 1.56rem; }
    .switch[aria-pressed="true"] { background: var(--switch-on); }
    .switch[aria-pressed="true"]::before { transform: translateX(2.3rem); }
    .switch-label { color: var(--muted); font-weight: 650; }
    .theme-switch { min-height: 2.35rem; padding: .2rem; width: 5.2rem; }
    .theme-switch[aria-pressed="true"]::before { transform: translateX(3.05rem); }
    .theme-icon { font-size: 1rem; line-height: 1; position: absolute; right: .7rem; }
    .theme-switch[aria-pressed="true"] .theme-icon { left: .7rem; right: auto; }
    .toggle-stack { background: var(--field); border: 1px solid var(--field-border); border-radius: 8px; display: grid; gap: .45rem; padding: .55rem .65rem; }
    .quote-category-panel { align-self: start; grid-column: 1; grid-row: span 2; }
    .grid-right { grid-column: 2; }
    .swatch-picker { display: grid; gap: .25rem; position: relative; }
    .swatch-button, .swatch-option { align-items: center; background: var(--field); border: 1px solid var(--field-border); color: var(--fg); display: flex; gap: .55rem; justify-content: flex-start; min-height: 2.6rem; text-align: left; }
    .swatch-button::after { content: "v"; margin-left: auto; }
    .swatch-menu { background: var(--panel); border: 1px solid var(--field-border); border-radius: 8px; box-shadow: 0 8px 20px rgb(0 0 0 / .35); display: grid; gap: .25rem; left: 0; padding: .35rem; position: absolute; right: 0; top: calc(100% + .25rem); z-index: 5; }
    .swatch-option.active { background: var(--accent); color: var(--accent-fg); }
    .swatch { border: 1px solid rgb(0 0 0 / .45); border-radius: 999px; box-shadow: inset 0 0 0 1px rgb(255 255 255 / .25); display: inline-block; flex: 0 0 auto; height: 1.25rem; width: 1.25rem; }
    .swatch[data-swatch="0"] { background: #e5b841; }
    .swatch[data-swatch="1"] { background: #c9332e; }
    .swatch[data-swatch="2"] { background: repeating-conic-gradient(#111 0 25%, #fff 0 50%) 50% / .55rem .55rem; }
    .swatch[data-swatch="3"] { background: repeating-conic-gradient(#c9332e 0 25%, #fff 0 50%) 50% / .55rem .55rem; }
    .swatch[data-swatch="4"] { background: repeating-conic-gradient(#c9332e 0 25%, #111 0 50%) 50% / .55rem .55rem; }
    .swatch[data-swatch="5"] { background: repeating-conic-gradient(#e5b841 0 25%, #fff 0 50%) 50% / .55rem .55rem; }
    .swatch[data-swatch="6"] { background: repeating-conic-gradient(#e5b841 0 25%, #111 0 50%) 50% / .55rem .55rem; }
    .swatch[data-swatch="7"] { background: repeating-conic-gradient(#c9332e 0 25%, #e5b841 0 50%) 50% / .55rem .55rem; }
    .swatch[data-swatch="8"] { background: #17140b; }
    .swatch[data-swatch="9"] { background: #fff; }
    .warning { border-color: #c9332e; color: var(--fg); }
    .range-row { align-items: center; display: grid; gap: .75rem; grid-template-columns: minmax(0, 1fr) 5rem; }
    .range-row output { margin: 0; min-height: 0; text-align: right; }
    input[type="range"] { --range-progress: 0%; appearance: none; -webkit-appearance: none; background: transparent; cursor: pointer; min-height: 2.6rem; padding: 0; }
    input[type="range"]::-webkit-slider-runnable-track { background: linear-gradient(90deg, var(--accent) 0 var(--range-progress), var(--field-border) var(--range-progress) 100%); border-radius: 999px; height: .55rem; }
    input[type="range"]::-webkit-slider-thumb { -webkit-appearance: none; background: #fff; border: 2px solid var(--accent); border-radius: 999px; box-shadow: 0 1px 4px rgb(0 0 0 / .4); height: 1.65rem; margin-top: -.52rem; width: 1.65rem; }
    input[type="range"]::-moz-range-track { background: var(--field-border); border: 0; border-radius: 999px; height: .55rem; }
    input[type="range"]::-moz-range-progress { background: var(--accent); border-radius: 999px; height: .55rem; }
    input[type="range"]::-moz-range-thumb { background: #fff; border: 2px solid var(--accent); border-radius: 999px; box-shadow: 0 1px 4px rgb(0 0 0 / .4); height: 1.65rem; width: 1.65rem; }
    input[type="range"]:focus-visible::-webkit-slider-thumb { outline: 2px solid var(--fg); outline-offset: 2px; }
    input[type="range"]:focus-visible::-moz-range-thumb { outline: 2px solid var(--fg); outline-offset: 2px; }
    .segmented, .layout-options, .time-options { display: grid; gap: .45rem; grid-template-columns: repeat(2, minmax(0, 1fr)); }
    .time-options { grid-template-columns: repeat(3, minmax(0, 1fr)); }
    .segmented button, .layout-options button, .time-options button { background: var(--tab); border: 1px solid var(--field-border); color: var(--fg); min-height: 2.4rem; }
    .segmented button.active, .layout-options button.active, .time-options button.active { background: var(--accent); color: var(--accent-fg); }
    .layout-thumb { align-items: center; display: grid; gap: .4rem; justify-items: center; }
    .screen { background: var(--field); border: 2px solid currentColor; display: grid; height: 2.1rem; padding: .15rem; width: 3.5rem; }
    .screen.portrait { height: 3.2rem; width: 2.25rem; }
    .screen::before { background: currentColor; content: ""; display: block; height: .45rem; }
    .screen.inverted { transform: rotate(180deg); }
    [hidden] { display: none !important; }
    .hidden { display: none; }
    output { color: var(--accent); display: block; margin-top: 1rem; overflow-wrap: anywhere; }
    @media (max-width: 720px) { .grid, .display-group-grid, .time-options { grid-template-columns: 1fr; } .grid-right, .quote-category-panel { grid-column: auto; grid-row: auto; } }
  </style>
</head>
<body>
<main>
  <header class="page-header">
    <h1>Quotes Clock</h1>
    <img class="page-logo" alt="Quotes Clock logo" src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADIAAAAyCAYAAAAeP4ixAAACIElEQVR42t2aO27DMAyGZUJb16IFfAwP2eMll8gtfI7eIodoF3v3kFu0Qy6RDoUdV9CDpEgpiYEACSxT/MyH5N9pvj/3V/MEh5U01h1n0vjzaSc2dyMRESqABlQWSArAdY46vghIyCmsM7nXi4D4nAg5sIxNnc+FsTkQEsW6tbHY7o4z2TbUhMBAiaeWNkTuXFCqvWq3dqAa0o4Gp22Ta6QEBHcuqLE18V2/fLgHSN0hriOxwqZEJdq1qN0jtfhxbGPHgUZexyLjRk6q7kCrSH0wLoBk87BaHSdWMxrdz2qtuAsMFYCbdmCe5LCai9Q2KthruWlnzZ2t0HfTtWodK0h7GE17GB/Gcddf6xtwS4u++jZ+m56xG21T1FugWhCYTLHYMBpjzM9Xz94UcoqekuoryNbJkIH2MLJgpAF8PjTvry9XTNt0jWOBqIpIap5QvQZBQlA5acZNY0yzQYGEis8HMw2X9fv+4w19zoWgdMrzaWeAWoTdcV4B3DSoAZG1RfnL+57kaOrccnO4axZoCA7TcPkH5n7f/pYSMUBTPQk57DuXu3uAe3jJI2EDJLbaXEem4ZINsfgO2MEpWE6Bx66hChSrrkXt2yFdmNKCQ8/nHF9YIDEo6qs3jBZWBIT6iKs1j9g7REx0sFHgNB/19+yY1JEQKGwpzfch/vnAqQFpmQhq6FgaWheUFuW0BDsoqTBqqo5QSi7Vlk6bZ/nj2S8KeIBMw6Q1lAAAAABJRU5ErkJggg==">
  </header>
  <section id="admin-setup" class="hidden">
    <h2>First-Run Admin Setup</h2>
    <form id="admin-form">
      <label>Admin password <input id="admin-password" type="password" minlength="8" required></label>
      <button type="submit">Set admin password</button>
    </form>
  </section>
  <section id="login-section">
    <h2>Admin Sign In</h2>
    <form id="login-form">
      <label>Admin password <input id="login-password" type="password"></label>
      <button type="submit">Use password</button>
    </form>
  </section>
  <section id="admin-panel" class="hidden">
    <nav id="tabs" class="tabs">
      <button class="tab active" type="button" data-tab="status">Status</button>
      <button class="tab" type="button" data-tab="wifi">Wi-Fi</button>
      <button class="tab" type="button" data-tab="display">Display</button>
      <button class="tab" type="button" data-tab="time">Time</button>
      <button class="tab" type="button" data-tab="ota">OTA</button>
      <button class="tab" type="button" data-tab="licenses">Licenses</button>
      <button id="theme-toggle" class="switch theme-switch theme-button" type="button" aria-pressed="false" aria-label="Toggle light mode"><span id="theme-icon" class="theme-icon">☾</span></button>
      <button id="logout" class="nav-button logout-button secondary" type="button">Log out</button>
    </nav>
    <div class="panel active" data-panel="status">
    <h2>Status</h2>
    <dl id="status"></dl>
    <p id="connection-summary" class="summary"></p>
    </div>
    <div class="panel" data-panel="wifi" hidden>
    <h2>Wi-Fi</h2>
    <form id="wifi-form">
      <div class="grid">
        <label>SSID <input id="ssid" autocomplete="off" required></label>
        <label>Password <span class="password-wrap"><input id="wifi-password" type="password" autocomplete="new-password"><button id="wifi-password-reveal" class="icon-button" type="button" aria-label="Reveal password">&#128065;</button></span></label>
        <label>Addressing <select id="static-ip"><option value="0">DHCP</option><option value="1">Static</option></select></label>
        <fieldset id="static-fields" hidden>
          <legend>Static address</legend>
          <div class="grid">
            <label>Static IP <input id="ip" placeholder="192.168.16.101"></label>
            <label>Subnet mask <input id="netmask" placeholder="255.255.255.0"></label>
            <label>Gateway <input id="gateway" placeholder="192.168.16.254"></label>
            <label>DNS 1 <input id="dns1" placeholder="192.168.16.254"></label>
            <label>DNS 2 <input id="dns2" placeholder="1.1.1.1"></label>
          </div>
        </fieldset>
      </div>
      <button type="submit">Save Wi-Fi</button>
      <button id="clear-wifi" class="secondary" type="button">Clear saved Wi-Fi</button>
    </form>
    </div>
    <div class="panel" data-panel="display" hidden>
    <h2>Display</h2>
    <form id="display-form">
      <label class="toggle-row display-primary"><span><span class="switch-label">Enabled</span></span><button id="display-enabled-toggle" class="switch" type="button" aria-pressed="true" aria-label="Toggle display enabled"></button><input id="display-enabled" type="hidden" value="1"></label>
      <fieldset>
        <legend>Clock and quote time</legend>
        <div class="display-group-grid">
          <label class="toggle-row"><span><span class="switch-label">Show Clock</span></span><button id="clock-visible-toggle" class="switch" type="button" aria-pressed="true" aria-label="Toggle clock visible"></button><input id="clock-visible" type="hidden" value="1"></label>
          <label id="watch-style-row" class="toggle-row" hidden><span><span class="switch-label">Watch style</span></span><button id="watch-style-toggle" class="switch" type="button" aria-pressed="false" aria-label="Toggle watch style"></button><input id="watch-style" type="hidden" value="0"></label>
          <label id="highlight-time-row" class="toggle-row grid-right"><span><span class="switch-label">Highlight time</span></span><button id="highlight-time-toggle" class="switch" type="button" aria-pressed="false" aria-label="Toggle time highlight"></button><input id="highlight-time-enabled" type="hidden" value="0"></label>
          <label class="toggle-row"><span><span class="switch-label">Show quotes</span></span><button id="quote-visible-toggle" class="switch" type="button" aria-pressed="true" aria-label="Toggle quotes visible"></button><input id="quote-visible" type="hidden" value="1"></label>
          <label id="cadence-row" class="grid-right">Quote change <span class="range-row"><input id="cadence" type="range" min="1" max="1440" step="1" value="1"><output id="cadence-label">Every 1 min</output></span></label>
          <div id="quote-category-group" class="toggle-stack quote-category-panel">
            <label id="quote-time-specific-row" class="toggle-row"><span><span class="switch-label">Time-specific quotes</span></span><button id="quote-time-specific-toggle" class="switch" type="button" aria-pressed="true" aria-label="Toggle time-specific quotes"></button><input id="quote-time-specific-enabled" type="hidden" value="1"></label>
            <label id="quote-classics-row" class="toggle-row"><span><span class="switch-label">Classic quotes</span></span><button id="quote-classics-toggle" class="switch" type="button" aria-pressed="false" aria-label="Toggle classic quotes"></button><input id="quote-classics-enabled" type="hidden" value="0"></label>
          </div>
          <div id="highlight-color-wrap" class="swatch-picker grid-right">
            <span class="field-label">Highlight colour</span>
            <button id="highlight-color-button" class="swatch-button" type="button" aria-haspopup="listbox" aria-expanded="false"><span id="highlight-color-preview" class="swatch" data-swatch="0"></span><span id="highlight-color-name">Yellow</span></button>
            <div id="highlight-color-menu" class="swatch-menu" role="listbox" hidden>
              <button class="swatch-option" type="button" role="option" data-highlight-bg-color="0"><span class="swatch" data-swatch="0"></span><span>Yellow</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-bg-color="1"><span class="swatch" data-swatch="1"></span><span>Red</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-bg-color="2"><span class="swatch" data-swatch="2"></span><span>Grey</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-bg-color="3"><span class="swatch" data-swatch="3"></span><span>Light red</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-bg-color="4"><span class="swatch" data-swatch="4"></span><span>Dark red</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-bg-color="5"><span class="swatch" data-swatch="5"></span><span>Light yellow</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-bg-color="6"><span class="swatch" data-swatch="6"></span><span>Dark yellow</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-bg-color="7"><span class="swatch" data-swatch="7"></span><span>Orange</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-bg-color="8"><span class="swatch" data-swatch="8"></span><span>Black</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-bg-color="9"><span class="swatch" data-swatch="9"></span><span>White</span></button>
            </div>
            <input id="highlight-color" type="hidden" value="0">
          </div>
          <div id="highlight-text-color-wrap" class="swatch-picker grid-right">
            <span class="field-label">Highlight text colour</span>
            <button id="highlight-text-color-button" class="swatch-button" type="button" aria-haspopup="listbox" aria-expanded="false"><span id="highlight-text-color-preview" class="swatch" data-swatch="8"></span><span id="highlight-text-color-name">Black</span></button>
            <div id="highlight-text-color-menu" class="swatch-menu" role="listbox" hidden>
              <button class="swatch-option" type="button" role="option" data-highlight-text-color="0"><span class="swatch" data-swatch="0"></span><span>Yellow</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-text-color="1"><span class="swatch" data-swatch="1"></span><span>Red</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-text-color="2"><span class="swatch" data-swatch="2"></span><span>Grey</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-text-color="3"><span class="swatch" data-swatch="3"></span><span>Light red</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-text-color="4"><span class="swatch" data-swatch="4"></span><span>Dark red</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-text-color="5"><span class="swatch" data-swatch="5"></span><span>Light yellow</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-text-color="6"><span class="swatch" data-swatch="6"></span><span>Dark yellow</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-text-color="7"><span class="swatch" data-swatch="7"></span><span>Orange</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-text-color="8"><span class="swatch" data-swatch="8"></span><span>Black</span></button>
              <button class="swatch-option" type="button" role="option" data-highlight-text-color="9"><span class="swatch" data-swatch="9"></span><span>White</span></button>
            </div>
            <input id="highlight-text-color" type="hidden" value="8">
          </div>
          <p id="highlight-color-warning" class="summary warning wide" hidden>Highlight and text colours match, so the highlighted time may be hard to read.</p>
        </div>
      </fieldset>
      <fieldset>
        <legend>Main pane</legend>
        <div class="display-group-grid">
          <div id="main-pane-bg-color-wrap" class="swatch-picker">
            <span class="field-label">Background colour</span>
            <button id="main-pane-bg-color-button" class="swatch-button" type="button" aria-haspopup="listbox" aria-expanded="false"><span id="main-pane-bg-color-preview" class="swatch" data-swatch="9"></span><span id="main-pane-bg-color-name">White</span></button>
            <div id="main-pane-bg-color-menu" class="swatch-menu" role="listbox" data-swatch-menu="main-pane-bg-color" hidden></div>
            <input id="main-pane-bg-color" type="hidden" value="9">
          </div>
          <div id="main-pane-text-color-wrap" class="swatch-picker">
            <span class="field-label">Text colour</span>
            <button id="main-pane-text-color-button" class="swatch-button" type="button" aria-haspopup="listbox" aria-expanded="false"><span id="main-pane-text-color-preview" class="swatch" data-swatch="8"></span><span id="main-pane-text-color-name">Black</span></button>
            <div id="main-pane-text-color-menu" class="swatch-menu" role="listbox" data-swatch-menu="main-pane-text-color" hidden></div>
            <input id="main-pane-text-color" type="hidden" value="8">
          </div>
          <p id="main-pane-color-warning" class="summary warning wide" hidden>Main pane background and text colours match, so text may be hard to read.</p>
        </div>
      </fieldset>
      <fieldset>
        <legend>Bars</legend>
        <div class="display-group-grid">
          <label class="toggle-row"><span><span class="switch-label">Show sidebar</span></span><button id="sidebar-visible-toggle" class="switch" type="button" aria-pressed="true" aria-label="Toggle sidebar visible"></button><input id="sidebar-visible" type="hidden" value="1"></label>
          <div id="sidebar-color-wrap" class="swatch-picker">
            <span class="field-label">Sidebar colour</span>
            <button id="sidebar-color-button" class="swatch-button" type="button" aria-haspopup="listbox" aria-expanded="false"><span id="sidebar-color-preview" class="swatch" data-swatch="1"></span><span id="sidebar-color-name">Red</span></button>
            <div id="sidebar-color-menu" class="swatch-menu" role="listbox" data-swatch-menu="sidebar-color" hidden></div>
            <input id="sidebar-color" type="hidden" value="1">
          </div>
          <label class="toggle-row"><span><span class="switch-label">Show top bar</span></span><button id="top-bar-visible-toggle" class="switch" type="button" aria-pressed="true" aria-label="Toggle top bar visible"></button><input id="top-bar-visible" type="hidden" value="1"></label>
          <div id="top-bar-bg-color-wrap" class="swatch-picker">
            <span class="field-label">Top background</span>
            <button id="top-bar-bg-color-button" class="swatch-button" type="button" aria-haspopup="listbox" aria-expanded="false"><span id="top-bar-bg-color-preview" class="swatch" data-swatch="8"></span><span id="top-bar-bg-color-name">Black</span></button>
            <div id="top-bar-bg-color-menu" class="swatch-menu" role="listbox" data-swatch-menu="top-bar-bg-color" hidden></div>
            <input id="top-bar-bg-color" type="hidden" value="8">
          </div>
          <div id="top-bar-text-color-wrap" class="swatch-picker">
            <span class="field-label">Top text colour</span>
            <button id="top-bar-text-color-button" class="swatch-button" type="button" aria-haspopup="listbox" aria-expanded="false"><span id="top-bar-text-color-preview" class="swatch" data-swatch="0"></span><span id="top-bar-text-color-name">Yellow</span></button>
            <div id="top-bar-text-color-menu" class="swatch-menu" role="listbox" data-swatch-menu="top-bar-text-color" hidden></div>
            <input id="top-bar-text-color" type="hidden" value="0">
          </div>
          <label id="top-bar-date-format-wrap">Date format <select id="top-bar-date-format"><option value="0">Auto by layout</option><option value="1">2026-06-02</option><option value="2">Tue, 02 Jun 2026</option><option value="3">Tuesday, 02 June 2026</option><option value="4">Tue, Jun 02, 2026</option><option value="5">Tuesday, June 02, 2026</option></select></label>
          <label class="toggle-row"><span><span class="switch-label">Show bottom bar</span></span><button id="bottom-bar-visible-toggle" class="switch" type="button" aria-pressed="true" aria-label="Toggle bottom bar visible"></button><input id="bottom-bar-visible" type="hidden" value="1"></label>
          <div id="bottom-bar-bg-color-wrap" class="swatch-picker">
            <span class="field-label">Bottom background</span>
            <button id="bottom-bar-bg-color-button" class="swatch-button" type="button" aria-haspopup="listbox" aria-expanded="false"><span id="bottom-bar-bg-color-preview" class="swatch" data-swatch="0"></span><span id="bottom-bar-bg-color-name">Yellow</span></button>
            <div id="bottom-bar-bg-color-menu" class="swatch-menu" role="listbox" data-swatch-menu="bottom-bar-bg-color" hidden></div>
            <input id="bottom-bar-bg-color" type="hidden" value="0">
          </div>
          <div id="bottom-bar-text-color-wrap" class="swatch-picker">
            <span class="field-label">Bottom text colour</span>
            <button id="bottom-bar-text-color-button" class="swatch-button" type="button" aria-haspopup="listbox" aria-expanded="false"><span id="bottom-bar-text-color-preview" class="swatch" data-swatch="8"></span><span id="bottom-bar-text-color-name">Black</span></button>
            <div id="bottom-bar-text-color-menu" class="swatch-menu" role="listbox" data-swatch-menu="bottom-bar-text-color" hidden></div>
            <input id="bottom-bar-text-color" type="hidden" value="8">
          </div>
        </div>
      </fieldset>
      <fieldset>
        <legend>Layout</legend>
        <div class="layout-options" role="radiogroup" aria-label="Display layout">
          <button class="layout-thumb" type="button" data-layout="0"><span class="screen"></span><span>Landscape</span></button>
          <button class="layout-thumb" type="button" data-layout="1"><span class="screen inverted"></span><span>Landscape inverted</span></button>
          <button class="layout-thumb" type="button" data-layout="2"><span class="screen portrait"></span><span>Portrait</span></button>
          <button class="layout-thumb" type="button" data-layout="3"><span class="screen portrait inverted"></span><span>Portrait inverted</span></button>
        </div>
        <input id="layout" type="hidden" value="0">
      </fieldset>
      <fieldset>
        <legend>Spacing</legend>
        <label>Content margin <span class="range-row"><input id="margin" type="range" min="16" max="72" step="4" value="40"><output id="margin-label">40 px</output></span></label>
      </fieldset>
      <button id="refresh" class="secondary" type="button">Force refresh</button>
    </form>
    </div>
    <div class="panel" data-panel="time" hidden>
    <h2>Time</h2>
    <form id="time-form">
      <div class="grid">
        <fieldset>
          <legend>Clock</legend>
          <div class="grid">
            <label>Timezone <select id="timezone"><option value="0">UTC</option><option value="1">Europe/London</option><option value="2">Europe/Central</option><option value="3">Europe/Eastern</option><option value="4">US/Eastern</option><option value="5">US/Central</option><option value="6">US/Mountain</option><option value="7">US/Pacific</option><option value="8">Australia/Sydney</option><option value="9">Asia/Tokyo</option></select></label>
            <label>DST <select id="dst"><option value="0">Off</option><option value="1">Auto</option><option value="2">On</option></select></label>
            <div>
              <span class="field-label">Format</span>
              <div class="segmented" role="radiogroup" aria-label="Clock format">
                <button type="button" data-clock="0">24-hour</button>
                <button type="button" data-clock="1">AM/PM</button>
              </div>
              <input id="clock-format" type="hidden" value="0">
            </div>
          </div>
        </fieldset>
        <fieldset>
          <legend>Synchronisation</legend>
          <div class="time-options" role="radiogroup" aria-label="NTP source">
            <button type="button" data-ntp="0">Default pool</button>
            <button type="button" data-ntp="1">DHCP option 42</button>
            <button type="button" data-ntp="2">Manual</button>
          </div>
          <input id="ntp-mode" type="hidden" value="0">
        </fieldset>
        <fieldset id="manual-ntp-fields" hidden>
          <legend>Manual servers</legend>
          <div class="grid">
            <label>NTP server 1 <input id="ntp1" placeholder="pool.ntp.org"></label>
            <label>NTP server 2 <input id="ntp2"></label>
            <label>NTP server 3 <input id="ntp3"></label>
          </div>
        </fieldset>
      </div>
      <button type="submit">Save time</button>
    </form>
    </div>
    <div class="panel" data-panel="ota" hidden>
    <h2>OTA</h2>
    <form id="ota-form">
      <label>Firmware binary <input id="ota-file" type="file" accept=".bin,application/octet-stream"></label>
      <p class="summary">OTA upload accepts the app image, usually quotes-clock-native.bin. The rescue image is a full serial-flash image and will not work through this uploader.</p>
      <button type="submit">Upload firmware</button>
    </form>
    </div>
    <div class="panel" data-panel="licenses" hidden>
    <h2>Licenses</h2>
    <p class="summary">Quotes Clock firmware and project tooling are open source under the MIT License. Quote data, imported datasets, and external references remain subject to their own rights and license terms.</p>
    <dl>
      <dt>ESP-IDF</dt><dd>Espressif IoT Development Framework, Apache-2.0. Used for ESP32 firmware, networking, TLS, OTA, NVS, and system services.</dd>
      <dt>FreeRTOS</dt><dd>Real-time kernel components supplied through ESP-IDF, MIT license.</dd>
      <dt>mbedTLS</dt><dd>TLS and cryptography components supplied through ESP-IDF, Apache-2.0.</dd>
      <dt>casio-f91w-fsm</dt><dd>MIT-licensed reference geometry for the optional watch-style clock display.</dd>
      <dt>Quotables</dt><dd>General/classic quote corpus from alvations/Quotables, CC0-1.0 source claim. Imported quotes remain tagged for rights and provenance review.</dd>
      <dt>Literary clock data</dt><dd>Import tooling can fetch external literary-clock datasets, but those generated datasets are not bundled in the public tree until reviewed.</dd>
    </dl>
    </div>
  </section>
  <output id="message">Loading...</output>
  <footer><span id="version">Quotes Clock</span><span>MIT License</span></footer>
</main>
<script>
let password = sessionStorage.getItem('qcAdminPassword') || '';
let wifiPasswordChanged = false;
let savedWifiPassword = '';
let displayAutosaveTimer = 0;
let messageClearTimer = 0;
let displaySyncHoldUntil = 0;
let suppressDisplayAutosave = false;
const $ = (id) => document.getElementById(id);
const message = $('message');
function setMessage(value, transient = false, timeout = 6000) {
  clearTimeout(messageClearTimer);
  message.value = value || '';
  if (transient && value) {
    const expected = message.value;
    messageClearTimer = setTimeout(() => {
      if (message.value === expected) message.value = '';
    }, timeout);
  }
}
function authHeaders(extra = {}) { return password ? {...extra, Authorization: 'Basic ' + btoa('admin:' + password)} : extra; }
function holdDisplaySync(ms = 2500) {
  displaySyncHoldUntil = Math.max(displaySyncHoldUntil, Date.now() + ms);
}
async function run(action) {
  try {
    await action();
  } catch (error) {
    setMessage(error.message || error);
    loadStatus(true).catch(() => {});
  }
}
async function json(path, options = {}) {
  const response = await fetch(path, {...options, cache: 'no-store'});
  const text = await response.text();
  if (!response.ok) throw new Error(text || response.status);
  return text ? JSON.parse(text) : {};
}
async function postJson(path, body) {
  return json(path, {method: 'POST', headers: authHeaders({'Content-Type':'application/json'}), body: JSON.stringify(body)});
}
function setValue(id, value) { if ($(id)) $(id).value = value ?? ''; }
function modeText(mode) { return ['default pool', 'DHCP option 42', 'manual'][mode] || `mode ${mode}`; }
function syncStatusText(status) { return ['reset', 'completed', 'in progress'][status] || `status ${status}`; }
function durationText(ms) {
  let seconds = Math.max(0, Math.floor(Number(ms || 0) / 1000));
  const days = Math.floor(seconds / 86400);
  seconds %= 86400;
  const hours = Math.floor(seconds / 3600);
  seconds %= 3600;
  const minutes = Math.floor(seconds / 60);
  seconds %= 60;
  const parts = [];
  if (days) parts.push(`${days}d`);
  if (hours || parts.length) parts.push(`${hours}h`);
  if (minutes || parts.length) parts.push(`${minutes}m`);
  parts.push(`${seconds}s`);
  return parts.join(' ');
}
function setTheme(theme) {
  const next = theme === 'light' ? 'light' : 'dark';
  document.documentElement.dataset.theme = next;
  localStorage.setItem('qcTheme', next);
  $('theme-toggle').setAttribute('aria-pressed', next === 'light' ? 'true' : 'false');
  $('theme-icon').textContent = next === 'light' ? '☀' : '☾';
}
setTheme(localStorage.getItem('qcTheme') || 'dark');
function setTab(name) {
  document.querySelectorAll('.tab').forEach((tab) => tab.classList.toggle('active', tab.dataset.tab === name));
  document.querySelectorAll('.panel').forEach((panel) => {
    const active = panel.dataset.panel === name;
    panel.classList.toggle('active', active);
    panel.hidden = !active;
  });
}
document.querySelectorAll('.tab').forEach((tab) => tab.addEventListener('click', () => setTab(tab.dataset.tab)));
$('theme-toggle').addEventListener('click', () => setTheme(document.documentElement.dataset.theme === 'light' ? 'dark' : 'light'));
function maskPassword(value) {
  return value ? '*'.repeat(Math.min(Math.max(value.length, 8), 16)) : '';
}
function setSavedWifiPassword(value) {
  const input = $('wifi-password');
  savedWifiPassword = value || '';
  wifiPasswordChanged = false;
  input.dataset.saved = savedWifiPassword ? '1' : '0';
  input.type = 'password';
  input.value = maskPassword(savedWifiPassword);
}
function wifiPasswordForSubmit() {
  const input = $('wifi-password');
  return input.dataset.saved === '1' && !wifiPasswordChanged && input.value === maskPassword(savedWifiPassword) ? '' : input.value;
}
function updateStaticFields() {
  const isStatic = $('static-ip').value === '1';
  $('static-fields').hidden = !isStatic;
  ['ip', 'netmask', 'gateway', 'dns1', 'dns2'].forEach((id) => $(id).disabled = !isStatic);
}
function setDisplayEnabled(enabled) {
  $('display-enabled').value = enabled ? '1' : '0';
  $('display-enabled-toggle').setAttribute('aria-pressed', enabled ? 'true' : 'false');
}
function setClockVisible(visible) {
  $('clock-visible').value = visible ? '1' : '0';
  $('clock-visible-toggle').setAttribute('aria-pressed', visible ? 'true' : 'false');
}
function setWatchStyle(enabled) {
  $('watch-style').value = enabled ? '1' : '0';
  $('watch-style-toggle').setAttribute('aria-pressed', enabled ? 'true' : 'false');
}
function updateWatchStyleAvailability() {
  const available = $('quote-visible').value !== '1' && $('top-bar-visible').value !== '1' &&
    $('bottom-bar-visible').value !== '1';
  $('watch-style-row').hidden = !available;
}
function setQuoteVisible(visible) {
  $('quote-visible').value = visible ? '1' : '0';
  $('quote-visible-toggle').setAttribute('aria-pressed', visible ? 'true' : 'false');
  $('cadence-row').hidden = !visible;
  $('cadence').disabled = !visible;
  $('quote-category-group').hidden = !visible;
  $('highlight-time-row').hidden = !visible;
  const highlightControlsVisible = visible && $('highlight-time-enabled').value === '1';
  $('highlight-color-wrap').hidden = !highlightControlsVisible;
  $('highlight-text-color-wrap').hidden = !highlightControlsVisible;
  updateHighlightWarning();
  updateWatchStyleAvailability();
}
function setQuoteTimeSpecific(enabled) {
  if (!enabled && $('quote-classics-enabled').value !== '1') enabled = true;
  $('quote-time-specific-enabled').value = enabled ? '1' : '0';
  $('quote-time-specific-toggle').setAttribute('aria-pressed', enabled ? 'true' : 'false');
}
function setQuoteClassics(enabled) {
  if (!enabled && $('quote-time-specific-enabled').value !== '1') enabled = true;
  $('quote-classics-enabled').value = enabled ? '1' : '0';
  $('quote-classics-toggle').setAttribute('aria-pressed', enabled ? 'true' : 'false');
}
function syncQuoteCategories(timeSpecific, classics) {
  if (!timeSpecific && !classics) timeSpecific = true;
  $('quote-time-specific-enabled').value = timeSpecific ? '1' : '0';
  $('quote-time-specific-toggle').setAttribute('aria-pressed', timeSpecific ? 'true' : 'false');
  $('quote-classics-enabled').value = classics ? '1' : '0';
  $('quote-classics-toggle').setAttribute('aria-pressed', classics ? 'true' : 'false');
}
function setHighlightTime(enabled) {
  $('highlight-time-enabled').value = enabled ? '1' : '0';
  $('highlight-time-toggle').setAttribute('aria-pressed', enabled ? 'true' : 'false');
  const visible = $('quote-visible').value === '1' && enabled;
  $('highlight-color-wrap').hidden = !visible;
  $('highlight-text-color-wrap').hidden = !visible;
  updateHighlightWarning();
}
const highlightColorNames = ['Yellow', 'Red', 'Grey', 'Light red', 'Dark red', 'Light yellow', 'Dark yellow', 'Orange', 'Black', 'White'];
const displayColorNames = highlightColorNames;
function normalizedHighlightColor(value, fallback) {
  const parsed = Number(value);
  return String(Math.max(0, Math.min(9, Number.isFinite(parsed) ? parsed : fallback)));
}
function populateSwatchMenu(control) {
  const menu = $(`${control}-menu`);
  if (!menu) return;
  menu.innerHTML = displayColorNames.map((name, index) => `<button class="swatch-option" type="button" role="option" data-color-control="${control}" data-color-value="${index}"><span class="swatch" data-swatch="${index}"></span><span>${name}</span></button>`).join('');
}
['main-pane-bg-color', 'main-pane-text-color', 'sidebar-color', 'top-bar-bg-color', 'top-bar-text-color', 'bottom-bar-bg-color', 'bottom-bar-text-color'].forEach(populateSwatchMenu);
function normalizedDisplayColor(value, fallback) {
  const parsed = Number(value);
  return String(Math.max(0, Math.min(9, Number.isFinite(parsed) ? parsed : fallback)));
}
function selectDisplayColor(control, value, fallback) {
  const selected = normalizedDisplayColor(value, fallback);
  $(`${control}`).value = selected;
  $(`${control}-preview`).dataset.swatch = selected;
  $(`${control}-name`).textContent = displayColorNames[Number(selected)] || displayColorNames[fallback];
  document.querySelectorAll(`[data-color-control="${control}"]`).forEach((button) => {
    const active = button.dataset.colorValue === selected;
    button.classList.toggle('active', active);
    button.setAttribute('aria-selected', active ? 'true' : 'false');
  });
  if (control === 'main-pane-bg-color' || control === 'main-pane-text-color') updateMainPaneWarning();
}
function setSwatchMenu(menuId, buttonId, open) {
  $(menuId).hidden = !open;
  $(buttonId).setAttribute('aria-expanded', open ? 'true' : 'false');
}
function setSidebarVisible(visible) {
  $('sidebar-visible').value = visible ? '1' : '0';
  $('sidebar-visible-toggle').setAttribute('aria-pressed', visible ? 'true' : 'false');
  $('sidebar-color-wrap').hidden = !visible;
}
function setTopBarVisible(visible) {
  $('top-bar-visible').value = visible ? '1' : '0';
  $('top-bar-visible-toggle').setAttribute('aria-pressed', visible ? 'true' : 'false');
  $('top-bar-bg-color-wrap').hidden = !visible;
  $('top-bar-text-color-wrap').hidden = !visible;
  $('top-bar-date-format-wrap').hidden = !visible;
  updateWatchStyleAvailability();
}
function setBottomBarVisible(visible) {
  $('bottom-bar-visible').value = visible ? '1' : '0';
  $('bottom-bar-visible-toggle').setAttribute('aria-pressed', visible ? 'true' : 'false');
  $('bottom-bar-bg-color-wrap').hidden = !visible;
  $('bottom-bar-text-color-wrap').hidden = !visible;
  updateWatchStyleAvailability();
}
function updateHighlightWarning() {
  $('highlight-color-warning').hidden = $('quote-visible').value !== '1' || $('highlight-time-enabled').value !== '1' || $('highlight-color').value !== $('highlight-text-color').value;
}
function updateMainPaneWarning() {
  $('main-pane-color-warning').hidden = $('main-pane-bg-color').value !== $('main-pane-text-color').value;
}
function selectHighlightColor(value) {
  const selected = normalizedHighlightColor(value, 0);
  $('highlight-color').value = selected;
  $('highlight-color-preview').dataset.swatch = selected;
  $('highlight-color-name').textContent = highlightColorNames[Number(selected)] || highlightColorNames[0];
  document.querySelectorAll('[data-highlight-bg-color]').forEach((button) => {
    const active = button.dataset.highlightBgColor === selected;
    button.classList.toggle('active', active);
    button.setAttribute('aria-selected', active ? 'true' : 'false');
  });
  updateHighlightWarning();
}
function selectHighlightTextColor(value) {
  const selected = normalizedHighlightColor(value, 8);
  $('highlight-text-color').value = selected;
  $('highlight-text-color-preview').dataset.swatch = selected;
  $('highlight-text-color-name').textContent = highlightColorNames[Number(selected)] || highlightColorNames[8];
  document.querySelectorAll('[data-highlight-text-color]').forEach((button) => {
    const active = button.dataset.highlightTextColor === selected;
    button.classList.toggle('active', active);
    button.setAttribute('aria-selected', active ? 'true' : 'false');
  });
  updateHighlightWarning();
}
function setHighlightMenu(menuId, buttonId, open) {
  $(menuId).hidden = !open;
  $(buttonId).setAttribute('aria-expanded', open ? 'true' : 'false');
}
function selectLayout(value) {
  const selected = String(value ?? 0);
  $('layout').value = selected;
  document.querySelectorAll('[data-layout]').forEach((button) => button.classList.toggle('active', button.dataset.layout === selected));
}
function selectClock(value) {
  const selected = String(value ?? 0);
  $('clock-format').value = selected;
  document.querySelectorAll('[data-clock]').forEach((button) => button.classList.toggle('active', button.dataset.clock === selected));
}
function updateRangeProgress(id) {
  const input = $(id);
  const min = Number(input.min || 0);
  const max = Number(input.max || 100);
  const value = Number(input.value || min);
  const percent = max > min ? ((value - min) / (max - min)) * 100 : 0;
  input.style.setProperty('--range-progress', `${Math.max(0, Math.min(100, percent))}%`);
}
function cadenceText(value) {
  const minutes = Math.max(1, Math.min(1440, Number(value) || 1));
  if (minutes === 1440) return 'Every day';
  if (minutes % 60 === 0) {
    const hours = minutes / 60;
    return `Every ${hours} h`;
  }
  return `Every ${minutes} min`;
}
function updateRangeLabels() {
  $('cadence-label').value = cadenceText($('cadence').value);
  $('margin-label').value = `${$('margin').value} px`;
  updateRangeProgress('cadence');
  updateRangeProgress('margin');
}
function updateNtpFields() {
  const manual = $('ntp-mode').value === '2';
  $('manual-ntp-fields').hidden = !manual;
  ['ntp1', 'ntp2', 'ntp3'].forEach((id) => $(id).disabled = !manual);
}
function displayPayload() {
  return {
    displayEnabled: $('display-enabled').value === '1', layout: +$('layout').value, cadence: +$('cadence').value,
    timezone: +$('timezone').value, dstMode: +$('dst').value, clockFormat: +$('clock-format').value, contentMargin: +$('margin').value,
    clockVisible: $('clock-visible').value === '1', quoteVisible: $('quote-visible').value === '1',
    quoteTimeSpecificEnabled: $('quote-time-specific-enabled').value === '1',
    quoteClassicsEnabled: $('quote-classics-enabled').value === '1',
    highlightTimeEnabled: $('highlight-time-enabled').value === '1',
    highlightTimeColor: +$('highlight-color').value, highlightTimeTextColor: +$('highlight-text-color').value,
    mainPaneBgColor: +$('main-pane-bg-color').value, mainPaneTextColor: +$('main-pane-text-color').value,
    sidebarVisible: $('sidebar-visible').value === '1', sidebarColor: +$('sidebar-color').value,
    bottomBarVisible: $('bottom-bar-visible').value === '1', bottomBarBgColor: +$('bottom-bar-bg-color').value,
    bottomBarTextColor: +$('bottom-bar-text-color').value, topBarVisible: $('top-bar-visible').value === '1',
    topBarBgColor: +$('top-bar-bg-color').value, topBarTextColor: +$('top-bar-text-color').value,
    topBarDateFormat: +$('top-bar-date-format').value, watchStyle: $('watch-style').value === '1'
  };
}
async function saveDisplayNow() {
  if (suppressDisplayAutosave || !$('admin-panel') || $('admin-panel').classList.contains('hidden')) return;
  clearTimeout(displayAutosaveTimer);
  holdDisplaySync();
  await run(async () => {
    await postJson('/api/display', displayPayload());
    holdDisplaySync(1200);
    setMessage('Display settings saved.', true);
    await loadStatus(true);
  });
}
$('wifi-password-reveal').addEventListener('click', () => {
  const input = $('wifi-password');
  const revealing = input.type === 'password';
  input.type = revealing ? 'text' : 'password';
  if (input.dataset.saved === '1' && !wifiPasswordChanged)
    input.value = revealing ? savedWifiPassword : maskPassword(savedWifiPassword);
});
$('wifi-password').addEventListener('focus', () => {
  if ($('wifi-password').dataset.saved === '1' && !wifiPasswordChanged) $('wifi-password').select();
});
$('wifi-password').addEventListener('input', () => {
  wifiPasswordChanged = true;
  $('wifi-password').dataset.saved = '0';
});
$('static-ip').addEventListener('change', updateStaticFields);
$('display-enabled-toggle').addEventListener('click', () => { setDisplayEnabled($('display-enabled').value !== '1'); saveDisplayNow(); });
$('clock-visible-toggle').addEventListener('click', () => { setClockVisible($('clock-visible').value !== '1'); saveDisplayNow(); });
$('watch-style-toggle').addEventListener('click', () => { setWatchStyle($('watch-style').value !== '1'); saveDisplayNow(); });
$('quote-visible-toggle').addEventListener('click', () => { setQuoteVisible($('quote-visible').value !== '1'); saveDisplayNow(); });
$('quote-time-specific-toggle').addEventListener('click', () => { setQuoteTimeSpecific($('quote-time-specific-enabled').value !== '1'); saveDisplayNow(); });
$('quote-classics-toggle').addEventListener('click', () => { setQuoteClassics($('quote-classics-enabled').value !== '1'); saveDisplayNow(); });
$('highlight-time-toggle').addEventListener('click', () => { setHighlightTime($('highlight-time-enabled').value !== '1'); saveDisplayNow(); });
$('sidebar-visible-toggle').addEventListener('click', () => { setSidebarVisible($('sidebar-visible').value !== '1'); saveDisplayNow(); });
$('top-bar-visible-toggle').addEventListener('click', () => { setTopBarVisible($('top-bar-visible').value !== '1'); saveDisplayNow(); });
$('bottom-bar-visible-toggle').addEventListener('click', () => { setBottomBarVisible($('bottom-bar-visible').value !== '1'); saveDisplayNow(); });
$('highlight-color-button').addEventListener('click', () => setHighlightMenu('highlight-color-menu', 'highlight-color-button', $('highlight-color-menu').hidden));
$('highlight-text-color-button').addEventListener('click', () => setHighlightMenu('highlight-text-color-menu', 'highlight-text-color-button', $('highlight-text-color-menu').hidden));
['main-pane-bg-color', 'main-pane-text-color', 'sidebar-color', 'top-bar-bg-color', 'top-bar-text-color', 'bottom-bar-bg-color', 'bottom-bar-text-color'].forEach((control) => {
  $(`${control}-button`).addEventListener('click', () => setSwatchMenu(`${control}-menu`, `${control}-button`, $(`${control}-menu`).hidden));
});
document.querySelectorAll('[data-highlight-bg-color]').forEach((button) => button.addEventListener('click', () => {
  selectHighlightColor(button.dataset.highlightBgColor);
  setHighlightMenu('highlight-color-menu', 'highlight-color-button', false);
  saveDisplayNow();
}));
document.querySelectorAll('[data-highlight-text-color]').forEach((button) => button.addEventListener('click', () => {
  selectHighlightTextColor(button.dataset.highlightTextColor);
  setHighlightMenu('highlight-text-color-menu', 'highlight-text-color-button', false);
  saveDisplayNow();
}));
document.querySelectorAll('[data-color-control]').forEach((button) => button.addEventListener('click', () => {
  selectDisplayColor(button.dataset.colorControl, button.dataset.colorValue, Number($(`${button.dataset.colorControl}`).value || 0));
  setSwatchMenu(`${button.dataset.colorControl}-menu`, `${button.dataset.colorControl}-button`, false);
  saveDisplayNow();
}));
document.addEventListener('click', (event) => {
  if (!$('highlight-color-wrap').contains(event.target)) setHighlightMenu('highlight-color-menu', 'highlight-color-button', false);
  if (!$('highlight-text-color-wrap').contains(event.target)) setHighlightMenu('highlight-text-color-menu', 'highlight-text-color-button', false);
  ['main-pane-bg-color', 'main-pane-text-color', 'sidebar-color', 'top-bar-bg-color', 'top-bar-text-color', 'bottom-bar-bg-color', 'bottom-bar-text-color'].forEach((control) => {
    if (!$(`${control}-wrap`).contains(event.target)) setSwatchMenu(`${control}-menu`, `${control}-button`, false);
  });
});
document.querySelectorAll('[data-layout]').forEach((button) => button.addEventListener('click', () => { selectLayout(button.dataset.layout); saveDisplayNow(); }));
document.querySelectorAll('[data-clock]').forEach((button) => button.addEventListener('click', () => { selectClock(button.dataset.clock); saveDisplayNow(); }));
document.querySelectorAll('[data-ntp]').forEach((button) => button.addEventListener('click', () => {
  $('ntp-mode').value = button.dataset.ntp;
  document.querySelectorAll('[data-ntp]').forEach((item) => item.classList.toggle('active', item.dataset.ntp === $('ntp-mode').value));
  updateNtpFields();
}));
$('cadence').addEventListener('input', updateRangeLabels);
$('margin').addEventListener('input', updateRangeLabels);
$('cadence').addEventListener('change', saveDisplayNow);
$('margin').addEventListener('change', saveDisplayNow);
$('timezone').addEventListener('change', saveDisplayNow);
$('dst').addEventListener('change', saveDisplayNow);
$('top-bar-date-format').addEventListener('change', saveDisplayNow);
$('ntp-mode').addEventListener('change', updateNtpFields);
function logout() {
  password = '';
  sessionStorage.removeItem('qcAdminPassword');
  setValue('login-password', '');
  setValue('admin-password', '');
  setSavedWifiPassword('');
  setValue('ota-file', '');
  setTab('status');
  $('admin-panel').classList.add('hidden');
  $('login-section').classList.remove('hidden');
  setMessage('Signed out.', true);
  loadStatus(true).catch(() => {});
}
$('logout').addEventListener('click', logout);
function reasonText(reason) {
  const reasons = {0: 'none', 2: 'auth expired', 15: '4-way handshake timeout', 201: 'AP not found', 202: 'authentication failed', 203: 'association failed', 204: 'handshake timeout'};
  return reasons[reason] || `code ${reason}`;
}
function connectionSummary(data) {
  const wifi = data.wifi || {};
  const platform = data.platform || {};
  const display = data.display || {};
  const displayText = display.busy ? 'Display is refreshing; e-paper boot refreshes can take a while.' : `Display idle; last refresh took ${display.lastRefreshMs || 0} ms.`;
  if (wifi.connected) {
    return `Connected to ${platform.stationSsid || 'Wi-Fi'} at ${wifi.ipAddress || 'no IP yet'}. Setup AP is off. ${displayText}`;
  }
  if (wifi.staConfigured) {
    const ap = platform.fallbackApActive ? ` Setup AP ${platform.fallbackApSsid || ''} is available.` : '';
    return `Trying saved Wi-Fi ${platform.stationSsid || ''}; last disconnect reason: ${reasonText(wifi.lastDisconnectReason || 0)}.${ap} ${displayText}`;
  }
  return `No saved Wi-Fi. Connect to setup AP ${platform.fallbackApSsid || ''} and save network details. ${displayText}`;
}
function syncDisplayFromStatus(display) {
  if (!display || suppressDisplayAutosave || $('admin-panel').classList.contains('hidden')) return;
  if (Date.now() < displaySyncHoldUntil) return;
  const form = $('display-form');
  if (form && form.contains(document.activeElement)) return;
  setDisplayEnabled(!!display.displayEnabled);
  selectLayout(display.layout);
  setValue('cadence', display.cadence);
  selectClock(display.clockFormat);
  setValue('margin', display.contentMargin);
  setClockVisible(display.clockVisible !== false);
  setQuoteVisible(display.quoteVisible !== false);
  setWatchStyle(!!display.watchStyle);
  syncQuoteCategories(display.quoteTimeSpecificEnabled !== false, !!display.quoteClassicsEnabled);
  setHighlightTime(!!display.highlightTimeEnabled);
  selectHighlightColor(display.highlightTimeColor);
  selectHighlightTextColor(display.highlightTimeTextColor);
  selectDisplayColor('main-pane-bg-color', display.mainPaneBgColor, 9);
  selectDisplayColor('main-pane-text-color', display.mainPaneTextColor, 8);
  updateMainPaneWarning();
  setSidebarVisible(display.sidebarVisible !== false);
  selectDisplayColor('sidebar-color', display.sidebarColor, 1);
  setTopBarVisible(display.topBarVisible !== false);
  selectDisplayColor('top-bar-bg-color', display.topBarBgColor, 8);
  selectDisplayColor('top-bar-text-color', display.topBarTextColor, 0);
  setValue('top-bar-date-format', display.topBarDateFormat ?? 0);
  setBottomBarVisible(display.bottomBarVisible !== false);
  selectDisplayColor('bottom-bar-bg-color', display.bottomBarBgColor, 0);
  selectDisplayColor('bottom-bar-text-color', display.bottomBarTextColor, 8);
  updateRangeLabels();
}
function watchStyleActive(display) {
  return !!display?.watchStyle && display?.clockVisible !== false && display?.quoteVisible === false &&
    display?.topBarVisible === false && display?.bottomBarVisible === false;
}
function renderStatus(data) {
  $('admin-setup').classList.toggle('hidden', !!data.adminConfigured);
  $('login-section').classList.toggle('hidden', !data.adminConfigured || !!password);
  $('admin-panel').classList.toggle('hidden', data.adminConfigured && !password);
  syncDisplayFromStatus(data.display);
  $('status').innerHTML = Object.entries({
    Version: data.app?.version || '',
    Admin: data.adminConfigured ? 'configured' : 'first-run setup required',
    Hostname: data.platform?.lanFqdn || data.platform?.hostname || '',
    StationMAC: data.platform?.stationMac || '',
    SavedSSID: data.platform?.stationSsid || '',
    WiFi: data.wifi?.connected ? `connected (${data.platform?.stationSsid || 'station'})` : 'not connected',
    Address: data.wifi?.ipAddress || '',
    LastReason: data.wifi?.lastDisconnectReason || 0,
    Reconnects: data.wifi?.reconnects ?? 0,
    SetupAP: data.platform?.fallbackApActive ? data.platform?.fallbackApSsid : 'off',
    Display: data.display?.busy ? 'busy' : 'idle',
    QuotePack: data.display?.quotePackReady ? `${data.display?.quoteCount ?? 0} quotes (${data.display?.timeSpecificQuoteCount ?? 0} time, ${data.display?.classicQuoteCount ?? 0} classics, ${data.display?.quotePackSize ?? 0} bytes)` : 'missing; using fallback',
    DisplaySettings: `${data.display?.displayEnabled ? 'enabled' : 'disabled'}, clock ${data.display?.clockVisible ? 'shown' : 'hidden'}, quotes ${data.display?.quoteVisible ? 'shown' : 'hidden'} (${data.display?.quoteTimeSpecificEnabled ? 'time' : ''}${data.display?.quoteTimeSpecificEnabled && data.display?.quoteClassicsEnabled ? '+' : ''}${data.display?.quoteClassicsEnabled ? 'classics' : ''}), watch ${watchStyleActive(data.display) ? 'on' : 'off'}, main ${displayColorNames[data.display?.mainPaneTextColor ?? 8] || 'text'} on ${displayColorNames[data.display?.mainPaneBgColor ?? 9] || 'background'}, highlight ${data.display?.highlightTimeEnabled ? `${highlightColorNames[data.display?.highlightTimeTextColor ?? 8] || 'text'} on ${highlightColorNames[data.display?.highlightTimeColor ?? 0] || 'highlight'}` : 'off'}, top ${data.display?.topBarVisible ? 'shown' : 'hidden'}, bottom ${data.display?.bottomBarVisible ? 'shown' : 'hidden'}, sidebar ${data.display?.sidebarVisible ? 'shown' : 'hidden'}, ${data.display?.quoteVisible ? cadenceText(data.display?.cadence) : 'quote cadence ignored'}, layout ${data.display?.layout ?? '?'}, margin ${data.display?.contentMargin ?? '?'} px`,
    Refreshes: data.display?.refreshes ?? 0,
    LastRefresh: `${data.display?.lastRefreshMs ?? 0} ms`,
    Uptime: durationText(data.system?.uptimeMs),
    SNTP: data.sntp?.enabled ? `${modeText(data.sntp.mode)}, ${syncStatusText(data.sntp.syncStatus)}${data.sntp?.servers?.length ? ` (${data.sntp.servers.join(', ')})` : ' (no servers)'}` : 'off',
    DhcpNtp: data.sntp?.dhcpNtpSupported ? 'compiled in' : 'not compiled in'
  }).map(([k,v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('');
  $('connection-summary').textContent = connectionSummary(data);
  $('version').textContent = `Quotes Clock ${data.app?.version || ''}`;
}
async function loadStatus(quiet = false) {
  const data = await json('/api/status');
  renderStatus(data);
  if (!quiet) setMessage(connectionSummary(data));
  return data;
}
async function loadConfig() {
  const data = await json('/api/config', {headers: authHeaders()});
  suppressDisplayAutosave = true;
  setValue('ssid', data.network?.ssid);
  setSavedWifiPassword(data.network?.password);
  setValue('static-ip', data.network?.staticIp ? '1' : '0');
  setValue('ip', data.network?.ip);
  setValue('netmask', data.network?.netmask);
  setValue('gateway', data.network?.gateway);
  setValue('dns1', data.network?.dns1);
  setValue('dns2', data.network?.dns2);
  updateStaticFields();
  setDisplayEnabled(!!data.display?.displayEnabled);
  selectLayout(data.display?.layout);
  setValue('cadence', data.display?.cadence);
  setValue('timezone', data.display?.timezone);
  setValue('dst', data.display?.dstMode);
  selectClock(data.display?.clockFormat);
  setValue('margin', data.display?.contentMargin);
  setClockVisible(data.display?.clockVisible !== false);
  setQuoteVisible(data.display?.quoteVisible !== false);
  setWatchStyle(!!data.display?.watchStyle);
  syncQuoteCategories(data.display?.quoteTimeSpecificEnabled !== false, !!data.display?.quoteClassicsEnabled);
  setHighlightTime(!!data.display?.highlightTimeEnabled);
  selectHighlightColor(data.display?.highlightTimeColor);
  selectHighlightTextColor(data.display?.highlightTimeTextColor);
  selectDisplayColor('main-pane-bg-color', data.display?.mainPaneBgColor, 9);
  selectDisplayColor('main-pane-text-color', data.display?.mainPaneTextColor, 8);
  updateMainPaneWarning();
  setSidebarVisible(data.display?.sidebarVisible !== false);
  selectDisplayColor('sidebar-color', data.display?.sidebarColor, 1);
  setTopBarVisible(data.display?.topBarVisible !== false);
  selectDisplayColor('top-bar-bg-color', data.display?.topBarBgColor, 8);
  selectDisplayColor('top-bar-text-color', data.display?.topBarTextColor, 0);
  setValue('top-bar-date-format', data.display?.topBarDateFormat ?? 0);
  setBottomBarVisible(data.display?.bottomBarVisible !== false);
  selectDisplayColor('bottom-bar-bg-color', data.display?.bottomBarBgColor, 0);
  selectDisplayColor('bottom-bar-text-color', data.display?.bottomBarTextColor, 8);
  updateRangeLabels();
  setValue('ntp-mode', data.time?.ntpMode);
  document.querySelectorAll('[data-ntp]').forEach((item) => item.classList.toggle('active', item.dataset.ntp === String(data.time?.ntpMode ?? 0)));
  setValue('ntp1', data.time?.servers?.[0]);
  setValue('ntp2', data.time?.servers?.[1]);
  setValue('ntp3', data.time?.servers?.[2]);
  updateNtpFields();
  suppressDisplayAutosave = false;
}
$('admin-form').addEventListener('submit', (event) => run(async () => {
  event.preventDefault();
  password = $('admin-password').value;
  sessionStorage.setItem('qcAdminPassword', password);
  await postJson('/api/admin-password', {password});
  $('admin-password').value = '';
  setMessage('Admin password set.', true);
  await loadStatus(); await loadConfig();
}));
$('login-form').addEventListener('submit', (event) => run(async () => {
  event.preventDefault();
  password = $('login-password').value;
  sessionStorage.setItem('qcAdminPassword', password);
  await loadConfig();
  $('login-password').value = '';
  setMessage('Signed in.', true);
  await loadStatus();
}));
$('wifi-form').addEventListener('submit', (event) => run(async () => {
  event.preventDefault();
  await postJson('/api/wifi', {
    ssid: $('ssid').value, password: wifiPasswordForSubmit(), passwordChanged: wifiPasswordChanged, staticIp: $('static-ip').value === '1',
    ip: $('ip').value, netmask: $('netmask').value, gateway: $('gateway').value, dns1: $('dns1').value, dns2: $('dns2').value
  });
  setMessage('Wi-Fi saved. Checking connection status...', true);
  await loadConfig();
  const data = await loadStatus(true);
  setMessage(connectionSummary(data));
}));
$('clear-wifi').addEventListener('click', () => run(async () => {
  await postJson('/api/recovery/clear-wifi', {});
  $('ssid').value = '';
  setSavedWifiPassword('');
  setMessage('Saved Wi-Fi cleared. Setup AP will stay available.', true);
  await loadStatus(true);
}));
$('time-form').addEventListener('submit', (event) => run(async () => {
  event.preventDefault();
  await postJson('/api/display', displayPayload());
  await postJson('/api/time', {ntpMode: +$('ntp-mode').value, servers: [$('ntp1').value, $('ntp2').value, $('ntp3').value]});
  setMessage('Time settings saved.', true);
  await loadStatus(true);
}));
$('refresh').addEventListener('click', () => run(async () => {
  clearTimeout(displayAutosaveTimer);
  await postJson('/api/display', displayPayload());
  await postJson('/api/refresh', {fast: true});
  setMessage('Display settings saved; refresh queued.', true);
  await loadStatus(true);
}));
$('ota-form').addEventListener('submit', (event) => run(async () => {
  event.preventDefault();
  const file = $('ota-file').files[0];
  if (!file) return;
  const response = await fetch('/ota-upload', {method: 'POST', headers: authHeaders({'Content-Type':'application/octet-stream'}), body: file});
  setMessage(response.ok ? 'OTA uploaded. Rebooting.' : `OTA failed: ${response.status} ${await response.text()}`, response.ok);
}));
setSavedWifiPassword('');
updateStaticFields();
setDisplayEnabled(true);
setClockVisible(true);
setWatchStyle(false);
setHighlightTime(false);
selectHighlightColor(0);
selectHighlightTextColor(8);
selectLayout(0);
selectClock(0);
updateRangeLabels();
updateNtpFields();
document.querySelectorAll('[data-ntp]').forEach((item) => item.classList.toggle('active', item.dataset.ntp === $('ntp-mode').value));
loadStatus().then(() => password ? loadConfig() : undefined).catch(error => setMessage(error.message || error));
setInterval(() => loadStatus(true).catch(() => {}), 5000);
</script>
</body>
</html>)HTML";

void send_plain(httpd_req_t *req, const char *status, const char *text) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Connection", "close");
  httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
}

void send_json(httpd_req_t *req, const std::string &text) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Connection", "close");
  httpd_resp_send(req, text.c_str(), text.size());
}

std::string json_escape(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char ch : value) {
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

std::string json_quote(const std::string &value) {
  return "\"" + json_escape(value) + "\"";
}

size_t json_value_start(const std::string &body, const char *key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  size_t pos = body.find(needle);
  if (pos == std::string::npos)
    return std::string::npos;
  pos = body.find(':', pos + needle.size());
  if (pos == std::string::npos)
    return std::string::npos;
  pos++;
  while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) pos++;
  return pos;
}

std::string json_get_string_at(const std::string &body, size_t pos) {
  if (pos == std::string::npos || pos >= body.size() || body[pos] != '"')
    return {};
  std::string out;
  for (pos++; pos < body.size(); pos++) {
    const char ch = body[pos];
    if (ch == '"')
      break;
    if (ch == '\\' && pos + 1 < body.size()) {
      const char escaped = body[++pos];
      if (escaped == 'n')
        out.push_back('\n');
      else if (escaped == 'r')
        out.push_back('\r');
      else if (escaped == 't')
        out.push_back('\t');
      else
        out.push_back(escaped);
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

std::string read_body(httpd_req_t *req, size_t max_len = kMaxJsonBody) {
  if (req->content_len == 0 || req->content_len > max_len)
    return {};
  std::string body;
  body.resize(req->content_len);
  size_t received = 0;
  while (received < body.size()) {
    const int ret = httpd_req_recv(req, body.data() + received, body.size() - received);
    if (ret <= 0)
      return {};
    received += static_cast<size_t>(ret);
  }
  return body;
}

std::string json_string(const std::string &body, const char *key) {
  return json_get_string_at(body, json_value_start(body, key));
}

int json_int(const std::string &body, const char *key, int fallback) {
  const size_t pos = json_value_start(body, key);
  if (pos == std::string::npos)
    return fallback;
  char *end = nullptr;
  const long value = std::strtol(body.c_str() + pos, &end, 10);
  return end == body.c_str() + pos ? fallback : static_cast<int>(value);
}

bool json_bool(const std::string &body, const char *key, bool fallback) {
  const size_t pos = json_value_start(body, key);
  if (pos == std::string::npos)
    return fallback;
  if (!body.compare(pos, 4, "true"))
    return true;
  if (!body.compare(pos, 5, "false"))
    return false;
  return fallback;
}

std::string json_array_string(const std::string &body, const char *key, int wanted_index) {
  size_t pos = json_value_start(body, key);
  if (pos == std::string::npos || pos >= body.size() || body[pos] != '[')
    return {};
  pos++;
  int index = 0;
  while (pos < body.size()) {
    while (pos < body.size() && (std::isspace(static_cast<unsigned char>(body[pos])) || body[pos] == ',')) pos++;
    if (pos >= body.size() || body[pos] == ']')
      break;
    if (index == wanted_index)
      return json_get_string_at(body, pos);
    if (body[pos] == '"') {
      pos++;
      while (pos < body.size()) {
        if (body[pos] == '\\' && pos + 1 < body.size()) {
          pos += 2;
          continue;
        }
        if (body[pos++] == '"')
          break;
      }
    }
    index++;
  }
  return {};
}

void delayed_restart_task(void *) {
  vTaskDelay(pdMS_TO_TICKS(1500));
  esp_restart();
}
}  // namespace

NativeHttpsServer &NativeHttpsServer::instance() {
  static NativeHttpsServer server;
  return server;
}

esp_err_t NativeHttpsServer::start() {
  if (_server)
    return ESP_OK;

  httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  config.httpd.global_user_ctx = this;
  config.httpd.lru_purge_enable = true;
  config.httpd.max_uri_handlers = 12;
  config.httpd.max_open_sockets = 4;
  config.httpd.backlog_conn = 2;
  config.httpd.recv_wait_timeout = 60;  // 60 seconds for large OTA uploads
  config.servercert = reinterpret_cast<const uint8_t *>(generated::kBootstrapCertPem);
  config.servercert_len = std::strlen(generated::kBootstrapCertPem) + 1;
  config.prvtkey_pem = reinterpret_cast<const uint8_t *>(generated::kBootstrapKeyPem);
  config.prvtkey_len = std::strlen(generated::kBootstrapKeyPem) + 1;

  esp_err_t err = httpd_ssl_start(&_server, &config);
  if (err != ESP_OK) {
    _server = nullptr;
    ESP_LOGE(kTag, "failed to start HTTPS server: %s", esp_err_to_name(err));
    return err;
  }

  auto reg = [&](const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *)) {
    httpd_uri_t route = {};
    route.uri = uri;
    route.method = method;
    route.handler = handler;
    route.user_ctx = this;
#if CONFIG_HTTPD_WS_SUPPORT
    route.is_websocket = false;
    route.handle_ws_control_frames = false;
    route.supported_subprotocol = nullptr;
#if CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
    route.ws_pre_handshake_cb = nullptr;
#endif
#if CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT
    route.ws_post_handshake_cb = nullptr;
#endif
#endif
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(_server, &route));
  };

  reg("/", HTTP_GET, index_handler);
  reg("/favicon.ico", HTTP_GET, favicon_handler);
  reg("/api/status", HTTP_GET, status_handler);
  reg("/api/config", HTTP_GET, config_handler);
  reg("/api/admin-password", HTTP_POST, admin_password_handler);
  reg("/api/wifi", HTTP_POST, wifi_handler);
  reg("/api/display", HTTP_POST, display_handler);
  reg("/api/time", HTTP_POST, time_handler);
  reg("/api/refresh", HTTP_POST, refresh_handler);
  reg("/api/recovery/clear-wifi", HTTP_POST, clear_wifi_handler);
  reg("/ota-upload", HTTP_POST, ota_handler);
  ESP_LOGI(kTag, "HTTPS server listening on port 443");
  return ESP_OK;
}

esp_err_t NativeHttpsServer::send_index(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t NativeHttpsServer::send_favicon(httpd_req_t *req) {
  httpd_resp_set_type(req, "image/x-icon");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
  httpd_resp_set_hdr(req, "Connection", "close");
  httpd_resp_send(req, reinterpret_cast<const char *>(kFaviconIco), sizeof(kFaviconIco));
  return ESP_OK;
}

esp_err_t NativeHttpsServer::send_status(httpd_req_t *req) {
  const auto *app = esp_app_get_description();
  const auto platform = platform_snapshot();
  const auto wifi = NativeWifi::instance().status();
  const auto display = NativeDisplay::instance().status();
  const auto display_options = NativeDisplay::instance().options();
  const auto saved_display_options = AppConfig::instance().display();
  const auto sntp = NativeSntp::instance().status();
#if CONFIG_LWIP_DHCP_GET_NTP_SRV
  constexpr bool dhcp_ntp_supported = true;
#else
  constexpr bool dhcp_ntp_supported = false;
#endif
  std::ostringstream out;
  out << "{"
      << "\"adminConfigured\":" << (AppConfig::instance().admin_configured() ? "true" : "false") << ","
      << "\"app\":{"
      << "\"name\":" << json_quote(app ? app->project_name : "") << ","
      << "\"version\":" << json_quote(app ? app->version : "") << "},"
      << "\"system\":{"
      << "\"uptimeMs\":" << static_cast<unsigned long long>(esp_timer_get_time() / 1000) << "},"
      << "\"platform\":{"
      << "\"fallbackApActive\":" << (platform.fallback_ap_active ? "true" : "false") << ","
      << "\"fallbackApSsid\":" << json_quote(platform.fallback_ap_ssid) << ","
      << "\"hostname\":" << json_quote(platform.hostname) << ","
      << "\"lanFqdn\":" << json_quote(platform.lan_fqdn) << ","
      << "\"stationMac\":" << json_quote(platform.station_mac) << ","
      << "\"apMac\":" << json_quote(platform.ap_mac) << ","
      << "\"stationSsid\":" << json_quote(platform.station_ssid) << "},"
      << "\"wifi\":{"
      << "\"staConfigured\":" << (wifi.sta_configured ? "true" : "false") << ","
      << "\"connected\":" << (wifi.connected ? "true" : "false") << ","
      << "\"gotIp\":" << (wifi.got_ip ? "true" : "false") << ","
      << "\"fallbackApActive\":" << (wifi.fallback_ap_active ? "true" : "false") << ","
      << "\"hostname\":" << json_quote(wifi.hostname) << ","
      << "\"ipAddress\":" << json_quote(wifi.ip_address) << ","
      << "\"lastDisconnectReason\":" << wifi.last_disconnect_reason << ","
      << "\"rssi\":" << wifi.rssi << ","
      << "\"reconnects\":" << static_cast<int>(wifi.reconnects) << "},"
      << "\"display\":{"
      << "\"initialized\":" << (display.initialized ? "true" : "false") << ","
      << "\"busy\":" << (display.busy ? "true" : "false") << ","
      << "\"refreshes\":" << display.refreshes << ","
      << "\"lastRefreshMs\":" << display.last_refresh_ms << ","
      << "\"lastRenderMs\":" << display.last_render_ms << ","
      << "\"lastInitMs\":" << display.last_init_ms << ","
      << "\"lastTransferMs\":" << display.last_transfer_ms << ","
      << "\"lastPanelRefreshMs\":" << display.last_panel_refresh_ms << ","
      << "\"lastSleepMs\":" << display.last_sleep_ms << ","
      << "\"lastError\":" << display.last_error << ","
      << "\"lastRefreshPartial\":" << (display.last_refresh_partial ? "true" : "false") << ","
      << "\"lastPartialBytes\":" << display.last_partial_bytes << ","
      << "\"quotePackReady\":" << (display.quote_pack_ready ? "true" : "false") << ","
      << "\"quoteCount\":" << display.quote_count << ","
      << "\"timeSpecificQuoteCount\":" << display.time_specific_quote_count << ","
      << "\"classicQuoteCount\":" << display.classic_quote_count << ","
      << "\"quotePackSize\":" << display.quote_pack_size << ","
      << "\"displayEnabled\":" << (display_options.display_enabled ? "true" : "false") << ","
      << "\"layout\":" << display_options.layout << ","
      << "\"cadence\":" << display_options.refresh_cadence_minutes << ","
      << "\"clockFormat\":" << display_options.clock_format << ","
      << "\"contentMargin\":" << display_options.content_margin << ","
      << "\"clockVisible\":" << (display_options.clock_visible ? "true" : "false") << ","
      << "\"quoteVisible\":" << (display_options.quote_visible ? "true" : "false") << ","
      << "\"quoteTimeSpecificEnabled\":" << (display_options.quote_time_specific_enabled ? "true" : "false") << ","
      << "\"quoteClassicsEnabled\":" << (display_options.quote_classics_enabled ? "true" : "false") << ","
      << "\"highlightTimeEnabled\":" << (display_options.highlight_time_enabled ? "true" : "false") << ","
      << "\"highlightTimeColor\":" << display_options.highlight_time_color << ","
      << "\"highlightTimeTextColor\":" << display_options.highlight_time_text_color << ","
      << "\"mainPaneBgColor\":" << display_options.main_pane_bg_color << ","
      << "\"mainPaneTextColor\":" << display_options.main_pane_text_color << ","
      << "\"sidebarVisible\":" << (display_options.sidebar_visible ? "true" : "false") << ","
      << "\"sidebarColor\":" << display_options.sidebar_color << ","
      << "\"bottomBarVisible\":" << (display_options.bottom_bar_visible ? "true" : "false") << ","
      << "\"bottomBarBgColor\":" << display_options.bottom_bar_bg_color << ","
      << "\"bottomBarTextColor\":" << display_options.bottom_bar_text_color << ","
      << "\"topBarVisible\":" << (display_options.top_bar_visible ? "true" : "false") << ","
      << "\"topBarBgColor\":" << display_options.top_bar_bg_color << ","
      << "\"topBarTextColor\":" << display_options.top_bar_text_color << ","
      << "\"topBarDateFormat\":" << display_options.top_bar_date_format << ","
      << "\"watchStyle\":" << (display_options.watch_style ? "true" : "false") << "},"
      << "\"displaySaved\":{"
      << "\"displayEnabled\":" << (saved_display_options.display_enabled ? "true" : "false") << ","
      << "\"layout\":" << saved_display_options.layout << ","
      << "\"cadence\":" << saved_display_options.refresh_cadence_minutes << ","
      << "\"clockFormat\":" << saved_display_options.clock_format << ","
      << "\"contentMargin\":" << saved_display_options.content_margin << ","
      << "\"clockVisible\":" << (saved_display_options.clock_visible ? "true" : "false") << ","
      << "\"quoteVisible\":" << (saved_display_options.quote_visible ? "true" : "false") << ","
      << "\"quoteTimeSpecificEnabled\":" << (saved_display_options.quote_time_specific_enabled ? "true" : "false") << ","
      << "\"quoteClassicsEnabled\":" << (saved_display_options.quote_classics_enabled ? "true" : "false") << ","
      << "\"highlightTimeEnabled\":" << (saved_display_options.highlight_time_enabled ? "true" : "false") << ","
      << "\"highlightTimeColor\":" << saved_display_options.highlight_time_color << ","
      << "\"highlightTimeTextColor\":" << saved_display_options.highlight_time_text_color << ","
      << "\"mainPaneBgColor\":" << saved_display_options.main_pane_bg_color << ","
      << "\"mainPaneTextColor\":" << saved_display_options.main_pane_text_color << ","
      << "\"sidebarVisible\":" << (saved_display_options.sidebar_visible ? "true" : "false") << ","
      << "\"sidebarColor\":" << saved_display_options.sidebar_color << ","
      << "\"bottomBarVisible\":" << (saved_display_options.bottom_bar_visible ? "true" : "false") << ","
      << "\"bottomBarBgColor\":" << saved_display_options.bottom_bar_bg_color << ","
      << "\"bottomBarTextColor\":" << saved_display_options.bottom_bar_text_color << ","
      << "\"topBarVisible\":" << (saved_display_options.top_bar_visible ? "true" : "false") << ","
      << "\"topBarBgColor\":" << saved_display_options.top_bar_bg_color << ","
      << "\"topBarTextColor\":" << saved_display_options.top_bar_text_color << ","
      << "\"topBarDateFormat\":" << saved_display_options.top_bar_date_format << ","
      << "\"watchStyle\":" << (saved_display_options.watch_style ? "true" : "false") << "},"
      << "\"sntp\":{"
      << "\"enabled\":" << (sntp.enabled ? "true" : "false") << ","
      << "\"mode\":" << sntp.mode << ","
      << "\"syncStatus\":" << sntp.sync_status << ","
      << "\"lastSyncAgeMs\":" << sntp.last_sync_age_ms << ","
      << "\"dhcpNtpSupported\":" << (dhcp_ntp_supported ? "true" : "false") << ","
      << "\"servers\":[";
  bool first_server = true;
  for (const auto &server : sntp.servers) {
    if (server.empty())
      continue;
    if (!first_server)
      out << ",";
    out << json_quote(server);
    first_server = false;
  }
  out << "]}"
      << "}";
  send_json(req, out.str());
  return ESP_OK;
}

esp_err_t NativeHttpsServer::send_config(httpd_req_t *req) {
  if (!authenticate(req))
    return ESP_OK;

  const auto network = AppConfig::instance().network();
  const auto display = AppConfig::instance().display();
  const auto time = AppConfig::instance().time();
  std::ostringstream out;
  out << "{"
      << "\"network\":{"
      << "\"ssid\":" << json_quote(network.ssid) << ","
      << "\"hasPassword\":" << (!network.password.empty() ? "true" : "false") << ","
      << "\"password\":" << json_quote(network.password) << ","
      << "\"staticIp\":" << (network.static_ip ? "true" : "false") << ","
      << "\"ip\":" << json_quote(network.ip) << ","
      << "\"netmask\":" << json_quote(network.netmask) << ","
      << "\"gateway\":" << json_quote(network.gateway) << ","
      << "\"dns1\":" << json_quote(network.dns[0]) << ","
      << "\"dns2\":" << json_quote(network.dns[1]) << "},"
      << "\"display\":{"
      << "\"displayEnabled\":" << (display.display_enabled ? "true" : "false") << ","
      << "\"layout\":" << display.layout << ","
      << "\"cadence\":" << display.refresh_cadence_minutes << ","
      << "\"timezone\":" << display.timezone << ","
      << "\"dstMode\":" << display.dst_mode << ","
      << "\"clockFormat\":" << display.clock_format << ","
      << "\"contentMargin\":" << display.content_margin << ","
      << "\"clockVisible\":" << (display.clock_visible ? "true" : "false") << ","
      << "\"quoteVisible\":" << (display.quote_visible ? "true" : "false") << ","
      << "\"quoteTimeSpecificEnabled\":" << (display.quote_time_specific_enabled ? "true" : "false") << ","
      << "\"quoteClassicsEnabled\":" << (display.quote_classics_enabled ? "true" : "false") << ","
      << "\"highlightTimeEnabled\":" << (display.highlight_time_enabled ? "true" : "false") << ","
      << "\"highlightTimeColor\":" << display.highlight_time_color << ","
      << "\"highlightTimeTextColor\":" << display.highlight_time_text_color << ","
      << "\"mainPaneBgColor\":" << display.main_pane_bg_color << ","
      << "\"mainPaneTextColor\":" << display.main_pane_text_color << ","
      << "\"sidebarVisible\":" << (display.sidebar_visible ? "true" : "false") << ","
      << "\"sidebarColor\":" << display.sidebar_color << ","
      << "\"bottomBarVisible\":" << (display.bottom_bar_visible ? "true" : "false") << ","
      << "\"bottomBarBgColor\":" << display.bottom_bar_bg_color << ","
      << "\"bottomBarTextColor\":" << display.bottom_bar_text_color << ","
      << "\"topBarVisible\":" << (display.top_bar_visible ? "true" : "false") << ","
      << "\"topBarBgColor\":" << display.top_bar_bg_color << ","
      << "\"topBarTextColor\":" << display.top_bar_text_color << ","
      << "\"topBarDateFormat\":" << display.top_bar_date_format << ","
      << "\"watchStyle\":" << (display.watch_style ? "true" : "false") << "},"
      << "\"time\":{"
      << "\"ntpMode\":" << static_cast<int>(time.ntp_mode) << ","
      << "\"servers\":[" << json_quote(time.ntp_servers[0]) << "," << json_quote(time.ntp_servers[1]) << ","
      << json_quote(time.ntp_servers[2]) << "]}"
      << "}";
  send_json(req, out.str());
  return ESP_OK;
}

esp_err_t NativeHttpsServer::set_admin_password(httpd_req_t *req) {
  if (AppConfig::instance().admin_configured() && !authenticate(req))
    return ESP_OK;
  auto body = read_body(req);
  const auto password = json_string(body, "password");
  if (password.size() < 8) {
    send_plain(req, "400 Bad Request", "Password must be at least 8 characters\n");
    return ESP_OK;
  }
  esp_err_t err = AppConfig::instance().set_admin_password(password);
  if (err != ESP_OK) {
    send_plain(req, "500 Internal Server Error", esp_err_to_name(err));
    return ESP_OK;
  }
  send_plain(req, "200 OK", "{\"ok\":true}\n");
  return ESP_OK;
}

esp_err_t NativeHttpsServer::set_wifi(httpd_req_t *req) {
  if (!authenticate(req))
    return ESP_OK;
  auto body = read_body(req);
  if (body.empty()) {
    send_plain(req, "400 Bad Request", "Invalid JSON\n");
    return ESP_OK;
  }
  NetworkConfig config;
  const auto previous = AppConfig::instance().network();
  config.ssid = json_string(body, "ssid");
  config.password = json_string(body, "password");
  const bool password_changed = json_bool(body, "passwordChanged", false);
  if (!password_changed && config.password.empty() && config.ssid == previous.ssid)
    config.password = previous.password;
  config.static_ip = json_bool(body, "staticIp", false);
  config.ip = json_string(body, "ip");
  config.netmask = json_string(body, "netmask");
  config.gateway = json_string(body, "gateway");
  config.dns[0] = json_string(body, "dns1");
  config.dns[1] = json_string(body, "dns2");
  if (config.ssid.empty()) {
    send_plain(req, "400 Bad Request", "SSID is required\n");
    return ESP_OK;
  }
  esp_err_t err = NativeWifi::instance().apply_config(config);
  if (err != ESP_OK) {
    send_plain(req, "500 Internal Server Error", esp_err_to_name(err));
    return ESP_OK;
  }
  send_plain(req, "200 OK", "{\"ok\":true}\n");
  return ESP_OK;
}

esp_err_t NativeHttpsServer::set_display(httpd_req_t *req) {
  if (!authenticate(req))
    return ESP_OK;
  auto body = read_body(req);
  if (body.empty()) {
    send_plain(req, "400 Bad Request", "Invalid JSON\n");
    return ESP_OK;
  }
  DisplayOptions options = AppConfig::instance().display();
  options.display_enabled = json_bool(body, "displayEnabled", options.display_enabled);
  options.layout = json_int(body, "layout", options.layout);
  options.refresh_cadence_minutes = json_int(body, "cadence", options.refresh_cadence_minutes);
  options.timezone = json_int(body, "timezone", options.timezone);
  options.dst_mode = json_int(body, "dstMode", options.dst_mode);
  options.clock_format = json_int(body, "clockFormat", options.clock_format);
  options.content_margin = json_int(body, "contentMargin", options.content_margin);
  options.clock_visible = json_bool(body, "clockVisible", options.clock_visible);
  options.quote_visible = json_bool(body, "quoteVisible", options.quote_visible);
  options.quote_time_specific_enabled = json_bool(body, "quoteTimeSpecificEnabled", options.quote_time_specific_enabled);
  options.quote_classics_enabled = json_bool(body, "quoteClassicsEnabled", options.quote_classics_enabled);
  options.highlight_time_enabled = json_bool(body, "highlightTimeEnabled", options.highlight_time_enabled);
  options.highlight_time_color = json_int(body, "highlightTimeColor", options.highlight_time_color);
  options.highlight_time_text_color = json_int(body, "highlightTimeTextColor", options.highlight_time_text_color);
  options.main_pane_bg_color = json_int(body, "mainPaneBgColor", options.main_pane_bg_color);
  options.main_pane_text_color = json_int(body, "mainPaneTextColor", options.main_pane_text_color);
  options.sidebar_visible = json_bool(body, "sidebarVisible", options.sidebar_visible);
  options.sidebar_color = json_int(body, "sidebarColor", options.sidebar_color);
  options.bottom_bar_visible = json_bool(body, "bottomBarVisible", options.bottom_bar_visible);
  options.bottom_bar_bg_color = json_int(body, "bottomBarBgColor", options.bottom_bar_bg_color);
  options.bottom_bar_text_color = json_int(body, "bottomBarTextColor", options.bottom_bar_text_color);
  options.top_bar_visible = json_bool(body, "topBarVisible", options.top_bar_visible);
  options.top_bar_bg_color = json_int(body, "topBarBgColor", options.top_bar_bg_color);
  options.top_bar_text_color = json_int(body, "topBarTextColor", options.top_bar_text_color);
  options.top_bar_date_format = json_int(body, "topBarDateFormat", options.top_bar_date_format);
  options.watch_style = json_bool(body, "watchStyle", options.watch_style);
  esp_err_t err = AppConfig::instance().set_display(options);
  if (err != ESP_OK) {
    send_plain(req, "500 Internal Server Error", esp_err_to_name(err));
    return ESP_OK;
  }
  NativeDisplay::instance().set_options(options);
  send_plain(req, "200 OK", "{\"ok\":true}\n");
  return ESP_OK;
}

esp_err_t NativeHttpsServer::set_time(httpd_req_t *req) {
  if (!authenticate(req))
    return ESP_OK;
  auto body = read_body(req);
  if (body.empty()) {
    send_plain(req, "400 Bad Request", "Invalid JSON\n");
    return ESP_OK;
  }
  TimeConfig config = AppConfig::instance().time();
  config.ntp_mode = static_cast<NtpMode>(std::clamp(json_int(body, "ntpMode", static_cast<int>(config.ntp_mode)), 0, 2));
  for (int i = 0; i < 3; i++) config.ntp_servers[i] = json_array_string(body, "servers", i);
  esp_err_t err = NativeSntp::instance().apply_config(config);
  if (err != ESP_OK) {
    send_plain(req, "500 Internal Server Error", esp_err_to_name(err));
    return ESP_OK;
  }
  send_plain(req, "200 OK", "{\"ok\":true}\n");
  return ESP_OK;
}

esp_err_t NativeHttpsServer::force_refresh(httpd_req_t *req) {
  if (!authenticate(req))
    return ESP_OK;
  NativeDisplay::instance().request_refresh(true);
  send_plain(req, "200 OK", "{\"ok\":true}\n");
  return ESP_OK;
}

esp_err_t NativeHttpsServer::clear_wifi(httpd_req_t *req) {
  if (!authenticate(req))
    return ESP_OK;
  esp_err_t err = NativeWifi::instance().apply_config(NetworkConfig{});
  if (err != ESP_OK) {
    send_plain(req, "500 Internal Server Error", esp_err_to_name(err));
    return ESP_OK;
  }
  NativeDisplay::instance().request_refresh(false);
  send_plain(req, "200 OK", "{\"ok\":true}\n");
  return ESP_OK;
}

esp_err_t NativeHttpsServer::ota_upload(httpd_req_t *req) {
  if (!authenticate(req))
    return ESP_OK;

  ESP_LOGI(kTag, "Starting OTA upload, free heap: %lu", esp_get_free_heap_size());

  const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
  if (!partition) {
    send_plain(req, "500 Internal Server Error", "No OTA partition\n");
    return ESP_OK;
  }

  esp_ota_handle_t ota = 0;
  esp_err_t err = esp_ota_begin(partition, req->content_len, &ota);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_begin failed: %s", esp_err_to_name(err));
    send_plain(req, "500 Internal Server Error", esp_err_to_name(err));
    return ESP_OK;
  }

  std::vector<uint8_t> buf(8192);  // 8KB buffer
  size_t remaining = req->content_len;
  size_t uploaded = 0;
  while (err == ESP_OK && remaining > 0) {
    const int ret = httpd_req_recv(req, reinterpret_cast<char *>(buf.data()), std::min(buf.size(), remaining));
    if (ret <= 0) {
      ESP_LOGE(kTag, "httpd_req_recv failed or timeout: %d", ret);
      err = ESP_FAIL;
      break;
    }
    err = esp_ota_write(ota, buf.data(), ret);
    uploaded += ret;
    remaining -= static_cast<size_t>(ret);

    // Log progress every 128KB
    if (uploaded % 131072 < buf.size()) {
      ESP_LOGI(kTag, "OTA progress: %lu/%lu bytes (%.1f%%)", uploaded, req->content_len,
               (100.0f * uploaded) / req->content_len);
    }
  }
  if (err == ESP_OK)
    err = esp_ota_end(ota);
  else
    esp_ota_abort(ota);
  if (err == ESP_OK)
    err = esp_ota_set_boot_partition(partition);

  if (err != ESP_OK) {
    ESP_LOGE(kTag, "OTA upload failed: %s", esp_err_to_name(err));
    send_plain(req, "500 Internal Server Error", esp_err_to_name(err));
    return ESP_OK;
  }

  ESP_LOGI(kTag, "OTA upload successful, will reboot");
  send_plain(req, "200 OK", "OTA uploaded; rebooting\n");
  xTaskCreate(delayed_restart_task, "qc_ota_restart", 3072, nullptr, 1, nullptr);
  return ESP_OK;
}

bool NativeHttpsServer::authenticate(httpd_req_t *req) {
  if (!AppConfig::instance().admin_configured())
    return true;

  const size_t len = httpd_req_get_hdr_value_len(req, "Authorization");
  if (len == 0 || len > 256) {
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Quotes Clock\"");
    send_plain(req, "401 Unauthorized", "Authentication required\n");
    return false;
  }

  std::vector<char> header_buf(len + 1);
  if (httpd_req_get_hdr_value_str(req, "Authorization", header_buf.data(), header_buf.size()) != ESP_OK) {
    send_plain(req, "401 Unauthorized", "Authentication required\n");
    return false;
  }
  std::string header(header_buf.data());
  if (header.rfind("Basic ", 0) != 0) {
    send_plain(req, "401 Unauthorized", "Authentication required\n");
    return false;
  }

  std::string encoded = header.substr(6);
  std::vector<unsigned char> decoded(encoded.size());
  size_t decoded_len = 0;
  if (mbedtls_base64_decode(decoded.data(), decoded.size(), &decoded_len,
                            reinterpret_cast<const unsigned char *>(encoded.data()), encoded.size()) != 0) {
    send_plain(req, "401 Unauthorized", "Authentication required\n");
    return false;
  }
  std::string user_info(reinterpret_cast<char *>(decoded.data()), decoded_len);
  constexpr const char *prefix = "admin:";
  if (user_info.rfind(prefix, 0) != 0 || !AppConfig::instance().check_admin_password(user_info.substr(std::strlen(prefix)))) {
    send_plain(req, "401 Unauthorized", "Authentication required\n");
    return false;
  }
  return true;
}

esp_err_t NativeHttpsServer::index_handler(httpd_req_t *req) {
  return static_cast<NativeHttpsServer *>(req->user_ctx)->send_index(req);
}

esp_err_t NativeHttpsServer::favicon_handler(httpd_req_t *req) {
  return static_cast<NativeHttpsServer *>(req->user_ctx)->send_favicon(req);
}

esp_err_t NativeHttpsServer::status_handler(httpd_req_t *req) {
  return static_cast<NativeHttpsServer *>(req->user_ctx)->send_status(req);
}

esp_err_t NativeHttpsServer::config_handler(httpd_req_t *req) {
  return static_cast<NativeHttpsServer *>(req->user_ctx)->send_config(req);
}

esp_err_t NativeHttpsServer::admin_password_handler(httpd_req_t *req) {
  return static_cast<NativeHttpsServer *>(req->user_ctx)->set_admin_password(req);
}

esp_err_t NativeHttpsServer::wifi_handler(httpd_req_t *req) {
  return static_cast<NativeHttpsServer *>(req->user_ctx)->set_wifi(req);
}

esp_err_t NativeHttpsServer::display_handler(httpd_req_t *req) {
  return static_cast<NativeHttpsServer *>(req->user_ctx)->set_display(req);
}

esp_err_t NativeHttpsServer::time_handler(httpd_req_t *req) {
  return static_cast<NativeHttpsServer *>(req->user_ctx)->set_time(req);
}

esp_err_t NativeHttpsServer::refresh_handler(httpd_req_t *req) {
  return static_cast<NativeHttpsServer *>(req->user_ctx)->force_refresh(req);
}

esp_err_t NativeHttpsServer::clear_wifi_handler(httpd_req_t *req) {
  return static_cast<NativeHttpsServer *>(req->user_ctx)->clear_wifi(req);
}

esp_err_t NativeHttpsServer::ota_handler(httpd_req_t *req) {
  return static_cast<NativeHttpsServer *>(req->user_ctx)->ota_upload(req);
}

}  // namespace quotes_clock
