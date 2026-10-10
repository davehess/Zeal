# Posters and pull request banners

Every change gets two images from one template (`../tools/poster.html`) and one small JSON file:
- `<change>.png`: a 1200x675 poster (title, pitch, picture, three bullets, a "Try it" line).
- `<change>-banner.png`: a 1280x320 header for the top of a pull request or a thread post (title, pitch, picture on the
  right). It is built to stay readable when GitHub scales it to about 800 pixels wide, so keep titles short.

Dark background, gold accent, no player names.

## Make a new poster

1. Put the picture in the change's folder, for example `showcase/my-change/preview.png`. If you have no screenshot, add
   `showcase/my-change/diagram.json` (see any existing one: a caption and up to five steps) and the script draws
   `diagram.png` and `diagram-mini.png` for you. Use `diagram-mini.png` as the poster picture: it has big labels that survive
   being scaled down.
2. Copy an existing `<change>.json` in this folder and edit it:

   | Key | What |
   |---|---|
   | `order` | Position in the index table |
   | `title` | Short, about 4 to 6 words |
   | `pitch` | One line a player understands |
   | `image` | Picture path, relative to `showcase/` |
   | `bullets` | Exactly three short lines |
   | `try` | The "Try it" line |
   | `status` | `draft`, `filed` or `merged` |
   | `pr_url` | The pull request link, blank until filed |
   | `branch` | Branch name shown in the index |

3. Run the script from the repository root:

   ```
   NODE_PATH=$(npm root -g) node showcase/tools/render-posters.js            # everything
   NODE_PATH=$(npm root -g) node showcase/tools/render-posters.js my-change  # one change
   ```

   It needs Playwright and Chromium. It looks for Chromium at `/opt/pw-browsers/chromium`; set `CHROMIUM=/path/to/chrome` to
   use another. It also rewrites the table in `../README.md`.
4. Write `showcase/my-change/README.md` (the post) and `TEST-CASES.md`, then commit the JSON, the PNGs and the text.

## When a pull request is filed

1. In the change's JSON set `"pr_url"` to the pull request link and `"status"` to `"filed"`.
2. Re-run the script. The banner and poster now carry a small **FILED** pill and the index row shows the link.
3. Commit the JSON, the two PNGs and `../README.md`.
4. When it is merged, set `"status"` to `"merged"` and repeat. The pill turns solid gold.

A change still at `"draft"` shows no pill.

## Using a banner in a post

Once this branch is pushed, a banner is at
`https://raw.githubusercontent.com/davehess/Zeal/showcase/showcase/posters/<change>-banner.png`. Markdown:

```
![Title](https://raw.githubusercontent.com/davehess/Zeal/showcase/showcase/posters/<change>-banner.png)
```

Each change's text already starts with that line.
