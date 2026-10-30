# Accessibility

BDLMiner V1 has two interfaces: the **web pages** each miner serves on the local
network (dashboard, settings, log), and the **small screen** of the boards that have
one. We want the web pages to be usable with a keyboard, a screen reader and
zoom, in English and French.

## What the web pages do

- A *Skip to content* link, a labelled navigation landmark and the current page
  marked for assistive technologies.
- Every control reachable with the keyboard, with a visible focus outline.
- Status messages (saving, connection, errors, test notifications) announced through
  live regions; form fields have labels and hints tied to them.
- The hashrate chart can be inspected with the arrow keys; its readings are also
  written out as text next to it and downloadable as CSV.
- States are written in words, not only shown by colour (online / offline, warnings,
  accepted / rejected shares).
- Layouts that work from phone width to large screens, and with browser zoom.
- Animations limited when the system asks for reduced motion.
- The page language is set (`en` or `fr`) so that screen readers pronounce it correctly.

## Known limitations

- **The board screens are visual only.** Their 0.9–2.8" displays show one number per
  page and are driven by one or two buttons; there is no audio output. Everything
  they show is also on the web dashboard.
- **The web pages have not had a formal audit** (WCAG conformance testing with
  assistive technologies). Contrast, for instance, has not been measured
  systematically on the dark theme.
- **Charts** are drawn as SVG: the keyboard inspection and the text summaries are the
  accessible way to read them.
- **Only English and French** are available.
- **The setup portal** (WiFi configuration at first start) is provided by the
  WiFiManager library and is outside our control.

## Reporting a barrier

If something gets in your way, please open an
[issue](https://github.com/bdlglua/BDLMinerV1/issues/new/choose) and add the
*accessibility* label if you can: say which page, which browser and assistive
technology, and what happened. Accessibility problems are treated as bugs.
