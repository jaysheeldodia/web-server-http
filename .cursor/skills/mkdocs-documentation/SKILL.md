---
name: mkdocs-documentation
description: Guides writing and structuring Markdown documentation for conversion to HTML with MkDocs (Python). Use when creating or editing docs, configuring mkdocs.yml, building or serving the site, or when the user mentions MkDocs, docs site, or converting Markdown to HTML.
---

# MkDocs Documentation

## Overview

MkDocs builds static HTML from Markdown. Content lives in Markdown (`.md`) under `docs/`; structure and nav are set in `mkdocs.yml`. Build command: `mkdocs build` (output usually `site/`). Preview: `mkdocs serve`.

## Setup (Python)

```bash
pip install mkdocs
# Optional theme and extras:
pip install mkdocs-material
```

Config file at repo root: **mkdocs.yml**.

## mkdocs.yml Structure

- **site_name**, **site_description**, **site_url** (optional).
- **nav**: List of pages and optional nested items. Order here defines the sidebar/nav; file order on disk does not.

```yaml
site_name: C++ WebServer Documentation
docs_dir: docs

nav:
  - Home: index.md
  - Getting Started:
    - Overview: getting-started.md
    - Quick Start: quick-start.md
  - API:
    - REST: api/rest.md
    - WebSocket: api/websocket.md
  - Development:
    - Building: building.md
    - Testing: testing.md
```

- **plugins**: e.g. `search`, `minify`. With Material: `material` in theme and plugins as needed.
- **theme**: `name: material` if using mkdocs-material; otherwise `name: readthedocs` or default.

## Writing Pages

- One logical topic per file under `docs/`.
- One H1 per file; use H2/H3 for sections. MkDocs uses the first H1 (or `title` in frontmatter) as the page title.
- Link between pages with relative paths to `.md` files: `[Link text](path/to/page.md)` or `[Section](page.md#heading-anchor)`.

## Build and Serve

```bash
mkdocs build          # → site/ (static HTML)
mkdocs serve          # dev server, live reload
mkdocs build --strict # fail on broken links (recommended in CI)
```

## Conventions for This Project

- Doc sources in **docs/**; avoid putting Jekyll-only syntax in Markdown if the target is MkDocs.
- Use **lowercase-with-hyphens** for doc filenames. Define nav in **mkdocs.yml** so the sidebar matches the desired doc flow.
- Code blocks: always specify language (`bash`, `cpp`, `json`, etc.) for syntax highlighting.

## Checklist for New or Updated Docs

- [ ] File under `docs/` with a clear, hyphenated name.
- [ ] Single H1 at top; consistent H2/H3 below.
- [ ] Internal links use relative `.md` paths (and `#anchor` if needed).
- [ ] Code blocks have a language tag.
- [ ] If adding a new page, add it to **nav** in `mkdocs.yml`.
- [ ] Run `mkdocs build` (and optionally `mkdocs build --strict`) to verify.
