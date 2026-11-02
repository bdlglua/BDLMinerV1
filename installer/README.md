# Public website

`tools/make_channels.py` builds the stable installer, opt-in beta installer and
update pointers. The same files are published at both the project Pages address
and the account root. No firmware release is needed for website changes.

`index.html` is the shared layout. Independent content templates live in
`pages/`; `tools/make_site_pages.py` renders the home page and separate
`start.html`, `boards.html`, `flash.html`, `demo.html`, `faq.html`,
`releases.html`, `stats.html`, `gallery.html` and `support.html` documents for
both stable and beta. Each document contains only its own main content.
The header/footer navigation and language preference are shared. Old home-page
section links redirect to their matching page. The board comparison links to
`flash.html?board=<profile>`, where only known profiles can be selected.

## Discovery content

`content.json` supplies the board comparison, donation destination, release
highlights and reviewed community gallery. The site's English/French copy is
in `assets/discovery.js`. Keep measured hashrates tied to the evidence linked in
each board entry; leave unmeasured targets without a `rate` value.

The support link is the owner's supplied GitHub Sponsors address. No address
from a pool configuration or statistics sample is reused as a donation address.
Bitcoin/Lightning fields are optional and remain empty unless supplied by the
owner. A site with no configured donation destination hides the support section.

Identification images and their source licences are in `assets/boards/`.
They are distinct from community submissions and physical BDLMiner measurements.

## Community photos

The gallery starts empty. The site's submission button opens
`.github/ISSUE_TEMPLATE/community_setup.yml`; publication requires manual review,
the author's permission, an attribution, the board model and the original
Reddit/Discord link. No public upload endpoint or automatic publication is added.

After review, save an optimized photo under `installer/assets/community/` and add
an entry to `content.json`:

```json
{
  "board": "M5StickC Plus2",
  "image": "assets/community/approved-setup.webp",
  "credit": "Contributor's chosen name",
  "caption": {"en": "Contributor-approved description", "fr": "Description approuvée"},
  "url": "https://www.reddit.com/r/bdlminer/comments/POST_ID/",
  "permission": true
}
```

Keep the permission record in the submission. Do not invent installations or
reuse other people's photos without permission.

## Interactive demos

`tools/make_site_demo.py` copies the actual miner dashboard, settings/log pages
and fleet hub browser assets into `demo/miner/` and `demo/hub/`. Demo-only
`demo.js` supplies fictional in-memory responses and simulated settings. It
never forwards API calls. A Content Security Policy with `connect-src 'none'`
blocks network connections from those documents. Maintenance actions are not
offered in the miner demo; hub hardware-only operations explain that a real
miner is required.

The demo banner, reserved example addresses and fictional boot ID distinguish
these readings from real measurements. These generated copies do not modify
the firmware interfaces or expose a local miner through the public site.

Run the packaged-site browser checks with `node test/web/test_site.cjs`, and
the builder tests with `python3 -m unittest discover -s test/tools -p test_site_builder.py`.
